#include "MainWindow.h"
#include "ThemeManager.h"
#include "StormTabBar.h"
#include "Sidebar.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSettings>
#include <QDialog>
#include <QListWidget>
#include <QSizeGrip>
#include <QTableWidget>
#include <QTreeWidget>
#include <QMap>
#include <QFont>
#include <QLineEdit>
#include <QPushButton>
#include <QHeaderView>
#include <QScrollBar>
#include <QApplication>
#include "AIAssistantWidget.h"
#include "NotesWidget.h"
#include "TodoWidget.h"
#include "PomodoroWidget.h"
#include "NewsWidget.h"
#include "GamesWidget.h"
#include "TranslatorWidget.h"
#include "VoiceChatWidget.h"
#include "WeatherWidget.h"
#include "FreeGamesWidget.h"
#include "EducationWidget.h"
#include "ArcadeWidget.h"
#include "ReaderWidget.h"
#include "TalkWidget.h"
#include <QWebEngineSettings>
#include <QWebEngineNewWindowRequest>
#include <QWebEngineFullScreenRequest>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QByteArray>
#include <QToolButton>
#include <QWebEngineProfile>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QMenu>
#include <QCoreApplication>
#include "MenuBuilder.h"
#include "CustomMenuPanel.h"
#include "BookmarksBridge.h"
#include "BookmarksPageHtml.h"
#include "DownloadsBridge.h"
#include "DownloadsPageHtml.h"
#include <QtAlgorithms>
#include "BrowserWebView.h"
#include "TabSpinner.h"
#include "PasswordManager.h"
#include "HomeAIBridge.h"
#include "HomeBridge.h"
#include <QWebChannel>
#include <QShortcut>
#include <QKeySequence>
#include <QtMath>
#include <QFileDialog>
#include <QTextStream>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QTabBar>
#include "BookmarksBar.h"
#include <QActionGroup>
#include "ScreenshotEditor.h"
#include "DownloadManager.h"
#include "FernetCrypto.h"
#include "StormWebPage.h"
#include "CertificateManager.h"
#include <QWebEngineCertificateError>
#include <QRadioButton>
#include <QDialogButtonBox>
#include <QStandardPaths>
#include "UpdateManager.h"
#include "GameModeManager.h"
#include <QTimer>
#include <QPointer>
#include <QWebEnginePage>
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
#include <QWebEngineDesktopMediaRequest>
#endif
#include "AdblockManager.h"
#include "TalkBridge.h"
#include "ShieldInterceptor.h"
#include <QStatusBar> 
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QSet>
#include <QUrlQuery>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QInputDialog>
#include "HelpPageHtml.h"
#include "Logger.h"
#include "ProxyManager.h"
#include "SettingsBridge.h"
#include "StormCloudBridge.h"
#include "PageTemplates.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QIcon>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsDropShadowEffect>
#include <QWebEngineFindTextResult>


#include "MainWindow_UiHelpers.h"

// ==========================================================================
// MainWindow_Tabs.cpp — вкладки: создание, закрытие, drag&drop, импорт/экспорт
// Создание новых вкладок, контекстное меню вкладки, категории/группировка
// вкладок, отрыв/приём вкладки между окнами (attachTab/detachTab),
// импорт и экспорт вкладок в HTML. Выделено из MainWindow.cpp.
// Использует applyMediaCodecFix() из MainWindow_UiHelpers.h.
// ==========================================================================


// C-1: единые на процесс профили для инкогнито- и игровых вкладок.
// Раньше КАЖДАЯ такая вкладка создавала свой QWebEngineProfile:
//  - инкогнито: новый анонимный профиль на вкладку → каждая имела свои
//    процессы Chromium и кэш в памяти; десяток приватных вкладок за долгую
//    сессию — и браузер исчерпывал память (OOM) и закрывался сам;
//  - игровые: несколько ИМЕНОВАННЫХ профилей "StormArcadeSandbox" на одних
//    каталогах хранилища — Chromium держит блокировку хранилища, и вторая
//    игровая вкладка могла остаться без профиля или уронить движок.
// Теперь каждый профиль создаётся один раз, живёт до конца процесса и
// переиспользуется всеми вкладками своего типа (как это делают Chrome/Firefox).
QWebEngineProfile* MainWindow::sharedAuxProfile(bool gameMode) {
    QWebEngineProfile*& prof = gameMode ? s_arcadeProfile : s_incognitoProfile;
    if (!prof) {
        if (gameMode) {
            prof = new QWebEngineProfile("StormArcadeSandbox");
        }
        else {
            prof = new QWebEngineProfile(); // без имени = off-the-record (инкогнито)
        }
        prof->setSpellCheckEnabled(true);
        prof->setSpellCheckLanguages(QStringList() << "en-US" << "ru-RU");

        applyMediaCodecFix(prof);
        prof->setHttpUserAgent(stormUserAgentFor(prof));
        if (!gameMode) {
            // У приватного профиля нет своего interceptor — ставим минимальный,
            // чтобы и здесь работала маска Firefox на страницах входа Google.
            prof->setUrlRequestInterceptor(new GoogleLoginUaInterceptor(prof));
            applyGoogleLoginUaScript(prof);
        }
    }
    return prof;
}


