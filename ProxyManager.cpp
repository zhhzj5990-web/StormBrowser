#include "ProxyManager.h"
#include "ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QTcpServer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <QUrl>
#include <QUrlQuery>
#include <QSettings>
#include <QDesktopServices>
#include <QMessageBox>
#include <QThread>
#include <QLabel>
#include <QMouseEvent>
#include <QEvent>
#include <QEventLoop>
#include <QTimer>
#include <atomic>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#endif

// --- Хелпер для перетаскивания кастомного безрамочного окна ---
// (аналогичен WindowDragFilter из MainWindow.cpp, используется для окон,
//  которые не наследуют MainWindow и не могут переиспользовать тот класс)
class ProxyWindowDragFilter : public QObject {
    QPoint dragPosition;
public:
    ProxyWindowDragFilter(QObject* parent = nullptr) : QObject(parent) {}
    bool eventFilter(QObject* obj, QEvent* event) override {
        QWidget* titleBar = qobject_cast<QWidget*>(obj);
        if (!titleBar) return false;

        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                dragPosition = me->globalPosition().toPoint() - titleBar->window()->frameGeometry().topLeft();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->buttons() & Qt::LeftButton) {
                titleBar->window()->move(me->globalPosition().toPoint() - dragPosition);
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

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
// --- ИНТЕРФЕЙС PROXY DIALOG ---
// ==========================================

ProxyDialog::ProxyDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(u8"Storm Proxy / VPN Manager");
    // Убираем стандартную белую рамку Windows — делаем окно полностью безрамочным,
    // как остальные кастомные окна браузера (см. showHistory() в MainWindow.cpp)
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    resize(620, 780);
    if (parent) setStyleSheet(parent->styleSheet());

    netManager = new QNetworkAccessManager(this);
    connect(netManager, &QNetworkAccessManager::finished, this, &ProxyDialog::onProxiesFetched);

    // --- Корневой layout без отступов: сверху кастомный титульник, ниже контент ---
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ==========================================
    // --- КАСТОМНЫЙ ЗАГОЛОВОК ОКНА (вместо системного) ---
    // ==========================================
    QWidget* titleBar = new QWidget(this);
    titleBar->setFixedHeight(35);
    titleBar->setObjectName("customTitleBar");

    QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(15, 0, 0, 0);
    titleLayout->setSpacing(0);

    QLabel* titleLabel = new QLabel(u8"🌐 Storm Proxy / VPN Manager", titleBar);
    titleLabel->setObjectName("customTitleLabel");

    QPushButton* btnCloseTitle = new QPushButton(u8"✕", titleBar);
    btnCloseTitle->setObjectName("titleCloseBtn");
    btnCloseTitle->setFixedSize(35, 35);

    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(btnCloseTitle);

    rootLayout->addWidget(titleBar);

    // Перетаскивание окна за титульник (т.к. системной рамки больше нет)
    titleBar->installEventFilter(new ProxyWindowDragFilter(titleBar));
    connect(btnCloseTitle, &QPushButton::clicked, this, &QDialog::reject);

    // ==========================================
    // --- ОСНОВНОЙ КОНТЕНТ ---
    // ==========================================
    QWidget* contentWidget = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(contentWidget);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(12);

    tabs = new QTabWidget(contentWidget);
    tabs->addTab(createSmartImportTab(), u8"✨ Smart Импорт");
    tabs->addTab(createSimpleProxyTab(), u8"🌐 Обычные прокси");
    mainLayout->addWidget(tabs);

    // БАГ-ФИКС: ThemeManager стилизует только сам QTabBar::tab (см. комментарий
    // в ThemeManager.cpp), а QTabWidget::pane — панель, на которой лежат
    // страницы вкладок — темой не покрывается вообще. Qt в таком случае рисует
    // её дефолтным белым фоном вместо цвета текущей темы, из-за чего весь
    // контент обеих вкладок выглядел как белое окно поверх тёмного диалога.
    // Достаём цвета активной темы напрямую и стилизуем pane/tab-bar вручную,
    // как это уже делает MainWindow для tabWidget.
    ThemeColors tc = ThemeManager::instance().getColors(QSettings().value("theme", "dark").toString());
    tabs->setStyleSheet(QString(
        "QTabWidget::pane { background-color: %1; border: 1px solid %2; border-radius: 8px; top: -1px; }"
        "QTabBar::tab { background-color: %3; color: %4; padding: 8px 16px; border-top-left-radius: 6px; border-top-right-radius: 6px; }"
        "QTabBar::tab:selected { background-color: %5; color: #ffffff; }"
        "QTabBar::tab:hover { background-color: %6; }"
    ).arg(tc.window, tc.border, tc.tab_bg, tc.text, tc.tab_selected, tc.button_hover));

    QHBoxLayout* bottomLayout = new QHBoxLayout();
    // БАГ-ФИКС: раньше статус всегда хардкодился как "Отключено" при каждом
    // создании диалога, независимо от того, жив ли процесс ядра / применён
    // ли системный прокси на самом деле. Если пользователь подключался и
    // закрывал окно кнопкой "Закрыть" (не "Отключить всё"), при повторном
    // открытии индикатор врал — показывал "выключено", хотя туннель
    // продолжал работать. Теперь реально опрашиваем текущее состояние.
    if (ProxyManager::isConnected()) {
        QString savedText = QSettings().value("proxy/last_status_text", u8"Статус: Подключено").toString();
        statusLabel = new QLabel(savedText, contentWidget);
        statusLabel->setStyleSheet("color: #56d39b; font-weight: bold; font-size: 14px;");
    }
    else {
        statusLabel = new QLabel(u8"Статус: Отключено", contentWidget);
        statusLabel->setStyleSheet("font-size: 14px; color: #8b949e; font-weight: bold;");
    }

    QPushButton* disconnectBtn = new QPushButton(u8"Отключить всё", contentWidget);
    disconnectBtn->setFixedHeight(34);
    disconnectBtn->setStyleSheet("background-color: rgba(255, 95, 95, 0.15); color: #ff5f5f; border: 1px solid #ff5f5f; font-weight: bold; border-radius: 6px; padding: 0 15px;");
    connect(disconnectBtn, &QPushButton::clicked, this, &ProxyDialog::disableAll);

    QPushButton* closeBtn = new QPushButton(u8"Закрыть", contentWidget);
    closeBtn->setFixedHeight(34);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    bottomLayout->addWidget(statusLabel);
    bottomLayout->addStretch();
    bottomLayout->addWidget(disconnectBtn);
    bottomLayout->addWidget(closeBtn);
    mainLayout->addLayout(bottomLayout);

    rootLayout->addWidget(contentWidget);
}

ProxyDialog::~ProxyDialog() {}

QWidget* ProxyDialog::createSmartImportTab() {
    QWidget* container = new QWidget(this);
    ThemeColors tc = ThemeManager::instance().getColors(QSettings().value("theme", "dark").toString());
    container->setStyleSheet(QString("background-color: %1;").arg(tc.window));
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(12);

    QLabel* title = new QLabel(u8"<b>🔗 Умное подключение (VLESS / VMess / Trojan / SS / Hysteria 2)</b>", container);
    title->setStyleSheet("font-size: 16px; color: #58a6ff;");
    layout->addWidget(title);

    QSettings settings;
    QString savedLink = settings.value("proxy/smart_link", "").toString();

    smartLinkInput = new QTextEdit(container);
    smartLinkInput->setPlaceholderText(u8"Вставьте вашу ссылку-ключ сюда (vless://, vmess://, trojan://...)...");
    smartLinkInput->setFixedHeight(100);
    if (!savedLink.isEmpty()) smartLinkInput->setText(savedLink);
    layout->addWidget(smartLinkInput);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* connectBtn = new QPushButton(u8"🚀 Расшифровать и Подключить", container);
    connectBtn->setFixedHeight(36);
    connectBtn->setStyleSheet("background-color: #1f6feb; color: white; font-weight: bold; border-radius: 6px; padding: 0 15px;");
    connect(connectBtn, &QPushButton::clicked, this, &ProxyDialog::processSmartLink);

    QPushButton* deleteBtn = new QPushButton(u8"🗑 Удалить ключ", container);
    deleteBtn->setFixedHeight(36);
    deleteBtn->setStyleSheet("background-color: rgba(255, 95, 95, 0.15); border: 1px solid #ff5f5f; color: #ff5f5f; font-weight: bold; border-radius: 6px; padding: 0 15px;");
    connect(deleteBtn, &QPushButton::clicked, this, &ProxyDialog::deleteSmartLink);

    btnLayout->addWidget(connectBtn);
    btnLayout->addWidget(deleteBtn);
    layout->addLayout(btnLayout);

    smartLog = new QLabel(u8"Готов к расшифровке...", container);
    smartLog->setWordWrap(true);
    layout->addWidget(smartLog);

    QWidget* infoBox = new QWidget(container);
    infoBox->setStyleSheet("background: rgba(255,255,255,0.03); border-radius: 10px; border: 1px solid #30363d;");
    QVBoxLayout* infoLayout = new QVBoxLayout(infoBox);

    QLabel* helpText = new QLabel(
        u8"<b>Где брать ссылки для подключения?</b><br><br>"
        u8"Если вам нужен VPN для базовых задач, воспользуйтесь <b>бесплатным Storm Cloud VPN</b> в Личном Кабинете.<br><br>"
        u8"Для <b>безлимитного 4K-видео и тяжелых загрузок</b> вы можете импортировать сюда свои личные ключи (VLESS, VMess и др.).", infoBox);
    helpText->setWordWrap(true);
    helpText->setStyleSheet("border: none; color: #c9d1d9; line-height: 1.5; font-size: 13px;");
    infoLayout->addWidget(helpText);

    QPushButton* refBtn = new QPushButton(u8"🔗 Купить безлимитный Premium (Wizard VPN)", infoBox);
    refBtn->setFixedHeight(34);
    refBtn->setCursor(Qt::PointingHandCursor);
    refBtn->setStyleSheet("background: #005a9e; border: 1px solid #0078d7; color: white; font-weight: bold; border-radius: 6px;");
    connect(refBtn, &QPushButton::clicked, [this]() {
        QDesktopServices::openUrl(QUrl("http://wizardvpn.co/ref/75362"));
        });
    infoLayout->addWidget(refBtn);

    layout->addWidget(infoBox);
    layout->addStretch();
    return container;
}

QWidget* ProxyDialog::createSimpleProxyTab() {
    QWidget* container = new QWidget(this);
    ThemeColors tc = ThemeManager::instance().getColors(QSettings().value("theme", "dark").toString());
    container->setStyleSheet(QString("background-color: %1;").arg(tc.window));
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(12);

    layout->addWidget(new QLabel(u8"<b>📍 Ручная настройка (SOCKS5 / HTTP)</b>", container));

    QGridLayout* grid = new QGridLayout();
    manualType = new QComboBox(container);
    manualType->addItems({ "SOCKS5", "HTTP" });
    manualHost = new QLineEdit(container);
    manualHost->setPlaceholderText(u8"Адрес сервера");
    manualPort = new QLineEdit(container);
    manualPort->setPlaceholderText(u8"Порт");
    manualPort->setFixedWidth(80);

    grid->addWidget(new QLabel(u8"Тип/Хост:", container), 0, 0);
    grid->addWidget(manualType, 0, 1);
    grid->addWidget(manualHost, 0, 2);
    grid->addWidget(manualPort, 0, 3);

    manualUser = new QLineEdit(container);
    manualUser->setPlaceholderText(u8"Логин (опционально)");
    manualPass = new QLineEdit(container);
    manualPass->setPlaceholderText(u8"Пароль (опционально)");
    manualPass->setEchoMode(QLineEdit::Password);

    grid->addWidget(new QLabel(u8"Авторизация:", container), 1, 0);
    grid->addWidget(manualUser, 1, 1);
    grid->addWidget(manualPass, 1, 2, 1, 2);
    layout->addLayout(grid);

    QPushButton* applyBtn = new QPushButton(u8"Применить настройки", container);
    applyBtn->setFixedHeight(34);
    connect(applyBtn, &QPushButton::clicked, this, &ProxyDialog::applyManualProxy);
    layout->addWidget(applyBtn);

    layout->addSpacing(10);
    layout->addWidget(new QLabel(u8"<b>🎁 Бесплатные прокси (Общие списки GitHub)</b>", container));

    proxyListWidget = new QListWidget(container);
    proxyListWidget->setFixedHeight(150);
    connect(proxyListWidget, &QListWidget::itemDoubleClicked, this, &ProxyDialog::applyListProxy);
    layout->addWidget(proxyListWidget);

    QHBoxLayout* fetchRow = new QHBoxLayout();
    fetchType = new QComboBox(container);
    fetchType->addItems({ "SOCKS5", "HTTP" });
    QPushButton* fetchBtn = new QPushButton(u8"Обновить список", container);
    fetchBtn->setFixedHeight(32);
    connect(fetchBtn, &QPushButton::clicked, this, &ProxyDialog::startFetchProxies);
    fetchRow->addWidget(fetchType);
    fetchRow->addWidget(fetchBtn);
    layout->addLayout(fetchRow);

    layout->addStretch();
    return container;
}

// ==========================================
// --- ПАРСЕР И ОБРАБОТЧИК ССЫЛОК ---
// ==========================================


void ProxyDialog::processSmartLink() {
    QString rawLink = smartLinkInput->toPlainText().trimmed();
    if (rawLink.isEmpty()) return;

    smartLog->setText(u8"⏳ Расшифровка конфигурации...");

    QString protocolName;
    QJsonObject outbound = ProxyManager::parseSmartLink(rawLink, protocolName);

    if (outbound.isEmpty()) {
        smartLog->setText(u8"❌ Ошибка: Неизвестный или неверный формат ссылки!");
        return;
    }

    // Базовый шаблон конфигурации Xray
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
        QString statusText = QString(u8"Статус: %1 Активен").arg(protocolName);
        QSettings().setValue("proxy/smart_link", rawLink);
        QSettings().setValue("is_official_vpn", "false");
        QSettings().setValue("proxy/last_status_text", statusText);
        statusLabel->setText(statusText);
        statusLabel->setStyleSheet("color: #56d39b; font-weight: bold; font-size: 14px;");
        smartLog->setText(QString(u8"✅ %1 успешно подключен!").arg(protocolName));
    }
    else {
        smartLog->setText(u8"❌ Ошибка ядра: " + msg);
    }
}

