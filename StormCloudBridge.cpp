#include "StormCloudBridge.h"
#include "FernetCrypto.h"
#include "ProxyManager.h"
#include "MainWindow.h"
#include "PasswordManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QDesktopServices>
#include <QDateTime>
#include <QUrl>
#include <QNetworkInterface>
#include <QCryptographicHash>
#include <QSysInfo>


static QString computeHwid() {
    QString macAddr;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
        if (!iface.flags().testFlag(QNetworkInterface::IsUp)) continue;
        QString hw = iface.hardwareAddress(); // формат "AA:BB:CC:DD:EE:FF"
        if (!hw.isEmpty() && hw != "00:00:00:00:00:00") {
            macAddr = hw;
            break;
        }
    }
    if (macAddr.isEmpty()) {

        macAddr = "00:00:00:00:00:00";
    }

    QString hexOnly = macAddr;
    hexOnly.remove(':');
    bool ok = false;
    quint64 macInt = hexOnly.toULongLong(&ok, 16);
    QString macDecimalStr = QString::number(macInt);

    QByteArray hash = QCryptographicHash::hash(macDecimalStr.toUtf8(), QCryptographicHash::Md5);
    return QString::fromLatin1(hash.toHex());
}

// hwid — это md5-хэш MAC-адреса, годится для анти-фрода, но показывать ЕГО
// пользователю в разделе "Устройства" бессмысленно (просто набор символов —
// собственно то, на что и была жалоба). Отдельно собираем читаемую метку:
// имя компьютера + версия ОС, например "DESKTOP-AB12CD · Windows 11".
static QString computeDeviceLabel() {
    QString host = QSysInfo::machineHostName();
    QString os = QSysInfo::prettyProductName();
    if (host.isEmpty() && os.isEmpty()) return QString();
    if (host.isEmpty()) return os;
    if (os.isEmpty()) return host;
    return host + " · " + os;
}

StormCloudBridge::StormCloudBridge(MainWindow* mainWin, QObject* parent)
    : QObject(parent), m_mainWindow(mainWin), m_serverUrl("https://storm-browser.online:8000")
{
    m_net = new QNetworkAccessManager(this);
    m_currentUser = m_settings.value("sync/username", "").toString();
    m_currentPassword = m_settings.value("sync/password", "").toString();
}

void StormCloudBridge::applyDecryptedSettings(const QString& encryptedSettingsB64, const QString& password) {
    if (encryptedSettingsB64.isEmpty()) return;

    QByteArray key = FernetCrypto::deriveKey(password.toUtf8(), "storm_settings_salt");
    bool ok = false;
    QString json = FernetCrypto::decrypt(encryptedSettingsB64, key, ok);
    if (!ok) return;

    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return;
    QJsonObject obj = doc.object();

    if (obj.contains("avatar")) {
        m_settings.setValue("sync/avatar", obj["avatar"].toString());
    }
    if (obj.contains("ai_key")) {
        m_settings.setValue("ai/openrouter_key", obj["ai_key"].toString());
    }
}

