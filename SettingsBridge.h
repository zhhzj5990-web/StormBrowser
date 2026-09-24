#pragma once
#include <QObject>
#include <QString>

class MainWindow;

// Мост QWebChannel между JS страницы storm://settings и C++.
// Работает по тому же принципу, что и HomeAIBridge для storm://home —
// JS вызывает Q_INVOKABLE методы напрямую как обычные функции объекта,
// без необходимости в кастомных URL-схемах (которые Chromium блокирует).
class SettingsBridge : public QObject {
    Q_OBJECT
public:
    explicit SettingsBridge(MainWindow* mainWin, QObject* parent = nullptr);

    // --- Основные ---
    Q_INVOKABLE void setLanguage(const QString& langCode);
    Q_INVOKABLE void setSearchEngine(const QString& engineName);
    Q_INVOKABLE void showStartupSettings();
    Q_INVOKABLE void setNewTabBackground();
    Q_INVOKABLE void toggleBookmarksBar(bool checked);
    Q_INVOKABLE void clearBrowserData();
    Q_INVOKABLE void printCurrentPage();
    Q_INVOKABLE void setAsDefaultBrowser();
    // Крестик окна сворачивает в трей вместо закрытия процесса — сама
    // иконка постоянная и создаётся один раз в MainWindow::setupTrayIcon(),
    // этот тумблер только переключает поведение MainWindow::closeEvent().
    // По умолчанию выключено (см. значение по умолчанию в closeEvent()).
    Q_INVOKABLE void toggleMinimizeToTray(bool enabled);

    // --- Вид ---
    Q_INVOKABLE void applyTheme(const QString& themeId);
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void toggleFullScreen();
    Q_INVOKABLE void toggleReaderMode();
    // Возвращает строку вида "120%" — вызывается из JS настроек после
    // zoomIn/zoomOut, чтобы обновить цифру рядом с кнопками масштаба
    // (раньше она была статичной и не менялась при клике).
    Q_INVOKABLE QString getCurrentZoom();

    // Боковая панель (виджеты Storm AI/Заметки/Задачи и т.д.) — раньше
    // переключалась пунктом "📊 Боковая панель" в гамбургер-меню, теперь это
    // часть настроек. mw->toggleSidebar() сам ничего не принимает и просто
    // переключает текущее состояние, поэтому здесь сверяемся с
    // mw->isSidebarVisible(), чтобы не дёрнуть его лишний раз, если чекбокс
    // и так уже соответствует реальному состоянию (например, после
    // применения снапшота на другой открытой вкладке настроек).
    Q_INVOKABLE void toggleSidebarVisible(bool enabled);
    // "left" (по умолчанию) или "right" — см. MainWindow::setSidebarPosition().
    Q_INVOKABLE void setSidebarPosition(const QString& position);

    // --- Конфиденциальность ---
    Q_INVOKABLE void toggleShield(bool enabled);
    Q_INVOKABLE void toggleGameMode(bool enabled);
    Q_INVOKABLE void toggleHwAccel(bool enabled);

    // Исключения Storm Shield — полный обход блокировки для перечисленных хостов
    // (см. ShieldInterceptor::isExcepted). Список возвращается/принимается как JSON-
    // массив строк, например ["vk.com","example.com"].
    Q_INVOKABLE QString getShieldExceptionsJson();
    Q_INVOKABLE void addShieldException(const QString& host);
    Q_INVOKABLE void removeShieldException(const QString& host);

    // Апгрейд http→https для основной навигации (см. анонимный класс
    // HttpsUpgradeInterceptor в MainWindow.cpp, который оборачивает
    // ShieldInterceptor). Как и аппаратное ускорение — применяется только
    // после перезапуска, поэтому здесь просто QSettings + предупреждение,
    // без обращения к mw.
    Q_INVOKABLE void toggleHttpsOnly(bool enabled);

    // Очистка данных сайтов (куки + кэш) при закрытии браузера, кроме хостов
    // из списка исключений — тот же формат JSON-массива строк, что и у
    // исключений Shield. Публичный API QWebEngineCookieStore асинхронный и не
    // даёт гарантий успеть удалить куки в момент самого закрытия (событийный
    // цикл может остановиться раньше), поэтому фактическая очистка отложена
    // до следующего запуска — см. MainWindow::closeEvent() (ставит флаг) и
    // MainWindow::setupUi() (выполняет её один раз в самом начале, пока
    // профиль ещё пустой и время не поджимает).
    Q_INVOKABLE void toggleClearSiteDataOnClose(bool enabled);
    Q_INVOKABLE QString getSiteDataExceptionsJson();
    Q_INVOKABLE void addSiteDataException(const QString& host);
    Q_INVOKABLE void removeSiteDataException(const QString& host);