// ==========================================================================
// C-2/P1-2: полный набор подключений одной вкладки к ЭТОМУ окну.
// Вызывается из addNewTab() при создании и из attachTab() при переносе
// вкладки между окнами. Перед вызовом вкладка обязана быть отключена от
// прежнего окна-владельца (detachTab/attachTab делают это явно), иначе
// сигналы уходили бы сразу в два окна, а после закрытия старого окна
// обработчики указывали бы на освобожденную память (use-after-free —
// одна из причин самопроизвольного закрытия браузера).
// ==========================================================================
void MainWindow::wireTab(BrowserWebView* view, StormWebPage* page) {
    // Флаги типа вкладки сохраняются в свойствах вида при создании
    // (addNewTab) и переживают любое число переносов между окнами.
    const bool isIncognito = view->property("isIncognito").toBool();
    const bool isGame = view->property("isGameTab").toBool();

    // Drop-зона возврата вкладки: фильтр перетаскивания стоит не только на
    // полосе вкладок, но и на самой веб-странице (и её внутренних дочерних
    // виджетах — их ловит ChildAdded внутри фильтра). Иначе QtWebEngine
    // перехватывал бы drag раньше нас, и вкладку, брошенную чуть ниже
    // полосы вкладок, вернуть было бы нельзя — открывалось новое окно.
    if (m_tabDragFilter) {
        view->installEventFilter(m_tabDragFilter);
        const auto children = view->children();
        for (QObject* child : children) {
            child->installEventFilter(m_tabDragFilter);
        }
    }

    // S-5: если рендер-процесс вкладки упал (нехватка памяти, баг страницы),
    // вкладка раньше навсегда оставалась мёртвой/серой без подсказки
    // пользователю. Автоперезапуск с ограничением: не больше 3 попыток за
    // жизнь вкладки, чтобы страница, стабильно убивающая свой рендерер,
    // не уводила браузер в бесконечный цикл перезагрузок.
    // Внутренние страницы (storm-talk и storm://) перезаливаются своим шаблоном
    // через reloadInternalTab() — обычный reload() для них ломал вкладку:
    // ходил по фиктивному baseUrl (http://localhost/storm-talk и т.п.) и
    // получал ERR_CONNECTION_REFUSED.
    connect(page, &QWebEnginePage::renderProcessTerminated, this,
        [this, view](QWebEnginePage::RenderProcessTerminationStatus /*status*/, int exitCode) {
            const int crashes = view->property("rendererCrashes").toInt() + 1;
            view->setProperty("rendererCrashes", crashes);
            qWarning() << "[Storm Tabs] Рендер-процесс вкладки упал (код" << exitCode
                << ", попытка" << crashes << ") —" << (crashes <= 3 ? "перезапускаю страницу" : "автоперезапуск остановлен");
            if (crashes <= 3) {
                QPointer<QWebEngineView> viewGuard(view);
                QTimer::singleShot(200, this, [this, viewGuard]() {
                    if (!viewGuard) return;
                    if (!reloadInternalTab(viewGuard)) viewGuard->reload();
                    });
            }
        });

    // Полноэкранный режим по двойному клику/запросу страницы (например, разворот
    // плитки участника в Storm Talk): раньше НИКТО не принимал этот запрос —
    // QtWebEngine молча отклонял его, и «развернуть на весь экран» не работало
    // вообще ни на одной странице. Теперь запрос принимается, а само окно
    // браузера уходит в полный экран и возвращается обратно при выходе.
    connect(page, &QWebEnginePage::fullScreenRequested, this,
        [this](QWebEngineFullScreenRequest request) {
            request.accept();
            if (request.toggleOn()) {
                showFullScreen();
            }
            else {
                showNormal();
            }
        });

    connect(page, &QWebEnginePage::featurePermissionRequested, this, [this, page](const QUrl& securityOrigin, QWebEnginePage::Feature feature) {
        // БАГФИКС: раньше здесь разрешался ТОЛЬКО ClipboardReadWrite, а вообще
        // всё остальное молча отклонялось — в том числе MediaAudioVideoCapture
        // (камера+микрофон) и DesktopVideoCapture/DesktopAudioVideoCapture
        // (демонстрация экрана). Из-за этого на странице Storm Talk (она грузится
        // через view->setHtml(..., QUrl("http://localhost/storm-talk")) чуть
        // ниже) navigator.mediaDevices.getUserMedia() ВСЕГДА получал
        // NotAllowedError, а демонстрация экрана вообще не могла запроситься
        // разрешение. "localhost" здесь — не настоящий сетевой адрес, а просто
        // базовый URL для setHtml, снаружи на него попасть нельзя, поэтому для
        // этого происхождения безопасно выдавать медиа-разрешения автоматически,
        // как для встроенной функции браузера (пользователю не нужно видеть
        // системный запрос "разрешить камеру localhost"). Для ЛЮБЫХ ДРУГИХ
        // сайтов поведение сознательно не менялось — они по-прежнему получают
        // отказ на всё, кроме буфера обмена.
        const bool isTrustedLocalPage = (securityOrigin.scheme() == QLatin1String("http") &&
            securityOrigin.host() == QLatin1String("localhost"));
        const bool isMediaOrScreenFeature = (feature == QWebEnginePage::MediaAudioCapture ||
            feature == QWebEnginePage::MediaVideoCapture ||
            feature == QWebEnginePage::MediaAudioVideoCapture ||
            feature == QWebEnginePage::DesktopVideoCapture ||
            feature == QWebEnginePage::DesktopAudioVideoCapture);

        if (feature == QWebEnginePage::ClipboardReadWrite ||
            (isTrustedLocalPage && isMediaOrScreenFeature)) {
            page->setFeaturePermission(securityOrigin, feature, QWebEnginePage::PermissionGrantedByUser);
        }
        else {
            // Раньше здесь было безусловное PermissionDeniedByUser для ВСЕГО
            // остального — то есть обычные сайты никогда не могли получить
            // камеру/микрофон/геолокацию/уведомления, даже если сами не
            // предлагали спросить. Теперь настоящий диалог "Разрешить/
            // Заблокировать" с запоминанием выбора по сайту (Настройки →
            // Конфиденциальность → Разрешения сайтов), см.
            // BrowserWebView::handlePermissionRequest().
            BrowserWebView::handlePermissionRequest(page, this, securityOrigin, feature);
        }
        });

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    // Выбор источника для демонстрации экрана (экран целиком / отдельное окно).
    // Начиная с Qt 6.6 разрешения "да/нет" из featurePermissionRequested выше
    // уже НЕДОСТАТОЧНО для getDisplayMedia(): DesktopVideoCapture/
    // DesktopAudioVideoCapture там можно только разрешить/запретить как факт,
    // а КОНКРЕТНЫЙ экран или окно хостовое приложение обязано выбрать само —
    // для этого у QWebEnginePage появился отдельный сигнал desktopMediaRequested
    // с моделями доступных экранов/окон. Без этого обработчика getDisplayMedia()
    // на Qt 6.6+ не отработает вообще, даже если разрешение выше выдано.
    //
    // Примечание про "вкладку браузера" отдельно: сам список доступных
    // источников (screensModel/windowsModel) формирует Chromium внутри
    // QtWebEngine, и он перечисляет экраны и ОС-окна — отдельных вкладок
    // как самостоятельных источников там, как правило, нет (это специфика
    // полноценного Chrome, а не встраиваемого движка). Constraint
    // displaySurface:'browser' на JS-стороне (см. PageTemplates_Talk2.cpp)
    // в этом случае обычно просто откроет этот же список окон/экранов —
    // выбора именно вкладки как отдельного пункта здесь может не быть.
    //
    // ВНИМАНИЕ: если этот блок не скомпилируется — сигнатуры/роли
    // QWebEngineDesktopMediaRequest могли измениться в вашей версии Qt
    // (6.6/6.7/6.8...). Откройте <QtWebEngineCore/qwebenginedesktopmediarequest.h>
    // из вашего Qt SDK и поправьте по месту — пришлите мне точную версию Qt,
    // подгоню сигнатуру.
    connect(page, &QWebEnginePage::desktopMediaRequested, this,
        [this](QWebEngineDesktopMediaRequest request) {
            QDialog dlg(this);
            dlg.setWindowTitle(u8"Выберите, что показать");
            dlg.resize(420, 380);
            auto* layout = new QVBoxLayout(&dlg);
            layout->addWidget(new QLabel(u8"Что вы хотите показать собеседникам?", &dlg));

            auto* list = new QListWidget(&dlg);
            layout->addWidget(list);

            auto* screensModel = request.screensModel();
            auto* windowsModel = request.windowsModel();
            const int screenCount = screensModel ? screensModel->rowCount() : 0;
            const int windowCount = windowsModel ? windowsModel->rowCount() : 0;

            if (screenCount > 0) {
                auto* header = new QListWidgetItem(u8"— Экраны —");
                header->setFlags(Qt::NoItemFlags);
                list->addItem(header);
                for (int i = 0; i < screenCount; ++i) {
                    const QModelIndex idx = screensModel->index(i, 0);
                    auto* item = new QListWidgetItem(u8"🖥️ " + screensModel->data(idx, Qt::DisplayRole).toString());
                    item->setData(Qt::UserRole, true);   // true = экран
                    item->setData(Qt::UserRole + 1, i);
                    list->addItem(item);
                }
            }
            if (windowCount > 0) {
                auto* header = new QListWidgetItem(u8"— Окна —");
                header->setFlags(Qt::NoItemFlags);
                list->addItem(header);
                for (int i = 0; i < windowCount; ++i) {
                    const QModelIndex idx = windowsModel->index(i, 0);
                    auto* item = new QListWidgetItem(u8"🪟 " + windowsModel->data(idx, Qt::DisplayRole).toString());
                    item->setData(Qt::UserRole, false);  // false = окно
                    item->setData(Qt::UserRole + 1, i);
                    list->addItem(item);
                }
            }
            if (screenCount == 0 && windowCount == 0) {
                auto* empty = new QListWidgetItem(u8"Нет доступных источников для показа.");
                empty->setFlags(Qt::NoItemFlags);
                list->addItem(empty);
            }
            if (list->count() > 0) list->setCurrentRow(screenCount > 0 ? 1 : (windowCount > 0 ? 1 : 0));

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            buttons->button(QDialogButtonBox::Ok)->setText(u8"Показать");
            buttons->button(QDialogButtonBox::Cancel)->setText(u8"Отмена");
            layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

            if (dlg.exec() == QDialog::Accepted && list->currentItem()) {
                const bool isScreen = list->currentItem()->data(Qt::UserRole).toBool();
                const int row = list->currentItem()->data(Qt::UserRole + 1).toInt();
                if (isScreen && screensModel) {
                    request.selectScreen(screensModel->index(row, 0));
                }
                else if (!isScreen && windowsModel) {
                    request.selectWindow(windowsModel->index(row, 0));
                }
                else {
                    request.cancel();
                }
            }
            else {
                request.cancel();
            }
        });
#endif

    connect(page, &StormWebPage::magnetLinkActivated, this, [this](const QString& link) {
        downloadManager->addTorrent(link);
        });

    if (!isIncognito && !isGame) {
        connect(page, &StormWebPage::credentialsCaptured, this,
            [this](const QString& domain, const QString& login, const QString& password) {
                qWarning() << "[MainWindow] credentialsCaptured получен, домен:" << domain;
                showSavePasswordPrompt(domain, login, password);
            });
    }

    if (!isGame) {
        connect(page, &StormWebPage::passwordSuggestionRequested, this,
            [this, page](const QString& domain, const QString& login) {
                qWarning() << "[MainWindow] passwordSuggestionRequested получен, домен:" << domain;

                QString suggested = passwordManager->generatePassword();

                QString question = u8"Предложить надёжный пароль для " + domain;
                if (!login.isEmpty()) question += u8"\nЛогин: " + login;
                question += u8"\n\nСгенерированный пароль:\n" + suggested;

                auto reply = QMessageBox::question(this, u8"Storm Пароли", question,
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                if (reply != QMessageBox::Yes) return;

                QString escaped = suggested;
                escaped.replace("\\", "\\\\").replace("'", "\\'");
                QString fillJs = QStringLiteral(
                    "(function(){"
                    "  var pw='%1';"
                    "  document.querySelectorAll('input[type=\"password\"]').forEach(function(f){"
                    "    f.value = pw;"
                    "    f.dispatchEvent(new Event('input', {bubbles:true}));"
                    "    f.dispatchEvent(new Event('change', {bubbles:true}));"
                    "  });"
                    "})();"
                ).arg(escaped);
                page->runJavaScript(fillJs);

                passwordManager->savePassword(domain, login, suggested);
                statusBar()->showMessage(u8"✅ Сгенерированный пароль сохранён в Storm Vault", 4000);
            });
    }

    connect(page, &QWebEnginePage::newWindowRequested, this, [this, isIncognito](QWebEngineNewWindowRequest& request) {
        addNewTab(QUrl(), isIncognito);
        QWidget* newWidget = tabWidget->widget(tabWidget->count() - 1);
        if (auto* newView = qobject_cast<QWebEngineView*>(newWidget)) {
            request.openIn(newView->page());
        }
        });

    connect(view, &QWebEngineView::urlChanged, this, &MainWindow::updateAddressBar);
    // C-2: у обеих лямбд ниже раньше НЕ БЫЛО приёмника-контекста. Они жили,
    // пока жив сам view, и продолжали срабатывать ПОСЛЕ переноса вкладки в
    // другое окно — уже по закрытому/уничтоженному исходному окну
    // (use-after-free, краш при отрыве вкладок). Приёмник this гарантирует
    // автоснятие связи при разрушении окна-владельца.
    connect(view, &QWebEngineView::titleChanged, this, [this, view, isIncognito](const QString& title) {
        int idx = tabWidget->indexOf(view);
        if (idx == -1) return;
        QString finalTitle = isIncognito ? (u8"🕶 " + title) : title;
        // v1.2.9: у закреплённой вкладки текст в полосе остаётся пустым
        // (компактная иконка), полный заголовок копится в pinnedTitle —
        // оттуда его берут меню вкладок, тултип и сохранение в сессию.
        if (view->property("tabPinned").toBool()) {
            view->setProperty("pinnedTitle", finalTitle);
            return;
        }
        tabWidget->setTabText(idx, finalTitle);
        });

    connect(view, &QWebEngineView::loadFinished, this, [this, view, isIncognito](bool ok) {
        if (ok && !isIncognito) {
            dbManager.addHistoryItem(view->title(), view->url().toString());
        }
        if (ok && tabWidget->currentWidget() == view) {
            QPixmap pix = view->grab();
            if (!pix.isNull()) view->setProperty("cachedPreview", pix);
        }
        });

    TabSpinner* spinner = new TabSpinner(tabWidget, view, this);
    connect(view, &QWebEngineView::loadStarted, spinner, &TabSpinner::start);
    connect(view, &QWebEngineView::loadFinished, spinner, &TabSpinner::stop);
    connect(view, &QWebEngineView::iconChanged, spinner, &TabSpinner::onIconChanged);
    connect(page, &QWebEnginePage::recentlyAudibleChanged, spinner, &TabSpinner::onAudibleChanged);

    // Кнопка Обновить/Стоп в BrowserTopBar отражает состояние загрузки
    // только активной вкладки — свойство "isLoading" на view хранит
    // актуальное состояние для ЛЮБОЙ вкладки (используется при переключении
    // вкладок в currentChanged, см. setupUi()), а topBar->setLoadingState()
    // дёргаем только когда грузится именно текущая вкладка.
    connect(view, &QWebEngineView::loadStarted, this, [this, view]() {
        view->setProperty("isLoading", true);
        if (tabWidget->currentWidget() == view) {
            topBar->setLoadingState(true);
        }
        });
    connect(view, &QWebEngineView::loadFinished, this, [this, view](bool /*ok*/) {
        view->setProperty("isLoading", false);
        if (tabWidget->currentWidget() == view) {
            topBar->setLoadingState(false);
        }
        });
}


void MainWindow::addNewTab(const QUrl& url, bool isIncognito) {
    BrowserWebView* view = new BrowserWebView(this, this);

    QString urlStr = url.toString();
    bool isGame = urlStr.startsWith("storm-game:") || (urlStr.contains(".swf", Qt::CaseInsensitive) && !urlStr.startsWith("http"));

    QWebEngineProfile* profile = nullptr;

    if (isGame) {
        profile = sharedAuxProfile(true);
    }
    else if (isIncognito) {
        profile = sharedAuxProfile(false);
    }
    else {
        profile = m_mainProfile;
    }

    if (profile) {
        profile->setSpellCheckEnabled(true);
        profile->setSpellCheckLanguages(QStringList() << "en-US" << "ru-RU");
    }

    StormWebPage* page = new StormWebPage(profile, this, view);
    view->setPage(page);

    // Доверие сертификатам Минцифры России (+ Windows/свои, если включены
    // в настройках) — см. CertificateManager.h. Логика одна на все вкладки
    // и живёт в StormWebPage::handleCertificateError (S-1: раньше ЗДЕСЬ же
    // подключался второй обработчик, а в самом StormWebPage невалидные
    // сертификаты принимались БЕЗУСЛОВНО — это перечёркивало осторожную
    // проверку и открывало дорогу MITM-атакам).

    // C-2/P1-2: ВСЕ подключения вкладки (страница + виджет + спиннер)
    // собраны в wireTab() — он вызывается и при создании вкладки, и при
    // переносе её в другое окно (attachTab), чтобы вкладка нигде не
    // оставалась с обработчиками, указывающими на уже закрытое окно.
    view->setProperty("isIncognito", isIncognito);
    view->setProperty("isGameTab", isGame);
    wireTab(view, page);

    view->settings()->setAttribute(QWebEngineSettings::PdfViewerEnabled, true);
    view->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, true);
    view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    view->settings()->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, true);
    view->settings()->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    view->settings()->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);

    if (urlStr == "storm-talk" || urlStr == "storm://talk") {
        // TalkBridge нужен только для одной вещи: отдельного always-on-top
        // окошка с двумя превью (собеседник / что уходит от вас) во время
        // демонстрации экрана — см. TalkBridge.h и TalkPipOverlay.h. Сам
        // звонок (WebRTC/чат/фон) через этот канал не идёт, только редкие
        // маленькие JPEG-кадры для этого окошка.
        QWebChannel* talkChannel = new QWebChannel(page);
        TalkBridge* talkBridge = new TalkBridge(page);
        talkChannel->registerObject("talkBridge", talkBridge);
        page->setWebChannel(talkChannel);

        view->setProperty("stormInternalId", "storm-talk");
        view->setHtml(pageTemplates.getTalkHtml(), QUrl("http://localhost/storm-talk"));
    }
    else if (isGame) {
        page->setBackgroundColor(QColor("#070a12"));

        QString filename = urlStr;
        if (filename.contains("/")) filename = filename.section('/', -1);
        if (filename.startsWith("storm-game:")) filename = filename.mid(11);

        QString gamesDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/StormBrowser/plugins/games";
        QString swfFullPath = gamesDir + "/" + filename;
        QString rawName = filename;
        rawName.replace(".swf", "").replace("_", " ");

        QString htmlContent = pageTemplates.getArcadeGameHtml(swfFullPath, rawName);
        QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/StormBrowser/arcade";
        QDir().mkpath(tempDir);
        QString tempHtmlPath = tempDir + "/" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".html";
        QFile tempHtmlFile(tempHtmlPath);
        if (tempHtmlFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            tempHtmlFile.write(htmlContent.toUtf8());
            tempHtmlFile.close();
            view->load(QUrl::fromLocalFile(tempHtmlPath));
        }
        else {
            qWarning().noquote() << "🕹️ [Arcade Widget] ❌ Не удалось записать временный HTML игры:" << tempHtmlPath;
            view->setHtml(htmlContent, QUrl("https://cdn.jsdelivr.net/npm/@ruffle-rs/ruffle/"));
        }
    }
    else if (url.toString() == "storm://home") {
        QWebChannel* homeChannel = new QWebChannel(page);

        HomeAIBridge* homeAiBridge = new HomeAIBridge(page);
        homeChannel->registerObject("homeAI", homeAiBridge);

        HomeBridge* homeBridge = new HomeBridge(this, page);
        homeChannel->registerObject("homeBridge", homeBridge);

        page->setWebChannel(homeChannel);

        view->setProperty("stormInternalId", "storm://home");
        view->setHtml(pageTemplates.getHomePageHtml(), QUrl("http://storm.home"));
    }
    else if (url.toString() == "storm://settings") {
        QWebChannel* settingsChannel = new QWebChannel(page);
        SettingsBridge* settingsBridge = new SettingsBridge(this, page);
        settingsChannel->registerObject("settingsBridge", settingsBridge);
        page->setWebChannel(settingsChannel);

        view->setProperty("stormInternalId", "storm://settings");
        view->setHtml(pageTemplates.getSettingsHtml(), QUrl("http://storm.settings"));
    }
    else if (url.toString() == "storm://cloud") {
        QWebChannel* cloudChannel = new QWebChannel(page);
        StormCloudBridge* cloudBridge = new StormCloudBridge(this, page);
        cloudChannel->registerObject("cloudBridge", cloudBridge);
        page->setWebChannel(cloudChannel);

        view->setProperty("stormInternalId", "storm://cloud");
        view->setHtml(pageTemplates.getStormCloudHtml(), QUrl("http://storm.cloud"));
    }
    else if (url.toString() == "storm://bookmarks") {
        QWebChannel* bookmarksChannel = new QWebChannel(page);
        BookmarksBridge* bookmarksBridge = new BookmarksBridge(this, page);
        bookmarksChannel->registerObject("bookmarksBridge", bookmarksBridge);
        page->setWebChannel(bookmarksChannel);

        view->setProperty("stormInternalId", "storm://bookmarks");
        view->setHtml(getBookmarksHtml(), QUrl("http://storm.bookmarks"));
    }
    else if (url.toString() == "storm://downloads") {
        // Полноценная страница со ВСЕЙ историей загрузок сразу (и обычные, и
        // торрент), в отличие от мини-попапа DownloadManager, который теперь
        // открывается кнопкой на тулбаре (см. DownloadManager::popupBelow())
        // и показывает только текущую сессию — см. DownloadsBridge.h.
        QWebChannel* downloadsChannel = new QWebChannel(page);
        DownloadsBridge* downloadsBridge = new DownloadsBridge(this, page);
        downloadsChannel->registerObject("downloadsBridge", downloadsBridge);
        page->setWebChannel(downloadsChannel);

        view->setProperty("stormInternalId", "storm://downloads");
        view->setHtml(getDownloadsHtml(), QUrl("http://storm.downloads"));
    }
    else if (url.toString() == "storm://help" || url.toString() == "storm://help/") {
        view->setProperty("stormInternalId", "storm://help");
        view->setHtml(getHelpHtml(), QUrl("http://storm.help"));
    }
    else if (url.toString() == "storm://newtab" || url.isEmpty()) {
        view->setProperty("stormInternalId", "storm://newtab");
        if (isIncognito) {
            view->setHtml(pageTemplates.getIncognitoHtml(), QUrl("http://storm.newtab"));
        }
        else {
            view->setHtml(pageTemplates.getNewTabHtml(), QUrl("http://storm.newtab"));
        }
    }
    else if (url.isLocalFile()) {
        QFile file(url.toLocalFile());
        if (!file.exists()) {
            QMessageBox::warning(this, u8"Ошибка", u8"Файл не найден:\n" + url.toLocalFile());
            // В view к этому моменту уже созданы StormWebPage с профилем и все
            // подключения wireTab — раньше здесь был голый return, и весь этот
            // объект утекал (вкладка при этом даже не вставлялась в таб-бар).
            view->deleteLater();
            return;
        }
        view->setUrl(url);
    }
    else {
        view->setUrl(url);
    }

    int index = tabWidget->count();
    QString tabTitle = isIncognito ? u8"🕶 Загрузка..." : u8"Загрузка...";
    tabWidget->insertTab(index, view, tabTitle);
    tabWidget->setCurrentIndex(index);
    updateTabsListButton();
}

