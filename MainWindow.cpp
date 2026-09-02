#include "MainWindow.h"
#include "ThemeManager.h"
#include "StormTabBar.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSettings>
#include <QDialog>
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
#include "ResearchWidget.h"
#include "TalkWidget.h"
#include "SmmAutoPublisherWidget.h"
#include "SmmPublishController.h"
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
#include "AdblockManager.h"
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
#include <QJsonDocument>
#include <QJsonObject>


#include "MainWindow_UiHelpers.h"

// ==========================================================================
// MainWindow.cpp — конструктор, setupUi() и жизненный цикл окна
// Здесь остались только: сборка интерфейса (setupUi), конструктор/
// деструктор и closeEvent. Остальные слоты MainWindow разнесены по
// тематическим файлам MainWindow_Navigation.cpp, MainWindow_Tabs.cpp,
// MainWindow_Bookmarks.cpp, MainWindow_Passwords.cpp,
// MainWindow_Settings.cpp и MainWindow_View.cpp — см. их шапки.
// Классы-фильтры событий, используемые только внутри setupUi(),
// остались локальными здесь же. WindowDragFilter и applyMediaCodecFix
// нужны ещё и другим файлам — они в MainWindow_UiHelpers.h.
// ==========================================================================

// =========================================================================
// --- ФИЛЬТР 1: Всплывающее превью вкладки при наведении мыши ---
// =========================================================================
class TabPreviewPopup : public QLabel {
public:
    TabPreviewPopup(QWidget* parent = nullptr) : QLabel(parent, Qt::ToolTip | Qt::FramelessWindowHint) {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TranslucentBackground);
    }
};


class TabHoverFilter : public QObject {
    QTabWidget* tabWidget;
    TabPreviewPopup* popup;
public:
    TabHoverFilter(QTabWidget* tw, QObject* parent = nullptr) : QObject(parent), tabWidget(tw) {
        popup = new TabPreviewPopup();
    }
    ~TabHoverFilter() { delete popup; }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::ToolTip) {
            QHelpEvent* he = static_cast<QHelpEvent*>(event);
            int idx = tabWidget->tabBar()->tabAt(he->pos());

            if (idx >= 0 && idx < tabWidget->count()) {
                if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->widget(idx))) {
                    QPixmap rawPixmap;

                    if (idx == tabWidget->currentIndex()) {
                        rawPixmap = view->grab();
                        if (!rawPixmap.isNull()) {
                            view->setProperty("cachedPreview", rawPixmap);
                        }
                    }
                    else {
                        rawPixmap = view->property("cachedPreview").value<QPixmap>();
                    }

                    QPixmap finalPixmap(280, 160);
                    finalPixmap.fill(Qt::transparent);

                    QPainter painter(&finalPixmap);
                    painter.setRenderHint(QPainter::Antialiasing);

                    QPainterPath clipPath;
                    clipPath.addRoundedRect(1, 1, 278, 158, 8, 8);
                    painter.setClipPath(clipPath);

                    if (!rawPixmap.isNull()) {
                        QPixmap scaled = rawPixmap.scaled(280, 160, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                        painter.drawPixmap(0, 0, scaled);
                    }
                    else {
                        painter.fillRect(0, 0, 280, 160, QColor("#1c2128"));

                        painter.setPen(QColor("#56d39b"));
                        painter.setFont(QFont("Segoe UI", 16, QFont::Bold));
                        painter.drawText(QRect(15, 20, 250, 40), Qt::AlignLeft | Qt::AlignVCenter, u8"🌩️ Storm Tab");

                        painter.setPen(QColor("#ffffff"));
                        painter.setFont(QFont("Segoe UI", 11, QFont::Bold));
                        painter.drawText(QRect(15, 65, 250, 45), Qt::AlignLeft | Qt::TextWordWrap, tabWidget->tabText(idx));

                        painter.setPen(QColor("#8b949e"));
                        painter.setFont(QFont("Segoe UI", 9));
                        QString urlStr = view->url().toString();
                        if (urlStr.length() > 35) urlStr = urlStr.left(35) + "...";
                        painter.drawText(QRect(15, 115, 250, 25), Qt::AlignLeft, urlStr);
                    }

                    painter.setClipping(false);
                    painter.setPen(QPen(QColor("#56d39b"), 2));
                    painter.drawRoundedRect(1, 1, 278, 158, 8, 8);
                    painter.end();

                    popup->setPixmap(finalPixmap);
                    popup->adjustSize();
                    popup->move(he->globalPos() + QPoint(12, 18));
                    popup->show();
                    return true;
                }
            }
            popup->hide();
        }
        else if (event->type() == QEvent::Leave || event->type() == QEvent::MouseButtonPress) {
            popup->hide();
        }
        return QObject::eventFilter(obj, event);
    }
};

// =========================================================================
// --- ФИЛЬТР 2: Перетаскивание вкладки — между окнами (D&D) или на рабочий стол ---
// =========================================================================
class StormTabMimeData : public QMimeData {
public:
    QWidget* tabView = nullptr;
    QString title;
    QIcon icon;
    MainWindow* sourceWindow = nullptr;

    StormTabMimeData(QWidget* view, const QString& t, const QIcon& ic, MainWindow* src)
        : tabView(view), title(t), icon(ic), sourceWindow(src) {
    }
};


static const char* const kStormTabMimeType = "application/x-storm-tab";