    // Разрешения сайтов (камера/микрофон/геолокация/уведомления и т.д.) —
    // сам запрос и диалог "Разрешить/Заблокировать" обрабатывает
    // BrowserWebView::wirePermissionHandling() при первом обращении сайта;
    // здесь только просмотр и отзыв уже принятых решений (после отзыва сайт
    // спросит снова при следующем обращении к этому разрешению).
    // JSON — массив {"featureId":.., "feature":"Камера","host":"..","allowed":true}.
    Q_INVOKABLE QString getSitePermissionsJson();
    Q_INVOKABLE void revokeSitePermission(int featureId, const QString& host);

    // Проверка сохранённых паролей на утечки через Pwned Passwords API
    // (Have I Been Pwned) по схеме k-anonymity: наружу уходят только первые
    // 5 символов SHA-1 хэша пароля, не сам пароль и не полный хэш. Результат —
    // асинхронно, сигналом passwordBreachCheckResult (по паролю на каждый
    // сетевой запрос, могут быть десятки, ждём все).
    // vaultLocked=true в результате означает, что хранилище паролей заперто
    // (getAllDecrypted() вернул пусто, хотя пароли есть) — нужно сначала
    // открыть "🔑 Storm Vault" выше, чтобы разблокировать.
    Q_INVOKABLE void checkPasswordsForBreaches();

    // --- Proxy / VPN ---
    // Раньше это было отдельное окно ProxyDialog (см. старый ProxyManager.h);
    // теперь Proxy/VPN — часть страницы настроек (своя вкладка в сайдбаре),
    // а вместо кнопок "Подключить"/"Применить" там тумблеры. Вся логика,
    // которая раньше жила в слотах ProxyDialog, перенесена сюда и дёргает те
    // же статические методы ProxyManager — сама бизнес-логика не менялась.
    //
    // Три способа подключения (Smart-ссылка / ручной SOCKS5-HTTP / прокси из
    // бесплатного списка) взаимоисключающие на бэкенде — включение одного
    // останавливает предыдущий (см. ProxyManager::disableProxy() внутри
    // startXrayCore()).
    //
    // Текущее состояние подключения одной строкой — {"connected":true,
    // "statusText":"Статус: VLESS Активен","smartLink":"vless://..."}.
    // smartLink — последняя сохранённая ссылка (proxy/smart_link), чтобы
    // страница настроек могла подставить её в поле при открытии/обновлении.
    Q_INVOKABLE QString getProxyStatusJson();
    // Расшифровывает ссылку-ключ (VLESS/VMess/Trojan/SS/Hysteria 2) и
    // запускает ядро xray. Результат приходит асинхронно сигналом
    // proxyConnectResult — сам парсинг быстрый, но старт процесса ядра и
    // ожидание его поднятия (см. ProxyManager::startXrayCore) не мгновенны.
    Q_INVOKABLE void connectSmartLink(const QString& rawLink);
    // Удаляет сохранённую ссылку-ключ (proxy/smart_link) из QSettings.
    Q_INVOKABLE void deleteSmartLink();
    // Применяет ручной SOCKS5/HTTP-прокси напрямую как системный прокси
    // приложения. user/pass — опциональны (пустая строка, если без
    // авторизации). Результат — сигналом proxyManualApplyResult.
    Q_INVOKABLE void applyManualProxy(const QString& type, const QString& host, int port, const QString& user, const QString& pass);
    // То же самое, но host:port берётся строкой из списка бесплатных прокси
    // (см. fetchFreeProxies) — без логина/пароля, как и раньше в ProxyDialog.
    // Результат — тем же сигналом proxyManualApplyResult.
    Q_INVOKABLE void applyListProxy(const QString& type, const QString& hostPort);
    // Отключает текущий активный способ (ядро xray и/или системный прокси) —
    // общий "выключатель", вызывается при переключении любого тумблера в
    // положение "выключено".
    Q_INVOKABLE void disableProxyAll();
    // Асинхронно скачивает список бесплатных прокси (тот же источник —
    // TheSpeedX/PROXY-List на GitHub, что и раньше в ProxyDialog). Результат —
    // сигналом freeProxiesFetched в виде JSON-массива строк "host:port".
    Q_INVOKABLE void fetchFreeProxies(const QString& type);
    // Открывает реферальную ссылку на Wizard VPN (Premium) во внешнем браузере —
    // раньше делалось прямо из лямбды кнопки в ProxyDialog, теперь через мост.
    Q_INVOKABLE void openWizardVpnRef();