void StormCloudBridge::applySyncedData(const QJsonObject& pullResponse, const QString& password) {
    if (!m_mainWindow) return;

    // --- Закладки: addBookmark() делает INSERT OR REPLACE по url — безопасно
    // вызывать многократно, дублей не будет.
    if (pullResponse.contains("bookmarks")) {
        for (const QJsonValue& v : pullResponse["bookmarks"].toArray()) {
            QJsonObject b = v.toObject();
            m_mainWindow->getDatabaseManager().addBookmark(
                b.value("title").toString(), b.value("url").toString());
        }
    }

    if (pullResponse.contains("history")) {
        for (const QJsonValue& v : pullResponse["history"].toArray()) {
            QJsonObject h = v.toObject();
            bool ok = false;
            qint64 ts = h.value("timestamp").toString().toLongLong(&ok);
            if (!ok) continue;
            m_mainWindow->getDatabaseManager().addHistoryItemAt(
                h.value("title").toString(), h.value("url").toString(), ts);
        }
    }


    PasswordManager* pm = m_mainWindow->getPasswordManager();
    if (pm && pullResponse.contains("passwords")) {
        QByteArray pwdKey = FernetCrypto::deriveKey(password.toUtf8(), "storm_passwords_salt");
        for (const QJsonValue& v : pullResponse["passwords"].toArray()) {
            QJsonObject p = v.toObject();
            bool ok = false;
            QString decrypted = FernetCrypto::decrypt(
                p.value("encrypted_password").toString(), pwdKey, ok);
            if (!ok) continue; // битая/чужая запись — пропускаем, не роняем весь синк
            pm->savePassword(p.value("site_url").toString(), p.value("login").toString(), decrypted);
        }
    }
}

void StormCloudBridge::login(const QString& username, const QString& password) {
    QJsonObject json;
    json["username"] = username;
    json["password"] = password;

    json["hwid"] = computeHwid();
    json["device_label"] = computeDeviceLabel();

    QNetworkRequest req(QUrl(m_serverUrl + "/sync/pull"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply, username, password]() {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());

        if (reply->error() == QNetworkReply::NoError) {
            m_currentUser = username;
            m_currentPassword = password;
            m_settings.setValue("sync/username", username);
            m_settings.setValue("sync/password", password);
            m_settings.setValue("profile/is_logged_in", true);

            if (doc.isObject() && doc.object().contains("settings")) {
                applyDecryptedSettings(doc.object()["settings"].toString(), password);
            }

            if (doc.isObject()) {
                applySyncedData(doc.object(), password);
            }
            emit authFinished(true, "", username);
            fetchBillingInfo(); // сразу обновляем закэшированный статус Premium после входа
        }
        else {
            QString err = u8"Ошибка соединения";
            if (doc.isObject() && doc.object().contains("detail"))
                err = doc.object()["detail"].toString();
            emit authFinished(false, err, "");
        }
        reply->deleteLater();
        });
}

void StormCloudBridge::registerAccount(const QString& username, const QString& password, const QString& inviteCode, const QString& email) {
    QJsonObject json;
    json["username"] = username;
    json["password"] = password;
    json["invite_code"] = inviteCode;
    json["hwid"] = computeHwid();
    json["email"] = email;

    QNetworkRequest req(QUrl(m_serverUrl + "/register"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply, username, password]() {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());

        if (reply->error() == QNetworkReply::NoError) {
            m_currentUser = username;
            m_currentPassword = password;
            m_settings.setValue("sync/username", username);
            m_settings.setValue("sync/password", password);
            m_settings.setValue("profile/is_logged_in", true);

            if (doc.isObject() && doc.object().contains("settings")) {
                applyDecryptedSettings(doc.object()["settings"].toString(), password);
            }
            emit authFinished(true, "", username);
            fetchBillingInfo(); // сразу обновляем закэшированный статус Premium после регистрации
        }
        else {
            QString err = u8"Ошибка соединения";
            if (doc.isObject() && doc.object().contains("detail"))
                err = doc.object()["detail"].toString();
            emit authFinished(false, err, "");
        }
        reply->deleteLater();
        });
}

void StormCloudBridge::logout() {
    m_settings.remove("sync/username");
    m_settings.remove("sync/password");
    m_settings.setValue("profile/is_logged_in", false);
    m_settings.remove("billing/is_premium"); // не оставляем закэшированный Premium от вышедшего пользователя
    m_settings.remove("billing/premium_until");
    m_currentUser.clear();
    m_currentPassword.clear();
}

void StormCloudBridge::checkPing() {
    QNetworkRequest req(QUrl(m_serverUrl + "/ping"));
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        bool online = (reply->error() == QNetworkReply::NoError);
        emit pingResult(online);
        reply->deleteLater();
        });
}

