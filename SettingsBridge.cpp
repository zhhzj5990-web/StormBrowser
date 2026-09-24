#include "SettingsBridge.h"
#include "MainWindow.h"
#include "ProxyManager.h"
#include "GameModeManager.h"
#include "ShieldInterceptor.h"
#include "DownloadManager.h"
#include "UpdateManager.h"
#include "CertificateManager.h"
#include "BrowserWebView.h"
#include "PasswordManager.h"
#include "Logger.h"
#include <QSettings>
#include <QMessageBox>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QFileDialog>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUuid>
#include <QProcess>
#include <QStringList>
#include <QCryptographicHash>
#include <memory>

// --- ОДНОРАЗОВАЯ МИГРАЦИЯ СТАРОГО КЛЮЧА ИИ ---
// Раньше ключ ИИ сохранялся под разными именами QSettings в разных частях
// приложения (баг рассинхрона ai/api_key vs ai/openrouter_key). У пользователей,
// уже успевших ввести ключ на багованной версии, он может лежать под старым
// именем "ai/api_key". Переносим его один раз на правильное имя и удаляем
// старое, чтобы никто не терял уже сохранённый ключ при обновлении.
// Идемпотентно: после первого запуска ключ "ai/api_key" больше не существует,
// повторные вызовы ничего не делают.
static void migrateLegacyAiKey() {
    QSettings s;
    if (!s.contains("ai/api_key")) return;

    QString legacyKey = s.value("ai/api_key", "").toString();
    if (!legacyKey.isEmpty() && s.value("ai/openrouter_key", "").toString().isEmpty()) {
        s.setValue("ai/openrouter_key", legacyKey);
    }
    s.remove("ai/api_key");
}

SettingsBridge::SettingsBridge(MainWindow* mainWin, QObject* parent)
    : QObject(parent), mw(mainWin)
{
    migrateLegacyAiKey();
}

void SettingsBridge::setLanguage(const QString& langCode) {
    mw->setUiLanguage(langCode);
}

void SettingsBridge::setSearchEngine(const QString& engineName) {
    mw->setSearchEngine(engineName);
}

void SettingsBridge::showStartupSettings() {
    mw->showStartupSettings();
}

void SettingsBridge::setNewTabBackground() {
    mw->setNewTabBackground();
}

void SettingsBridge::toggleBookmarksBar(bool checked) {
    Q_UNUSED(checked);
    mw->toggleBookmarksBar();
}

void SettingsBridge::clearBrowserData() {
    mw->clearBrowserData();
}

void SettingsBridge::printCurrentPage() {
    mw->printCurrentPage();
}

void SettingsBridge::setAsDefaultBrowser() {
    mw->setAsDefaultBrowser();
}

void SettingsBridge::toggleMinimizeToTray(bool enabled) {
    QSettings s;
    s.setValue("browser/minimize_to_tray", enabled);
}

void SettingsBridge::applyTheme(const QString& themeId) {
    mw->applyTheme(themeId);
}

void SettingsBridge::zoomIn() {
    mw->zoomIn();
}

void SettingsBridge::zoomOut() {
    mw->zoomOut();
}

void SettingsBridge::toggleFullScreen() {
    mw->toggleFullScreen();
}

void SettingsBridge::toggleReaderMode() {
    mw->toggleReaderMode();
}

QString SettingsBridge::getCurrentZoom() {
    return mw->getCurrentZoomString();
}

void SettingsBridge::toggleSidebarVisible(bool enabled) {
    if (mw->isSidebarVisible() != enabled) {
        mw->toggleSidebar();
    }
}

void SettingsBridge::setSidebarPosition(const QString& position) {
    mw->setSidebarPosition(position);
}

