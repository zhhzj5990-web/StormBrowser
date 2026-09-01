#include "MainWindow.h"
#include "ThemeManager.h"
#include "StormTabBar.h"
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


void MainWindow::addNewTab(const QUrl& url, bool isIncognito) {
    BrowserWebView* view = new BrowserWebView(this, this);

    QString urlStr = url.toString();
    bool isGame = urlStr.startsWith("storm-game:") || (urlStr.contains(".swf", Qt::CaseInsensitive) && !urlStr.startsWith("http"));

    QWebEngineProfile* profile = nullptr;

    if (isGame) {
        profile = new QWebEngineProfile("StormArcadeSandbox", this);
    }
    else if (isIncognito) {
        profile = new QWebEngineProfile(QString(), this);
        profile->setSpellCheckEnabled(true);
        profile->setSpellCheckLanguages(QStringList() << "en-US" << "ru-RU");

        applyMediaCodecFix(profile);
        QString firefoxUa = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0";
        profile->setHttpUserAgent(firefoxUa);
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

    connect(page, &QWebEnginePage::featurePermissionRequested, this, [page](const QUrl& securityOrigin, QWebEnginePage::Feature feature) {
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
            page->setFeaturePermission(securityOrigin, feature, QWebEnginePage::PermissionDeniedByUser);
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

        view->setHtml(pageTemplates.getHomePageHtml(), QUrl("http://storm.home"));
    }
    else if (url.toString() == "storm://settings") {
        QWebChannel* settingsChannel = new QWebChannel(page);
        SettingsBridge* settingsBridge = new SettingsBridge(this, page);
        settingsChannel->registerObject("settingsBridge", settingsBridge);
        page->setWebChannel(settingsChannel);

        view->setHtml(pageTemplates.getSettingsHtml(), QUrl("http://storm.settings"));
    }
    else if (url.toString() == "storm://cloud") {
        QWebChannel* cloudChannel = new QWebChannel(page);
        StormCloudBridge* cloudBridge = new StormCloudBridge(this, page);
        cloudChannel->registerObject("cloudBridge", cloudBridge);
        page->setWebChannel(cloudChannel);

        view->setHtml(pageTemplates.getStormCloudHtml(), QUrl("http://storm.cloud"));
    }
    else if (url.toString() == "storm://bookmarks") {
        QWebChannel* bookmarksChannel = new QWebChannel(page);
        BookmarksBridge* bookmarksBridge = new BookmarksBridge(this, page);
        bookmarksChannel->registerObject("bookmarksBridge", bookmarksBridge);
        page->setWebChannel(bookmarksChannel);

        view->setHtml(getBookmarksHtml(), QUrl("http://storm.bookmarks"));
    }
    else if (url.toString() == "storm://help" || url.toString() == "storm://help/") {
        view->setHtml(getHelpHtml(), QUrl("http://storm.help"));
    }
    else if (url.toString() == "storm://newtab" || url.isEmpty()) {
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


    connect(view, &QWebEngineView::urlChanged, this, &MainWindow::updateAddressBar);
    connect(view, &QWebEngineView::titleChanged, [this, view, isIncognito](const QString& title) {
        int idx = tabWidget->indexOf(view);
        QString finalTitle = isIncognito ? (u8"🕶 " + title) : title;
        if (idx != -1) tabWidget->setTabText(idx, finalTitle);
        });

    connect(view, &QWebEngineView::loadFinished, [this, view, isIncognito](bool ok) {
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


void MainWindow::closeTab(int index) {
    if (tabWidget->count() > 1) {
        if (tabWidget->currentIndex() == index && index > 0) {
            tabWidget->setCurrentIndex(index - 1);
        }
        QWidget* widget = tabWidget->widget(index);
        tabWidget->removeTab(index);
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

    QAction* detachAct = menu.addAction(u8"🗗 Открепить в новое окно");
    connect(detachAct, &QAction::triggered, this, [this, index]() {
        detachTab(index);
        });

    menu.addSeparator();

    QAction* reloadAct = menu.addAction(u8"↻ Обновить вкладку");
    connect(reloadAct, &QAction::triggered, this, [this, index]() {
        auto* view = qobject_cast<QWebEngineView*>(tabWidget->widget(index));
        if (view) view->reload();
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

    QString currentTitle = tabWidget->tabText(index);
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
    QString title = tabWidget->tabText(index);
    QIcon icon = tabWidget->tabIcon(index);

    tabWidget->removeTab(index);

    MainWindow* newWindow = new MainWindow(nullptr, true);
    newWindow->resize(1080, 650);

    newWindow->attachTab(tabView, title);
    newWindow->getTabWidget()->setTabIcon(0, icon);

    newWindow->move(QCursor::pos() - QPoint(150, 25));
    newWindow->show();
}


void MainWindow::attachTab(QWidget* tabView, const QString& title, const QIcon& icon, int insertIndex) {
    tabView->setParent(tabWidget);

    int insertIdx = (insertIndex >= 0 && insertIndex <= tabWidget->count())
        ? insertIndex
        : tabWidget->count();

    tabWidget->insertTab(insertIdx, tabView, icon, title);
    tabWidget->setCurrentIndex(insertIdx);

    if (auto* view = qobject_cast<QWebEngineView*>(tabView)) {

        connect(view, &QWebEngineView::urlChanged, this, &MainWindow::updateAddressBar);
        connect(view, &QWebEngineView::titleChanged, [this, view](const QString& newTitle) {
            int idx = tabWidget->indexOf(view);
            if (idx != -1) tabWidget->setTabText(idx, newTitle);
            });
    }
}