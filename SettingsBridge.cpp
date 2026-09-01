#include "SettingsBridge.h"
#include "MainWindow.h"
#include "ProxyManager.h"
#include "GameModeManager.h"
#include "ShieldInterceptor.h"
#include "DownloadManager.h"
#include "UpdateManager.h"
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

void SettingsBridge::openProxy() {
    ProxyDialog dlg(mw);
    dlg.exec();
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
        QSslConfiguration sslConf = req.sslConfiguration();
        sslConf.setPeerVerifyMode(QSslSocket::VerifyNone); // как и в основном AI-чате — особенность серверов GigaChat
        req.setSslConfiguration(sslConf);

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