// Восстановление встроенной страницы после F5 / краха рендер-процесса.
// У внутренних вкладок (storm-talk, storm://home, ...) нет реального сетевого
// адреса — содержимое живёт в setHtml с фиктивным baseUrl, и обычный reload()
// ходил бы по этому baseUrl в сеть (http://localhost/storm-talk → ошибка
// соединения, вкладка превращалась в страницу ошибки). WebChannel со всеми
// мостами остаётся прикреплён к page и переживает перезаливку шаблона.
bool MainWindow::reloadInternalTab(QWebEngineView* view) {
    if (!view || !view->page()) return false;
    const QString id = view->property("stormInternalId").toString();
    if (id.isEmpty()) return false;

    QString html;
    QUrl baseUrl;
    if (id == QLatin1String("storm-talk")) {
        html = pageTemplates.getTalkHtml();
        baseUrl = QUrl(QStringLiteral("http://localhost/storm-talk"));
    }
    else if (id == QLatin1String("storm://home")) {
        html = pageTemplates.getHomePageHtml();
        baseUrl = QUrl(QStringLiteral("http://storm.home"));
    }
    else if (id == QLatin1String("storm://settings")) {
        html = pageTemplates.getSettingsHtml();
        baseUrl = QUrl(QStringLiteral("http://storm.settings"));
    }
    else if (id == QLatin1String("storm://cloud")) {
        html = pageTemplates.getStormCloudHtml();
        baseUrl = QUrl(QStringLiteral("http://storm.cloud"));
    }
    else if (id == QLatin1String("storm://bookmarks")) {
        html = getBookmarksHtml();
        baseUrl = QUrl(QStringLiteral("http://storm.bookmarks"));
    }
    else if (id == QLatin1String("storm://downloads")) {
        html = getDownloadsHtml();
        baseUrl = QUrl(QStringLiteral("http://storm.downloads"));
    }
    else if (id == QLatin1String("storm://help")) {
        html = getHelpHtml();
        baseUrl = QUrl(QStringLiteral("http://storm.help"));
    }
    else if (id == QLatin1String("storm://newtab")) {
        html = view->property("isIncognito").toBool()
            ? pageTemplates.getIncognitoHtml()
            : pageTemplates.getNewTabHtml();
        baseUrl = QUrl(QStringLiteral("http://storm.newtab"));
    }
    else {
        return false;
    }

    view->setHtml(html, baseUrl);
    return true;
}