class TabDragDropFilter : public QObject {
    MainWindow* mainWindow;
    QTabWidget* tabWidget;
    QPoint dragStartPos;
    bool isDetaching = false;
public:
    TabDragDropFilter(MainWindow* mw, QTabWidget* tw) : QObject(mw), mainWindow(mw), tabWidget(tw) {
        tabWidget->tabBar()->setAcceptDrops(true);
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                dragStartPos = me->pos();
                isDetaching = false;
            }
            break;
        }
        case QEvent::MouseMove: {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (!isDetaching && (me->buttons() & Qt::LeftButton)) {
                if ((me->pos() - dragStartPos).manhattanLength() > 50 && !tabWidget->tabBar()->geometry().contains(me->pos())) {
                    int idx = tabWidget->tabBar()->tabAt(dragStartPos);
                    if (idx >= 0 && idx < tabWidget->count() && tabWidget->count() > 1) {
                        isDetaching = true;
                        startDrag(idx);
                        return true;
                    }
                }
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            isDetaching = false;
            break;

        case QEvent::DragEnter: {
            auto* de = static_cast<QDragEnterEvent*>(event);
            if (de->mimeData()->hasFormat(kStormTabMimeType)) {
                de->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::DragMove: {
            auto* de = static_cast<QDragMoveEvent*>(event);
            if (de->mimeData()->hasFormat(kStormTabMimeType)) {
                de->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::Drop: {
            auto* de = static_cast<QDropEvent*>(event);
            if (de->mimeData()->hasFormat(kStormTabMimeType)) {
                auto* stormMime = static_cast<const StormTabMimeData*>(de->mimeData());

                int dropIndex = tabWidget->tabBar()->tabAt(de->position().toPoint());
                if (dropIndex < 0 || dropIndex >= tabWidget->count()) {
                    dropIndex = tabWidget->count() > 0 ? tabWidget->count() : 0;
                }

                mainWindow->attachTab(stormMime->tabView, stormMime->title, stormMime->icon, dropIndex);

                de->setDropAction(Qt::MoveAction);
                de->accept();
                return true;
            }
            break;
        }
        default:
            break;
        }
        return QObject::eventFilter(obj, event);
    }

private:
    void startDrag(int idx) {
        QWidget* tabView = tabWidget->widget(idx);
        if (!tabView) return;
        QString title = tabWidget->tabText(idx);
        QIcon icon = tabWidget->tabIcon(idx);

        tabWidget->removeTab(idx);

        auto* drag = new QDrag(tabWidget->tabBar());
        auto* mime = new StormTabMimeData(tabView, title, icon, mainWindow);
        mime->setData(kStormTabMimeType, QByteArray("1"));
        drag->setMimeData(mime);

        QPixmap pixmap(190, 18);
        pixmap.fill(QColor("#1c2128"));
        QPainter p(&pixmap);
        p.setPen(Qt::white);
        QString shortTitle = title.left(20);
        p.drawText(pixmap.rect(), Qt::AlignCenter, shortTitle.isEmpty() ? QStringLiteral("Storm Tab") : shortTitle);
        p.end();
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(95, 15));

        Qt::DropAction result = drag->exec(Qt::MoveAction);

        if (result != Qt::MoveAction) {
            MainWindow* newWindow = new MainWindow(nullptr, true);
            newWindow->resize(1080, 650);
            newWindow->attachTab(tabView, title, icon, 0);
            newWindow->move(QCursor::pos() - QPoint(150, 25));
            newWindow->show();
        }
    }
};


class StormTabWidget : public QTabWidget {
public:
    explicit StormTabWidget(QWidget* parent = nullptr) : QTabWidget(parent) {
        setTabBar(new StormTabBar(this));
    }
};

// =========================================================================
// --- ФИЛЬТР 4: держит checkable-кнопку "Загрузки" в topBar синхронизированной
// с реальной видимостью панели загрузок ---
// =========================================================================
class DownloadsPanelVisibilityFilter : public QObject {
    QPushButton* button;
public:
    DownloadsPanelVisibilityFilter(QPushButton* btn, QObject* parent = nullptr)
        : QObject(parent), button(btn) {
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
            if (auto* w = qobject_cast<QWidget*>(obj)) {
                button->setChecked(w->isVisible());
            }
        }
        return QObject::eventFilter(obj, event);
    }
};


MainWindow::MainWindow(QWidget* parent, bool isDetached)
    : QMainWindow(parent), topBar(nullptr), tabWidget(nullptr), sidebar(nullptr), pageTemplates(this)
{
    passwordManager = new PasswordManager(this);
    setWindowFlags(Qt::FramelessWindowHint);

    setProperty("isDetachedWindow", isDetached);

    if (isDetached) {
        setAttribute(Qt::WA_DeleteOnClose);
    }

    setupUi(isDetached);
}


MainWindow::~MainWindow() {}


void MainWindow::setupUi(bool isDetached) {
    QSettings settings;
    QString savedTheme = settings.value("theme", "dark").toString();
    this->applyTheme(savedTheme);

    if (!dbManager.initDatabase()) {
        QMessageBox::critical(this, u8"Ошибка БД", u8"Не удалось инициализировать базу данных!");
    }

    QWidget* centralWidget = new QWidget(this);
    centralWidget->setObjectName("centralWidget");
    setCentralWidget(centralWidget);

    m_mainProfile = new QWebEngineProfile("StormMainProfile", this);

    m_mainProfile->settings()->setAttribute(QWebEngineSettings::PdfViewerEnabled, true);
    m_mainProfile->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, true);
    m_mainProfile->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);

    m_mainProfile->setSpellCheckEnabled(true);
    m_mainProfile->setSpellCheckLanguages(QStringList() << "en-US" << "ru-RU");

    qCritical().noquote() << "[DIAG] m_mainProfile isSpellCheckEnabled:" << m_mainProfile->isSpellCheckEnabled();
    qCritical().noquote() << "[DIAG] m_mainProfile spellCheckLanguages:" << m_mainProfile->spellCheckLanguages();

    QPushButton* securityStatusBtn = new QPushButton(u8"🛡️ Storm Shield: Активен", this);
    securityStatusBtn->setObjectName("securityStatusBtn");
    securityStatusBtn->setCursor(Qt::PointingHandCursor);
    securityStatusBtn->setStyleSheet(
        "QPushButton { background-color: rgba(86, 211, 155, 0.1); color: #56d39b; border: 1px solid rgba(86, 211, 155, 0.3); border-radius: 6px; padding: 4px 12px; font-size: 12px; font-weight: bold; margin-right: 15px; }"
        "QPushButton:hover { background-color: rgba(86, 211, 155, 0.2); }"
    );
    // БАГ-ФИКС: курсор-рука уже намекал, что кнопка кликабельна, но клика не было
    // вообще — просто индикатор. Теперь ведёт в Настройки → Конфиденциальность,
    // где и живут переключатель Shield, и список исключений.
    connect(securityStatusBtn, &QPushButton::clicked, this, [this]() {
        openSettingsTab();
        });
    this->statusBar()->addPermanentWidget(securityStatusBtn);

    // =========================================================================
    // --- ЯВНАЯ НАСТРОЙКА ОСНОВНОГО ПРОФИЛЯ ---
    // =========================================================================
    {
        QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QString profilePath = appData + "/ProfileData";
        QDir().mkpath(profilePath + "/Cache");
        QDir().mkpath(profilePath + "/Storage");

        m_mainProfile->setCachePath(profilePath + "/Cache");
        m_mainProfile->setPersistentStoragePath(profilePath + "/Storage");
        m_mainProfile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
        m_mainProfile->cookieStore()->loadAllCookies();

        QString firefoxUa = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0";
        m_mainProfile->setHttpUserAgent(firefoxUa);

        qCritical().noquote() << "[DIAG] Firefox User-Agent set to:" << firefoxUa;
        qWarning() << "[DIAG] isOffTheRecord:" << m_mainProfile->isOffTheRecord();
        qWarning() << "[DIAG] Cache path:" << m_mainProfile->cachePath();
        qWarning() << "[DIAG] Storage path:" << m_mainProfile->persistentStoragePath();
        qWarning() << "[DIAG] Cookies policy:" << m_mainProfile->persistentCookiesPolicy();
    }

    int initialBlockedCount = dbManager.getBlockedThreatsCount();
    ShieldInterceptor* mainInterceptor = new ShieldInterceptor(this, initialBlockedCount);
    mainInterceptor->setObjectName("ShieldInterceptor");

    {
        QSettings shieldSettings;
        bool shieldEnabled = shieldSettings.value("shield/enabled", true).toBool();
        mainInterceptor->setEnabled(shieldEnabled);
        updateShieldStatusIndicator(shieldEnabled);

        mainInterceptor->setExceptions(shieldSettings.value("shield/exceptions").toStringList());
    }

    m_mainProfile->setUrlRequestInterceptor(mainInterceptor);

    AdblockManager::instance().init(m_mainProfile, nullptr);

    applyMediaCodecFix(m_mainProfile);
    // =========================================================
    // --- СТЕЛС-СКРИПТ ДЛЯ СКРЫТИЯ ВЕРХНЕЙ ПАНЕЛИ ПЕРЕВОДЧИКА ---
    // =========================================================
    QWebEngineScript hideBannerScript;
    hideBannerScript.setName("HideTranslatorBanner");
    QString hideJs = u8R"JS(
        (function() {
            var host = window.location.hostname;
            if (host.includes('translate.goog') || host.includes('translate.google') || host.includes('translate.yandex')) {
                var style = document.createElement('style');
                style.id = 'storm-hide-translator-header';
                style.innerHTML = `
                    /* Полное удаление верхней панели Google Translate */
                    iframe.skiptranslate, .skiptranslate, #gt-nvframe, #gt-c, header[role="banner"], #gb { 
                        display: none !important; height: 0 !important; opacity: 0 !important; pointer-events: none !important; 
                    }
                    body { top: 0px !important; position: static !important; }
                    
                    /* Полное удаление панели Яндекс Переводчика */
                    #yt-hdr, .yt-header, [class*="translate-header"], #ya-widget-header { 
                        display: none !important; height: 0 !important; 
                    }
                `;
                document.head.appendChild(style);
            }
        })();
    )JS";
    hideBannerScript.setSourceCode(hideJs);
    hideBannerScript.setInjectionPoint(QWebEngineScript::DocumentReady);
    hideBannerScript.setWorldId(QWebEngineScript::MainWorld);
    hideBannerScript.setRunsOnSubFrames(true);
    m_mainProfile->scripts()->insert(hideBannerScript);

    connect(mainInterceptor, &ShieldInterceptor::blockedCountChanged, this, [this, securityStatusBtn](int total) {
        QMetaObject::invokeMethod(this, [this, securityStatusBtn, total]() {
            dbManager.setBlockedThreatsCount(total);

            securityStatusBtn->setText(QString(u8"🛑 Заблокировано угроз: %1").arg(total));
            securityStatusBtn->setStyleSheet(
                "QPushButton { background-color: rgba(255, 95, 95, 0.1); color: #ff5f5f; border: 1px solid rgba(255, 95, 95, 0.3); border-radius: 6px; padding: 4px 12px; font-size: 12px; font-weight: bold; margin-right: 15px; }"
                "QPushButton:hover { background-color: rgba(255, 95, 95, 0.2); }"
            );
            }, Qt::QueuedConnection);
        });

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    topBar = new BrowserTopBar(this);
    mainLayout->addWidget(topBar);

    bookmarksBar = new BookmarksBar(this);
    mainLayout->addWidget(bookmarksBar);

    bool showBm = settings.value("browser/show_bookmarks_bar", false).toBool();
    bookmarksBar->setVisible(showBm);

    QWidget* workArea = new QWidget(centralWidget);
    workArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QHBoxLayout* workLayout = new QHBoxLayout(workArea);
    workLayout->setContentsMargins(0, 0, 0, 0);
    workLayout->setSpacing(0);

    sidebar = new Sidebar(this);
    workLayout->addWidget(sidebar);

    tabWidget = new StormTabWidget(workArea);
    tabWidget->setTabsClosable(false);
    tabWidget->setMovable(true);
    tabWidget->setDocumentMode(true);
    tabWidget->setIconSize(QSize(18, 18));
    tabWidget->tabBar()->setElideMode(Qt::ElideRight);

    tabWidget->setStyleSheet(StormTabBar::buildTabBarStyleSheet());
    StormTabBar* stormTabBar = qobject_cast<StormTabBar*>(tabWidget->tabBar());
    if (stormTabBar) {
        connect(stormTabBar, &StormTabBar::plusClicked, this, [this]() {
            this->addNewTab(QUrl("storm://newtab"));
            });
    }

    tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabWidget->tabBar(), &QTabBar::customContextMenuRequested,
        this, &MainWindow::showTabContextMenu);
    tabWidget->tabBar()->installEventFilter(new TabHoverFilter(tabWidget, this));
    tabWidget->tabBar()->installEventFilter(new TabDragDropFilter(this, tabWidget));

    connect(tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index >= 0) {
            if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->widget(index))) {
                updateAddressBar(view->url());
                // "isLoading" — свойство на самом view, выставляется в
                // addNewTab() на loadStarted/loadFinished; переживает и
                // переключение вкладок, и перенос вкладки между окнами
                // (attachTab/detachTab), поэтому кнопка Обновить/Стоп в
                // BrowserTopBar всегда отражает состояние именно этой вкладки.
                topBar->setLoadingState(view->property("isLoading").toBool());
            }
        }
        });

    workLayout->addWidget(tabWidget, 1);

    downloadManager = new DownloadManager(this);
    downloadManager->hide();
    workLayout->addWidget(downloadManager);

    if (topBar && topBar->getDownloadsButton()) {
        downloadManager->installEventFilter(new DownloadsPanelVisibilityFilter(topBar->getDownloadsButton(), this));
    }

    connect(m_mainProfile, &QWebEngineProfile::downloadRequested, this, [this](QWebEngineDownloadRequest* request) {
        downloadManager->addDownload(request);
        });

    mainLayout->addWidget(workArea, 1);

    AIAssistantWidget* aiWidget = new AIAssistantWidget(this);
    aiAssistantWidget = aiWidget;
    sidebar->addItem(u8"🧠", u8"Storm AI", aiWidget);

    connect(aiWidget, &AIAssistantWidget::panelActivationRequested, this, [this]() {
        if (sidebar) {
            sidebar->setVisible(true);
            sidebar->openItem(aiAssistantWidget);
        }
        });

    NotesWidget* notesWidget = new NotesWidget(this);
    sidebar->addItem(u8"📝", u8"Заметки", notesWidget);

    TodoWidget* todoWidget = new TodoWidget(this);
    sidebar->addItem(u8"✅", u8"Задачи", todoWidget);

    PomodoroWidget* pomodoroWidget = new PomodoroWidget(this);
    sidebar->addItem(u8"🍅", u8"Pomodoro Таймер", pomodoroWidget);

    NewsWidget* newsWidget = new NewsWidget(this);
    sidebar->addItem(u8"📰", u8"Новости", newsWidget);

    GamesWidget* gamesWidget = new GamesWidget(this);
    sidebar->addItem(u8"🎮", u8"Игры и стримы", gamesWidget);

    VoiceChatWidget* chatWidget = new VoiceChatWidget(this);
    sidebar->addItem(u8"🎧", u8"Голосовой чат", chatWidget);

    TranslatorWidget* translatorWidget = new TranslatorWidget(this);
    sidebar->addItem(u8"🌐", u8"Переводчик", translatorWidget);

    WeatherWidget* weatherWidget = new WeatherWidget(this);
    sidebar->addItem(u8"☁", u8"Погода", weatherWidget);

    FreeGamesWidget* freeGamesWidget = new FreeGamesWidget(this);
    sidebar->addItem(u8"🎁", u8"Раздачи игр", freeGamesWidget);

    EducationWidget* educationWidget = new EducationWidget(this);
    sidebar->addItem(u8"🎓", u8"Обучение и Помощь", educationWidget);

    ArcadeWidget* arcadeWidget = new ArcadeWidget(this);
    sidebar->addItem(u8"🕹️", u8"Storm Arcade", arcadeWidget);
    connect(arcadeWidget, &ArcadeWidget::openGameRequested, this, [this](const QString& urlStr) {
        this->addNewTab(QUrl(urlStr));
        });

    ReaderWidget* readerWidget = new ReaderWidget(this);
    sidebar->addItem(u8"📚", u8"Библиотека", readerWidget);
    connect(readerWidget, &ReaderWidget::openBookRequested, this, [this](const QUrl& url) {
        this->addNewTab(url);
        });

    ResearchWidget* researchWidget = new ResearchWidget(this);
    this->researchWidget = researchWidget;
    sidebar->addItem(u8"🔬", u8"Глубокое исследование", researchWidget);
    connect(researchWidget, &ResearchWidget::openReportRequested, this, [this](const QUrl& url) {
        this->addNewTab(url);
        });
    connect(researchWidget, &ResearchWidget::reportSavedToLibrary, readerWidget, &ReaderWidget::loadBooks);

    TalkWidget* talkWidget = new TalkWidget(this);
    sidebar->addItem(u8"📹", u8"Storm Talk (Видеозвонки)", talkWidget);
    connect(talkWidget, &TalkWidget::launchTalkRequested, this, [this]() {
        this->addNewTab(QUrl("storm-talk"));
        });

    SmmAutoPublisherWidget* smmWidget = new SmmAutoPublisherWidget(this);
    sidebar->addItem(u8"📅", u8"SMM Авто-постинг", smmWidget);
    // Доводит очередь до реальной публикации: открывает фоновую вкладку
    // платформы и запускает на ней WebPageAgent. Пока умеет дойти только до
    // capturePageContext() — конкретной последовательности click/type для
    // отправки поста ещё нет, см. подробный комментарий в
    // SmmPublishController.cpp про недостающий цикл принятия решений.
    smmPublishController = new SmmPublishController(this, smmWidget->queueManager(), this);

    connect(tabWidget->tabBar(), &QTabBar::tabCloseRequested, this, &MainWindow::closeTab);

    if (!isDetached) {
        QSettings startupSettings;
        int startupMode = startupSettings.value("startup/mode", 0).toInt();

        if (startupMode == 1) {
            QStringList savedTabs = dbManager.loadSession();
            if (savedTabs.isEmpty()) {
                savedTabs = startupSettings.value("session/open_tabs").toStringList();
            }
            if (!savedTabs.isEmpty()) {
                for (const QString& urlStr : savedTabs) {
                    addNewTab(QUrl(urlStr));
                }
            }
            else {
                addNewTab(QUrl("storm://home"));
            }
        }
        else if (startupMode == 2) {
            QString customUrlStr = startupSettings.value("startup/custom_url", "https://duckduckgo.com").toString();
            if (!customUrlStr.startsWith("http://") && !customUrlStr.startsWith("https://")) {
                customUrlStr = "https://" + customUrlStr;
            }
            addNewTab(QUrl(customUrlStr));
        }
        else {
            addNewTab(QUrl("storm://home"));
        }
    }

    QShortcut* fsShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(fsShortcut, &QShortcut::activated, this, &MainWindow::toggleFullScreen);

    // =========================================================
    // --- ГОРЯЧИЕ КЛАВИШИ ---
    // =========================================================

    QShortcut* newTabShortcut = new QShortcut(QKeySequence("Ctrl+T"), this);
    connect(newTabShortcut, &QShortcut::activated, this, [this]() { addNewTab(QUrl("storm://newtab")); });

    QShortcut* closeTabShortcut = new QShortcut(QKeySequence("Ctrl+W"), this);
    connect(closeTabShortcut, &QShortcut::activated, this, [this]() { closeTab(tabWidget->currentIndex()); });

    QShortcut* bookmarkShortcut = new QShortcut(QKeySequence("Ctrl+D"), this);
    connect(bookmarkShortcut, &QShortcut::activated, this, &MainWindow::addCurrentBookmark);

    QShortcut* historyShortcut = new QShortcut(QKeySequence("Ctrl+H"), this);
    connect(historyShortcut, &QShortcut::activated, this, &MainWindow::showHistory);

    QShortcut* downloadsShortcut = new QShortcut(QKeySequence("Ctrl+J"), this);
    connect(downloadsShortcut, &QShortcut::activated, this, &MainWindow::openDownloads);

    auto focusAddressBar = [this]() {
        topBar->getAddressBar()->setFocus();
        topBar->getAddressBar()->selectAll();
        };
    QShortcut* focusUrl1 = new QShortcut(QKeySequence("Ctrl+L"), this);
    QShortcut* focusUrl2 = new QShortcut(QKeySequence("Alt+D"), this);
    connect(focusUrl1, &QShortcut::activated, this, focusAddressBar);
    connect(focusUrl2, &QShortcut::activated, this, focusAddressBar);

    QShortcut* reloadShortcut = new QShortcut(QKeySequence("F5"), this);
    connect(reloadShortcut, &QShortcut::activated, this, &MainWindow::reloadPage);

    QShortcut* hardReloadShortcut = new QShortcut(QKeySequence("Ctrl+F5"), this);
    connect(hardReloadShortcut, &QShortcut::activated, this, [this]() {
        if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
            view->page()->triggerAction(QWebEnginePage::ReloadAndBypassCache);
        }
        });

    QShortcut* printShortcut = new QShortcut(QKeySequence("Ctrl+P"), this);
    connect(printShortcut, &QShortcut::activated, this, &MainWindow::printCurrentPage);

    QShortcut* zoomIn1 = new QShortcut(QKeySequence("Ctrl+="), this);
    QShortcut* zoomIn2 = new QShortcut(QKeySequence("Ctrl++"), this);
    connect(zoomIn1, &QShortcut::activated, this, &MainWindow::zoomIn);
    connect(zoomIn2, &QShortcut::activated, this, &MainWindow::zoomIn);

    QShortcut* zoomOutShortcut = new QShortcut(QKeySequence("Ctrl+-"), this);
    connect(zoomOutShortcut, &QShortcut::activated, this, &MainWindow::zoomOut);

    QShortcut* zoomResetShortcut = new QShortcut(QKeySequence("Ctrl+0"), this);
    connect(zoomResetShortcut, &QShortcut::activated, this, [this]() {
        if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
            view->setZoomFactor(1.0);
        }
        });

    QShortcut* clearDataShortcut = new QShortcut(QKeySequence("Ctrl+Shift+Delete"), this);
    connect(clearDataShortcut, &QShortcut::activated, this, &MainWindow::clearBrowserData);

    QShortcut* findShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(findShortcut, &QShortcut::activated, this, &MainWindow::showFindBar);

    // F12 — DevTools (тело вынесено в openDevTools(), см. ниже — тот же
    // метод теперь переиспользует и "🔍 Просмотреть код" из контекстного
    // меню в BrowserWebView.cpp)
    QShortcut* devToolsShortcut = new QShortcut(QKeySequence("F12"), this);
    connect(devToolsShortcut, &QShortcut::activated, this, [this]() {
        auto* currentView = qobject_cast<QWebEngineView*>(tabWidget->currentWidget());
        if (currentView) openDevTools(currentView, /*forceShow=*/false);
        });

    MenuBuilder::buildMenu(this);
    loadBookmarksIntoMenu();

    // =========================================================
    // --- Фоновая проверка обновлений (только для главного окна) ---
    // =========================================================
    if (!isDetached) {
        UpdateManager* autoUpdater = new UpdateManager(this);
        autoUpdater->setObjectName("UpdateManager");

        connect(autoUpdater, &UpdateManager::updateStagedAndReady, this, [](const QString& ver) {
            qDebug() << "🔥 [Storm Updater] Обновление" << ver << "успешно скачано в фоне и ждет перезапуска!";
            });

        autoUpdater->startPeriodicChecks();
    }

    // =========================================================
    // --- Постоянная иконка в трее (только для главного окна) ---
    // =========================================================
    // Не привязана к конкретной функции — общий, всегда живой канал:
    // closeEvent() ниже сворачивает сюда окно, если это включено в
    // настройках, а showTrayNotification() доступен любому виджету
    // приложения вместо того, чтобы каждый заводил свой QSystemTrayIcon
    // (как сейчас делает разовое уведомление AntiSub в TodoWidget).
    if (!isDetached && QSystemTrayIcon::isSystemTrayAvailable()) {
        setupTrayIcon();
    }
}


