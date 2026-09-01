#include "SmmPublishController.h"
#include "MainWindow.h"
#include "PostQueueManager.h"
#include "WebPageAgent.h"
#include "AiAgentTaskRunner.h"
#include <QWebEngineView>
#include <QTabWidget>
#include <QPointer>
#include <QUrl>
#include <QTimer>
#include <QStringList>
#include <QJsonObject>
#include <memory>

// vk.com/web.telegram.org/facebook.com — тяжёлые SPA: QWebEngineView::loadFinished
// сигнализирует только о загрузке базового HTML/скриптов, а сам композитор
// поста дорисовывается JS'ом ПОСЛЕ этого события, иногда с заметной задержкой.
// Без паузы агент делал самый первый снимок страницы почти сразу после
// loadFinished, получал полупустой/недорисованный контент, не находил на нём
// ничего похожего на форму поста и сразу сдавался (см. историю переписки —
// реальный кейс с ВК, который "завершился" меньше чем за 30 секунд).
static constexpr int kPageSettleDelayMs = 1800;

SmmPublishController::SmmPublishController(MainWindow* mainWindow, PostQueueManager* queueManager, QObject* parent)
    : QObject(parent), m_mainWindow(mainWindow), m_queueManager(queueManager) {
    connect(m_queueManager, &PostQueueManager::publishDue, this, &SmmPublishController::onPublishDue);
}

void SmmPublishController::onPublishDue(const ScheduledPost& post) {
    if (post.platforms.isEmpty()) {
        m_queueManager->markFailed(post.id, u8"Не выбрана ни одна платформа");
        return;
    }

    // MVP: публикуем только на первую выбранную платформу поста.
    // Мульти-платформенная публикация (по очереди на каждую площадку из
    // post.platforms) сознательно не реализована — сначала должен заработать
    // базовый сценарий на одной, дальше это просто цикл поверх того же кода.
    SocialPlatform platform = *post.platforms.constBegin();

    // post.targetUrl задан → пользователь указал конкретную группу/страницу/
    // канал/сабреддит — открываем СРАЗУ её, а не общий composeUrl платформы.
    // Это не просто эквивалентно "агент сам перейдёт по ссылке": он тогда
    // не должен получить лишний navigate-шаг и повод свернуть не туда, а
    // ещё это делает страницу для Reddit/Facebook-Page обязательной, а не
    // подразумеваемой — см. заметку про Reddit в SmmTypes.h.
    QUrl composeUrl = post.targetUrl.isEmpty()
        ? QUrl(platformRegistry().value(platform).composeUrl)
        : QUrl(post.targetUrl);

    // MainWindow::addNewTab делает новую вкладку текущей (см. существующие
    // вызовы вида addNewTab(imgUrl) в BrowserWebView.cpp) — отдельного
    // API для "открыть НЕ переключаясь" сейчас нет, так что "фоновая" вкладка
    // на практике на секунду станет видимой пользователю. Если это неприемлемо
    // для UX — нужно расширять MainWindow::addNewTab необязательным
    // параметром makeCurrent, отдельная небольшая задача.
    m_mainWindow->addNewTab(composeUrl);

    auto* view = qobject_cast<QWebEngineView*>(m_mainWindow->getTabWidget()->currentWidget());
    if (!view) {
        m_queueManager->markFailed(post.id, u8"Не удалось открыть вкладку для публикации");
        return;
    }

    // loadFinished может сработать несколько раз (SPA-редиректы у той же
    // vk.com/web.telegram.org до основной отрисовки) — отписываемся сразу
    // после первого срабатывания через сохранённый QMetaObject::Connection,
    // чтобы не запустить startAgentSequence() повторно на той же вкладке.
    // QPointer<QWebEngineView> — на случай, если пользователь успеет закрыть
    // именно эту вкладку до того, как она догрузится.
    QPointer<QWebEngineView> viewPtr = view;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(view, &QWebEngineView::loadFinished, this, [this, viewPtr, post, connection](bool ok) {
        QObject::disconnect(*connection);
        if (!viewPtr) {
            m_queueManager->markFailed(post.id, u8"Вкладка публикации была закрыта до загрузки страницы");
            return;
        }
        if (!ok) {
            m_queueManager->markFailed(post.id, u8"Страница платформы не загрузилась");
            return;
        }
        // Даём JS-композитору соцсети время дорисоваться (см. kPageSettleDelayMs
        // выше) — вместо того, чтобы снимать страницу сразу по loadFinished.
        QTimer::singleShot(kPageSettleDelayMs, this, [this, viewPtr, post]() {
            if (!viewPtr) {
                m_queueManager->markFailed(post.id, u8"Вкладка публикации была закрыта до начала работы агента");
                return;
            }
            startAgentSequence(viewPtr.data(), post);
            });
        });
}

