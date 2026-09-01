#include "AiAgentTaskRunner.h"
#include "WebPageAgent.h"
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QSet>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUuid>
#include <QDateTime>
#include <QTimer>
#include <QEventLoop>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <memory>

// Тот же обход проверки сертификата, что и в
// AIAssistantWidget.cpp/SettingsBridge.cpp — особенность серверов GigaChat,
// не общая практика для остальных запросов.
static void applyGigaChatSslBypass(QNetworkRequest& req) {
    QSslConfiguration sslConf = req.sslConfiguration();
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(sslConf);
}

// Компактная "подпись" действия — для сравнения "это то же самое действие,
// что провалилось на прошлом шаге, или другое". Полный QJsonObject напрямую
// не сравниваем (порядок полей в QJsonObject не гарантирован при обходе, а
// сравнение по строковому дампу зависит от него) — собираем только те поля,
// что реально определяют действие.
static QString actionSignature(const QJsonObject& action) {
    return action.value("type").toString() + "|"
        + QString::number(action.value("target").toInt(-1)) + "|"
        + action.value("target_text").toString() + "|"
        + action.value("value").toString() + "|"
        + action.value("url").toString();
}

// Адаптация AIAssistantWidget::agentSystemPrompt() под headless-задачу без
// живого чата: те же правила работы со страницей (id элементов, click по
// target_text, запрет угадывать navigate, проверка "страница не изменилась"
// и т.д.), но дополнительно требует явный булев признак успеха в финальном
// action — в чате его нет, потому что человек сам читает "message" и судит
// об итоге, а здесь по этому флагу выбирается markPublished/markFailed.
static QString taskAgentSystemPrompt() {
    return QString::fromUtf8(u8R"PROMPT(Ты Storm AI — ассистент, встроенный в браузер Storm Browser, выполняешь ОДНУ конкретную фоновую задачу без участия пользователя в реальном времени (он увидит только итоговый результат, не переписку). Ты видишь текст текущей открытой страницы и список её видимых интерактивных элементов (ссылки, кнопки, поля ввода), и можешь взаимодействовать со страницей: найти нужный элемент, заполнить и отправить форму, перейти по ссылке и так далее — чтобы довести задачу до конца.

Каждый интерактивный элемент приходит с числовым полем "id" — используй именно это число как "target" в своём действии, не пытайся придумывать CSS-селекторы или XPath.

Отвечай СТРОГО одним JSON-объектом, без какого-либо текста до или после него, в формате:
{"message": "краткое пояснение текущего шага или итогового результата", "action": {"type": "click | type | scroll | navigate | done", "target": <id элемента для click/type/scroll>, "target_text": "точный видимый текст элемента (запасной вариант для click)", "value": "текст для ввода при type", "url": "адрес при navigate", "success": <true/false — ТОЛЬКО когда type == "done">}}

Правила:
- Одно действие за один ответ. После выполнения тебе пришлют обновлённое состояние страницы, и ты сможешь продолжить.
- Для click в первую очередь используй "target" (id из списка elements). Если нужный элемент виден в тексте страницы, но НЕ попал в список elements — верни "action":{"type":"click"} с "target_text" вместо "target" — система сама найдёт и нажмёт его.
- Действие "look" (снимок экрана для визуального анализа) в этом режиме НЕДОСТУПНО — не используй его, разбирайся по тексту/списку элементов и попробуй другой способ (другой элемент, scroll, click по target_text вместо id).
- Никогда не угадывай адрес для "navigate" внутри того же сайта (не выдумывай /ссылки/, /id, /слаги). Для перехода внутри сайта всегда предпочитай click/target_text.
- Если предыдущее действие завершилось со статусом "не удалось" ИЛИ страница не изменилась — не повторяй его в том же виде. Попробуй другой способ.
- Прежде чем считать, что ты уже на нужном разделе/форме — сверяйся со строками "Текущий адрес (URL)" и "Заголовок вкладки", а не только с текстом на странице.
- Когда задача выполнена, невозможна или зашла в тупик после нескольких разных попыток — верни "action": {"type": "done", "success": true или false}. success=true ТОЛЬКО если задача реально доведена до конца (например, форма отправлена и это подтверждено изменением страницы) — если сомневаешься или не можешь убедиться, ставь success=false и честно объясни причину в "message".
- Никогда не выдумывай данные, которых нет на странице. Не совершай необратимые или чувствительные действия, не относящиеся напрямую к описанной задаче.)PROMPT");
}