void MainWindow::setupTrayIcon() {
    m_trayIcon = new QSystemTrayIcon(windowIcon(), this);
    m_trayIcon->setToolTip(u8"Storm Browser");

    QMenu* trayMenu = new QMenu(this);

    QAction* showAction = trayMenu->addAction(u8"Открыть Storm Browser");
    connect(showAction, &QAction::triggered, this, [this]() {
        show();
        raise();
        activateWindow();
        });

    trayMenu->addSeparator();

    QAction* quitAction = trayMenu->addAction(u8"Выход");
    connect(quitAction, &QAction::triggered, this, [this]() {
        // Настоящий выход: взводим флаг ДО close(), чтобы closeEvent() не
        // свернул окно в трей повторно, даже если такая настройка включена.
        m_isQuitting = true;
        close();
        });

    m_trayIcon->setContextMenu(trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);

    m_trayIcon->show();
}


void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    // Trigger — одиночный клик (в Windows это ЛКМ по иконке). DoubleClick
    // добавлен на случай окружений, где одиночный клик не считается
    // активацией — оба варианта просто разворачивают окно, без дублирования.
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        if (isHidden()) {
            show();
            raise();
            activateWindow();
        }
    }
}


void MainWindow::showTrayNotification(const QString& title, const QString& message) {
    if (m_trayIcon) {
        m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 4000);
    }
}


