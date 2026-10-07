#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QIcon>
#include <QPointer>
#include <QWebEngineView>
#include <QSystemTrayIcon>
#include <QStringList>
#include "BrowserTopBar.h"
#include "DatabaseManager.h"
#include "Sidebar.h"
#include "PasswordManager.h"
#include "BookmarksBar.h"
#include "DownloadManager.h"
#include "PageTemplates.h"
#include <QWebEnginePage>

class PasswordManager;
class AIAssistantWidget;
class ResearchWidget; // Модуль боковой панели "🔬 Глубокое исследование" — см. ResearchWidget.h
class SmmPublishController; // Доводит посты SMM Auto-Publisher до реальной публикации — см. SmmPublishController.h
class BrowserWebView; // Вкладка-виджет: владеет указателем на окно-владельца (см. wireTab/attachTab)
class StormWebPage;   // Страница вкладки: тоже владеет указателем на окно-владельца
class QLineEdit;
class QToolButton;
class QLabel;
class QMenu;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr, bool isDetached = false);
    ~MainWindow();
    void applyTheme(const QString& themeName);
    QString getCurrentZoomString();
    QTabWidget* getTabWidget() const { return tabWidget; }

    // Делаем этот метод public, чтобы BookmarksBar мог его вызывать
    void loadBookmarksIntoMenu();
    AIAssistantWidget* getAiAssistant() const { return aiAssistantWidget; }
    ResearchWidget* getResearchWidget() const { return researchWidget; }
    // Нужны BrowserWebView для контекстного меню (генератор паролей, скачивание видео)
    PasswordManager* getPasswordManager() const { return passwordManager; }
    DownloadManager* getDownloadManager() const { return downloadManager; }
    DatabaseManager& getDatabaseManager() { return dbManager; } // ← добавлено для StormCloudBridge
    bool isCurrentPageBookmarked();
    bool isSidebarVisible() const;
    // Обновляет текст/цвет индикатора Storm Shield в статус-баре — вызывается
    // и при старте (синхронизация с сохранённым состоянием), и из
    // SettingsBridge::toggleShield() при каждом переключении в настройках.
    // Раньше индикатор вообще не обновлялся при выключении — всегда
    // показывал "Активен" независимо от реального состояния.
    void updateShieldStatusIndicator(bool enabled);

    // Переставляет боковую панель (sidebar) в рабочей области — "left" (по
    // умолчанию, слева от вкладок) или "right" (справа от вкладок, но левее
    // выезжающей панели загрузок). Сохраняет выбор в QSettings
    // ("browser/sidebar_position"), чтобы позиция восстанавливалась при
    // следующем запуске. Вызывается из SettingsBridge::setSidebarPosition()
    // (Настройки → Вид), а также один раз из setupUi() при старте, чтобы
    // применить ранее сохранённое положение.
    void setSidebarPosition(const QString& position);

    // Недавно закрытые вкладки — для пункта "🕒 История" в гамбургер-меню
    // (см. MenuBuilder.cpp) и восстановления по Ctrl+Shift+T. Список только в
    // памяти (не переживает перезапуск браузера, как и в большинстве
    // браузеров), самый свежий — в начале, максимум 15 штук. Заполняется из
    // MainWindow::closeTab() (см. MainWindow_Tabs.cpp).
    void recordClosedTab(const QUrl& url);
    QStringList recentlyClosedUrls() const { return m_recentlyClosedUrls; }

    // Показывает системное уведомление через ОБЩУЮ, постоянную иконку в трее
    // (см. m_trayIcon ниже), а не через отдельный QSystemTrayIcon, который
    // сейчас поднимает под себя, например, AntiSub в TodoWidget. Ничего не
    // делает, если иконка не создана (детач-окно или трей недоступен в ОС —
    // см. setupTrayIcon()). Публичный метод специально для того, чтобы другие
    // виджеты (TodoWidget/AntiSub, SmmAutoPublisherWidget и т.п.) могли слать
    // уведомления через mainWindow, не заводя каждый свою иконку.
    void showTrayNotification(const QString& title, const QString& message);

    // DevTools / Storm Shield — вызываются и из F12 (см. setupUi()), и из
    // пунктов "🔍 Просмотреть код" / "🚫 Заблокировать элемент" контекстного
    // меню (BrowserWebView::contextMenuEvent). Публичные, т.к. BrowserWebView
    // вызывает их через mainWindow.
    //
    // forceShow=false (по умолчанию для F12) — окно DevTools тоггл-
    // переключается: если уже открыто, скрывается. forceShow=true (как
    // всегда вызывает inspectElementAt()) — окно всегда показывается,
    // повторный правый клик по другому элементу не должен его прятать.
    void openDevTools(QWebEngineView* view, bool forceShow = true);
    void inspectElementAt(QWebEngineView* view);
    void blockElementAt(QWebEngineView* view, const QPoint& pos);
    void forceQuitForUpdate() { m_isQuitting = true; }