void MainWindow::closeTab(int index) {
    if (tabWidget->count() > 1) {
        if (tabWidget->currentIndex() == index && index > 0) {
            tabWidget->setCurrentIndex(index - 1);
        }
        QWidget* widget = tabWidget->widget(index);

        // Для "🕒 Недавно закрытые" (гамбургер-меню → История, Ctrl+Shift+T) —
        // кроме инкогнито-вкладок, как и в обычных браузерах: приватная
        // сессия не должна оставлять следов после закрытия.
        if (auto* closedView = qobject_cast<QWebEngineView*>(widget)) {
            if (closedView->page() && !closedView->page()->profile()->isOffTheRecord()) {
                recordClosedTab(closedView->url());
            }
        }

        tabWidget->removeTab(index);
        updateTabsListButton();
        delete widget;
    }
    else {
        window()->close();
    }
}


void MainWindow::showTabContextMenu(const QPoint& pos) {
    int index = tabWidget->tabBar()->tabAt(pos);
    if (index == -1) return;

    QMenu menu(this);
    menu.setStyleSheet(this->styleSheet());

    QAction* newTabAct = menu.addAction(u8"➕ Новая вкладка");
    connect(newTabAct, &QAction::triggered, this, [this]() {
        addNewTab(QUrl("storm://newtab"));
        });

    menu.addSeparator();

    QMenu* groupMenu = menu.addMenu(u8"📁 Назначение и цвет");
    groupMenu->setStyleSheet(this->styleSheet());

    struct Category { QString name; QString icon; QColor color; };
    QList<Category> categories = {
        { u8"Работа", u8"💼", QColor("#56d39b") },
        { u8"Учеба",  u8"🎓", QColor("#ffc857") },
        { u8"Видео",  u8"🎬", QColor("#ff5f5f") },
        { u8"Музыка", u8"🎧", QColor("#a371f7") },
        { u8"Без категории", u8"⚪", QColor("#8b949e") }
    };

    for (const auto& cat : categories) {
        QAction* catAct = groupMenu->addAction(cat.icon + " " + cat.name);
        connect(catAct, &QAction::triggered, this, [this, index, cat]() {
            setTabCategory(index, cat.name, cat.color);
            });
    }

    groupMenu->addSeparator();
    QAction* sortAct = groupMenu->addAction(u8"🔄 Сгруппировать все по категориям");
    connect(sortAct, &QAction::triggered, this, &MainWindow::groupTabsByCategory);

    menu.addSeparator();

    // v1.2.9: закрепление вкладки — компактная вкладка-иконка в начале
    // полосы, без случайного закрытия крестиком (см. StormTabBar).
    if (QWidget* pinTarget = tabWidget->widget(index)) {
        const bool alreadyPinned = pinTarget->property("tabPinned").toBool();
        QAction* pinAct = menu.addAction(alreadyPinned
            ? QString(u8"📌 Открепить вкладку")
            : QString(u8"📌 Закрепить вкладку"));
        connect(pinAct, &QAction::triggered, this, [this, index]() {
            toggleTabPin(index);
            });
    }

    menu.addSeparator();

    QAction* detachAct = menu.addAction(u8"🗗 Открепить в новое окно");
    connect(detachAct, &QAction::triggered, this, [this, index]() {
        detachTab(index);
        });

    // Возврат вкладки в основное окно: раньше единственный путь обратно —
    // перетащить вкладку мышью И ТОЧНО попасть в полоску вкладок другого
    // окна; при промахе открывалось очередное новое окно. Теперь и отсюда
    // можно вернуть вкладку в основной окно одним кликом.
    if (MainWindow* homeWindow = findHomeWindowForReattach()) {
        QAction* reattachAct = menu.addAction(u8"⬅ Вернуть в основное окно");
        connect(reattachAct, &QAction::triggered, this, [this, index, homeWindow]() {
            moveTabToWindow(index, homeWindow);
            });
    }

    menu.addSeparator();

    QAction* reloadAct = menu.addAction(u8"↻ Обновить вкладку");
    connect(reloadAct, &QAction::triggered, this, [this, index]() {
        auto* view = qobject_cast<QWebEngineView*>(tabWidget->widget(index));
        if (view && !reloadInternalTab(view)) view->reload();
        });

    QAction* closeAct = menu.addAction(u8"❌ Закрыть вкладку");
    connect(closeAct, &QAction::triggered, this, [this, index]() {
        closeTab(index);
        });

    QAction* closeOthersAct = menu.addAction(u8"🗑️ Закрыть другие вкладки");
    connect(closeOthersAct, &QAction::triggered, this, [this, index]() {
        for (int i = tabWidget->count() - 1; i >= 0; --i) {
            if (i != index) closeTab(i);
        }
        });

    menu.exec(tabWidget->tabBar()->mapToGlobal(pos));
}