void MainWindow::closeEvent(QCloseEvent* event) {
    if (!property("isDetachedWindow").toBool()) {
        QSettings sessionSettings;
        QStringList tabsToSave;

        for (int i = 0; i < tabWidget->count(); ++i) {
            if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->widget(i))) {
                QString currentUrl = view->url().toString();
                if (currentUrl.isEmpty() || currentUrl == "about:blank") {
                    currentUrl = "storm://home";
                }
                tabsToSave.append(currentUrl);
            }
        }

        sessionSettings.setValue("session/open_tabs", tabsToSave);
        dbManager.saveSession(tabsToSave);
    }

    UpdateManager* updater = this->findChild<UpdateManager*>("UpdateManager");
    if (updater && updater->isUpdateStaged()) {
        qDebug() << "🔄 Запуск установки скачанного обновления перед выходом...";
        // applyStagedUpdateAndRestart() теперь возвращает false, если
        // обновление на самом деле не запустилось (например, скачанный
        // установщик оказался повреждён — не совпал хэш). Раньше в этом
        // редком случае мы всё равно безусловно "проглатывали" закрытие
        // окна (event->ignore()), и пользователю приходилось нажимать
        // крестик второй раз без какого-либо объяснения. Теперь при
        // неудаче просто продолжаем закрываться как обычно, без апдейта.
        if (updater->applyStagedUpdateAndRestart()) {
            event->ignore();
            return;
        }
    }

    // Сворачивание в трей вместо закрытия — только для главного окна
    // (детач-окна всегда закрываются как обычно), только если m_trayIcon
    // вообще создан (см. setupTrayIcon()), только если это НЕ настоящий
    // выход через пункт "Выход" трей-меню (m_isQuitting), и только если
    // пользователь явно включил это в настройках (browser/minimize_to_tray,
    // см. SettingsBridge::toggleMinimizeToTray). По умолчанию выключено —
    // у существующих пользователей поведение окна не меняется само по себе.
    if (!m_isQuitting && !property("isDetachedWindow").toBool() && m_trayIcon
        && QSettings().value("browser/minimize_to_tray", false).toBool()) {
        event->ignore();
        hide();
        m_trayIcon->showMessage(u8"Storm Browser",
            u8"Браузер свёрнут в трей. Кликните по иконке, чтобы вернуть окно.",
            QSystemTrayIcon::Information, 2000);
        return;
    }

    event->accept();
}