public slots:
    void navigateToUrl();
    void goBack();
    void goForward();
    void reloadPage();
    void stopLoading();
    // Открывает/поднимает наверх плавающую панель поиска по странице (Ctrl+F).
    // Панель не модальная и остаётся видимой поверх контента вкладки, пока
    // её не закроют крестиком/Esc — раньше здесь был одноразовый модальный
    // QInputDialog без кнопок "следующее/предыдущее совпадение".
    void showFindBar();
    // Ведёт на страницу, заданную в настройках стартовой страницы
    // (startup/mode / startup/custom_url) — используется кнопкой "🧭 На
    // главную" в BrowserTopBar (см. BrowserTopBar.cpp).
    void goHome();
    void toggleSidebar();
    void toggleReaderMode();
    void toggleAIPanel();
    void openIncognitoTab();
    void takeScreenshot();
    void openDownloads();
    void openProfile();
    void openSettingsTab();
    void openBookmarksTab();

    void addNewTab(const QUrl& url, bool isIncognito = false);
    // Восстановление встроенной страницы (storm-talk, storm://home, ...) после
    // F5/краха рендер-процесса: у этих вкладок нет реального сетевого адреса,
    // обычный reload() ходит по фиктивному baseUrl и получает ошибку соединения.
    // Возвращает true, если вкладка была внутренней и содержимое перезалито.
    bool reloadInternalTab(QWebEngineView* view);
    void addCurrentBookmark();
    void clearBookmarks();
    void removeCurrentBookmark();
    void showHistory();
    void clearHistory();
    void closeTab(int index);
    void detachTab(int index);                      // Отрыв вкладки в новое окно
    // Возврат/приём вкладки из другого окна. icon и insertIndex — опциональны
    // (по умолчанию ведёт себя как раньше: без иконки, вставка перед кнопкой "+").
    // Добавлены, чтобы TabDragDropFilter мог передать иконку и точную позицию
    // сброса при перетаскивании вкладки между двумя уже открытыми окнами.
    void attachTab(QWidget* tabView, const QString& title, const QIcon& icon = QIcon(), int insertIndex = -1);
    void setTabCategory(int index, const QString& category, const QColor& color);
    // v1.2.9 — закрепление вкладок (pin): компактные вкладки-иконки в начале
    // полосы. setTabPinned — ядро (по виджету, работает и после переноса
    // вкладки в другое окно); toggleTabPin — обёртка для контекстного меню.
    void toggleTabPin(int index);
    void setTabPinned(QWidget* view, bool pinned);
    // Полный заголовок вкладки «для передачи»: у закреплённой вкладки текст
    // в полосе пуст (компактная иконка), полный хранится в pinnedTitle.
    QString tabTitleForTransport(int index) const;
    void groupTabsByCategory();
    void showTabContextMenu(const QPoint& pos);
    void showPasswordManager();
    void changeMasterPassword();
    void resetPasswordVault();
    void importPasswords();
    void addFavorite();
    void removeFavorite();
    void clearFavorites();
    void zoomIn();
    void zoomOut();
    void toggleFullScreen();
    void openImportedTabs();
    void importTabs();
    void exportTabs();
    void printCurrentPage();
    void clearBrowserData();
    void setAsDefaultBrowser();
    void toggleBookmarksBar();

    // Восстановление вкладок из recentlyClosedUrls() — reopenClosedTab()
    // используется пунктами меню (конкретный URL из списка), а
    // reopenLastClosedTab() — шорткатом Ctrl+Shift+T (всегда самый верхний,
    // то есть последний закрытый).
    void reopenClosedTab(const QUrl& url);
    void reopenLastClosedTab();

    // v1.2.9 — раскрыть боковую панель на вкладке Заметок (используется
    // web-clipper'ом после «📝 Сохранить в Заметки», чтобы пользователь
    // сразу видел результат).
    void openNotesPanel();

    // v1.2.9 — горячие клавиши сессий: Ctrl+Alt+S сохраняет текущую сессию
    // (имя «Сессия от ДД.ММ ЧЧ:ММ», как на storm://bookmarks), Ctrl+Alt+R
    // восстанавливает последнюю сохранённую. Результат — в статус-баре.
    void quickSaveSession();
    void quickRestoreLastSession();

    void toggleShieldException();
    void setNewTabBackground();
    void showStartupSettings();
    void setSearchEngine(const QString& engineName);
    void setUiLanguage(const QString& langCode);

    // Перевод текущей открытой страницы через виджет Google Translate (внедряется в DOM страницы)
    void translateCurrentPage(const QString& langCode, const QString& engine = "google");

    // Вызывается из контекстного меню страницы (BrowserWebView) для пунктов подменю "Storm AI"
    void processAiAction(const QString& actionType, const QString& textContext);

