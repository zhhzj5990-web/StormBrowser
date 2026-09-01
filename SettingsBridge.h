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

    // --- Конфиденциальность ---
    Q_INVOKABLE void openProxy();
    Q_INVOKABLE void toggleShield(bool enabled);
    Q_INVOKABLE void toggleGameMode(bool enabled);
    Q_INVOKABLE void toggleHwAccel(bool enabled);

    // Исключения Storm Shield — полный обход блокировки для перечисленных хостов
    // (см. ShieldInterceptor::isExcepted). Список возвращается/принимается как JSON-
    // массив строк, например ["vk.com","example.com"].
    Q_INVOKABLE QString getShieldExceptionsJson();
    Q_INVOKABLE void addShieldException(const QString& host);
    Q_INVOKABLE void removeShieldException(const QString& host);

    // --- Пароли ---
    Q_INVOKABLE void showPasswordManager();
    Q_INVOKABLE void changeMasterPassword();
    Q_INVOKABLE void resetPasswordVault();
    Q_INVOKABLE void importPasswords();

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
    //  "cloudLoggedIn":true,"cloudUsername":"user123","minimizeToTray":false}
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

private:
    MainWindow* mw;
};