// ==========================================================================
// DevTools / Storm Shield — вызывается из F12 (см. setupUi()) и из пунктов
// "🔍 Просмотреть код" / "🚫 Заблокировать элемент" контекстного меню
// (BrowserWebView::contextMenuEvent).
// ==========================================================================

void MainWindow::openDevTools(QWebEngineView* view, bool forceShow) {
    if (!view || !view->page()) return;

    if (!m_devToolsWindow) {
        m_devToolsWindow = new QMainWindow(this);
        m_devToolsWindow->setWindowTitle("Storm DevTools");
        m_devToolsWindow->resize(900, 600);
        m_devToolsWindow->setWindowFlags(Qt::FramelessWindowHint | Qt::Window);

        QWidget* centralWidget = new QWidget(m_devToolsWindow);
        QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        QWidget* titleBar = new QWidget(centralWidget);
        titleBar->setFixedHeight(35);
        titleBar->setObjectName("customTitleBar");

        QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
        titleLayout->setContentsMargins(15, 0, 0, 0);
        titleLayout->setSpacing(0);

        QLabel* titleLabel = new QLabel(u8"Storm DevTools", titleBar);
        titleLabel->setObjectName("customTitleLabel");

        QPushButton* btnCloseTitle = new QPushButton(u8"✕", titleBar);
        btnCloseTitle->setObjectName("titleCloseBtn");
        btnCloseTitle->setFixedSize(35, 35);

        titleLayout->addWidget(titleLabel);
        titleLayout->addStretch();
        titleLayout->addWidget(btnCloseTitle);

        mainLayout->addWidget(titleBar);
        titleBar->installEventFilter(new WindowDragFilter(titleBar));

        connect(btnCloseTitle, &QPushButton::clicked, m_devToolsWindow, &QMainWindow::hide);

        m_devToolsView = new QWebEngineView(centralWidget);
        mainLayout->addWidget(m_devToolsView, 1);

        QHBoxLayout* bottomLayout = new QHBoxLayout();
        bottomLayout->setContentsMargins(0, 0, 0, 0);
        bottomLayout->addStretch();
        bottomLayout->addWidget(new QSizeGrip(centralWidget));
        mainLayout->addLayout(bottomLayout);

        m_devToolsWindow->setCentralWidget(centralWidget);
    }

    m_devToolsWindow->setStyleSheet(this->styleSheet());

    // F12 по-прежнему тоггл-переключает окно (forceShow=false). Вызов из
    // "Просмотреть код" всегда должен ПОКАЗЫВАТЬ DevTools, а не прятать их,
    // если окно вдруг уже было открыто — иначе повторный правый клик на
    // другом элементе "закрывал" бы DevTools вместо перехода к нему.
    if (m_devToolsWindow->isVisible() && !forceShow) {
        m_devToolsWindow->hide();
        return;
    }

    if (!m_devToolsView->page() || m_devToolsView->page()->profile() != m_mainProfile) {
        QWebEnginePage* inspectorPage = new QWebEnginePage(m_mainProfile, m_devToolsView);
        m_devToolsView->setPage(inspectorPage);

        static const QString jsDarkMode = QStringLiteral(
            "try {"
            "    let prefs = JSON.parse(window.localStorage.getItem('devtools-preferences') || '{}');"
            "    if (prefs['uiTheme'] !== '\"dark\"') {"
            "        prefs['uiTheme'] = '\"dark\"';"
            "        window.localStorage.setItem('devtools-preferences', JSON.stringify(prefs));"
            "        window.location.reload();"
            "    }"
            "} catch(e) {}"
        );
        connect(inspectorPage, &QWebEnginePage::loadFinished, inspectorPage, [inspectorPage](bool ok) {
            if (ok) inspectorPage->runJavaScript(jsDarkMode);
            });
    }

    view->page()->setDevToolsPage(m_devToolsView->page());

    if (auto* label = m_devToolsWindow->findChild<QLabel*>("customTitleLabel")) {
        label->setText(u8"DevTools — " + view->title());
    }

    m_devToolsWindow->show();
    m_devToolsWindow->raise();
    m_devToolsWindow->activateWindow();
}


