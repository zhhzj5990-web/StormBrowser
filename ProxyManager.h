#pragma once
#include <QObject>
#include <QProcess>
#include <QNetworkProxy>
#include <QJsonObject>

// ==========================================
// --- ЯДРО И УПРАВЛЕНИЕ СЕТЬЮ ---
// ==========================================
class ProxyManager : public QObject {
    Q_OBJECT
public:
    static quint16 findFreePort();
    static bool applyProxy(const QString& proxyType, const QString& host, quint16 port, const QString& user = "", const QString& pass = "");
    static QPair<bool, QString> startXrayCore(QJsonObject configObj);
    static void disableProxy();
    // Реальное состояние подключения (жив ли процесс ядра ИЛИ применён
    // системный прокси вручную/из списка) — используется UI, чтобы не
    // хардкодить статус "Отключено" при каждом открытии окна.
    static bool isConnected();

    static QJsonObject parseSmartLink(const QString& rawLink, QString& outProtocolName);
    static QPair<bool, QString> connectFromLink(const QString& rawLink, QString& outProtocolName);


    static void startTrafficReporting(const QString& username, const QString& password, const QString& serverUrl);
    static void stopTrafficReporting();

private:
    static QProcess* s_coreProcess;
};