// ==========================================
// --- PROXY / VPN ---
// Раньше это открывало отдельное окно ProxyDialog. Теперь Proxy/VPN — часть
// страницы настроек (см. SettingsPageHtml.cpp, раздел "🌐 Proxy / VPN"),
// а вместо кнопок "Подключить"/"Применить" там тумблеры — вся логика,
// которая раньше жила в слотах ProxyDialog, перенесена сюда один в один,
// только результат теперь уходит в JS не напрямую (через statusLabel),
// а сигналами, на которые страница настроек подписывается.
// ==========================================

QString SettingsBridge::getProxyStatusJson() {
    bool connected = ProxyManager::isConnected();
    QJsonObject obj;
    obj["connected"] = connected;
    obj["statusText"] = connected
        ? QSettings().value("proxy/last_status_text", u8"Статус: Подключено").toString()
        : QString(u8"Статус: Отключено");
    obj["smartLink"] = QSettings().value("proxy/smart_link", "").toString();
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void SettingsBridge::connectSmartLink(const QString& rawLink) {
    QString link = rawLink.trimmed();
    if (link.isEmpty()) {
        emit proxyConnectResult(false, u8"Вставьте ссылку-ключ перед подключением", "");
        return;
    }

    QString protocolName;
    auto [success, msg] = ProxyManager::connectFromLink(link, protocolName);
    if (success) {
        QSettings().setValue("proxy/smart_link", link);
        QSettings().setValue("is_official_vpn", "false");
    }
    emit proxyConnectResult(success, msg, protocolName);
}

void SettingsBridge::deleteSmartLink() {
    QSettings().remove("proxy/smart_link");
}

void SettingsBridge::applyManualProxy(const QString& type, const QString& host, int port, const QString& user, const QString& pass) {
    bool ok = false;
    QString statusText;
    if (!host.isEmpty() && port > 0) {
        ok = ProxyManager::applyProxy(type, host, static_cast<quint16>(port), user, pass);
        if (ok) {
            statusText = QString(u8"Статус: %1:%2").arg(host).arg(port);
            QSettings().setValue("proxy/last_status_text", statusText);
            QSettings().setValue("is_official_vpn", "false");
        }
    }
    emit proxyManualApplyResult(ok, statusText);
}

void SettingsBridge::applyListProxy(const QString& type, const QString& hostPort) {
    QStringList parts = hostPort.split(":");
    bool ok = false;
    QString statusText;
    if (parts.size() == 2) {
        quint16 port = static_cast<quint16>(parts[1].toUInt());
        ok = ProxyManager::applyProxy(type, parts[0], port);
        if (ok) {
            statusText = u8"Статус: " + parts[0];
            QSettings().setValue("proxy/last_status_text", statusText);
            QSettings().setValue("is_official_vpn", "false");
        }
    }
    emit proxyManualApplyResult(ok, statusText);
}

void SettingsBridge::disableProxyAll() {
    ProxyManager::disableProxy();
    QSettings().setValue("is_official_vpn", "false");
    QSettings().remove("proxy/last_status_text");
}

void SettingsBridge::fetchFreeProxies(const QString& type) {
    // Тот же источник списков, что и раньше использовал ProxyDialog::startFetchProxies.
    QString url = (type.toUpper() == "SOCKS5")
        ? "https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/socks5.txt"
        : "https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/http.txt";

    // Владелец — mw, а не this, по той же причине, что и в testAiConnection():
    // страница настроек может закрыться раньше ответа сервера.
    auto* manager = new QNetworkAccessManager(mw);
    QNetworkReply* reply = manager->get(QNetworkRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
        QJsonArray arr;
        if (reply->error() == QNetworkReply::NoError) {
            QString text = QString::fromUtf8(reply->readAll());
            QStringList lines = text.split("\n", Qt::SkipEmptyParts);
            int count = 0;
            for (const QString& line : lines) {
                QString trimmed = line.trimmed();
                if (trimmed.contains(":")) {
                    arr.append(trimmed);
                    if (++count >= 100) break; // Берём первые 100
                }
            }
        }
        emit freeProxiesFetched(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        reply->deleteLater();
        manager->deleteLater();
        });
}

void SettingsBridge::openWizardVpnRef() {
    QDesktopServices::openUrl(QUrl("http://wizardvpn.co/ref/75362"));
}

void SettingsBridge::toggleShield(bool enabled) {
    auto* sh = mw->findChild<ShieldInterceptor*>("ShieldInterceptor");
    if (sh) sh->setEnabled(enabled);

    // БАГ-ФИКС: раньше состояние нигде не сохранялось — после перезапуска
    // Storm Shield всегда включался заново, независимо от выбора пользователя.
    QSettings s;
    s.setValue("shield/enabled", enabled);

    // БАГ-ФИКС: индикатор в статус-баре раньше не обновлялся при переключении
    // из настроек — всегда показывал "Активен" независимо от реального состояния.
    mw->updateShieldStatusIndicator(enabled);
}

void SettingsBridge::toggleGameMode(bool enabled) {
    QSettings s;
    s.setValue("browser/game_mode", enabled);
    GameModeManager::toggleGameMode(mw, enabled);
}

void SettingsBridge::toggleHwAccel(bool enabled) {
    QSettings s;
    s.setValue("browser/hw_accel", enabled);
    QMessageBox::information(mw, u8"Требуется перезапуск",
        u8"Настройка сохранена. Перезапустите Storm Browser для применения изменений.");
}

QString SettingsBridge::getShieldExceptionsJson() {
    auto* sh = mw->findChild<ShieldInterceptor*>("ShieldInterceptor");
    QJsonArray arr;
    if (sh) {
        for (const QString& host : sh->exceptions()) arr.append(host);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void SettingsBridge::addShieldException(const QString& host) {
    // Пользователь мог вставить ссылку целиком ("https://example.com/page") вместо
    // голого домена — вытаскиваем host через QUrl в обоих случаях.
    QString raw = host.trimmed();
    if (raw.isEmpty()) return;
    QString h = QUrl(raw.contains("://") ? raw : ("http://" + raw)).host().toLower();
    if (h.isEmpty()) return;

    auto* sh = mw->findChild<ShieldInterceptor*>("ShieldInterceptor");
    if (sh) sh->addException(h);

    QSettings s;
    QStringList list = s.value("shield/exceptions").toStringList();
    if (!list.contains(h, Qt::CaseInsensitive)) {
        list.append(h);
        s.setValue("shield/exceptions", list);
    }
}

void SettingsBridge::removeShieldException(const QString& host) {
    QString h = host.trimmed().toLower();
    if (h.isEmpty()) return;

    auto* sh = mw->findChild<ShieldInterceptor*>("ShieldInterceptor");
    if (sh) sh->removeException(h);

    QSettings s;
    QStringList list = s.value("shield/exceptions").toStringList();
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list.at(i).compare(h, Qt::CaseInsensitive) == 0) list.removeAt(i);
    }
    s.setValue("shield/exceptions", list);
}

void SettingsBridge::toggleHttpsOnly(bool enabled) {
    QSettings().setValue("browser/https_only", enabled);
    QMessageBox::information(mw, u8"Требуется перезапуск",
        u8"Настройка сохранена. Перезапустите Storm Browser для применения изменений.");
}

void SettingsBridge::toggleClearSiteDataOnClose(bool enabled) {
    QSettings().setValue("browser/clear_site_data_on_close", enabled);
}

QString SettingsBridge::getSiteDataExceptionsJson() {
    QJsonArray arr;
    for (const QString& host : QSettings().value("browser/site_data_exceptions").toStringList()) {
        arr.append(host);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void SettingsBridge::addSiteDataException(const QString& host) {
    // То же извлечение домена из произвольной ссылки, что и в addShieldException().
    QString raw = host.trimmed();
    if (raw.isEmpty()) return;
    QString h = QUrl(raw.contains("://") ? raw : ("http://" + raw)).host().toLower();
    if (h.isEmpty()) return;

    QSettings s;
    QStringList list = s.value("browser/site_data_exceptions").toStringList();
    if (!list.contains(h, Qt::CaseInsensitive)) {
        list.append(h);
        s.setValue("browser/site_data_exceptions", list);
    }
}

void SettingsBridge::removeSiteDataException(const QString& host) {
    QString h = host.trimmed().toLower();
    if (h.isEmpty()) return;

    QSettings s;
    QStringList list = s.value("browser/site_data_exceptions").toStringList();
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list.at(i).compare(h, Qt::CaseInsensitive) == 0) list.removeAt(i);
    }
    s.setValue("browser/site_data_exceptions", list);
}

QString SettingsBridge::getSitePermissionsJson() {
    QSettings s;
    QJsonArray arr;
    s.beginGroup("permissions");
    for (const QString& featureGroup : s.childGroups()) {
        s.beginGroup(featureGroup);
        int featureId = featureGroup.toInt();
        for (const QString& host : s.childKeys()) {
            QJsonObject obj;
            obj["featureId"] = featureId;
            obj["feature"] = BrowserWebView::featureDisplayName(static_cast<QWebEnginePage::Feature>(featureId));
            obj["host"] = host;
            obj["allowed"] = s.value(host).toBool();
            arr.append(obj);
        }
        s.endGroup();
    }
    s.endGroup();
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void SettingsBridge::revokeSitePermission(int featureId, const QString& host) {
    QSettings().remove(QString("permissions/%1/%2").arg(featureId).arg(host));
}

QString SettingsBridge::getCertificatesJson() {
    return QString::fromUtf8(QJsonDocument(CertificateManager::manageableCertificatesJson()).toJson(QJsonDocument::Compact));
}

int SettingsBridge::getSystemCertCount() {
    return CertificateManager::systemCertificateCount();
}

void SettingsBridge::toggleUseSystemCerts(bool enabled) {
    CertificateManager::setUseSystemStoreEnabled(enabled);
}

void SettingsBridge::openSystemCertStore() {
#ifdef Q_OS_WIN
    // certmgr.msc — не самостоятельный .exe, поэтому запускается через mmc,
    // который сам найдёт оснастку в System32 по имени.
    QProcess::startDetached("mmc.exe", QStringList() << "certmgr.msc");
#else
    QMessageBox::information(mw, u8"Недоступно",
        u8"Управление системным хранилищем сертификатов доступно только на Windows.");
#endif
}

QString SettingsBridge::importCertificateFile() {
    QString path = QFileDialog::getOpenFileName(mw, u8"Установить сертификат",
        QString(), u8"Сертификаты (*.cer *.crt *.pem);;Все файлы (*.*)");
    if (path.isEmpty()) {
        return QString(); // пользователь отменил выбор — не ошибка
    }
    return CertificateManager::importCertificateFromFile(path);
}

void SettingsBridge::removeCertificate(const QString& id) {
    CertificateManager::removeCustomCertificate(id);
}

void SettingsBridge::showPasswordManager() {
    mw->showPasswordManager();
}

void SettingsBridge::changeMasterPassword() {
    mw->changeMasterPassword();
}

void SettingsBridge::resetPasswordVault() {
    mw->resetPasswordVault();
}

void SettingsBridge::importPasswords() {
    mw->importPasswords();
}

QString SettingsBridge::getSavedAddressesJson() {
    QJsonDocument doc = QJsonDocument::fromJson(QSettings().value("autofill/addresses").toByteArray());
    QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray();
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void SettingsBridge::addSavedAddress(const QString& fullName, const QString& phone, const QString& email,
    const QString& addressLine, const QString& city, const QString& zip) {
    // Хотя бы одно поле должно быть заполнено, иначе в списке появится
    // видимая пустая строка без возможности понять, что это за запись.
    if (fullName.trimmed().isEmpty() && phone.trimmed().isEmpty() && email.trimmed().isEmpty()
        && addressLine.trimmed().isEmpty() && city.trimmed().isEmpty() && zip.trimmed().isEmpty()) {
        return;
    }

    QSettings s;
    QJsonDocument doc = QJsonDocument::fromJson(s.value("autofill/addresses").toByteArray());
    QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray();

    QJsonObject obj;
    obj["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    obj["fullName"] = fullName.trimmed();
    obj["phone"] = phone.trimmed();
    obj["email"] = email.trimmed();
    obj["addressLine"] = addressLine.trimmed();
    obj["city"] = city.trimmed();
    obj["zip"] = zip.trimmed();
    arr.append(obj);

    s.setValue("autofill/addresses", QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void SettingsBridge::removeSavedAddress(const QString& id) {
    if (id.trimmed().isEmpty()) return;

    QSettings s;
    QJsonDocument doc = QJsonDocument::fromJson(s.value("autofill/addresses").toByteArray());
    QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray();

    QJsonArray filtered;
    for (const QJsonValue& v : arr) {
        if (v.toObject().value("id").toString() != id) filtered.append(v);
    }
    s.setValue("autofill/addresses", QJsonDocument(filtered).toJson(QJsonDocument::Compact));
}

void SettingsBridge::toggleOfferSaveAddress(bool enabled) {
    QSettings().setValue("autofill/offer_save_address", enabled);
}

void SettingsBridge::checkPasswordsForBreaches() {
    QJsonArray passwords = mw->getPasswordManager()->getAllDecrypted();

    if (passwords.isEmpty()) {
        // Пусто может значить и "паролей правда нет", и "хранилище заперто"
        // (getAllDecrypted() молча возвращает пусто, если мастер-пароль ещё
        // не вводили в этой сессии) — отличаем по счётчику записей.
        bool locked = mw->getPasswordManager()->getPasswordCount() > 0;
        emit passwordBreachCheckResult(QString::fromUtf8(QJsonDocument(QJsonArray()).toJson(QJsonDocument::Compact)), locked);
        return;
    }

    // shared_ptr — чтобы пережить каждую отдельную асинхронную лямбду ниже
    // и корректно посчитать, когда завершились ВСЕ запросы (по одному на
    // пароль, могут прийти в любом порядке).
    auto results = std::make_shared<QJsonArray>();
    auto remaining = std::make_shared<int>(passwords.size());
    auto* manager = new QNetworkAccessManager(mw);

    for (const QJsonValue& v : passwords) {
        QJsonObject entry = v.toObject();
        QString password = entry.value("password").toString();

        // k-anonymity: отправляем только первые 5 символов SHA-1 хэша пароля —
        // ни сам пароль, ни его полный хэш никуда не уходят.
        QByteArray sha1 = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha1).toHex().toUpper();
        QString prefix = QString::fromLatin1(sha1.left(5));
        QString suffix = QString::fromLatin1(sha1.mid(5));

        QNetworkReply* reply = manager->get(QNetworkRequest(QUrl("https://api.pwnedpasswords.com/range/" + prefix)));
        connect(reply, &QNetworkReply::finished, this, [this, reply, suffix, entry, results, remaining, manager]() {
            if (reply->error() == QNetworkReply::NoError) {
                QString body = QString::fromUtf8(reply->readAll());
                const QStringList lines = body.split("\r\n", Qt::SkipEmptyParts);
                for (const QString& line : lines) {
                    QStringList parts = line.split(':');
                    if (parts.size() == 2 && parts[0].compare(suffix, Qt::CaseInsensitive) == 0) {
                        QJsonObject breach;
                        breach["site"] = entry.value("site_url").toString();
                        breach["login"] = entry.value("login").toString();
                        breach["count"] = parts[1].toInt();
                        results->append(breach);
                        break;
                    }
                }
            }
            reply->deleteLater();

            if (--(*remaining) <= 0) {
                emit passwordBreachCheckResult(QString::fromUtf8(QJsonDocument(*results).toJson(QJsonDocument::Compact)), false);
                manager->deleteLater();
            }
            });
    }
}

QString SettingsBridge::chooseDownloadFolder() {
    QString dir = QFileDialog::getExistingDirectory(mw, u8"Папка для загрузок по умолчанию",
        DownloadManager::lastDownloadDir());
    if (!dir.isEmpty()) {
        DownloadManager::setLastDownloadDir(dir);
    }
    return dir; // пустая строка — пользователь отменил выбор, JS ничего не меняет
}

void SettingsBridge::toggleDownloadAskEachTime(bool enabled) {
    QSettings s;
    s.setValue("browser/download_ask_each_time", enabled);
}

void SettingsBridge::openStormCloud() {
    mw->openProfile();
}

QString SettingsBridge::getSettingsSnapshotJson() {
    QSettings s;
    QJsonObject obj;
    obj["bookmarksBar"] = s.value("browser/show_bookmarks_bar", false).toBool();
    obj["shield"] = s.value("shield/enabled", true).toBool();
    obj["gameMode"] = s.value("browser/game_mode", false).toBool();
    obj["hwAccel"] = s.value("browser/hw_accel", true).toBool();
    obj["downloadAskEachTime"] = s.value("browser/download_ask_each_time", true).toBool();
    obj["downloadDir"] = DownloadManager::lastDownloadDir();
    // Те же ключи, что пишет StormCloudBridge::login()/logout() — отдельный
    // StormCloudBridge здесь не создаём, только читаем состояние.
    obj["cloudLoggedIn"] = s.value("profile/is_logged_in", false).toBool();
    obj["cloudUsername"] = s.value("sync/username", "").toString();
    obj["minimizeToTray"] = s.value("browser/minimize_to_tray", false).toBool();
    obj["useSystemCerts"] = CertificateManager::useSystemStoreEnabled();
    obj["proxyConnected"] = ProxyManager::isConnected();
    obj["sidebarVisible"] = mw->isSidebarVisible();
    obj["sidebarPosition"] = s.value("browser/sidebar_position", "left").toString();
    obj["httpsOnly"] = s.value("browser/https_only", false).toBool();
    obj["clearSiteDataOnClose"] = s.value("browser/clear_site_data_on_close", false).toBool();
    obj["offerSaveAddress"] = s.value("autofill/offer_save_address", true).toBool();
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void SettingsBridge::saveAI(const QString& mode, const QString& apiKey, const QString& gigaKey, const QString& gigaModel,
    const QString& visionModel, const QString& gigaVisionModel,
    const QString& imageKey, const QString& videoKey, const QString& tavilyKey) {
    QSettings s;
    s.setValue("ai/mode", mode.toInt());
    s.setValue("ai/openrouter_key", apiKey);

    if (!gigaKey.isEmpty() || !gigaModel.isEmpty()) {
        s.setValue("ai/gigachat_key", gigaKey);
        s.setValue("ai/gigachat_model", gigaModel.isEmpty() ? "GigaChat-2" : gigaModel);
    }
    if (!visionModel.isEmpty()) {
        s.setValue("ai/vision_model", visionModel);
    }
    if (!gigaVisionModel.isEmpty()) {
        s.setValue("ai/gigachat_vision_model", gigaVisionModel);
    }

    // Новое:
    s.setValue("ai/image_key", imageKey);
    s.setValue("ai/video_key", videoKey);

    // Как и ai/openrouter_key — сохраняем безусловно (не под "если не пусто"),
    // чтобы очистка поля реально отключала Tavily и откатывала модуль
    // исследования обратно на бесплатный DuckDuckGo, а не оставляла старый
    // ключ висеть в QSettings незаметно для пользователя.
    s.setValue("research/tavily_key", tavilyKey);
}

void SettingsBridge::resetGigaTokenCounter() {
    QSettings s;
    s.setValue("ai/gigachat_tokens_used", 0);
}

void SettingsBridge::testAiConnection(const QString& backend, const QString& key) {
    QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty()) {
        emit aiConnectionTested(backend, false, u8"Ключ не введён");
        return;
    }

    // Владелец — mw, а не this: SettingsBridge живёт ровно пока открыта вкладка
    // storm://settings, а проверка не должна оборваться, если пользователь успеет
    // закрыть вкладку до ответа сервера.
    auto* manager = new QNetworkAccessManager(mw);

    if (backend == "gigachat") {
        // Тот же обмен ключа на access_token, что и перед обычным AI-чатом (см.
        // AIAssistantWidget::ensureGigaChatToken) — если он проходит, ключ рабочий.
        // Реального чат-запроса не делаем, так что это ничего не стоит пользователю.
        QNetworkRequest req(QUrl("https://ngw.devices.sberbank.ru:9443/api/v2/oauth"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        req.setRawHeader("Accept", "application/json");
        req.setRawHeader("RqUID", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
        req.setRawHeader("Authorization", ("Basic " + trimmedKey).toUtf8());
        // Раньше здесь стоял QSslSocket::VerifyNone (полное отключение проверки
        // сертификата) — сервер GigaChat использует TLS-сертификат, выпущенный
        // НУЦ Минцифры, которому Qt/ОС не доверяют "из коробки". Теперь, когда
        // у CertificateManager есть встроенные корневые сертификаты Минцифры,
        // используем нормальную проверку с расширенным списком доверенных CA
        // вместо полного отключения — запрос по-прежнему отклонит настоящий
        // MITM-сертификат, а не примет вообще любой.
        req.setSslConfiguration(CertificateManager::trustedSslConfiguration());

        QNetworkReply* reply = manager->post(req, QByteArray("scope=GIGACHAT_API_PERS"));
        connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
            bool ok = (reply->error() == QNetworkReply::NoError);
            QString msg;
            if (ok) {
                QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
                ok = obj.contains("access_token") && !obj.value("access_token").toString().isEmpty();
                msg = ok ? u8"Ключ действителен" : u8"Сервер GigaChat не вернул токен доступа";
            }
            else {
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                msg = (status == 401 || status == 403)
                    ? u8"Ключ отклонён (неверный или истёк)"
                    : (u8"Ошибка сети: " + reply->errorString());
            }
            emit aiConnectionTested("gigachat", ok, msg);
            reply->deleteLater();
            manager->deleteLater();
            });
    }
    else if (backend == "openrouter") {
        // Эндпоинт информации о ключе — не тратит токены на реальный чат-запрос,
        // только проверяет валидность авторизации.
        QNetworkRequest req(QUrl("https://openrouter.ai/api/v1/key"));
        req.setRawHeader("Authorization", ("Bearer " + trimmedKey).toUtf8());

        QNetworkReply* reply = manager->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            bool ok = (reply->error() == QNetworkReply::NoError) && (status == 200);
            QString msg = ok ? u8"Ключ действителен"
                : (status == 401 ? u8"Ключ отклонён (неверный или истёк)"
                    : (u8"Ошибка сети: " + reply->errorString()));
            emit aiConnectionTested("openrouter", ok, msg);
            reply->deleteLater();
            manager->deleteLater();
            });
    }
    else {
        emit aiConnectionTested(backend, false, u8"Неизвестный провайдер ИИ");
        manager->deleteLater();
    }
}

void SettingsBridge::showHelp() {
    mw->addNewTab(QUrl("storm://help"));
}

void SettingsBridge::checkUpdates() {
    static UpdateManager* upd = new UpdateManager(mw);
    upd->checkForUpdatesManual(mw);
}

void SettingsBridge::openLogs() {
    QString p = Logger::getLogDir();
    QDir().mkpath(p);
    QDesktopServices::openUrl(QUrl::fromLocalFile(p));
}