AiAgentTaskRunner::AiAgentTaskRunner(WebPageAgent* agent, QObject* parent)
    : QObject(parent), m_agent(agent) {
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &AiAgentTaskRunner::onNetworkReply);
}

void AiAgentTaskRunner::run(const QString& taskDescription, int maxSteps) {
    m_taskDescription = taskDescription;
    m_maxSteps = maxSteps > 0 ? maxSteps : MAX_STEPS; // защита от случайного 0/отрицательного значения снаружи
    m_history.append(AgentMsg{ "user", taskDescription });
    m_running = true;
    m_stepCount = 0;
    sendStep();
}

void AiAgentTaskRunner::finishWith(bool success, const QString& message) {
    if (!m_running) return;
    m_running = false;
    emit finished(success, message);
}

void AiAgentTaskRunner::sendStep() {
    if (!m_running) return;

    if (m_stepCount >= m_maxSteps) {
        finishWith(false, u8"Достигнут лимит шагов (" + QString::number(m_maxSteps) + u8") — задача остановлена, чтобы не зациклиться.");
        return;
    }
    m_stepCount++;

    m_agent->capturePageContext([this](const QJsonObject& page) {
        if (!m_running) return;

        // Механическая подстраховка ДО обращения к ИИ (не тратит шаг/токены
        // зря): если на первом снимке страница технически загружена, но
        // текста подозрительно мало — скорее всего JS соцсети ещё не
        // дорисовал контент (см. kPageSettleDelayMs в SmmPublishController).
        // Даём ОДНУ повторную попытку с короткой паузой, прежде чем вообще
        // спрашивать модель, — не зацикливаемся флагом m_settleRetryUsed.
        bool pageLooksEmpty = page.value("hasPage").toBool()
            && page.value("text").toString().trimmed().length() < 40;
        if (m_stepCount == 1 && pageLooksEmpty && !m_settleRetryUsed) {
            m_settleRetryUsed = true;
            m_stepCount--; // эта попытка не считается полноценным шагом агента
            QTimer::singleShot(1500, this, [this]() { if (m_running) sendStep(); });
            return;
        }

        // Вкладка задачи пропала (закрыта пользователем, краш рендерера
        // страницы и т.п.) — раньше в этом случае мы всё равно отправляли
        // запрос к ИИ с пометкой "вкладка недоступна, ответь done:false" и
        // полагались, что модель правильно ей последует. Надёжнее и дешевле
        // (по токенам) завершить сразу самим, не спрашивая ИИ вообще —
        // результат детерминирован и не зависит от того, аккуратно ли модель
        // выполнит инструкцию.
        if (!page.value("hasPage").toBool()) {
            finishWith(false, u8"Вкладка задачи стала недоступна (закрыта или произошла ошибка) — публикация прервана.");
            return;
        }

        // Капча/проверка "вы не робот" — агент всё равно не умеет её решать
        // (см. системный промпт, пункт про "look"), так что вместо того,
        // чтобы жечь все 15 шагов на бессмысленные попытки, останавливаемся
        // сразу с честной причиной. Проверяем на КАЖДОМ шаге, не только на
        // первом — капча может появиться и в середине сценария, например
        // после клика по кнопке "Опубликовать".
        QString bodyTextLower = page.value("text").toString().toLower();
        bool looksLikeCaptcha = bodyTextLower.contains(u8"подтвердите, что вы не робот")
            || bodyTextLower.contains(u8"я не робот")
            || bodyTextLower.contains("captcha")
            || bodyTextLower.contains(u8"проверка браузера")
            || bodyTextLower.contains("checking your browser")
            || bodyTextLower.contains("cloudflare");
        if (looksLikeCaptcha) {
            finishWith(false, u8"Площадка запросила капчу/проверку браузера — автоматическая публикация невозможна в этот раз, опубликуйте вручную.");
            return;
        }

        // "Стена входа" — проверяем ТОЛЬКО по URL (login/signin/auth в пути),
        // а не по тексту страницы: текстовые эвристики вроде "войти" слишком
        // легко ловят обычную страницу с кнопкой входа где-нибудь в шапке и
        // ошибочно обрывают рабочий сценарий. URL — надёжнее, хоть и не ловит
        // все случаи (например, модальное окно логина без смены адреса) —
        // сознательно принимаем это ограничение ради меньшего числа ложных срабатываний.
        QString urlLower = page.value("url").toString().toLower();
        if (urlLower.contains("login") || urlLower.contains("signin")
            || urlLower.contains("sign-in") || urlLower.contains("/auth")) {
            finishWith(false, u8"Похоже, вы не авторизованы на этой площадке (страница входа) — войдите в аккаунт в Storm Browser и запланируйте пост заново.");
            return;
        }

        QSettings settings;
        int aiMode = settings.value("ai/mode", 0).toInt();
        bool useCustomApi = (aiMode == 0);
        bool useGigaChat = (aiMode == 2);
        QString apiKey = settings.value("ai/openrouter_key", "").toString();
        QString gigaKey = settings.value("ai/gigachat_key", "").toString();
        QString gigaModel = settings.value("ai/gigachat_model", "GigaChat-2").toString();
        QString user = settings.value("sync/username", "").toString();
        QString pwd = settings.value("sync/password", "").toString();

        QJsonObject sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = taskAgentSystemPrompt();
        QJsonArray messagesArray;
        messagesArray.append(sysMsg);

        for (const auto& msg : m_history) {
            QJsonObject m; m["role"] = msg.role; m["content"] = msg.content;
            messagesArray.append(m);
        }

        QString contextContent;
        {
            // hasPage == false сюда уже не попадает — отсечено ранним
            // return'ом выше, поэтому упрощаем: строим контекст, считая
            // страницу всегда доступной.
            QString url = page.value("url").toString();
            QString title = page.value("title").toString();
            QString bodyText = page.value("text").toString();

            QString verificationNote;
            if (m_stepCount > 1) {
                bool changed = (url != m_lastSeenUrl) || (bodyText != m_lastSeenPageText);
                verificationNote = changed
                    ? u8"\nПроверка: страница изменилась после последнего действия."
                    : u8"\nПроверка: страница НЕ изменилась после последнего действия — вероятно, клик пришёлся мимо, элемент не тот, или нужен другой способ.";
            }
            m_lastSeenUrl = url;
            m_lastSeenPageText = bodyText;

            QJsonDocument pageDoc(page);
            contextContent = u8"[Текущее состояние открытой страницы]\n"
                u8"Текущий адрес (URL): " + url + u8"\n"
                u8"Заголовок вкладки: " + title + u8"\n\n"
                u8"Полный срез страницы (JSON — текст и интерактивные элементы):\n"
                + QString::fromUtf8(pageDoc.toJson(QJsonDocument::Compact))
                + u8"\n\nПоследнее выполненное действие: " + m_lastActionSummary
                + verificationNote;
        }
        QJsonObject ctxMsg; ctxMsg["role"] = "user"; ctxMsg["content"] = contextContent;
        messagesArray.append(ctxMsg);

        QJsonObject json;
        json["messages"] = messagesArray;

        QNetworkRequest request;
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        // Без этого таймаута зависший ответ сервера ИИ (не ошибка, а именно
        // тишина) оставлял бы пост в статусе "Publishing" навсегда — finished()
        // у QNetworkAccessManager просто никогда бы не пришёл. 30с с запасом
        // хватает на обычный ответ модели даже с длинным контекстом страницы.
        request.setTransferTimeout(30000);

        if (useCustomApi) {
            json["model"] = QString("openrouter/auto");
            json["route"] = QString("fallback");
            json["response_format"] = QJsonObject{ {"type", "json_object"} };
            request.setUrl(QUrl("https://openrouter.ai/api/v1/chat/completions"));
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
            request.setRawHeader("HTTP-Referer", "https://storm-browser.ru/");
            request.setRawHeader("X-Title", "Storm Browser");
            m_networkManager->post(request, QJsonDocument(json).toJson());
        }
        else if (useGigaChat) {
            QString authErr;
            if (!ensureGigaChatToken(gigaKey, authErr)) {
                finishWith(false, u8"Ошибка авторизации GigaChat: " + authErr);
                return;
            }
            json["model"] = gigaModel;
            request.setUrl(QUrl("https://api.giga.chat/v1/chat/completions"));
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            request.setRawHeader("Authorization", ("Bearer " + m_cachedGigaToken).toUtf8());
            applyGigaChatSslBypass(request);
            m_networkManager->post(request, QJsonDocument(json).toJson());
        }
        else {
            json["username"] = user;
            json["password"] = pwd;
            json["user_api_key"] = QString("");
            request.setUrl(QUrl("https://storm-browser.online:8000/api/ai/chat"));
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            m_networkManager->post(request, QJsonDocument(json).toJson());
        }
        });
}

