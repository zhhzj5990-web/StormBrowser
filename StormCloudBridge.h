#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QString>
#include <QJsonArray>

class MainWindow;

// Мост QWebChannel для вкладки storm://cloud (Личный Кабинет).
// Сетевая логика повторяет StormCloud.cpp / sync_dialog.py (тот же сервер,
// те же эндпоинты), но результаты возвращает в JS через сигналы.
class StormCloudBridge : public QObject {
    Q_OBJECT
public:
    // mainWin нужен для доступа к истории (DatabaseManager) и паролям
    // (PasswordManager) при реальной синхронизации с облаком.
    explicit StormCloudBridge(MainWindow* mainWin, QObject* parent = nullptr);

    Q_INVOKABLE void login(const QString& username, const QString& password);
    // email теперь ОБЯЗАТЕЛЕН для новых регистраций — нужен для восстановления
    // пароля и уведомлений о новом устройстве (см. forgotPassword/setEmail ниже).
    Q_INVOKABLE void registerAccount(const QString& username, const QString& password, const QString& inviteCode, const QString& email);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void checkPing();
    Q_INVOKABLE void fetchBillingInfo();
    Q_INVOKABLE void purchase(const QString& packageId);
    // Пакеты отчётов Deep Research Report — те же package_id уходят в уже
    // существующий purchase() (сервер решает цену/содержимое по одному
    // общему справочнику пакетов, отдельный метод под это не нужен):
    // "pack_research_10" / "pack_research_20" / "pack_research_30".
    // Свежий статус квоты после покупки — обычный fetchResearchQuota().
    Q_INVOKABLE void fetchResearchQuota();
    Q_INVOKABLE void requestBeta();
    Q_INVOKABLE void openVkCommunity();

    Q_INVOKABLE void saveAvatar(const QString& avatarData);
    Q_INVOKABLE void savePremiumTheme(const QString& bgChoice, const QString& frameChoice, const QString& avatarChoice);
    Q_INVOKABLE void setAutoSync(bool enabled);

    Q_INVOKABLE void startVpn();
    Q_INVOKABLE void stopVpn();

    Q_INVOKABLE void clearCloudData();

    // Реальная синхронизация: читает историю (DatabaseManager) и пароли
    // (PasswordManager), шифрует через FernetCrypto, отправляет на /sync/push.
    Q_INVOKABLE void requestManualSync();

    // Реальные счётчики для карточек "История"/"Пароли"/"Закладки" в ЛК.
    Q_INVOKABLE void fetchLocalStats();

    // --- ДРУЗЬЯ ---
    // Добавленные вручную (по username) + приглашённые по рефералке — см. friendsReceived.
    Q_INVOKABLE void fetchFriends();
    Q_INVOKABLE void addFriend(const QString& friendUsername);
    Q_INVOKABLE void removeFriend(const QString& friendUsername);

    // --- EMAIL / ВОССТАНОВЛЕНИЕ ПАРОЛЯ ---
    // Для аккаунтов, зарегистрированных ДО введения email — добавить его задним числом.
    Q_INVOKABLE void setEmail(const QString& email);
    // loginOrEmail — можно указать либо username, либо email. Работает БЕЗ логина
    // (вызывается прямо с экрана входа) — m_currentUser/m_currentPassword ещё не заданы.
    Q_INVOKABLE void forgotPassword(const QString& loginOrEmail);

    // --- ХАБ НАСТРОЕК (шестерёнка): устройства, смена пароля, уведомления ---
    Q_INVOKABLE void fetchLoginHistory();
    // Смена пароля БЕЗ выхода из аккаунта — в отличие от forgotPassword знает
    // текущий пароль заранее. При успехе сервер очищает облачные passwords/settings
    // (они зашифрованы ключом от пароля аккаунта), поэтому на своей стороне
    // обновляем m_currentPassword — локальные данные не трогаем, они сами
    // уйдут в облако при следующей синхронизации.
    Q_INVOKABLE void changePassword(const QString& oldPassword, const QString& newPassword);
    Q_INVOKABLE void fetchNotificationPrefs();
    Q_INVOKABLE void saveNotificationPrefs(bool notifyNewDevice, bool notifyPurchases, bool notifyPremiumExpiry);

    // --- PREMIUM: подарок другу ---
    // packageId — один из pack_premium_1m/6m/1y. Сервер сам проверяет, что
    // friendUsername реально есть в списке друзей (в любую сторону).
    Q_INVOKABLE void giftPremium(const QString& friendUsername, const QString& packageId);

signals:
    void authFinished(bool success, const QString& message, const QString& username);
    void pingResult(bool online);
    void billingInfoReceived(int balance, int tokens, bool isPremium, const QString& premiumUntil,
        int invitesLeft, const QString& inviteCode,
        bool vpnActive, double vpnTrafficMb, const QString& email,
        const QJsonArray& purchaseHistory,
        const QString& premiumTier, double vpnLimitMb);
    void purchaseFinished(bool success, const QString& message);

    // used/limit — расход за текущий календарный месяц; tier — "free" или
    // один из package_id пакетов ("pack_research_10"/"20"/"30"); resetsAt —
    // дата следующего обнуления ("YYYY-MM-DD"), пусто если сервер не прислал.
    // Витрина storm://cloud использует это, чтобы показать "осталось X из Y"
    // и предложить пакет побольше, если пользователь уже близко к лимиту.
    void researchQuotaReceived(int used, int limit, const QString& tier, const QString& resetsAt);
    void betaFinished(bool success, const QString& message);
    void vpnStateChanged(bool connected, const QString& message);
    void cloudClearFinished(bool success, const QString& message);
    void syncFinished(bool success, const QString& message);
    void localStatsReceived(int historyCount, int passwordCount, int bookmarkCount);
    void notImplementedYet(const QString& feature);

    // addedFriends / referredFriends / addedMe — каждый элемент {"friend_username":.., "added_at"/"used_at":..}.
    // addedMe — обратный список: кто добавил ТЕКУЩЕГО пользователя себе в друзья.
    void friendsReceived(const QJsonArray& addedFriends, const QJsonArray& referredFriends, const QJsonArray& addedMe);
    void friendActionFinished(bool success, const QString& message);

    void setEmailFinished(bool success, const QString& message);
    void forgotPasswordFinished(bool success, const QString& message);

    // --- Хаб настроек ---
    // events — массив {"ip":.., "hwid":.., "ts":..}, до 20 последних входов.
    void loginHistoryReceived(const QJsonArray& events);
    void changePasswordFinished(bool success, const QString& message);
    void notificationPrefsReceived(bool notifyNewDevice, bool notifyPurchases, bool notifyPremiumExpiry);
    void notificationPrefsSaved(bool success, const QString& message);

    // --- Premium: подарок другу ---
    void giftPremiumFinished(bool success, const QString& message);

private:
    void applyDecryptedSettings(const QString& encryptedSettingsB64, const QString& password);

    // Записывает историю/пароли/закладки, пришедшие в ответе /sync/pull (login()),
    // обратно в локальное хранилище (DatabaseManager/PasswordManager). Раньше
    // pull только менял credentials в QSettings и настройки — сами данные из
    // ответа сервера нигде не сохранялись, из-за чего заход с нового устройства
    // ничего реально не восстанавливал.
    void applySyncedData(const QJsonObject& pullResponse, const QString& password);

    MainWindow* m_mainWindow;
    QNetworkAccessManager* m_net;
    QString m_serverUrl;
    QString m_currentUser;
    QString m_currentPassword;
    QSettings m_settings;
};