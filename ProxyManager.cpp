#include "ProxyManager.h"
#include <QTcpServer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QUrl>
#include <QUrlQuery>
#include <QSettings>
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <atomic>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#endif

QProcess* ProxyManager::s_coreProcess = nullptr;

// ==========================================
// --- СТАТИЧЕСКИЕ МЕТОДЫ УПРАВЛЕНИЯ ЯДРОМ ---
// ==========================================

quint16 ProxyManager::findFreePort() {
    QTcpServer server;
    if (server.listen(QHostAddress::LocalHost, 0)) {
        quint16 port = server.serverPort();
        server.close();
        return port;
    }
    return 2080; // Резервный порт по умолчанию
}

bool ProxyManager::applyProxy(const QString& proxyType, const QString& host, quint16 port, const QString& user, const QString& pass) {
    QNetworkProxy proxy;
    if (proxyType.toUpper() == "SOCKS5") {
        proxy.setType(QNetworkProxy::Socks5Proxy);
    }
    else if (proxyType.toUpper() == "HTTP") {
        proxy.setType(QNetworkProxy::HttpProxy);
    }
    else {
        proxy.setType(QNetworkProxy::NoProxy);
    }

    proxy.setHostName(host);
    proxy.setPort(port);
    if (!user.isEmpty()) proxy.setUser(user);
    if (!pass.isEmpty()) proxy.setPassword(pass);

    QNetworkProxy::setApplicationProxy(proxy);
    return true;
}

void ProxyManager::disableProxy() {
    // Останавливаем фоновый учёт трафика вместе с самим туннелем — иначе
    // воркер продолжит опрашивать systemwide-счётчики трафика после того,
    // как VPN уже выключен.
    stopTrafficReporting();

    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));

    if (s_coreProcess) {
        s_coreProcess->terminate();
        if (!s_coreProcess->waitForFinished(2000)) {
            s_coreProcess->kill();
        }
        delete s_coreProcess;
        s_coreProcess = nullptr;
    }

}

bool ProxyManager::isConnected() {
    // Smart-ссылка / официальный VPN: работает через ядро Xray в отдельном процессе.
    if (s_coreProcess && s_coreProcess->state() == QProcess::Running) {
        return true;
    }
    // Ручной SOCKS5/HTTP или прокси из списка: процесса ядра нет вообще,
    // но системный прокси применён напрямую через QNetworkProxy.
    return QNetworkProxy::applicationProxy().type() != QNetworkProxy::NoProxy;
}