bool AiAgentTaskRunner::ensureGigaChatToken(const QString& gigaKey, QString& errorOut) {
    // Идентично AIAssistantWidget::ensureGigaChatToken — свой собственный
    // кэш токена (m_cachedGigaToken/m_gigaTokenExpireTime), НЕ общий с чатом:
    // раннер живёт ровно на время одной публикации и создаётся заново на
    // каждый пост, так что переиспользовать чужой кэш смысла нет, а тянуть
    // сюда экземпляр AIAssistantWidget ради этого — лишняя связность.
    qint64 currentTime = QDateTime::currentSecsSinceEpoch();
    if (!m_cachedGigaToken.isEmpty() && currentTime < m_gigaTokenExpireTime) return true;

    QNetworkRequest authReq(QUrl("https://ngw.devices.sberbank.ru:9443/api/v2/oauth"));
    authReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    authReq.setRawHeader("Accept", "application/json");
    authReq.setRawHeader("RqUID", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
    authReq.setRawHeader("Authorization", ("Basic " + gigaKey).toUtf8());
    // Здесь тайм-аут даже важнее, чем в основном запросе: без него завис бы
    // не только сетевой запрос, а вложенный QEventLoop::exec() ниже — то есть
    // весь AiAgentTaskRunner (и, как следствие, вся публикация) встал бы
    // намертво, а не просто "долго ждал".
    authReq.setTransferTimeout(15000);
    applyGigaChatSslBypass(authReq);

    QNetworkAccessManager authManager;
    QNetworkReply* authReply = authManager.post(authReq, "scope=GIGACHAT_API_PERS");
    QEventLoop loop;
    QObject::connect(authReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool ok = (authReply->error() == QNetworkReply::NoError);
    if (ok) {
        QJsonDocument authDoc = QJsonDocument::fromJson(authReply->readAll());
        m_cachedGigaToken = authDoc.object().value("access_token").toString();
        m_gigaTokenExpireTime = currentTime + 1740;
    }
    else {
        errorOut = authReply->errorString();
    }
    authReply->deleteLater();
    return ok;
}

void AiAgentTaskRunner::onNetworkReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (!m_running) return;

    if (reply->error() != QNetworkReply::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // Различаем "сервер явно отказал" (неверный ключ, битый запрос —
        // повтор того же самого не поможет) от "временная неполадка"
        // (таймаут/перегрузка/сеть моргнула — часто самоустраняется за
        // пару секунд). 401/403/400 и подобные — НЕ повторяем, это не
        // изменится от повторной попытки. 429/5xx и сетевые обрывы без
        // HTTP-ответа вообще — повторяем, с растущей паузой.
        bool retryable = (statusCode == 429 || statusCode == 500 || statusCode == 502
            || statusCode == 503 || statusCode == 504);
        if (statusCode == 0) {
            auto err = reply->error();
            retryable = (err == QNetworkReply::TimeoutError
                || err == QNetworkReply::TemporaryNetworkFailureError
                || err == QNetworkReply::NetworkSessionFailedError
                || err == QNetworkReply::RemoteHostClosedError
                || err == QNetworkReply::ConnectionRefusedError
                || err == QNetworkReply::UnknownNetworkError);
        }

        if (retryable && m_networkRetryCount < kMaxNetworkRetries) {
            m_networkRetryCount++;
            m_stepCount--; // временный сетевой сбой не считается полноценным шагом агента
            int backoffMs = 2000 * m_networkRetryCount; // 2с, затем 4с
            QTimer::singleShot(backoffMs, this, [this]() { if (m_running) sendStep(); });
            return;
        }

        QString errorMsg = (statusCode == 0)
            ? u8"Нет подключения к серверу ИИ."
            : (u8"Ошибка " + QString::number(statusCode) + u8": " + reply->errorString());
        finishWith(false, errorMsg);
        return;
    }
    // Успешный ответ — сбрасываем счётчик, следующий сбой (если будет)
    // получает полный бюджет повторов заново, а не донашивает чужой.
    m_networkRetryCount = 0;

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject rootObj = doc.object();
    QString content;

    if (rootObj.contains("choices")) {
        QJsonArray choices = rootObj.value("choices").toArray();
        if (!choices.isEmpty()) {
            content = choices[0].toObject().value("message").toObject().value("content").toString();
        }
    }
    else if (rootObj.contains("reply")) {
        content = rootObj.value("reply").toString();
    }

    if (rootObj.contains("usage")) {
        QSettings tokenSettings;
        if (tokenSettings.value("ai/mode", 0).toInt() == 2) {
            qint64 totalTokens = rootObj.value("usage").toObject().value("total_tokens").toVariant().toLongLong();
            qint64 usedSoFar = tokenSettings.value("ai/gigachat_tokens_used", 0).toLongLong();
            tokenSettings.setValue("ai/gigachat_tokens_used", usedSoFar + totalTokens);
        }
    }

    // Та же подстраховка, что и в AIAssistantWidget::onAgentNetworkReply —
    // если модель обернула JSON лишним текстом, достаём его по первой/
    // последней фигурной скобке.
    QString jsonPart = content;
    int jsonStart = content.indexOf('{');
    int jsonEnd = content.lastIndexOf('}');
    if (jsonStart != -1 && jsonEnd != -1 && jsonEnd > jsonStart) {
        jsonPart = content.mid(jsonStart, jsonEnd - jsonStart + 1);
    }

    QJsonParseError parseErr;
    QJsonObject parsed = QJsonDocument::fromJson(jsonPart.toUtf8(), &parseErr).object();

    QString message;
    QJsonObject action;
    if (parseErr.error == QJsonParseError::NoError && parsed.contains("message")) {
        message = parsed.value("message").toString();
        action = parsed.value("action").toObject();
    }
    else {
        finishWith(false, content.isEmpty() ? u8"Сервер вернул пустой ответ или неизвестный формат." : content);
        return;
    }

    if (!message.isEmpty()) {
        m_history.append(AgentMsg{ "assistant", message });
        if (m_history.size() > 10) m_history.removeFirst();
        emit progress(message);
    }

    QString actionType = action.value("type").toString(u8"done");
    if (actionType.isEmpty() || actionType == "done") {
        // success читаем из самого action, как того требует taskAgentSystemPrompt();
        // если модель его не прислала (не выполнила формат) — считаем неуспехом,
        // а не молча "угадываем" по тексту message.
        bool success = action.value("success").toBool(false);
        finishWith(success, message);
        return;
    }

    if (actionType == "look") {
        // "look" в этом режиме не поддержан (см. AiAgentTaskRunner.h) — прямо
        // сообщаем модели и продолжаем обычным циклом без реального снимка.
        m_history.append(AgentMsg{ "user", u8"[\"look\" недоступен в этом режиме — продолжай без него.]" });
        sendStep();
        return;
    }

    // Модель может вернуть тип действия за пределами протокола (не идеальна
    // послушность инструкциям) — раньше это тихо проваливалось в
    // executeAction() как "неизвестное" без внятной причины в логе. Явно
    // отсекаем и просим модель исправиться, вместо того чтобы передавать
    // непонятно что дальше в WebPageAgent.
    static const QSet<QString> kKnownActions = { "click", "type", "scroll", "navigate" };
    if (!kKnownActions.contains(actionType)) {
        m_history.append(AgentMsg{ "user",
            QString(u8"[Неизвестный тип действия \"%1\" — используй только click/type/scroll/navigate/done.]").arg(actionType) });
        sendStep();
        return;
    }

    // Если модель прислала ТОЧНО то же самое действие, что уже провалилось
    // на предыдущем шаге — не отправляем его в WebPageAgent повторно
    // (заведомо провалится тем же образом), а сразу просим попробовать
    // иначе. Промпт просит модель не повторяться сама, но это лишь просьба;
    // здесь — механическая гарантия, не зависящая от послушности модели.
    QString sig = actionSignature(action);
    if (!m_lastFailedActionSignature.isEmpty() && sig == m_lastFailedActionSignature) {
        m_history.append(AgentMsg{ "user",
            u8"[Это действие уже пробовали на предыдущем шаге, и оно не сработало — не повторяй его буквально, попробуй другой элемент или другой способ.]" });
        sendStep();
        return;
    }

    QString stepLabel;
    if (actionType == "click") stepLabel = u8"нажимаю на элемент страницы…";
    else if (actionType == "type") stepLabel = u8"ввожу текст в поле…";
    else if (actionType == "scroll") stepLabel = u8"прокручиваю страницу…";
    else if (actionType == "navigate") stepLabel = u8"перехожу по ссылке…";
    m_agent->updateStatus(stepLabel);

    m_agent->executeAction(action, [this, actionType, sig](bool success) {
        if (!m_running) return;

        // Запоминаем сигнатуру ТОЛЬКО провалившегося действия — успешное
        // явно очищает "память о провале", иначе после случайного успеха
        // того же типа действия следующий такой же (уже другой по сути)
        // клик мог бы ошибочно попасть под запрет повтора.
        m_lastFailedActionSignature = success ? QString() : sig;

        m_lastActionSummary = QString(u8"[%1] — %2").arg(actionType, success ? u8"выполнено" : u8"не удалось (элемент не найден или устарел, список элементов будет обновлён)");

        int fallbackDelay = (actionType == "navigate") ? 8000 : 900;
        scheduleNextStep(fallbackDelay);
        });
}

void AiAgentTaskRunner::scheduleNextStep(int fallbackDelayMs) {
    if (!m_running) return;

    // Идентичная защита от двойного запуска, что и в
    // AIAssistantWidget::scheduleNextAgentStep — ждём либо реальной загрузки
    // страницы, либо fallback-таймаут, смотря что наступит раньше.
    auto proceeded = std::make_shared<bool>(false);
    auto conn = std::make_shared<QMetaObject::Connection>();

    QWebEngineView* view = m_agent->targetView();
    if (view) {
        *conn = connect(view->page(), &QWebEnginePage::loadFinished, this, [this, proceeded, conn](bool) {
            if (*proceeded) return;
            *proceeded = true;
            QObject::disconnect(*conn);
            if (m_running) sendStep();
            });
    }

    QTimer::singleShot(fallbackDelayMs, this, [this, proceeded, conn]() {
        if (*proceeded) return;
        *proceeded = true;
        if (*conn) QObject::disconnect(*conn);
        if (m_running) sendStep();
        });
}