void ProxyDialog::deleteSmartLink() {
    QSettings().remove("proxy/smart_link");
    smartLinkInput->clear();
    smartLog->setText(u8"✅ Сохраненный ключ удален из памяти.");
}

void ProxyDialog::applyManualProxy() {
    QString host = manualHost->text().trimmed();
    quint16 port = manualPort->text().toUInt();
    if (!host.isEmpty() && port > 0) {
        if (ProxyManager::applyProxy(manualType->currentText(), host, port, manualUser->text(), manualPass->text())) {
            QString statusText = QString(u8"Статус: %1:%2").arg(host).arg(port);
            QSettings().setValue("proxy/last_status_text", statusText);
            statusLabel->setText(statusText);
            statusLabel->setStyleSheet("color: #58a6ff; font-weight: bold; font-size: 14px;");
        }
    }
}

void ProxyDialog::startFetchProxies() {
    proxyListWidget->clear();
    proxyListWidget->addItem(u8"⏳ Загрузка списка с GitHub...");

    QString url = fetchType->currentText() == "SOCKS5"
        ? "https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/socks5.txt"
        : "https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/http.txt";

    netManager->get(QNetworkRequest(QUrl(url)));
}

void ProxyDialog::onProxiesFetched(QNetworkReply* reply) {
    reply->deleteLater();
    proxyListWidget->clear();

    if (reply->error() == QNetworkReply::NoError) {
        QString text = QString::fromUtf8(reply->readAll());
        QStringList lines = text.split("\n", Qt::SkipEmptyParts);
        int count = 0;
        for (const QString& line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.contains(":")) {
                proxyListWidget->addItem(trimmed);
                if (++count >= 100) break; // Берём первые 100 быстрых
            }
        }
    }
    else {
        proxyListWidget->addItem(u8"❌ Ошибка загрузки списка");
    }
}

void ProxyDialog::applyListProxy(QListWidgetItem* item) {
    QStringList parts = item->text().split(":");
    if (parts.size() == 2) {
        if (ProxyManager::applyProxy(fetchType->currentText(), parts[0], parts[1].toUInt())) {
            QString statusText = u8"Статус: " + parts[0];
            QSettings().setValue("proxy/last_status_text", statusText);
            statusLabel->setText(statusText);
            statusLabel->setStyleSheet("color: #58a6ff; font-weight: bold; font-size: 14px;");
        }
    }
}

void ProxyDialog::disableAll() {
    ProxyManager::disableProxy();
    QSettings().setValue("is_official_vpn", "false");
    QSettings().remove("proxy/last_status_text");
    statusLabel->setText(u8"Статус: Отключено");
    statusLabel->setStyleSheet("color: #8b949e; font-weight: bold; font-size: 14px;");
    smartLog->setText(u8"Готов к работе");
}

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