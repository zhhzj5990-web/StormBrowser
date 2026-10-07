#include "AnalyticsClient.h"
#include "UpdateManager.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QSysInfo>
#include <QUrl>
#include <QDebug>

const char* AnalyticsClient::kSettingsKey = "analytics/enabled";

AnalyticsClient::AnalyticsClient(QObject* parent)
    : QObject(parent),
      net(new QNetworkAccessManager(this)),
      startedAt(QDateTime::currentDateTime())
{
    QSettings s;

    // --- Первый запуск на этой машине: событие "install" ---
    // install_id — случайный UUID (НЕ привязан к железу). Генерируется один
    // раз, дальше просто переиспользуется: «сколько разных UUID видели
    // хотя бы раз» = сколько реальных установок.
    QString installId = s.value("analytics/install_id").toString();
    bool isFirstLaunch = installId.isEmpty();
    if (isFirstLaunch) {
        installId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.setValue("analytics/install_id", installId);
    }

    // Пульс раз в 30 минут: «сколько установок» даёт install, «как часто
    // пользуются» — DAU/WAU/MAU по heartbeat'ам. Сорок минут молчания между
    // пульсами перекрывается с запасом: пропущенный пульс занижает метрику
    // лишь на минуты активности, но никогда не теряет день целиком, пока
    // браузер запущен.
    heartbeatTimer = new QTimer(this);
    heartbeatTimer->setInterval(30 * 60 * 1000);
    connect(heartbeatTimer, &QTimer::timeout, this, &AnalyticsClient::sendHeartbeat);
    heartbeatTimer->start();

    // Сразу два события старта: install — только в самый первый раз,
    // heartbeat — при каждом запуске (minutes=0), чтобы день запуска
    // гарантированно попал в DAU даже при короткой сессии < 30 минут.
    if (isFirstLaunch) {
        sendEvent(QStringLiteral("install"), 0);
    }
    sendEvent(QStringLiteral("heartbeat"), 0);
}

bool AnalyticsClient::isEnabled() {
    return QSettings().value(kSettingsKey, true).toBool();
}

void AnalyticsClient::sendHeartbeat() {
    // Выключатель перечитывается перед КАЖДЫМ пульсом: пользователь снял
    // галочку в Настройках — и уже через полчаса браузер замолкает навсегда,
    // без ожидания перезапуска.
    sendEvent(QStringLiteral("heartbeat"),
              int(startedAt.secsTo(QDateTime::currentDateTime()) / 60));
}

void AnalyticsClient::sendEvent(const QString& event, int minutesActive) {
    if (!isEnabled()) return; // уважаем выбор пользователя

    QSettings s;
    const QString installId = s.value("analytics/install_id").toString();
    if (installId.isEmpty()) return; // теоретически невозможно, но не падаем

    QJsonObject json;
    json["install_id"] = installId;
    json["event"] = event;
    json["version"] = UpdateManager::BROWSER_VERSION;
    // productType() короткий ("windows"), prettyProductName() человекочитаем
    // ("Windows 11"/"Windows 10 Pro") — для графика ОС в админке удобнее второй,
    // обрезаем до 40 символов на случай экзотических сборок с длинными именами.
    QString os = QSysInfo::prettyProductName();
    if (os.size() > 40) os.resize(40);
    json["os"] = os;
    json["minutes"] = qBound(0, minutesActive, 60 * 24 * 14); // потолок 2 недели

    // Fire-and-forget: никаких диалогов, никаких retries, никаких сообщений
    // об ошибке — статистика не должна мешать пользоваться браузером. Сервер
    // недоступен (нет сети/прокси) — просто пропустим этот пульс.
    QNetworkRequest req(QUrl(QStringLiteral("https://storm-browser.online:8000/api/analytics/ping")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = net->post(req, QJsonDocument(json).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}