void SmmPublishController::startAgentSequence(QWebEngineView* view, const ScheduledPost& post) {
    // Собственный WebPageAgent на конкретную зафиксированную вкладку задачи —
    // не тот, что использует AIAssistantWidget для обычного чата (у него
    // activeViewProvider отвечает за "текущую активную вкладку браузера",
    // здесь же нам всегда нужна именно эта фоновая вкладка публикации,
    // независимо от того, что пользователь смотрит в моменте).
    auto* agent = new WebPageAgent([view]() -> QWebEngineView* { return view; }, this);
    agent->beginTask();
    agent->showWorking(true);
    agent->updateStatus(u8"Storm AI готовит публикацию…");

    auto* runner = new AiAgentTaskRunner(agent, this);

    // Копим все промежуточные "message" от ИИ по ходу задачи — раньше сигнал
    // progress никуда не подключался и весь пошаговый ход выполнения просто
    // терялся: при провале был виден только финальный текст, без единого
    // намёка на то, что агент вообще пытался сделать до этого. Теперь при
    // неудаче полный ход приклеивается к причине ошибки, которую видно во
    // всплывающей подсказке у красной точки в очереди (см. QueueItemWidget).
    auto stepLog = std::make_shared<QStringList>();
    connect(runner, &AiAgentTaskRunner::progress, this, [stepLog](const QString& msg) {
        stepLog->append(msg);
        });

    QString postId = post.id;
    QString postText = post.text;
    connect(runner, &AiAgentTaskRunner::finished, this, [this, agent, runner, postId, postText, stepLog](bool success, const QString& message) {
        auto cleanup = [agent, runner]() {
            agent->showWorking(false);
            agent->clearStatus();
            agent->endTask();
            agent->deleteLater();
            runner->deleteLater();
            };

        auto buildFailReason = [stepLog](const QString& finalMessage) {
            QString reason = finalMessage.isEmpty() ? u8"Публикация не удалась" : finalMessage;
            if (!stepLog->isEmpty()) {
                QStringList numbered;
                for (int i = 0; i < stepLog->size(); ++i) {
                    numbered << QString("%1. %2").arg(i + 1).arg(stepLog->at(i));
                }
                reason += u8"\n\nХод выполнения:\n" + numbered.join("\n");
            }
            return reason;
            };

        if (!success) {
            cleanup();
            m_queueManager->markFailed(postId, buildFailReason(message));
            return;
        }

        // ИИ сообщил об успехе — не доверяем этому слепо: задача выполняется
        // без присмотра человека, а "success:true" — это самооценка модели,
        // а не подтверждённый факт. Один дополнительный механический снимок
        // страницы и грубая проверка, что начало текста поста на ней реально
        // появилось, прежде чем показать пользователю "Опубликовано". Это не
        // железобетонное доказательство (площадка могла переформатировать
        // текст), но дешёвый и по большей части надёжный барьер против
        // ложных "успехов".
        agent->capturePageContext([this, agent, runner, postId, postText, buildFailReason, cleanup](const QJsonObject& page) {
            cleanup();

            QString pageText = page.value("text").toString();
            QString probe = postText.left(30).trimmed();
            bool verified = probe.isEmpty() || pageText.contains(probe, Qt::CaseInsensitive);

            if (verified) {
                m_queueManager->markPublished(postId);
            }
            else {
                m_queueManager->markFailed(postId, buildFailReason(
                    u8"ИИ сообщил об успешной публикации, но проверка не нашла текст поста на странице — статус не подтверждён, проверьте вручную."));
            }
            });
        });

    // *post.platforms.constBegin() без повторной проверки на пустоту — как и
    // в onPublishDue() выше, безопасно: пустая post.platforms отсеивается
    // ДО того, как startAgentSequence() вообще вызывается.
    runner->run(buildTaskDescription(post), platformRegistry().value(*post.platforms.constBegin()).maxAgentSteps);
}

QString SmmPublishController::buildTaskDescription(const ScheduledPost& post) const {
    QString task = u8"Опубликуй на этой странице новую запись (пост) со следующим текстом:\n\n\""
        + post.text + u8"\"\n\nНайди на странице поле для создания новой записи/поста, вставь туда "
        u8"этот текст и нажми кнопку публикации/отправки. Убедись, что запись действительно "
        u8"опубликована (например, появилась в ленте/на стене), прежде чем сообщать об успехе.";

    if (!post.targetUrl.isEmpty()) {
        // Агент уже открыт ровно на нужной группе/странице/канале/сабреддите
        // (см. SmmPublishController::onPublishDue) — явно запрещаем ему
        // самому "искать" куда публиковать или переходить в другой раздел,
        // иначе он может по инерции уйти на свою личную страницу вместо
        // указанной пользователем цели.
        task += u8"\n\nТы уже находишься именно на той странице/группе/канале, куда нужно "
            u8"опубликовать запись — публикуй здесь и никуда не переходи.";
    }

    if (!post.mediaPaths.isEmpty()) {
        // Загрузка файлов через <input type=file> сейчас не поддержана
        // автоматизацией (см. комментарий в SmmPublishController.h) —
        // предупреждаем модель заранее, а не даём ей упереться в кнопку
        // "добавить фото", которую она всё равно не сможет использовать.
        task += u8"\n\nВАЖНО: у пользователя есть прикреплённые к посту фото, но автоматическая "
            u8"загрузка файлов сейчас не поддерживается — опубликуй ТОЛЬКО текст, без фото, и "
            u8"обязательно упомяни в итоговом message, что фото не было прикреплено.";
    }

    return task;
}