void StormCloudBridge::fetchBillingInfo() {
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/billing/info"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject data = doc.object();

            int balance = data.value("balance").toInt(0);
            int tokens = data.value("tokens").toInt(0);
            bool isPremium = data.value("is_premium").toInt(0) == 1;
            QString premiumUntil = data.value("premium_until").toString();

            // Кэшируем статус подписки локально: TodoWidget (лимит ссылок в
            // радаре вакансий — 1 для Free / 5 для Pro) и другие виджеты
            // читают этот флаг синхронно через QSettings, не дожидаясь
            // сетевого ответа на каждое действие. Обновляется здесь и сразу
            // после логина (см. login()/registerAccount() ниже). Новый
            // серверный эндпоинт не нужен — /api/billing/info уже отдаёт
            // is_premium, чего-то не хватало только на клиенте.
            m_settings.setValue("billing/is_premium", isPremium);
            m_settings.setValue("billing/premium_until", premiumUntil);

            int invitesLeft = data.value("invites_left").toInt(0);
            QString inviteCode = data.value("invite_code").toString();
            bool vpnActive = data.value("vpn_active").toInt(0) == 1;
            double vpnTrafficMb = data.value("vpn_traffic").toDouble(0) / (1024.0 * 1024.0);
            QString email = data.value("email").toString();
            // Список покупок УЖЕ отдавался сервером и раньше — просто нигде не
            // читался на клиенте. Теперь реально прокидываем его в UI (история
            // покупок в кошельке).
            QJsonArray purchaseHistory = data.value("history").toArray();
            // Тариф ('1m'/'6m'/'12m') и динамический лимит VPN (15ГБ на 6/12 мес,
            // иначе 5ГБ) — сервер уже считает лимит сам, тут просто конвертация в МБ.
            QString premiumTier = data.value("premium_tier").toString();
            double vpnLimitMb = data.value("vpn_limit_bytes").toDouble(5368709120.0) / (1024.0 * 1024.0);

            emit billingInfoReceived(balance, tokens, isPremium, premiumUntil,
                invitesLeft, inviteCode, vpnActive, vpnTrafficMb, email, purchaseHistory,
                premiumTier, vpnLimitMb);
        }
        reply->deleteLater();
        });
}

void StormCloudBridge::purchase(const QString& packageId) {
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;
    json["package_id"] = packageId;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/billing/buy"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() == QNetworkReply::NoError) {
            // Раньше здесь был один и тот же общий текст на любую покупку — теперь
            // берём конкретное сообщение сервера (что именно куплено/зачислено,
            // остаток SC), см. buy_tokens() в server.py.
            QString msg = u8"🎉 Покупка прошла успешно!";
            if (doc.isObject() && doc.object().contains("message"))
                msg = doc.object()["message"].toString();
            emit purchaseFinished(true, msg);
        }
        else {
            QString err = u8"Ошибка покупки";
            if (doc.isObject() && doc.object().contains("detail"))
                err = doc.object()["detail"].toString();
            emit purchaseFinished(false, err);
        }
        reply->deleteLater();
        });
}

// ---------------- DEEP RESEARCH REPORT: КВОТА ОТЧЁТОВ ----------------
// Тот же сервер (/api/research/quota), которым уже пользуется ResearchManager
// для собственной предварительной проверки перед стартом отчёта (см.
// ResearchManager::checkQuotaThenProceed) — здесь тот же эндпоинт вызывается
// отдельно, чтобы витрина storm://cloud могла показать текущий расход
// независимо от того, открывал ли пользователь панель "🔬" вообще.
void StormCloudBridge::fetchResearchQuota() {
    if (m_currentUser.isEmpty()) return; // не залогинен — нечего спрашивать

    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/research/quota"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
            int used = data.value("used").toInt(0);
            int limit = data.value("limit").toInt(4); // 4 — бесплатный лимит по умолчанию
            QString tier = data.value("tier").toString(u8"free");
            QString resetsAt = data.value("resets_at").toString();

            // Кэшируем как и billing/is_premium — так виджет "🔬" в сайдбаре
            // (или любой другой код) может прочитать последний известный
            // остаток синхронно из QSettings, не дожидаясь сети.
            m_settings.setValue("research/quota_used", used);
            m_settings.setValue("research/quota_limit", limit);
            m_settings.setValue("research/quota_tier", tier);

            emit researchQuotaReceived(used, limit, tier, resetsAt);
        }
        reply->deleteLater();
        });
}