    // --- Сертификаты ---
    // См. CertificateManager.h для полного описания трёх источников доверия
    // (встроенные Минцифры / системные Windows / пользовательские).
    //
    // Метаданные встроенных + пользовательских сертификатов (не системных —
    // их могут быть десятки, для них отдельно getSystemCertCount()).
    Q_INVOKABLE QString getCertificatesJson();
    // Сколько сертификатов сейчас в системном хранилище Windows — просто
    // информационная цифра для карточки в настройках, не зависит от тумблера.
    Q_INVOKABLE int getSystemCertCount();
    // Тумблер "использовать сертификаты Windows" (по умолчанию включён).
    Q_INVOKABLE void toggleUseSystemCerts(bool enabled);
    // Открывает системную оснастку управления сертификатами Windows
    // (certmgr.msc) — тонкая настройка самого хранилища ОС остаётся там, мы
    // её не дублируем.
    Q_INVOKABLE void openSystemCertStore();
    // Открывает диалог выбора файла (.cer/.crt/.pem) и импортирует
    // сертификат(ы) из него. Возвращает пустую строку при успехе ИЛИ если
    // пользователь отменил выбор файла (JS отличает эти два случая по
    // отдельному возврату диалога — здесь достаточно того, что both cases
    // ничего не показывают пользователю), иначе — текст ошибки для показа.
    Q_INVOKABLE QString importCertificateFile();
    // Удаляет пользовательский сертификат по id (SHA-256 отпечатку, см.
    // getCertificatesJson). На встроенные сертификаты не действует.
    Q_INVOKABLE void removeCertificate(const QString& id);

    // --- Пароли ---
    Q_INVOKABLE void showPasswordManager();
    Q_INVOKABLE void changeMasterPassword();
    Q_INVOKABLE void resetPasswordVault();
    Q_INVOKABLE void importPasswords();

    // --- Автозаполнение (адреса) ---
    // Хранилище пока живёт прямо в QSettings как JSON-массив объектов —
    // {"id":"...","fullName":"...","phone":"...","email":"...",
    //  "addressLine":"...","city":"...","zip":"..."}, по той же логике, что
    // и getCertificatesJson()/getShieldExceptionsJson() выше. id генерируется
    // здесь же (QUuid) при добавлении.
    //
    // ВАЖНО: это только хранение и управление списком в настройках. Реальная
    // подстановка сохранённого адреса в поля формы на странице и предложение
    // "Сохранить адрес?" после отправки формы — это работа с DOM страницы,
    // которая должна жить там же, где уже сделан похожий автозаполнитель для
    // паролей (контекстное меню BrowserWebView) — здесь эта часть НЕ
    // реализована, только сами данные и тумблер.
    Q_INVOKABLE QString getSavedAddressesJson();
    Q_INVOKABLE void addSavedAddress(const QString& fullName, const QString& phone, const QString& email,
        const QString& addressLine, const QString& city, const QString& zip);
    Q_INVOKABLE void removeSavedAddress(const QString& id);
    Q_INVOKABLE void toggleOfferSaveAddress(bool enabled);

    // --- Загрузки ---
    // Открывает системный диалог выбора папки и, если пользователь что-то выбрал,
    // сохраняет её как папку по умолчанию (тот же QSettings-ключ, что использует
    // DownloadManager::lastDownloadDir() для истории/торрентов). Возвращает выбранный
    // путь или пустую строку, если пользователь отменил — так JS сразу знает, обновлять
    // ли подпись на странице, не делая отдельный round-trip за текущим значением.
    Q_INVOKABLE QString chooseDownloadFolder();
    // Если выключено — DownloadManager::addDownload()/TorrentItem::startDownload()
    // сохраняют без диалога, сразу в папку по умолчанию.
    Q_INVOKABLE void toggleDownloadAskEachTime(bool enabled);

    // --- Аккаунт ---
    // Открывает вкладку storm://cloud (или переключается на неё, если уже открыта) —
    // тот же MainWindow::openProfile(), что использует и остальной браузер. Сам статус
    // входа (залогинен/нет, под каким именем) читается напрямую из QSettings в
    // getSettingsSnapshotJson() ниже — заводить здесь второй StormCloudBridge только
    // ради строки статуса избыточно, вся реальная работа с аккаунтом остаётся на
    // самой странице storm://cloud.
    Q_INVOKABLE void openStormCloud();