// ==========================================
// --- ФОНОВЫЙ УЧЁТ ТРАФИКА VPN ---
// (аналог isolated_vpn_worker() из proxy_manager.py)
// ==========================================
// В Python это был отдельный multiprocessing.Process — полностью изолированный
// от UI. В Qt/C++ отдельный процесс избыточен и плохо дружит с общими
// настройками (QSettings/QNetworkProxy), поэтому используем QThread: вся
// работа (опрос процесса, чтение счётчиков интерфейсов, HTTP POST) выполняется
// целиком внутри run(), на выделенном потоке, и не блокирует UI так же, как
// не блокировал его отдельный процесс в Python.
namespace {

#ifdef Q_OS_WIN
    bool isXrayProcessRunning() {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;

        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (QString::fromWCharArray(entry.szExeFile).compare("xray.exe", Qt::CaseInsensitive) == 0) {
                    found = true;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    }

    // Суммарный трафик (вход+выход) по всем сетевым интерфейсам — грубый
    // системный аналог psutil.net_io_counters(), которым пользуется Python.
    // Намеренно используем "классический" GetIfTable/MIB_IFTABLE, а не
    // GetIfTable2/MIB_IF_TABLE2: последний объявлен в Netioapi.h под guard'ом
    // "#if NTDDI_VERSION >= NTDDI_VISTA", и если проект не поднимает
    // _WIN32_WINNT/NTDDI_VERSION явно (или windows.h успевает подключиться
    // раньше через заголовки Qt со старым значением по умолчанию), SDK эти
    // символы просто не объявляет — отсюда "необъявленный идентификатор".
    // GetIfTable доступен без всяких version-defines с Windows 2000.
    quint64 totalNetworkBytes() {
        ULONG size = 0;
        if (GetIfTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || size == 0) {
            return 0;
        }

        QByteArray buffer(static_cast<int>(size), 0);
        PMIB_IFTABLE ifTable = reinterpret_cast<PMIB_IFTABLE>(buffer.data());

        quint64 total = 0;
        if (GetIfTable(ifTable, &size, FALSE) == NO_ERROR) {
            for (DWORD i = 0; i < ifTable->dwNumEntries; ++i) {
                const MIB_IFROW& row = ifTable->table[i];
                total += static_cast<quint64>(row.dwInOctets) + static_cast<quint64>(row.dwOutOctets);
            }
        }
        return total;
    }
#endif

    class VpnTrafficWorker : public QThread {
    public:
        VpnTrafficWorker(const QString& username, const QString& password, const QString& serverUrl)
            : m_username(username), m_password(password), m_serverUrl(serverUrl), m_running(true) {
        }

        void requestStop() { m_running.store(false); }

    protected:
        void run() override {
#ifdef Q_OS_WIN
            quint64 lastBytes = 0;
            bool haveBaseline = false;

            while (m_running.load()) {
                if (!isXrayProcessRunning()) {
                    // Ядро не запущено — сбрасываем базовую точку отсчёта,
                    // как и Python (last_net_bytes = None)
                    haveBaseline = false;
                }
                else {
                    quint64 current = totalNetworkBytes();
                    if (haveBaseline) {
                        quint64 delta = (current >= lastBytes) ? (current - lastBytes) : 0;
                        // Тот же порог, что и в Python: репортим при расходе > 500 КБ
                        if (delta > 500 * 1024) {
                            reportTraffic(delta);
                            lastBytes = current;
                        }
                    }
                    else {
                        lastBytes = current;
                        haveBaseline = true;
                    }
                }

                // 10 секунд ожидания между проверками, как time.sleep(10) в
                // Python, но с проверкой флага каждую секунду, чтобы
                // stopTrafficReporting() не подвисал надолго на wait().
                for (int i = 0; i < 10 && m_running.load(); ++i) {
                    QThread::sleep(1);
                }
            }
#endif
        }

    private:
        void reportTraffic(quint64 bytesUsed) {
            QNetworkAccessManager manager;
            QNetworkRequest request(QUrl(m_serverUrl + "/api/vpn/report_traffic"));
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

            QJsonObject body{
                {"username", m_username},
                {"password", m_password},
                {"bytes_used", static_cast<double>(bytesUsed)}
            };

            QEventLoop loop;
            QNetworkReply* reply = manager.post(request, QJsonDocument(body).toJson());
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(5000, &loop, &QEventLoop::quit); // timeout=5, как в requests.post
            loop.exec();
            if (reply->isRunning()) {
                reply->abort(); // Принудительно обрываем запрос, если сработал таймаут EventLoop
            }
            reply->deleteLater(); // молча игнорируем ошибки сети, как и Python (except: pass)
        }

        QString m_username, m_password, m_serverUrl;
        std::atomic<bool> m_running;
    };

    VpnTrafficWorker* g_trafficWorker = nullptr;

} // namespace

void ProxyManager::startTrafficReporting(const QString& username, const QString& password, const QString& serverUrl) {
    stopTrafficReporting(); // не запускаем два воркера параллельно

    g_trafficWorker = new VpnTrafficWorker(username, password, serverUrl);
    g_trafficWorker->start();
}

void ProxyManager::stopTrafficReporting() {
    if (g_trafficWorker) {
        g_trafficWorker->requestStop();
        g_trafficWorker->wait(2000);
        delete g_trafficWorker;
        g_trafficWorker = nullptr;
    }
}

QPair<bool, QString> ProxyManager::startXrayCore(QJsonObject configObj) {
    disableProxy();

    quint16 freePort = findFreePort();

    // Принудительно меняем входящий порт на свободный
    QJsonArray inbounds = configObj["inbounds"].toArray();
    if (!inbounds.isEmpty()) {
        QJsonObject firstInbound = inbounds[0].toObject();
        firstInbound["port"] = freePort;
        firstInbound["listen"] = "127.0.0.1";
        inbounds[0] = firstInbound;
        configObj["inbounds"] = inbounds;
    }
    else {
        return { false, u8"Ошибка: в конфигурации отсутствует секция inbounds" };
    }

    QString xrayPath = QCoreApplication::applicationDirPath() + "/system_core/xray.exe";
    if (!QFile::exists(xrayPath)) {
        return { false, u8"Ядро xray.exe не найдено по пути: " + xrayPath };
    }

    // Сохраняем временный конфиг
    QString tempConfigPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/storm_xray_config.json";
    QFile configFile(tempConfigPath);
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        configFile.write(QJsonDocument(configObj).toJson());
        configFile.close();
    }
    else {
        return { false, u8"Не удалось записать конфигурационный файл" };
    }