static QString htmlUnescapeMinimal(const QString& input) {
    QString result;
    result.reserve(input.size());
    int i = 0;
    while (i < input.size()) {
        if (input[i] == QLatin1Char('&')) {
            if (input.mid(i, 5) == "&amp;") { result += '&'; i += 5; continue; }
            if (input.mid(i, 4) == "&lt;") { result += '<'; i += 4; continue; }
            if (input.mid(i, 4) == "&gt;") { result += '>'; i += 4; continue; }
            if (input.mid(i, 6) == "&quot;") { result += '"'; i += 6; continue; }
            if (input.mid(i, 5) == "&#39;") { result += '\''; i += 5; continue; }
            if (input.mid(i, 6) == "&apos;") { result += '\''; i += 6; continue; }
        }
        result += input[i];
        ++i;
    }
    return result;
}


void MainWindow::exportTabs() {
    QString fileName = QFileDialog::getSaveFileName(this, u8"Экспорт открытых вкладок", "", u8"HTML Файлы (*.html)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);

        out << "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n";
        out << "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n";
        out << "<TITLE>Storm Browser Tabs</TITLE>\n";
        out << "<H1>Storm Browser Tabs</H1>\n";
        out << "<DL><p>\n";

        for (int i = 0; i < tabWidget->count(); ++i) {
            if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->widget(i))) {
                QString url = view->url().toString();
                QString title = view->title();
                if (!url.isEmpty() && url != "storm://newtab" && url != "storm://home") {
                    out << "    <DT><A HREF=\"" << url.toHtmlEscaped() << "\">" << title.toHtmlEscaped() << "</A>\n";
                }
            }
        }
        out << "</DL><p>\n";
        file.close();
        QMessageBox::information(this, u8"Экспорт", u8"Открытые вкладки успешно сохранены в HTML файл!");
    }
}