void StormCloudBridge::requestBeta() {
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/beta/request"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() == QNetworkReply::NoError) {
            QString msg = doc.object().value("message").toString(u8"Успешно отправлено!");
            emit betaFinished(true, msg);
        }
        else {
            QString err = u8"Ошибка сети";
            if (doc.isObject() && doc.object().contains("detail"))
                err = doc.object()["detail"].toString();
            emit betaFinished(false, err);
        }
        reply->deleteLater();
        });
}

void StormCloudBridge::openVkCommunity() {
    // Раньше уходило в системный браузер по умолчанию через QDesktopServices —
    // жалоба была именно на это. Теперь открывается новой вкладкой внутри
    // самого Storm Browser через MainWindow::addNewTab (подтверждено по MainWindow.h).
    if (m_mainWindow) {
        m_mainWindow->addNewTab(QUrl("https://vk.com/club27550153"));
    }
    else {
        QDesktopServices::openUrl(QUrl("https://vk.com/club27550153"));
    }
}

void StormCloudBridge::saveAvatar(const QString& avatarData) {
    m_settings.setValue("sync/avatar", avatarData);
}

void StormCloudBridge::savePremiumTheme(const QString& bgChoice, const QString& frameChoice, const QString& avatarChoice) {
    m_settings.setValue("premium_ui_bg", bgChoice);
    m_settings.setValue("premium_ui_frame", frameChoice);
    m_settings.setValue("premium_ui_avatar", avatarChoice);
}

void StormCloudBridge::setAutoSync(bool enabled) {
    m_settings.setValue("sync/auto_enabled", enabled ? "true" : "false");
}

void StormCloudBridge::startVpn() {
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/vpn/get_key"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());

        if (reply->error() == QNetworkReply::NoError) {
            QString masterKey = doc.object().value("vpn_key").toString();
            if (masterKey.isEmpty()) {
                emit vpnStateChanged(false, u8"Сервер не вернул ключ VPN");
            }
            else {
                QString protocolName;
                auto result = ProxyManager::connectFromLink(masterKey, protocolName);
                if (result.first) {

                    m_settings.setValue("is_official_vpn", "true");

                    ProxyManager::startTrafficReporting(m_currentUser, m_currentPassword, m_serverUrl);
                }
                emit vpnStateChanged(result.first,
                    result.first ? QString(u8"✅ %1 подключен!").arg(protocolName) : result.second);
            }
        }
        else {
            QString err = u8"Доступ запрещён";
            if (doc.isObject() && doc.object().contains("detail"))
                err = doc.object()["detail"].toString();
            emit vpnStateChanged(false, err);
        }
        reply->deleteLater();
        });
}

void StormCloudBridge::stopVpn() {
    ProxyManager::disableProxy();
    m_settings.setValue("is_official_vpn", "false");
    emit vpnStateChanged(false, u8"VPN отключен пользователем");
}

void StormCloudBridge::clearCloudData() {
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;
    json["target"] = "all";

    QNetworkRequest req(QUrl(m_serverUrl + "/sync/clear"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        bool success = (reply->error() == QNetworkReply::NoError);
        emit cloudClearFinished(success,
            success ? u8"Данные в облаке очищены" : u8"Не удалось очистить облако (проверьте связь)");
        reply->deleteLater();
        });
}