    s_coreProcess = new QProcess();
    s_coreProcess->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
#ifdef Q_OS_WIN
        args->flags |= 0x08000000; // CREATE_NO_WINDOW (скрываем черную консоль)
#endif
        });

    s_coreProcess->start(xrayPath, { "-c", tempConfigPath });

    // Ждём старта ядра ~1 секунду, чтобы отловить немедленный крэш (битый
    // конфиг и т.п.). Раньше здесь стоял QThread::sleep(1) — он блокировал
    // весь event loop UI-потока, из-за чего окно на секунду переставало
    // перерисовываться и реагировать на клики при каждом подключении.
    // QEventLoop с таймером ждёт то же время, но продолжает крутить цикл
    // событий Qt, так что интерфейс остаётся отзывчивым.
    {
        QEventLoop waitLoop;
        QTimer::singleShot(1000, &waitLoop, &QEventLoop::quit);
        waitLoop.exec();
    }

    if (s_coreProcess->state() != QProcess::Running) {
        QString err = QString::fromUtf8(s_coreProcess->readAllStandardError());
        disableProxy();
        return { false, u8"Ядро не запустилось:\n" + err };
    }

    applyProxy("SOCKS5", "127.0.0.1", freePort);
    return { true, QString(u8"Подключено (порт: %1)").arg(freePort) };
}

// ==========================================
// --- (Proxy/VPN UI встроен в страницу настроек — см. SettingsPageHtml.cpp
//      и SettingsBridge.cpp; отдельного окна ProxyDialog больше нет) ---
// ==========================================