void MainWindow::importTabs() {
    QString fileName = QFileDialog::getOpenFileName(this, u8"Импорт вкладок в закладки", "", u8"HTML Файлы (*.html)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString content = in.readAll();
        file.close();

        QRegularExpression re("<a[^>]*href=\"([^\"]*)\"[^>]*>([^<]*)</a>", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator i = re.globalMatch(content);

        int count = 0;
        QSet<QString> seenUrls;
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            QString url = htmlUnescapeMinimal(match.captured(1).trimmed());
            QString title = htmlUnescapeMinimal(match.captured(2).trimmed());

            if (!url.isEmpty() && !seenUrls.contains(url)) {
                seenUrls.insert(url);
                dbManager.addBookmark(title.isEmpty() ? url : title, url);
                count++;
            }
        }
        loadBookmarksIntoMenu();
        QMessageBox::information(this, u8"Импорт", QString(u8"Успешно добавлено %1 вкладок в ваши закладки!").arg(count));
    }
}


void MainWindow::openImportedTabs() {
    QString fileName = QFileDialog::getOpenFileName(this, u8"Открыть вкладки из HTML файла", "", u8"HTML Файлы (*.html)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString content = in.readAll();
        file.close();

        QRegularExpression re("<a[^>]*href=\"([^\"]*)\"[^>]*>([^<]*)</a>", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator i = re.globalMatch(content);

        int count = 0;
        QSet<QString> seenUrls;
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            QString url = htmlUnescapeMinimal(match.captured(1).trimmed());
            if (!url.isEmpty() && !seenUrls.contains(url)) {
                seenUrls.insert(url);
                addNewTab(QUrl(url));
                count++;
            }
            if (count >= 20) {
                QMessageBox::warning(this, u8"Лимит", u8"Открыто 20 вкладок. Остальные пропущены для экономии ОЗУ.");
                break;
            }
        }
    }
}


void MainWindow::setTabCategory(int index, const QString& category, const QColor& color) {
    QWidget* widget = tabWidget->widget(index);
    if (!widget) return;

    widget->setProperty("tabCategory", category);
    widget->setProperty("tabColor", color.name());

    tabWidget->tabBar()->update();

    QString currentTitle = tabTitleForTransport(index);
    tabWidget->tabBar()->setTabToolTip(index, QString(u8"[%1] %2").arg(category, currentTitle));
}


void MainWindow::groupTabsByCategory() {
    int count = tabWidget->count();
    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            QString catI = tabWidget->widget(i)->property("tabCategory").toString();
            QString catJ = tabWidget->widget(j)->property("tabCategory").toString();

            if (!catJ.isEmpty() && catI > catJ) {
                tabWidget->tabBar()->moveTab(j, i);
            }
        }
    }
}


void MainWindow::detachTab(int index) {
    if (tabWidget->count() <= 1) {
        return;
    }

    QWidget* tabView = tabWidget->widget(index);
    QString title = tabTitleForTransport(index);
    QIcon icon = tabWidget->tabIcon(index);

    // C-2: снимаем ВСЕ подключения вкладки (и её вида, и её страницы) к
    // ЭТОМУ окну ДО того, как она уедет в новое. Раньше лямбды оставались
    // висеть на старом окне: пока оно живо — бессмысленные срабатывания,
    // после закрытия — use-after-free и самопроизвольное закрытие всего
    // браузера. Новое окно переведёт вкладку на себя в attachTab()->wireTab().
    if (auto* stormView = qobject_cast<BrowserWebView*>(tabView)) {
        stormView->disconnect(this);
        if (auto* sp = qobject_cast<StormWebPage*>(stormView->page())) {
            sp->disconnect(this);
        }
    }
    else {
        tabView->disconnect(this);
    }

    // Снимаем и фильтр перетаскивания (он ставится на view и его детей
    // от имени окна-владельца — см. wireTab/attachTab): в новом окне
    // вкладка получит фильтр уже нового окна.
    if (m_tabDragFilter && qobject_cast<BrowserWebView*>(tabView)) {
        tabView->removeEventFilter(m_tabDragFilter);
        const auto children = tabView->children();
        for (QObject* child : children) {
            child->removeEventFilter(m_tabDragFilter);
        }
    }

    tabWidget->removeTab(index);
    updateTabsListButton();

    MainWindow* newWindow = new MainWindow(nullptr, true);
    newWindow->resize(1080, 650);

    newWindow->attachTab(tabView, title);
    newWindow->getTabWidget()->setTabIcon(0, icon);

    newWindow->move(QCursor::pos() - QPoint(150, 25));
    newWindow->show();
}