void MainWindow::inspectElementAt(QWebEngineView* view) {
    if (!view || !view->page()) return;

    openDevTools(view, /*forceShow=*/true);

    // InspectElement у QWebEnginePage триггерится по координатам ПОСЛЕДНЕГО
    // запроса контекстного меню на этой странице — Chromium сам помнит, по
    // какому узлу кликнули, когда наше кастомное меню строилось в
    // BrowserWebView::contextMenuEvent(). Поэтому вызов идёт сразу же,
    // без задержек — пока Chromium ещё помнит клик.
    view->page()->triggerAction(QWebEnginePage::InspectElement);
}


void MainWindow::blockElementAt(QWebEngineView* view, const QPoint& pos) {
    if (!view || !view->page()) return;

    QString js = QString(u8R"JS(
        (function() {
            const el = document.elementFromPoint(%1, %2);
            if (!el) return null;

            // От мелких инлайновых узлов (span/img/a внутри карточки)
            // поднимаемся на пару уровней вверх — реклама/баннер почти
            // всегда это контейнер-обёртка, а не сама картинка/текст.
            let target = el;
            const tiny = new Set(['SPAN', 'IMG', 'A', 'B', 'I', 'STRONG', 'EM']);
            let hops = 0;
            while (target.parentElement && tiny.has(target.tagName) && hops < 3) {
                target = target.parentElement;
                hops++;
            }

            function buildSelector(node) {
                if (node.id) return '#' + CSS.escape(node.id);
                if (node.className && typeof node.className === 'string') {
                    const cls = node.className.trim().split(/\s+/).filter(Boolean).slice(0, 2);
                    if (cls.length) {
                        return node.tagName.toLowerCase() + '.' +
                            cls.map(c => CSS.escape(c)).join('.');
                    }
                }
                return node.tagName.toLowerCase();
            }

            return JSON.stringify({
                selector: buildSelector(target),
                preview: (target.outerHTML || '').slice(0, 150)
            });
        })();
    )JS").arg(pos.x()).arg(pos.y());

    QPointer<MainWindow> self(this);
    QPointer<QWebEngineView> viewGuard(view);

    view->page()->runJavaScript(js, [self, viewGuard](const QVariant& result) {
        if (!self || !viewGuard) return; // вкладка/окно могли закрыться, пока летел JS

        QString json = result.toString();
        if (json.isEmpty() || json == "null") {
            QMessageBox::information(self, u8"Storm Shield", u8"Не удалось определить элемент под курсором.");
            return;
        }

        QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
        QString selector = obj.value("selector").toString();
        QString host = viewGuard->url().host().toLower();
        if (selector.isEmpty() || host.isEmpty()) return;

        // Кастомный фреймлесс-диалог вместо QInputDialog::getText() — у
        // системного QInputDialog белая ОС-рамка, которая выбивается из
        // тёмной темы браузера. Заголовок сделан по тому же образцу, что и
        // у окна DevTools (customTitleBar/customTitleLabel/titleCloseBtn +
        // WindowDragFilter для перетаскивания) — эти имена объектов уже
        // стилизуются текущей темой через общий QSS, поэтому просто
        // наследуем self->styleSheet() и рамка сама совпадёт с браузером.
        QDialog dlg(self);
        dlg.setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
        dlg.setStyleSheet(self->styleSheet());
        dlg.setFixedWidth(420);

        QVBoxLayout* mainLayout = new QVBoxLayout(&dlg);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        QWidget* titleBar = new QWidget(&dlg);
        titleBar->setFixedHeight(35);
        titleBar->setObjectName("customTitleBar");

        QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
        titleLayout->setContentsMargins(15, 0, 0, 0);
        titleLayout->setSpacing(0);

        QLabel* titleLabel = new QLabel(u8"🚫 Заблокировать элемент", titleBar);
        titleLabel->setObjectName("customTitleLabel");

        QPushButton* btnCloseTitle = new QPushButton(u8"✕", titleBar);
        btnCloseTitle->setObjectName("titleCloseBtn");
        btnCloseTitle->setFixedSize(35, 35);
        connect(btnCloseTitle, &QPushButton::clicked, &dlg, &QDialog::reject);

        titleLayout->addWidget(titleLabel);
        titleLayout->addStretch();
        titleLayout->addWidget(btnCloseTitle);

        mainLayout->addWidget(titleBar);
        titleBar->installEventFilter(new WindowDragFilter(titleBar));

        QWidget* body = new QWidget(&dlg);
        QVBoxLayout* bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(20, 16, 20, 16);
        bodyLayout->setSpacing(10);

        QLabel* infoLabel = new QLabel(
            QString(u8"Скрыть этот элемент на %1?\nCSS-селектор (можно поправить вручную):").arg(host), body);
        infoLabel->setWordWrap(true);

        QLineEdit* selectorEdit = new QLineEdit(selector, body);

        QHBoxLayout* buttonsLayout = new QHBoxLayout();
        buttonsLayout->addStretch();
        QPushButton* btnCancel = new QPushButton(u8"Отмена", body);
        QPushButton* btnOk = new QPushButton(u8"Заблокировать", body);
        btnOk->setDefault(true);
        buttonsLayout->addWidget(btnCancel);
        buttonsLayout->addWidget(btnOk);

        connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
        connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(selectorEdit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);

        bodyLayout->addWidget(infoLabel);
        bodyLayout->addWidget(selectorEdit);
        bodyLayout->addLayout(buttonsLayout);
        mainLayout->addWidget(body);

        selectorEdit->setFocus();
        selectorEdit->selectAll();

        if (dlg.exec() != QDialog::Accepted) return;

        QString finalSelector = selectorEdit->text().trimmed();
        if (finalSelector.isEmpty()) return;

        AdblockManager::instance().addCustomHideSelector(host, finalSelector);

        // Прячем сразу на открытой странице, не дожидаясь перезагрузки —
        // сохранённое правило само подхватится на следующих заходах через
        // applyCosmeticAndStealthScripts().
        QString escaped = finalSelector;
        escaped.replace("\\", "\\\\").replace("'", "\\'");
        QString hideNowJs = QString(
            "(function(){ try {"
            "  document.querySelectorAll('%1').forEach(e => e.style.setProperty('display','none','important'));"
            "} catch(e) {} })();"
        ).arg(escaped);
        viewGuard->page()->runJavaScript(hideNowJs);

        self->statusBar()->showMessage(u8"🚫 Элемент добавлен в правила Storm Shield", 4000);
        });
}