void StormCloudBridge::fetchLocalStats() {
    int historyCount = m_mainWindow ? m_mainWindow->getDatabaseManager().getHistoryCount() : 0;
    int bookmarkCount = m_mainWindow ? m_mainWindow->getDatabaseManager().getAllBookmarks().size() : 0;
    int passwordCount = 0;
    if (m_mainWindow) {
        PasswordManager* pm = m_mainWindow->getPasswordManager();
        if (pm) passwordCount = pm->getPasswordCount();
    }
    emit localStatsReceived(historyCount, passwordCount, bookmarkCount);
}

// --- ДРУЗЬЯ ---
void StormCloudBridge::fetchFriends() {
    if (m_currentUser.isEmpty()) return;
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/friends/list"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject obj = doc.object();
            emit friendsReceived(obj.value("added_friends").toArray(),
                obj.value("referred_friends").toArray(),
                obj.value("added_me").toArray());
        }
        reply->deleteLater();
        });
}

void StormCloudBridge::addFriend(const QString& friendUsername) {
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;
    json["friend_username"] = friendUsername;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/friends/add"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        bool success = (reply->error() == QNetworkReply::NoError);
        QString msg = success ? u8"Друг добавлен" : u8"Не удалось добавить";
        if (!success && doc.isObject() && doc.object().contains("detail"))
            msg = doc.object()["detail"].toString();
        emit friendActionFinished(success, msg);
        if (success) fetchFriends();
        reply->deleteLater();
        });
}

void StormCloudBridge::removeFriend(const QString& friendUsername) {
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;
    json["friend_username"] = friendUsername;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/friends/remove"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        bool success = (reply->error() == QNetworkReply::NoError);
        emit friendActionFinished(success, success ? u8"Друг удалён" : u8"Не удалось удалить");
        if (success) fetchFriends();
        reply->deleteLater();
        });
}