void MainWindow::attachTab(QWidget* tabView, const QString& title, const QIcon& icon, int insertIndex) {
    // C-2/P1-2: запоминаем, в каком окне вкладка жила ДО переноса, чтобы
    // корректно снять её сигналы со СТАРОГО окна. При отрыве вкладки это
    // делает и сам detachTab, а при перетаскивании между окнами вкладка
    // попадает сюда напрямую, мимо detachTab — поэтому перестраховываемся
    // и здесь.
    MainWindow* prevOwner = nullptr;
    StormWebPage* prevPage = nullptr;
    if (auto* stormView = qobject_cast<BrowserWebView*>(tabView)) {
        prevOwner = stormView->ownerMainWindow();
        prevPage = qobject_cast<StormWebPage*>(stormView->page());
    }
    if (prevOwner && prevOwner != this) {
        tabView->disconnect(prevOwner);
        if (prevPage) prevPage->disconnect(prevOwner);
        // Снимаем и drag-фильтр прежнего владельца (с view и его детей),
        // иначе события перетаскивания над этой вкладкой достались бы
        // старому окну.
        if (prevOwner->m_tabDragFilter) {
            tabView->removeEventFilter(prevOwner->m_tabDragFilter);
            const auto prevChildren = tabView->children();
            for (QObject* child : prevChildren) {
                child->removeEventFilter(prevOwner->m_tabDragFilter);
            }
        }
    }

    tabView->setParent(tabWidget);

    int insertIdx = (insertIndex >= 0 && insertIndex <= tabWidget->count())
        ? insertIndex
        : tabWidget->count();

    tabWidget->insertTab(insertIdx, tabView, icon, title);
    // v1.2.9: закреплённая вкладка приехала с полным заголовком (title),
    // но в полосе должна остаться компактной иконкой — гасим текст.
    if (tabView->property("tabPinned").toBool()) {
        tabWidget->setTabText(insertIdx, QString());
    }
    tabWidget->setCurrentIndex(insertIdx);
    updateTabsListButton();

    // Фильтр перетаскивания от ЭТОГО окна — на принятую вкладку (см.
    // wireTab). Нужен и когда prevOwner == this (вкладку вернули в своё
    // же окно через drop на веб-контент): фильтр с неё уже снят в
    // startDrag()'е источника.
    if (m_tabDragFilter) {
        tabView->installEventFilter(m_tabDragFilter);
        const auto curChildren = tabView->children();
        for (QObject* child : curChildren) {
            child->installEventFilter(m_tabDragFilter);
        }
    }

    if (auto* stormView = qobject_cast<BrowserWebView*>(tabView)) {
        // P1-2: переносим "владельца" вкладки. BrowserWebView (контекстные
        // меню) и StormWebPage (мост автозаполнения паролей) держали указатель
        // на окно, где вкладку СОЗДАЛИ, — после закрытия того окна указатели
        // становились висячими. Теперь они обновляются при каждом переносе.
        stormView->setOwnerMainWindow(this);
        if (prevPage) prevPage->setMainWindow(this);

        // Полная перепривязка ВСЕХ обработчиков вкладки к ЭТОМУ окну —
        // адресная строка, заголовки, история, разрешения, magnet-ссылки,
        // перехват паролей, попапы, спиннер, индикатор загрузки.
        if (prevOwner != this && prevPage) {
            wireTab(stormView, prevPage);
        }
    }
}

// =========================================================================
// v1.2.9: раскрыть боковую панель на вкладке Заметок. Вызывается
// web-clipper'ом после сохранения вырезки — результат виден сразу, а не
// «куда-то молча сохранилось».
// =========================================================================
void MainWindow::openNotesPanel() {
    if (!sidebar) return;
    sidebar->show();
    if (NotesWidget* notes = findChild<NotesWidget*>()) {
        sidebar->openItem(notes);
    }
}

// =========================================================================
// v1.2.9: ГОРЯЧИЕ КЛАВИШИ СЕССИЙ (Ctrl+Alt+S / Ctrl+Alt+R)
// Тот же механизм saved_sessions, что и дропдаун «💾 Сессии ▾» на
// storm://bookmarks, — просто без открытия страницы закладок. Стек-объект
// BookmarksBridge безопасен: родителя не передаём, никто его не удалит
// кроме нас самих (ensureSchema() идемпотентен, повторный запуск безвреден).
// =========================================================================
void MainWindow::quickSaveSession() {
    BookmarksBridge bridge(this);
    QString name;
    const QString err = bridge.quickSaveCurrentSession(&name);
    if (err.isEmpty()) {
        statusBar()->showMessage(
            QString(u8"✅ Сессия «%1» сохранена — Закладки → 💾 Сессии ▾").arg(name), 6000);
    }
    else {
        statusBar()->showMessage(u8"⚠ " + err, 6000);
    }
}

void MainWindow::quickRestoreLastSession() {
    BookmarksBridge bridge(this);
    int opened = 0;
    QString name;
    const QString err = bridge.quickRestoreLatestSession(&opened, &name);
    if (err.isEmpty()) {
        statusBar()->showMessage(
            QString(u8"✅ Сессия «%1» восстановлена: +%2 вкладок").arg(name).arg(opened), 6000);
    }
    else {
        statusBar()->showMessage(u8"⚠ " + err, 6000);
    }
}

// =========================================================================
// v1.2.9: ЗАКРЕПЛЁННЫЕ ВКЛАДКИ (PIN)
// Закреплённая вкладка: компактная (только иконка, kPinnedTabWidth в
// StormTabBar), без крестика закрытия (случайный клик её не убьёт),
// живёт в начале полосы. Полный заголовок хранится в свойстве виджета
// pinnedTitle — по нему работают меню «☰ N ▾», тултипы, детач/перенос
// между окнами и сохранение в сессии.
// =========================================================================
void MainWindow::setTabPinned(QWidget* view, bool pinned) {
    if (!view) return;

    // Ищем QTabWidget, в котором вкладка живёт ПРЯМО СЕЙЧАС: метод вызывается
    // и для своих вкладок, и из BookmarksBridge для только что добавленных —
    // искать через tabWidget этого окна было бы слишком самонадеянно.
    QTabWidget* owner = nullptr;
    for (QWidget* w = view->parentWidget(); w && !owner; w = w->parentWidget()) {
        owner = qobject_cast<QTabWidget*>(w);
    }
    if (!owner) owner = tabWidget;
    const int idx = owner->indexOf(view);
    if (idx < 0) return;

    view->setProperty("tabPinned", pinned);
    if (pinned) {
        // Полный заголовок забираем один раз (дальше его обновляет лямбда
        // titleChanged в wireTab), текст вкладки гасим — остаётся иконка.
        if (view->property("pinnedTitle").toString().isEmpty()) {
            view->setProperty("pinnedTitle", owner->tabText(idx));
        }
        owner->setTabText(idx, QString());
    }
    else {
        owner->setTabText(idx, view->property("pinnedTitle").toString());
    }
    // setTabText сам триггерит переразмерку (tabSizeHint смотрит свойство
    // tabPinned), но подстрахуемся явной перерисовкой полосы.
    owner->tabBar()->update();
}