private slots:
    void updateAddressBar(const QUrl& url);
    // Кнопки ▲/▼ панели поиска — переход к предыдущему/следующему совпадению
    void findNext();
    void findPrevious();
    // Крестик/Esc панели поиска — прячет панель и снимает подсветку на странице
    void hideFindBar();

    // Клик/двойной клик по иконке в трее — разворачивает окно обратно.
    // Само меню иконки (Открыть/Выход) обрабатывается лямбдами прямо в
    // setupTrayIcon(), сюда отдельный слот не нужен.
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void setupUi(bool isDetached = false);

    // C-1/C-7: ВСЕ окна браузера (главное и откреплённые) используют ОДИН
    // основной профиль WebEngine на весь процесс. Раньше каждое окно
    // создавало свой QWebEngineProfile "StormMainProfile" на одних и тех же
    // каталогах Cache/Storage — несколько живых профилей на одном хранилище
    // Chromium не поддерживает: конфликты файловых блокировок, порча кэша,
    // падения; а профиль, умерший вместе со своим окном, оставался нужен
    // вкладкам, перенесённым в другие окна (use-after-free).
    // Профили создаются один раз и сознательно живут до конца процесса
    // (уничтожение общего профиля до выхода — само по себе источник гонок).
    static QWebEngineProfile* s_sharedMainProfile;

    // C-1: инкогнито- и игровые вкладки раньше создавали СВОЙ профиль на
    // каждую вкладку — при долгих сессиях с десятками вкладок это
    // исчерпывало память (OOM). Теперь каждый из этих профилей тоже один
    // на процесс и переиспользуется всеми вкладками своего типа.
    static QWebEngineProfile* s_incognitoProfile;
    static QWebEngineProfile* s_arcadeProfile;
    static QWebEngineProfile* sharedAuxProfile(bool gameMode);

    // C-2/P1-2: полный набор сигнальных подключений ОДНОЙ вкладки к ЭТОМУ
    // окну (страница + вид + спиннер + индикатор загрузки). Вызывается из
    // addNewTab() при создании вкладки и из attachTab() при переносе её в
    // другое окно — чтобы вкладка никогда не оставалась с обработчиками,
    // указывающими на уже закрытое окно (use-after-free при отрыве вкладок
    // был одной из причин самопроизвольного закрытия браузера).
    void wireTab(BrowserWebView* view, StormWebPage* page);

    // Создаёт постоянную иконку Storm Browser в системном трее — один раз,
    // только для главного (не детач) окна, и только если ОС вообще
    // поддерживает трей (QSystemTrayIcon::isSystemTrayAvailable()). Не
    // привязана ни к одной конкретной функции: closeEvent() использует её
    // для сворачивания при browser/minimize_to_tray=true, а
    // showTrayNotification() выше даёт остальному приложению общий канал
    // уведомлений вместо того, чтобы каждый виджет заводил свою иконку.
    void setupTrayIcon();

    void performFindBarSearch(bool backward, bool resetHighlight);

    // --- Вкладки: кнопка «Все открытые вкладки» и перенос между окнами ---
    // Пересобирает выпадающий список открытых вкладок (вызывается перед
    // каждым показом меню) и обновляет счётчик на самой кнопке. Кнопка
    // появилась взамен штатных кнопок прокрутки QTabBar, которые при
    // нехватке места выглядели как пара серых квадратиков, сливавшихся
    // с фоном, и не давали увидеть полный список вкладок.
    void rebuildTabsMenu(QMenu* menu);
    void updateTabsListButton();
    // Поиск окна-цели для возврата вкладки (основное, иначе любое другое)
    // и сам перенос — пункты контекстного меню вкладки. См.
    // MainWindow_Tabs.cpp.
    MainWindow* findHomeWindowForReattach() const;
    void moveTabToWindow(int index, MainWindow* target);

    // Куки не за которых нет в списке исключений удаляются на СЛЕДУЮЩЕМ
    // старте (см. подробное объяснение у SettingsBridge::toggleClearSiteDataOnClose
    // в SettingsBridge.h) — вызывается один раз из setupUi(), если в
    // QSettings стоит флаг "session/pending_site_data_cleanup".
    void runSiteDataCleanup(const QStringList& exceptions);

    bool isCurrentPageShieldExcepted();
    PasswordManager* passwordManager;

    Sidebar* sidebar;
    BrowserTopBar* topBar;
    BookmarksBar* bookmarksBar;
    QTabWidget* tabWidget;
    DatabaseManager dbManager;
    DownloadManager* downloadManager;
    AIAssistantWidget* aiAssistantWidget = nullptr;
    ResearchWidget* researchWidget = nullptr;
    // Владеет им MainWindow (не сам SmmAutoPublisherWidget) — контроллеру
    // нужен доступ к addNewTab()/getTabWidget(), то есть он логически часть
    // "браузерной" стороны интеграции, а не UI-виджета боковой панели.
    SmmPublishController* smmPublishController = nullptr;

    QWebEngineProfile* m_mainProfile = nullptr;

    PageTemplates pageTemplates;

    QPointer<QMainWindow> m_devToolsWindow;
    QPointer<QWebEngineView> m_devToolsView;


    // Фильтр перетаскивания вкладок (TabDragDropFilter из MainWindow.cpp).
    // Держим указатель, чтобы ставить его же на каждый BrowserWebView
    // (drop-зона «вернуть вкладку» — вся площадь окна, а не только полоса
    // вкладок) и снимать при переносе вкладки в другое окно.
    QObject* m_tabDragFilter = nullptr;
    // Кнопка «☰ N ▾» в правом углу полосы вкладок — список всех вкладок.
    QToolButton* m_tabsListBtn = nullptr;

    QWidget* m_findBar = nullptr;
    QLineEdit* m_findLineEdit = nullptr;
    QLabel* m_findMatchCountLabel = nullptr;
    QToolButton* m_findPrevButton = nullptr;
    QToolButton* m_findNextButton = nullptr;
    QToolButton* m_findCloseButton = nullptr;

    void showSavePasswordPrompt(const QString& domain, const QString& login, const QString& password);

    // Постоянная иконка в трее главного окна. nullptr для детач-окон и в
    // окружениях без поддержки трея — весь код, который её использует,
    // проверяет указатель перед обращением (см. closeEvent(),
    // showTrayNotification()).
    QSystemTrayIcon* m_trayIcon = nullptr;
    // Взводится ТОЛЬКО пунктом "Выход" в меню трея перед вызовом close() —
    // сигнал closeEvent()'у, что это настоящий выход, а не обычное закрытие
    // окна, даже если включена настройка "сворачивать в трей".
    bool m_isQuitting = false;

    // Недавно закрытые вкладки — см. recordClosedTab()/recentlyClosedUrls()
    // выше. Самый свежий URL — в начале списка (index 0).
    QStringList m_recentlyClosedUrls;

protected:
    void closeEvent(QCloseEvent* event) override;
};