void StormCloudBridge::requestManualSync() {
    if (!m_mainWindow) {
        emit syncFinished(false, u8"Внутренняя ошибка: нет доступа к данным браузера");
        return;
    }
    if (m_currentUser.isEmpty() || m_currentPassword.isEmpty()) {
        emit syncFinished(false, u8"Вы не авторизованы");
        return;
    }

    // 1. История
    QJsonArray historyData = m_mainWindow->getDatabaseManager().getAllHistory();

    // 1б. Закладки
    QJsonArray bookmarksData = m_mainWindow->getDatabaseManager().getAllBookmarks();

    // 2. Пароли (расшифрованные локально нашим мастер-паролем хранилища)
    PasswordManager* pm = m_mainWindow->getPasswordManager();
    QJsonArray rawPasswords = pm ? pm->getAllDecrypted() : QJsonArray();

    // 3. Настройки для облака (аватар + ключ ИИ)
    QJsonObject userSettingsObj;
    userSettingsObj["avatar"] = m_settings.value("sync/avatar", u8"👤").toString();
    userSettingsObj["ai_key"] = m_settings.value("ai/openrouter_key", "").toString();
    QString settingsJson = QString::fromUtf8(
        QJsonDocument(userSettingsObj).toJson(QJsonDocument::Compact));

    QByteArray secret = m_currentPassword.toUtf8();
    QString timestamp = QString::number(QDateTime::currentSecsSinceEpoch());

    // 4. HMAC-подпись метаданных. ВАЖНО: строка ниже собрана вручную, а НЕ через
    // QJsonDocument::toJson(), т.к. сервер (Python) считает подпись над строкой,
    // полученной через json.dumps(payload, sort_keys=True) — формат с пробелами
    // после ":" и ",". QJsonDocument::Compact пробелы не добавляет, из-за чего
    // подписи не совпадут побайтово, и сервер отклонит синхронизацию.
    // Ключи строго в алфавитном порядке (как даёт python json.dumps(sort_keys=True)
    // на сервере) — bookmarks_count добавлен сюда же, чтобы количество закладок
    // тоже покрывалось подписью и не могло быть подменено по дороге.
    QString signData = QString(
        "{\"bookmarks_count\": %1, \"history_count\": %2, \"passwords_count\": %3, \"ts\": \"%4\", \"username\": \"%5\"}")
        .arg(bookmarksData.size())
        .arg(historyData.size())
        .arg(rawPasswords.size())
        .arg(timestamp, m_currentUser);
    QString signature = FernetCrypto::hmacSha256Hex(secret, signData.toUtf8());

    // 5. Шифруем пакет настроек (тот же ключ/соль, что при логине)
    QByteArray settingsKey = FernetCrypto::deriveKey(secret, "storm_settings_salt");
    QString encryptedSettings = FernetCrypto::encrypt(settingsJson, settingsKey);

    // 6. Пароли шифруются ОДНИМ фиксированным ключом на пользователя —
    // PBKDF2(secret, "storm_passwords_salt"), точно тем же паттерном, что уже
    // используется для settings (см. applyDecryptedSettings/п.5 выше). Раньше
    // соль была замешана на ТЕКУЩЕМ timestamp пуша — сервер его нигде не хранил,
    // из-за чего расшифровать пароли обратно при pull было нечем в принципе.
    // Fernet-токен уже даёт случайный IV на каждый вызов encrypt(), так что
    // повторное использование одного и того же ключа для разных паролей не
    // ослабляет шифрование — ровно как переиспользование storm_settings_salt.
    QByteArray pwdKey = FernetCrypto::deriveKey(secret, "storm_passwords_salt");
    QJsonArray encryptedPasswords;
    for (const QJsonValue& v : rawPasswords) {
        QJsonObject p = v.toObject();
        QString encPwd = FernetCrypto::encrypt(p.value("password").toString(), pwdKey);

        QJsonObject entry;
        entry["site_url"] = p.value("site_url").toString();
        entry["login"] = p.value("login").toString();
        entry["encrypted_password"] = encPwd;
        encryptedPasswords.append(entry);
    }

    // 7. Итоговый payload на /sync/push
    QJsonObject payload;
    payload["username"] = m_currentUser;
    payload["password"] = m_currentPassword;
    payload["history"] = historyData;
    payload["passwords"] = encryptedPasswords;
    payload["bookmarks"] = bookmarksData;
    payload["settings"] = encryptedSettings;
    payload["ts"] = timestamp;
    payload["signature"] = signature;

    QNetworkRequest req(QUrl(m_serverUrl + "/sync/push"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(payload).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QString nowStr = QDateTime::currentDateTime().toString("dd.MM HH:mm");
            m_settings.setValue("sync/last_time", nowStr);
            emit syncFinished(true, nowStr);
        }
        else {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QString err = u8"Ошибка сервера при синхронизации";
            if (doc.isObject() && doc.object().contains("detail"))
                err = doc.object()["detail"].toString();
            emit syncFinished(false, err);
        }
        reply->deleteLater();
        });
}

// --- EMAIL / ВОССТАНОВЛЕНИЕ ПАРОЛЯ ---
void StormCloudBridge::setEmail(const QString& email) {
    if (m_currentUser.isEmpty()) return;
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;
    json["email"] = email;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/account/set_email"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        bool success = (reply->error() == QNetworkReply::NoError);
        QString msg = success ? u8"Email сохранён" : u8"Не удалось сохранить email";
        if (!success && doc.isObject() && doc.object().contains("detail"))
            msg = doc.object()["detail"].toString();
        emit setEmailFinished(success, msg);
        reply->deleteLater();
        });
}