void MainWindow::toggleTabPin(int index) {
    QWidget* view = tabWidget->widget(index);
    if (!view) return;

    const bool willPin = !view->property("tabPinned").toBool();
    setTabPinned(view, willPin);

    // Перестановка: закреплённые держим в начале полосы (после уже
    // закреплённых), откреплённые возвращаем сразу после закреплённого блока.
    int target = 0;
    if (!willPin) {
        for (int i = 0; i < tabWidget->count(); ++i) {
            if (tabWidget->widget(i)->property("tabPinned").toBool()) {
                target = i + 1;
            }
        }
    }
    const int cur = tabWidget->indexOf(view);
    if (target != cur) {
        tabWidget->tabBar()->moveTab(cur, target);
    }
    updateTabsListButton();
}

QString MainWindow::tabTitleForTransport(int index) const {
    QWidget* w = tabWidget->widget(index);
    if (w && w->property("tabPinned").toBool()) {
        const QString pinned = w->property("pinnedTitle").toString();
        if (!pinned.isEmpty()) return pinned;
    }
    return tabWidget->tabText(index);
}

// Поиск окна, куда возвращать вкладку из откреплённого: предпочитаем
// основное (не откреплённое) окно; если таких нет (пользователь закрыл
// главное, а детач-окна остались) — любое другое окно браузера.
MainWindow* MainWindow::findHomeWindowForReattach() const {
    MainWindow* anyOther = nullptr;
    // P1-2 (compile fix): topLevelWidgets() — СТАТИЧЕСКИЙ метод QApplication,
    // а не QWidget (QApplication::topLevelWidgets() возвращает список всех
    // окон приложения). Именно из-за QWidget::topLevelWidgets() MSVC сыпал
    // C2039/C3861, а за ними каскад C3312/C2143 на range-for ниже.
    const QWidgetList tops = QApplication::topLevelWidgets();
    for (QWidget* w : tops) {
        auto* mw = qobject_cast<MainWindow*>(w);
        if (!mw || mw == this) continue;
        if (!mw->property("isDetachedWindow").toBool()) {
            return mw; // основное окно — идеальная цель
        }
        if (!anyOther) anyOther = mw;
    }
    return anyOther;
}

// Перенос вкладки index ЭТОГО окна в окно target (меню «⬅ Вернуть в
// основное окно»). Повторяет логику detachTab (снятие сигналов и
// drag-фильтра, removeTab), но вкладка прицепляется к СУЩЕСТВУЮЩЕМУ окну,
// а не к новому. Если после переноса здесь не осталось вкладок —
// откреплённое окно закрывается (у него стоит WA_DeleteOnClose).
void MainWindow::moveTabToWindow(int index, MainWindow* target) {
    if (!target || target == this) return;
    QWidget* tabView = tabWidget->widget(index);
    if (!tabView) return;

    QString title = tabTitleForTransport(index);
    QIcon icon = tabWidget->tabIcon(index);

    // Снимаем все подключения вкладки к ЭТОМУ окну (как detachTab) —
    // целевое окно переведёт вкладку на себя в attachTab()->wireTab().
    if (auto* stormView = qobject_cast<BrowserWebView*>(tabView)) {
        stormView->disconnect(this);
        if (auto* sp = qobject_cast<StormWebPage*>(stormView->page())) {
            sp->disconnect(this);
        }
        if (m_tabDragFilter) {
            tabView->removeEventFilter(m_tabDragFilter);
            const auto children = tabView->children();
            for (QObject* child : children) {
                child->removeEventFilter(m_tabDragFilter);
            }
        }
    }
    else {
        tabView->disconnect(this);
    }

    tabWidget->removeTab(index);
    updateTabsListButton();

    target->attachTab(tabView, title, icon);
    target->raise();
    target->activateWindow();

    // Пустое откреплённое окно больше не нужно — закрываем (WA_DeleteOnClose
    // у детач-окон чистит память). Основное окно с нулём вкладок не трогаем.
    if (tabWidget->count() == 0 && property("isDetachedWindow").toBool()) {
        close();
    }
}

// =========================================================================
// Кнопка «☰ N ▾» в правом углу полосы вкладок: счётчик + выпадающий список
// всех открытых вкладок (у не влезающих в полосу вкладок заголовки всё
// равно не видны — раньше их можно было найти только кнопками прокрутки).
// =========================================================================
void MainWindow::updateTabsListButton() {
    if (!m_tabsListBtn || !tabWidget) return;
    m_tabsListBtn->setText(QString(u8"☰ %1 ▾").arg(tabWidget->count()));
}

void MainWindow::rebuildTabsMenu(QMenu* menu) {
    if (!menu || !tabWidget) return;
    menu->clear();

    for (int i = 0; i < tabWidget->count(); ++i) {
        QString title = tabWidget->tabText(i);
        // v1.2.9: у закреплённой вкладки текст пуст — берём полный заголовок
        // из pinnedTitle и помечаем булавкой, чтобы в списке их было видно.
        if (title.isEmpty()) {
            QWidget* pw = tabWidget->widget(i);
            if (pw && pw->property("tabPinned").toBool()) {
                title = u8"📌 " + pw->property("pinnedTitle").toString();
            }
        }
        if (title.isEmpty()) title = u8"(без названия)";
        // Активную вкладку помечаем жирным — в списке из многих похожих
        // заголовков сразу видно, где сейчас находишься.
        QAction* act = menu->addAction(tabWidget->tabIcon(i), title);
        if (i == tabWidget->currentIndex()) {
            QFont bold = act->font();
            bold.setBold(true);
            act->setFont(bold);
        }
        QWidget* pageWidget = tabWidget->widget(i);
        connect(act, &QAction::triggered, this, [this, pageWidget]() {
            const int idx = tabWidget->indexOf(pageWidget);
            if (idx >= 0) tabWidget->setCurrentIndex(idx);
            });
    }

    if (tabWidget->count() == 0) {
        QAction* emptyAct = menu->addAction(u8"Нет открытых вкладок");
        emptyAct->setEnabled(false);
    }

    menu->addSeparator();
    QAction* newTabAct = menu->addAction(u8"➕ Новая вкладка");
    connect(newTabAct, &QAction::triggered, this, [this]() {
        addNewTab(QUrl("storm://newtab"));
        });
}