QJsonObject ProxyManager::parseSmartLink(const QString& rawLink, QString& outProtocolName) {
    QUrl url(rawLink);
    QUrlQuery qs(url);
    QJsonObject outbound;

    if (rawLink.startsWith("vless://")) {
        outProtocolName = "VLESS";
        // БАГ-ФИКС: если в ссылке нет хоста или порта (обрезанная/битая
        // ссылка), url.port() у Qt возвращает -1, и это -1 тихо уезжало
        // прямо в JSON-конфиг Xray. Проверяем заранее и возвращаем пустой
        // outbound, чтобы UI показал понятную ошибку формата.
        if (url.host().isEmpty() || url.port() <= 0) {
            return QJsonObject();
        }
        QString uuid = QUrl::fromPercentEncoding(url.userName().toUtf8());
        QString sni = qs.hasQueryItem("sni") ? qs.queryItemValue("sni") : url.host();

        QJsonObject userObj{ {"id", uuid}, {"encryption", "none"} };
        if (qs.hasQueryItem("flow")) userObj["flow"] = qs.queryItemValue("flow");

        QJsonObject streamSettings{
            {"network", qs.hasQueryItem("type") ? qs.queryItemValue("type") : "tcp"},
            {"security", qs.hasQueryItem("security") ? qs.queryItemValue("security") : "none"}
        };

        if (streamSettings["security"].toString() == "reality") {
            streamSettings["realitySettings"] = QJsonObject{
                {"fingerprint", qs.hasQueryItem("fp") ? qs.queryItemValue("fp") : "chrome"},
                {"serverName", sni},
                {"publicKey", qs.queryItemValue("pbk")},
                {"shortId", qs.queryItemValue("sid")}
            };
        }

        outbound = QJsonObject{
            {"protocol", "vless"},
            {"settings", QJsonObject{{"vnext", QJsonArray{QJsonObject{{"address", url.host()}, {"port", url.port()}, {"users", QJsonArray{userObj}}}}}}},
            {"streamSettings", streamSettings}
        };
    }
    else if (rawLink.startsWith("vmess://")) {
        outProtocolName = "VMess";
        QString b64Data = rawLink.mid(8);
        QByteArray decoded = QByteArray::fromBase64(b64Data.toUtf8());

        // БАГ-ФИКС: раньше при битом base64 или невалидном JSON парсинг не
        // прерывался — QJsonDocument::fromJson() на ошибке просто возвращает
        // "нулевой" документ, а .object() от него — пустой QJsonObject. Все
        // поля (add/port/id) тихо становились пустыми/нулевыми, и функция
        // всё равно возвращала непустой outbound с этим мусором внутри.
        // Дальше это уходило прямо в Xray, который падал с невнятной для
        // пользователя ошибкой вместо понятного "неверный формат ссылки".
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(decoded, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            return QJsonObject();
        }
        QJsonObject data = doc.object();
        if (data["add"].toString().isEmpty() || data["id"].toString().isEmpty() || data["port"].toInt() <= 0) {
            return QJsonObject();
        }

        outbound = QJsonObject{
            {"protocol", "vmess"},
            {"settings", QJsonObject{{"vnext", QJsonArray{QJsonObject{
                {"address", data["add"].toString()},
                {"port", data["port"].toInt()},
                {"users", QJsonArray{QJsonObject{{"id", data["id"].toString()}, {"alterId", data["aid"].toInt()}, {"security", "auto"}}}}
            }}}}},
            {"streamSettings", QJsonObject{
                {"network", data["net"].toString("tcp")},
                {"security", data["tls"].toString() == "tls" ? "tls" : "none"}
            }}
        };
    }
    else if (rawLink.startsWith("trojan://")) {
        outProtocolName = "Trojan";
        if (url.host().isEmpty() || url.port() <= 0) {
            return QJsonObject();
        }
        outbound = QJsonObject{
            {"protocol", "trojan"},
            {"settings", QJsonObject{{"servers", QJsonArray{QJsonObject{
                {"address", url.host()},
                {"port", url.port()},
                {"password", QUrl::fromPercentEncoding(url.userName().toUtf8())}
            }}}}},
            {"streamSettings", QJsonObject{
                {"security", "tls"},
                {"tlsSettings", QJsonObject{{"serverName", qs.hasQueryItem("sni") ? qs.queryItemValue("sni") : url.host()}}}
            }}
        };
    }
    else if (rawLink.startsWith("ss://")) {
        // ВАЖНО: этой ветки не было вообще, хотя заголовок вкладки уже
        // рекламирует поддержку "SS" (Shadowsocks) — ссылки ss:// проваливались
        // в конец функции с пустым outbound и получали "Неизвестный формат ссылки".
        // Портируем логику из proxy_manager.py::process_smart_link один в один:
        // два формата — SIP002 (ss://BASE64(method:password)@host:port#tag)
        // и старый полностью-base64 (ss://BASE64(method:password@host:port)).
        outProtocolName = "Shadowsocks";
        QString body = rawLink.mid(5); // всё после "ss://"
        QString method, ssPassword, ssHost;
        int ssPort = 0;

        int atIdx = body.indexOf('@');
        if (atIdx != -1) {
            // SIP002
            QString authPart = body.left(atIdx);
            QString serverPart = body.mid(atIdx + 1).split('#').first(); // отбрасываем #tag

            QString authDecoded = QString::fromUtf8(QByteArray::fromBase64(authPart.toUtf8()));
            int colonIdx = authDecoded.indexOf(':');
            int portColonIdx = serverPart.lastIndexOf(':');
            // БАГ-ФИКС: раньше индексы не проверялись на -1. Если в декодированной
            // строке не было ":" (битый auth) или в serverPart не было ":" (нет
            // порта), left(-1)/mid(0) от Qt просто возвращали часть/всю строку
            // "как есть", метод и пароль получались перепутаны/задвоены, а ошибка
            // нигде не всплывала — Xray потом падал с непонятной причиной.
            if (colonIdx == -1 || portColonIdx == -1) {
                return QJsonObject();
            }
            method = authDecoded.left(colonIdx);
            ssPassword = authDecoded.mid(colonIdx + 1);

            ssHost = serverPart.left(portColonIdx);
            ssPort = serverPart.mid(portColonIdx + 1).toInt();
        }
        else {
            // Старый формат: ss://BASE64(method:password@host:port)
            QString b64 = body.split('#').first();
            QString full = QString::fromUtf8(QByteArray::fromBase64(b64.toUtf8()));

            int lastAtIdx = full.lastIndexOf('@');
            if (lastAtIdx == -1) {
                return QJsonObject();
            }
            QString methodPass = full.left(lastAtIdx);
            QString hostPort = full.mid(lastAtIdx + 1);

            int colonIdx = methodPass.indexOf(':');
            int portColonIdx = hostPort.lastIndexOf(':');
            if (colonIdx == -1 || portColonIdx == -1) {
                return QJsonObject();
            }
            method = methodPass.left(colonIdx);
            ssPassword = methodPass.mid(colonIdx + 1);

            ssHost = hostPort.left(portColonIdx);
            ssPort = hostPort.mid(portColonIdx + 1).toInt();
        }

        if (ssHost.isEmpty() || ssPort <= 0) {
            return QJsonObject();
        }

        outbound = QJsonObject{
            {"protocol", "shadowsocks"},
            {"settings", QJsonObject{{"servers", QJsonArray{QJsonObject{
                {"address", ssHost},
                {"port", ssPort},
                {"method", method},
                {"password", ssPassword}
            }}}}}
        };
    }
    else if (rawLink.startsWith("hysteria2://") || rawLink.startsWith("hy2://")) {
        outProtocolName = "Hysteria 2";
        if (url.host().isEmpty() || url.port() <= 0) {
            return QJsonObject();
        }
        QString uuid = QUrl::fromPercentEncoding(url.userName().toUtf8());
        QString sni = qs.hasQueryItem("sni") ? qs.queryItemValue("sni") : url.host();
        bool allowInsecure = qs.queryItemValue("insecure") == "1";

        outbound = QJsonObject{
            {"protocol", "hysteria"},
            {"settings", QJsonObject{{"version", 2}, {"address", url.host()}, {"port", url.port()}}},
            {"streamSettings", QJsonObject{
                {"network", "hysteria"},
                {"security", "tls"},
                {"tlsSettings", QJsonObject{{"serverName", sni}, {"allowInsecure", allowInsecure}, {"alpn", QJsonArray{"h3"}}}},
                {"hysteriaSettings", QJsonObject{{"version", 2}, {"auth", uuid}}}
            }}
        };
    }

    return outbound;
}