void StormCloudBridge::forgotPassword(const QString& loginOrEmail) {
    // Вызывается с экрана входа, ДО login() — m_currentUser/m_currentPassword
    // ещё не установлены, поэтому шлём то, что ввели в самом поле.
    QJsonObject json;
    json["login_or_email"] = loginOrEmail;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/account/forgot_password"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        bool success = (reply->error() == QNetworkReply::NoError);
        QString msg = success && doc.isObject() && doc.object().contains("message")
            ? doc.object()["message"].toString()
            : (success ? u8"Если такой аккаунт существует, письмо отправлено" : u8"Ошибка соединения");
        emit forgotPasswordFinished(success, msg);
        reply->deleteLater();
        });
}

// ---------------- ХАБ НАСТРОЕК ----------------

void StormCloudBridge::fetchLoginHistory() {
    if (m_currentUser.isEmpty()) return;
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/account/login_history"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            emit loginHistoryReceived(doc.object().value("events").toArray());
        }
        reply->deleteLater();
        });
}

void StormCloudBridge::changePassword(const QString& oldPassword, const QString& newPassword) {
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = oldPassword;
    json["new_password"] = newPassword;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/account/change_password"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply, newPassword]() {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        bool success = (reply->error() == QNetworkReply::NoError);
        QString msg = success ? u8"Пароль изменён" : u8"Не удалось сменить пароль";
        if (doc.isObject() && doc.object().contains("message"))
            msg = doc.object()["message"].toString();
        else if (!success && doc.isObject() && doc.object().contains("detail"))
            msg = doc.object()["detail"].toString();

        if (success) {
            // Сервер уже принял новый пароль и очистил облачные passwords/settings
            // (они шифровались ключом от старого пароля) — локальные данные не
            // трогаем, они сами уйдут в облако при следующей синхронизации.
            m_currentPassword = newPassword;
            m_settings.setValue("sync/password", newPassword);
        }
        emit changePasswordFinished(success, msg);
        reply->deleteLater();
        });
}

void StormCloudBridge::fetchNotificationPrefs() {
    if (m_currentUser.isEmpty()) return;
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/account/notification_prefs"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
            emit notificationPrefsReceived(
                data.value("notify_new_device").toBool(true),
                data.value("notify_purchases").toBool(true),
                data.value("notify_premium_expiry").toBool(true));
        }
        reply->deleteLater();
        });
}

void StormCloudBridge::saveNotificationPrefs(bool notifyNewDevice, bool notifyPurchases, bool notifyPremiumExpiry) {
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;
    json["notify_new_device"] = notifyNewDevice;
    json["notify_purchases"] = notifyPurchases;
    json["notify_premium_expiry"] = notifyPremiumExpiry;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/account/save_notification_prefs"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        bool success = (reply->error() == QNetworkReply::NoError);
        QString msg = success ? u8"Настройки уведомлений сохранены" : u8"Не удалось сохранить настройки";
        if (doc.isObject() && doc.object().contains("message"))
            msg = doc.object()["message"].toString();
        else if (!success && doc.isObject() && doc.object().contains("detail"))
            msg = doc.object()["detail"].toString();
        emit notificationPrefsSaved(success, msg);
        reply->deleteLater();
        });
}

// ---------------- PREMIUM: ПОДАРОК ДРУГУ ----------------

void StormCloudBridge::giftPremium(const QString& friendUsername, const QString& packageId) {
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;
    json["friend_username"] = friendUsername;
    json["package_id"] = packageId;

    QNetworkRequest req(QUrl(m_serverUrl + "/api/billing/gift_premium"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = m_net->post(req, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        bool success = (reply->error() == QNetworkReply::NoError);
        QString msg = success ? u8"🎁 Premium подарен!" : u8"Не удалось подарить Premium";
        if (doc.isObject() && doc.object().contains("message"))
            msg = doc.object()["message"].toString();
        else if (!success && doc.isObject() && doc.object().contains("detail"))
            msg = doc.object()["detail"].toString();
        emit giftPremiumFinished(success, msg);
        reply->deleteLater();
        });
}