    // --- Синхронизация состояния ---
    // Снапшот тумблеров с мгновенным применением (Shield/Game Mode/HW-ускорение/
    // панель закладок/папка загрузок/статус входа в Storm Cloud) в виде JSON-строки:
    // {"bookmarksBar":false,"shield":true,"gameMode":false,"hwAccel":true,
    //  "downloadAskEachTime":true,"downloadDir":"C:/Users/.../Downloads",
    //  "cloudLoggedIn":true,"cloudUsername":"user123","minimizeToTray":false,
    //  "useSystemCerts":true,"sidebarVisible":true,"sidebarPosition":"left",
    //  "httpsOnly":false,"clearSiteDataOnClose":false,
    //  "offerSaveAddress":true}
    // Нужен, чтобы вкладка storm://settings могла подтянуть актуальное состояние
    // при получении фокуса — например, если открыто две вкладки настроек, или
    // состояние поменялось где-то ещё в приложении (в т.ч. вход/выход на вкладке
    // Storm Cloud), а не через саму эту страницу.
    // Настройки ИИ сюда намеренно не входят: они применяются только по явной
    // кнопке "Сохранить" (saveAI), а не сразу при изменении, так что рассинхрона
    // на лету для них не бывает.
    Q_INVOKABLE QString getSettingsSnapshotJson();

    // --- ИИ ---
    // visionModel/gigaVisionModel — необязательные (по умолчанию пустые), чтобы старый вызов
    // из JS без них не сломался: settings.html может обновиться позже отдельным изменением,
    // а мост тут уже готов их принять, когда поля появятся.
    // visionModel — модель для запросов со скриншотом через OpenRouter (ai/vision_model).
    // gigaVisionModel — то же самое, но для GigaChat (ai/gigachat_vision_model); нужна
    // модель уровня GigaChat-2-Max/Pro, иначе распознавание изображений не работает.
    // tavilyKey — ключ поиска Tavily для модуля "🔬 Глубокое исследование"
    // (research/tavily_key). Не относится к ai/* — это отдельный сервис (поиск
    // источников), не провайдер чат-ИИ, поэтому и не участвует в выборе ai-mode.
    // Пустая строка (по умолчанию) — модуль исследования использует бесплатный
    // DuckDuckGo без ключа (см. ResearchManager::beginLinkGathering).
    Q_INVOKABLE void saveAI(const QString& mode, const QString& apiKey, const QString& gigaKey, const QString& gigaModel,
        const QString& visionModel = QString(), const QString& gigaVisionModel = QString(),
        const QString& imageKey = QString(), const QString& videoKey = QString(),
        const QString& tavilyKey = QString());
    // Обнуляет счётчик суммарно потраченных токенов GigaChat (ai/gigachat_tokens_used),
    // который копится в AIAssistantWidget::onNetworkReply() из поля "usage" ответа.
    // Кнопка сброса на странице настроек видна только в секции GigaChat.
    Q_INVOKABLE void resetGigaTokenCounter();

    // Асинхронная проверка ключа: делает лёгкий сетевой запрос (для GigaChat — тот же
    // обмен ключа на access_token, что и перед обычным чатом; для OpenRouter — запрос
    // информации о ключе, без реального чат-запроса и трат токенов) и сообщает результат
    // через сигнал aiConnectionTested, а не через возврат значения — HTTP-запрос идёт по
    // сети, поэтому сама testAiConnection() возвращается сразу же, не дожидаясь ответа.
    // backend — "openrouter" или "gigachat". key передаётся явно (а не читается из
    // QSettings), чтобы можно было проверить ключ, который пользователь только что ввёл
    // в поле, но ещё не нажал "Сохранить". Логика запроса намеренно не переиспользует
    // AIAssistantWidget (не хотим тянуть сюда зависимость от агентного виджета ради
    // проверки ключа) — минимальное дублирование двух already-существующих запросов.
    Q_INVOKABLE void testAiConnection(const QString& backend, const QString& key);

    // --- Справка ---
    Q_INVOKABLE void showHelp();
    Q_INVOKABLE void checkUpdates();
    Q_INVOKABLE void openLogs();

signals:
    void aiConnectionTested(const QString& backend, bool ok, const QString& message);
    // Результат для Smart-ссылки (parseSmartLink + startXrayCore) —
    // protocolName пустой при неудаче (ссылку не удалось даже распознать).
    void proxyConnectResult(bool ok, const QString& message, const QString& protocolName);
    // Общий результат для ручного прокси И для прокси из бесплатного списка
    // (applyManualProxy/applyListProxy) — statusText пустой при неудаче.
    void proxyManualApplyResult(bool ok, const QString& statusText);
    // JSON-массив строк "host:port", максимум 100 штук (см. fetchFreeProxies).
    void freeProxiesFetched(const QString& listJson);
    // Результат checkPasswordsForBreaches() — см. описание там же.
    void passwordBreachCheckResult(const QString& breachesJson, bool vaultLocked);

private:
    MainWindow* mw;
};