QPair<bool, QString> ProxyManager::connectFromLink(const QString& rawLink, QString& outProtocolName) {
    QJsonObject outbound = parseSmartLink(rawLink, outProtocolName);
    if (outbound.isEmpty()) {
        return { false, u8"Неизвестный или неверный формат VPN-ссылки" };
    }

    QJsonObject configObj{
        {"log", QJsonObject{{"access", "none"}, {"error", "none"}, {"loglevel", "none"}}},
        {"dns", QJsonObject{{"servers", QJsonArray{"1.1.1.1", "8.8.8.8"}}}},
        {"inbounds", QJsonArray{QJsonObject{
            {"port", 2080},
            {"listen", "127.0.0.1"},
            {"protocol", "socks"},
            {"settings", QJsonObject{{"udp", true}}}
        }}},
        {"outbounds", QJsonArray{outbound, QJsonObject{{"protocol", "freedom"}, {"tag", "direct"}}}},
        {"routing", QJsonObject{{"domainStrategy", "IPIfNonMatch"}, {"rules", QJsonArray()}}}
    };

    auto [success, msg] = ProxyManager::startXrayCore(configObj);
    if (success) {
        QSettings().setValue("is_official_vpn", "true");
        QSettings().setValue("proxy/last_status_text", QString(u8"Статус: %1 Активен").arg(outProtocolName));
    }
    return { success, msg };
}