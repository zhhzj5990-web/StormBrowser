#include "StormCloud.h"
#include <QMouseEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDebug>
#include <QGridLayout>

StormCloud::StormCloud(QWidget* parent)
    : QDialog(parent), m_serverUrl("https://storm-browser.online:8000")
{
    setWindowTitle("Storm Cloud");
    setFixedSize(820, 720);

    setStyleSheet("StormCloud { background-color: #0f172a; }");

    m_settings.beginGroup("sync");
    m_currentUser = m_settings.value("username", "").toString();
    m_currentPassword = m_settings.value("password", "").toString();
    m_settings.endGroup();

    m_networkManager = new QNetworkAccessManager(this);

    setupUi();

    m_pingTimer = new QTimer(this);
    connect(m_pingTimer, &QTimer::timeout, this, &StormCloud::checkServerOnline);
    m_pingTimer->start(10000);
    checkServerOnline();
}

StormCloud::~StormCloud() {}

void StormCloud::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    m_container = new QFrame(this);
    m_container->setObjectName("MainContainer");
    m_container->setStyleSheet("QFrame#MainContainer { background-color: #0f172a; border: 1px solid #334155; border-radius: 20px; }");

    mainLayout->addWidget(m_container);

    QVBoxLayout* containerLayout = new QVBoxLayout(m_container);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget(m_container);
    containerLayout->addWidget(m_stack);

    m_loginWidget = createLoginScreen();
    m_manageWidget = createManageScreen();

    m_stack->addWidget(m_loginWidget);
    m_stack->addWidget(m_manageWidget);

    if (!m_currentUser.isEmpty() && !m_currentPassword.isEmpty()) {
        m_stack->setCurrentWidget(m_manageWidget);
        updateProfileUi();
    }
    else {
        m_stack->setCurrentWidget(m_loginWidget);
    }
}

// ==========================================
// --- ЭКРАН ЛОГИНА И РЕГИСТРАЦИИ ---
// ==========================================
QWidget* StormCloud::createLoginScreen() {
    QWidget* wrapper = new QWidget();

    // 1. Главный вертикальный слой всего окна (обертки)
    QVBoxLayout* mainV = new QVBoxLayout(wrapper);
    mainV->setContentsMargins(20, 20, 20, 20);

    // Горизонтальный слой для центрирования нашей 400px карточки
    QHBoxLayout* centerH = new QHBoxLayout();
    centerH->addStretch();

    QFrame* widget = new QFrame();
    widget->setFixedWidth(400);
    QVBoxLayout* layout = new QVBoxLayout(widget);

    layout->setContentsMargins(20, 40, 20, 20);
    layout->setSpacing(15);

    QLabel* logo = new QLabel(u8"⛈️");
    logo->setStyleSheet("font-size: 50px;");
    logo->setAlignment(Qt::AlignCenter);

    QLabel* title = new QLabel("Storm Cloud");
    title->setStyleSheet("font-size: 24px; font-weight: bold; color: #e2e8f0;");
    title->setAlignment(Qt::AlignCenter);

    QString inputStyle = "padding: 12px; border-radius: 8px; background: #1e293b; border: 1px solid #334155; color: #e2e8f0;";

    m_usernameInput = new QLineEdit(m_currentUser);
    m_usernameInput->setPlaceholderText(u8"Логин");
    m_usernameInput->setStyleSheet(inputStyle);

    m_passwordInput = new QLineEdit();
    m_passwordInput->setPlaceholderText(u8"Пароль");
    m_passwordInput->setEchoMode(QLineEdit::Password);
    m_passwordInput->setStyleSheet(inputStyle);

    m_inviteInput = new QLineEdit();
    m_inviteInput->setPlaceholderText(u8"Код приглашения (Даст +200 SC)");
    m_inviteInput->setStyleSheet(inputStyle);
    m_inviteInput->hide();

    m_btnAuth = new QPushButton(u8"Войти в аккаунт");
    m_btnAuth->setCursor(Qt::PointingHandCursor);
    m_btnAuth->setStyleSheet("QPushButton { padding: 12px; background: #a371f7; border-radius: 8px; font-weight: bold; color: #fff; font-size: 14px; } QPushButton:hover { background: #b180ff; }");
    connect(m_btnAuth, &QPushButton::clicked, this, &StormCloud::handleAuth);

    m_modeToggle = new QPushButton(u8"Нет аккаунта? Создать");
    m_modeToggle->setFlat(true);
    m_modeToggle->setCursor(Qt::PointingHandCursor);
    m_modeToggle->setStyleSheet("color: #cbd5e1; font-size: 12px;");
    connect(m_modeToggle, &QPushButton::clicked, this, &StormCloud::toggleMode);

    // =========================================================
    // 2. ПРИВЯЗЫВАЕМ ENTER В ПОЛЯХ ВВОДА К ВХОДУ В АККАУНТ
    // =========================================================
    connect(m_usernameInput, &QLineEdit::returnPressed, this, &StormCloud::handleAuth);
    connect(m_passwordInput, &QLineEdit::returnPressed, this, &StormCloud::handleAuth);
    connect(m_inviteInput, &QLineEdit::returnPressed, this, &StormCloud::handleAuth);

    // Собираем элементы внутри 400px карточки
    layout->addWidget(logo);
    layout->addWidget(title);
    layout->addSpacing(20);
    layout->addWidget(m_usernameInput);
    layout->addWidget(m_passwordInput);
    layout->addWidget(m_inviteInput);
    layout->addWidget(m_btnAuth);
    layout->addWidget(m_modeToggle);

    centerH->addWidget(widget);
    centerH->addStretch();

    // =========================================================
    // 3. БЛОК КНОПКИ "ЗАКРЫТЬ" В ПРАВОМ НИЖНЕМ УГЛУ ВСЕГО ОКНА
    // =========================================================
    QHBoxLayout* loginBottomLayout = new QHBoxLayout();
    loginBottomLayout->addStretch(); // Пружина выталкивает кнопку в самый правый угол окна

    QPushButton* btnClose = new QPushButton(u8"Закрыть");
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setStyleSheet("QPushButton { padding: 6px 15px; background: rgba(255, 255, 255, 0.05); color: #cbd5e1; border-radius: 6px; border: 1px solid #334155; font-size: 13px; } QPushButton:hover { background: rgba(255, 95, 95, 0.1); color: #ff5f5f; border-color: #ff5f5f; }");
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);

    loginBottomLayout->addWidget(btnClose);

    // Собираем главное окно: пружина сверху, карточка по центру, пружина снизу и кнопка "Закрыть" в углу
    mainV->addStretch();
    mainV->addLayout(centerH);
    mainV->addStretch();
    mainV->addLayout(loginBottomLayout);

    return wrapper;
}

QFrame* StormCloud::createStatCard(const QString& title, const QString& value) {
    QFrame* card = new QFrame(this);
    card->setStyleSheet("background: #1e293b; border-radius: 12px; border: 1px solid #334155;");
    QVBoxLayout* l = new QVBoxLayout(card);
    l->setContentsMargins(12, 10, 12, 10);

    QLabel* t = new QLabel(title, card);
    t->setStyleSheet("color: #cbd5e1; font-size: 12px; border: none; background: transparent;");

    QLabel* v = new QLabel(value, card);
    v->setObjectName("val");
    v->setStyleSheet("color: #e2e8f0; font-size: 22px; font-weight: bold; border: none; background: transparent;");

    l->addWidget(t);
    l->addWidget(v);
    return card;
}

// ==========================================
// --- ЭКРАН ЛИЧНОГО КАБИНЕТА ---
// ==========================================
QWidget* StormCloud::createManageScreen() {
    QWidget* widget = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(25, 20, 25, 20);
    mainLayout->setSpacing(20);

    // ==========================================
    // 1. ШАПКА
    // ==========================================
    QHBoxLayout* header = new QHBoxLayout();

    m_avatarBtn = new QPushButton();
    m_avatarBtn->setFixedSize(70, 70);
    m_avatarBtn->setCursor(Qt::PointingHandCursor);
    m_avatarBtn->setStyleSheet("background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #a371f7, stop:1 #b180ff); border-radius: 35px; color: white; font-size: 32px; border: none;");
    m_avatarBtn->setText(u8"👤"); // Заглушка аватара

    QVBoxLayout* infoVbox = new QVBoxLayout();
    m_userNameLabel = new QLabel("User");
    m_userNameLabel->setStyleSheet("font-size: 22px; font-weight: bold; color: #e2e8f0;");

    QHBoxLayout* statusHbox = new QHBoxLayout();
    m_statusDot = new QLabel(u8"●");
    m_statusDot->setStyleSheet("color: gray; font-size: 14px;");
    m_statusTextLabel = new QLabel(u8"Подключение...");
    m_statusTextLabel->setStyleSheet("color: #cbd5e1; font-size: 13px;");
    statusHbox->addWidget(m_statusDot);
    statusHbox->addWidget(m_statusTextLabel);
    statusHbox->addStretch();

    m_lastSyncLabel = new QLabel(u8"Синхронизация: —");
    m_lastSyncLabel->setStyleSheet("color: #cbd5e1; font-size: 12px;");

    infoVbox->addWidget(m_userNameLabel);
    infoVbox->addLayout(statusHbox);
    infoVbox->addWidget(m_lastSyncLabel);

    // Блок быстрой статистики (Балансы)
    QFrame* quickStatsBox = new QFrame();
    quickStatsBox->setStyleSheet("background: rgba(0, 0, 0, 0.15); border: 1px solid #334155; border-radius: 12px;");
    QHBoxLayout* quickStatsLayout = new QHBoxLayout(quickStatsBox);
    quickStatsLayout->setContentsMargins(15, 8, 15, 8);
    quickStatsLayout->setSpacing(20);

    m_lblHeaderSc = new QLabel("0 SC");
    m_lblHeaderSc->setStyleSheet("color: #56d39b; font-weight: bold; font-size: 15px; background: transparent; border: none;");

    m_lblHeaderAi = new QLabel(u8"🧠 0 Токенов");
    m_lblHeaderAi->setStyleSheet("color: #a371f7; font-weight: bold; font-size: 15px; background: transparent; border: none;");

    m_lblHeaderPrem = new QLabel(u8"Стандарт");
    m_lblHeaderPrem->setStyleSheet("color: #cbd5e1; font-size: 13px; background: transparent; border: none;");

    quickStatsLayout->addWidget(m_lblHeaderSc);
    quickStatsLayout->addWidget(m_lblHeaderAi);
    quickStatsLayout->addWidget(m_lblHeaderPrem);

    m_settingsBtn = new QPushButton(u8"⚙️");
    m_settingsBtn->setFixedSize(36, 36);
    m_settingsBtn->setCursor(Qt::PointingHandCursor);
    m_settingsBtn->setStyleSheet("background: transparent; border: none; font-size: 22px; color: #e2e8f0;");

    header->addWidget(m_avatarBtn);
    header->addLayout(infoVbox);
    header->addStretch(1);
    header->addWidget(quickStatsBox, 0, Qt::AlignVCenter);
    header->addSpacing(15);
    header->addWidget(m_settingsBtn, 0, Qt::AlignTop);

    // ==========================================
    // 2. ДВЕ КОЛОНКИ (Главный контент)
    // ==========================================
    QHBoxLayout* columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(25);

    QVBoxLayout* leftCol = new QVBoxLayout();
    leftCol->setSpacing(15);

    QVBoxLayout* rightCol = new QVBoxLayout();
    rightCol->setSpacing(15);

    // --------- ЛЕВАЯ КОЛОНКА ---------

    // 1. Статистика
    QHBoxLayout* statsLayout = new QHBoxLayout();
    m_histCard = createStatCard(u8"📜 История", "0");
    m_passCard = createStatCard(u8"🔑 Пароли", "0");
    m_histCard->setMinimumHeight(65);
    m_passCard->setMinimumHeight(65);
    statsLayout->addWidget(m_histCard);
    statsLayout->addWidget(m_passCard);
    leftCol->addLayout(statsLayout);

    // 2. Магазин Storm
    QFrame* storeBox = new QFrame();
    storeBox->setStyleSheet("background: #1e293b; border-radius: 10px; border: 1px solid #a371f7;");
    QVBoxLayout* storeLayout = new QVBoxLayout(storeBox);
    storeLayout->setContentsMargins(15, 15, 15, 15);
    storeLayout->setSpacing(10);

    QLabel* storeLbl = new QLabel(u8"🛒 <b>Магазин Storm Cloud</b>");
    storeLbl->setStyleSheet("color: #a371f7; font-size: 14px; border: none; background: transparent;");

    m_btnOpenStore = new QPushButton(u8"🛍️ Открыть витрину");
    m_btnOpenStore->setCursor(Qt::PointingHandCursor);
    m_btnOpenStore->setStyleSheet("QPushButton { padding: 10px; background: #a371f7; color: white; border-radius: 6px; font-weight: bold; font-size: 13px; border: none; } QPushButton:hover { background: #b180ff; }");

    m_btnWalletInfo = new QPushButton(u8"💳 Мой кошелек");
    m_btnWalletInfo->setCursor(Qt::PointingHandCursor);
    m_btnWalletInfo->setStyleSheet("background: rgba(255,255,255,0.05); border: 1px solid #334155; border-radius: 6px; color: #e2e8f0; font-size: 11px; padding: 6px;");
    connect(m_btnOpenStore, &QPushButton::clicked, this, &StormCloud::showStoreDialog);
    connect(m_btnWalletInfo, &QPushButton::clicked, this, &StormCloud::showWalletDialog);

    QLabel* apiHintLbl = new QLabel(u8"💡 <i>Хотите протестировать нейросеть прямо сейчас? Вы можете бесплатно использовать собственный API-ключ в Настройках браузера.</i>");
    apiHintLbl->setStyleSheet("color: #cbd5e1; font-size: 11px; border: none; background: transparent; padding-top: 4px;");
    apiHintLbl->setWordWrap(true);

    storeLayout->addWidget(storeLbl);
    storeLayout->addWidget(m_btnOpenStore);
    storeLayout->addWidget(m_btnWalletInfo);
    storeLayout->addWidget(apiHintLbl);
    leftCol->addWidget(storeBox);

    // 3. Рефералка
    QFrame* refBox = new QFrame();
    refBox->setStyleSheet("background: rgba(59, 130, 246, 0.1); border-radius: 10px; border: 1px dashed #3b82f6;");
    QVBoxLayout* refLayout = new QVBoxLayout(refBox);
    refLayout->setContentsMargins(15, 15, 15, 15);

    m_refHeader = new QLabel(u8"🎁 <b>Пригласи друга (Осталось: Загрузка...)</b>");
    m_refHeader->setStyleSheet("color: #3b82f6; font-size: 14px; border: none; background: transparent;");
    m_refCodeLbl = new QLabel(u8"Твой код: Загрузка...");
    m_refCodeLbl->setStyleSheet("color: #e2e8f0; font-size: 13px; font-weight: bold; border: none; background: transparent;");
    m_refCodeLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);

    refLayout->addWidget(m_refHeader);
    refLayout->addWidget(m_refCodeLbl);
    leftCol->addWidget(refBox);

    // 4. VPN
    QFrame* vpnBox = new QFrame();
    vpnBox->setMinimumHeight(180);
    vpnBox->setStyleSheet("background: #1e293b; border-radius: 10px; border: 1px solid #334155;");
    QVBoxLayout* vpnLayout = new QVBoxLayout(vpnBox);
    vpnLayout->setContentsMargins(15, 15, 15, 15);

    QLabel* vpnHeader = new QLabel(u8"🛡️ <b>Storm VPN (Beta)</b>");
    vpnHeader->setStyleSheet("color: #56d39b; font-size: 14px; border: none; background: transparent;");

    m_vpnStatusLbl = new QLabel(u8"Статус: Загрузка...");
    m_vpnStatusLbl->setStyleSheet("color: #e2e8f0; font-size: 13px; border: none; background: transparent;");

    m_vpnTrafficLbl = new QLabel(u8"Трафик: 0 МБ / 5000 МБ");
    m_vpnTrafficLbl->setStyleSheet("color: #cbd5e1; font-size: 12px; border: none; background: transparent;");

    m_vpnProgress = new QProgressBar();
    m_vpnProgress->setFixedHeight(6);
    m_vpnProgress->setTextVisible(false);
    m_vpnProgress->setStyleSheet("QProgressBar { background: rgba(255,255,255,0.05); border: none; border-radius: 3px; } QProgressBar::chunk { background: #56d39b; border-radius: 3px; }");

    m_btnBuyVpn = new QPushButton(u8"Активировать VPN (390 SC)");
    m_btnBuyVpn->setMinimumHeight(35);
    m_btnBuyVpn->setCursor(Qt::PointingHandCursor);
    m_btnBuyVpn->setStyleSheet("QPushButton { padding: 8px; background: rgba(86, 211, 155, 0.1); color: #56d39b; border: 1px solid #56d39b; border-radius: 6px; font-weight: bold; font-size: 13px; } QPushButton:hover { background: rgba(86, 211, 155, 0.2); }");
    connect(m_btnBuyVpn, &QPushButton::clicked, this, &StormCloud::handleVpnClick);

    m_vpnFraudNote = new QLabel(u8"* Обновление пула адресов происходит раз в неделю.");
    m_vpnFraudNote->setStyleSheet("color: #cbd5e1; font-size: 10px; border: none; background: transparent;");

    vpnLayout->addWidget(vpnHeader);
    vpnLayout->addWidget(m_vpnStatusLbl);
    vpnLayout->addWidget(m_vpnTrafficLbl);
    vpnLayout->addWidget(m_vpnProgress);
    vpnLayout->addStretch(1);
    vpnLayout->addWidget(m_btnBuyVpn);
    vpnLayout->addWidget(m_vpnFraudNote);
    leftCol->addWidget(vpnBox);

    // --------- ПРАВАЯ КОЛОНКА ---------

    // 1. Соц. сети
    QFrame* socialBox = new QFrame();
    socialBox->setStyleSheet("background: #1e293b; border-radius: 10px; border: 1px solid #334155;");
    QVBoxLayout* socialLayout = new QVBoxLayout(socialBox);
    socialLayout->setContentsMargins(15, 18, 15, 18);

    QLabel* socialHeader = new QLabel(u8"👥 <b>Мы в соц. сетях</b>");
    socialHeader->setStyleSheet("color: #4facfe; font-size: 14px; border: none; background: transparent;");

    m_btnVk = new QPushButton(u8" Наше сообщество ВКонтакте");
    m_btnVk->setMinimumHeight(45);
    m_btnVk->setCursor(Qt::PointingHandCursor);
    m_btnVk->setStyleSheet("background: rgba(79, 172, 254, 0.1); color: #4facfe; border: 1px solid #4facfe; border-radius: 6px; font-weight: bold;");

    socialLayout->addWidget(socialHeader);
    socialLayout->addWidget(m_btnVk);
    rightCol->addWidget(socialBox);

    // 2. Beta-тест
    QHBoxLayout* betaLayout = new QHBoxLayout();
    m_betaBtn = new QPushButton(u8"🚀 Подать заявку на Beta-тест");
    m_betaBtn->setMinimumHeight(40);
    m_betaBtn->setCursor(Qt::PointingHandCursor);
    m_betaBtn->setStyleSheet("QPushButton { background-color: #22c55e; color: white; border-radius: 6px; padding: 8px; font-weight: bold; border: none; font-size: 13px; } QPushButton:hover { background-color: #16a34a; }");

    m_infoBtn = new QPushButton(u8"❓");
    m_infoBtn->setFixedSize(40, 40);
    m_infoBtn->setCursor(Qt::PointingHandCursor);
    m_infoBtn->setStyleSheet("QPushButton { background-color: #374151; color: #a371f7; border-radius: 20px; font-weight: bold; border: 2px solid #a371f7; font-size: 16px; }");
    connect(m_infoBtn, &QPushButton::clicked, this, &StormCloud::showBetaInfo);

    betaLayout->addWidget(m_betaBtn);
    betaLayout->addWidget(m_infoBtn);
    rightCol->addLayout(betaLayout);

    // 3. Синхронизация и Выход
    QFrame* syncBox = new QFrame();
    syncBox->setStyleSheet("background: #1e293b; border-radius: 10px; border: 1px solid #334155;");
    QVBoxLayout* syncBoxLayout = new QVBoxLayout(syncBox);
    syncBoxLayout->setContentsMargins(12, 12, 12, 12);
    syncBoxLayout->setSpacing(10);

    m_autoSyncBtn = new QPushButton(u8"🔄 Авто-синхронизация: ВЫКЛ");
    m_autoSyncBtn->setMinimumHeight(38);
    m_autoSyncBtn->setCheckable(true);
    m_autoSyncBtn->setCursor(Qt::PointingHandCursor);
    m_autoSyncBtn->setStyleSheet("QPushButton { padding: 10px; background: #0f172a; color: #e2e8f0; border: 1px solid #334155; border-radius: 8px; font-weight: bold; }");

    m_syncProgress = new QProgressBar();
    m_syncProgress->setFixedHeight(4);
    m_syncProgress->setTextVisible(false);
    m_syncProgress->setStyleSheet("QProgressBar { background: #0f172a; border: none; border-radius: 2px; } QProgressBar::chunk { background: #a371f7; border-radius: 2px; }");
    m_syncProgress->hide();

    m_btnSync = new QPushButton(u8"🔄 Синхронизировать сейчас");
    m_btnSync->setMinimumHeight(45);
    m_btnSync->setCursor(Qt::PointingHandCursor);
    m_btnSync->setStyleSheet("QPushButton { background: #a371f7; color: #fff; border-radius: 8px; font-weight: bold; font-size: 14px; border: none; } QPushButton:hover { background: #b180ff; }");

    QPushButton* btnLogout = new QPushButton(u8"🚪 Выйти из аккаунта");
    btnLogout->setMinimumHeight(38);
    btnLogout->setCursor(Qt::PointingHandCursor);
    btnLogout->setStyleSheet("QPushButton { color: #ff5f5f; background: transparent; border: 1px solid #334155; border-radius: 8px; font-weight: bold; font-size: 12px; } QPushButton:hover { background: rgba(255, 95, 95, 0.1); border-color: #ff5f5f; }");
    connect(btnLogout, &QPushButton::clicked, this, &StormCloud::logout);

    syncBoxLayout->addWidget(m_autoSyncBtn);
    syncBoxLayout->addWidget(m_syncProgress);
    syncBoxLayout->addWidget(m_btnSync);
    syncBoxLayout->addWidget(btnLogout);
    rightCol->addWidget(syncBox);
    rightCol->addStretch(1);

    columnsLayout->addLayout(leftCol, 1);
    columnsLayout->addLayout(rightCol, 1);

    mainLayout->addLayout(header);
    mainLayout->addLayout(columnsLayout);

    // ==========================================
    // 3. НИЖНЯЯ ПАНЕЛЬ
    // ==========================================
    QHBoxLayout* bottomHbox = new QHBoxLayout();
    bottomHbox->setContentsMargins(0, 10, 0, 0); // Небольшой отступ сверху

    QPushButton* btnClearCloud = new QPushButton(u8"🗑 Очистить облако");
    btnClearCloud->setFlat(true);
    btnClearCloud->setCursor(Qt::PointingHandCursor);
    btnClearCloud->setStyleSheet("QPushButton { color: #cbd5e1; font-size: 12px; background: transparent; border: none; } QPushButton:hover { color: #ff5f5f; text-decoration: underline; }");

    // Пружина растолкает кнопки по краям: "Очистить" влево, "Закрыть" вправо
    bottomHbox->addWidget(btnClearCloud);
    bottomHbox->addStretch();

    QPushButton* btnCloseFinal = new QPushButton(u8"Закрыть");
    btnCloseFinal->setCursor(Qt::PointingHandCursor);
    btnCloseFinal->setStyleSheet("QPushButton { padding: 8px 25px; background: #1e293b; color: #e2e8f0; border-radius: 6px; border: 1px solid #334155; font-size: 13px; font-weight: bold; } QPushButton:hover { background: rgba(255, 95, 95, 0.15); color: #ff5f5f; border-color: #ff5f5f; }");
    connect(btnCloseFinal, &QPushButton::clicked, this, &QDialog::reject);

    bottomHbox->addWidget(btnCloseFinal);

    mainLayout->addLayout(bottomHbox);

    return widget;
}

// ==========================================
// --- СЕТЕВАЯ ЛОГИКА ---
// ==========================================
void StormCloud::handleAuth() {
    QString user = m_usernameInput->text().trimmed();
    QString pwd = m_passwordInput->text().trimmed();
    QString inviteCode = m_inviteInput->isHidden() ? "" : m_inviteInput->text().trimmed();

    if (user.isEmpty() || pwd.isEmpty()) return;

    bool isRegister = (m_btnAuth->text() == u8"Зарегистрироваться");
    QString endpoint = isRegister ? "/register" : "/sync/pull";

    QJsonObject json;
    json["username"] = user;
    json["password"] = pwd;
    if (isRegister) {
        json["invite_code"] = inviteCode;
        json["hwid"] = "cpp_hwid_placeholder";
    }

    QNetworkRequest request(QUrl(m_serverUrl + endpoint));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_networkManager->post(request, QJsonDocument(json).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, user, pwd]() {
        onAuthReply(reply);
        if (reply->error() == QNetworkReply::NoError) {
            m_currentUser = user;
            m_currentPassword = pwd;
            m_settings.setValue("sync/username", user);
            m_settings.setValue("sync/password", pwd);
        }
        });
}

void StormCloud::onAuthReply(QNetworkReply* reply) {
    // Вывод логов для отладки
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qDebug() << "[NETWORK] HTTP Status Code:" << statusCode;
    qDebug() << "[NETWORK] Error String:" << reply->errorString();

    QByteArray responseData = reply->readAll();
    qDebug() << "[NETWORK] Server Answer:" << responseData;

    if (reply->error() == QNetworkReply::NoError) {
        m_settings.setValue("profile/is_logged_in", true);
        m_settings.setValue("profile/username", m_currentUser);
        updateProfileUi();
        animateTransition(m_manageWidget);
    }
    else {
        // Обработка ошибки
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
        QString errorMsg = u8"Ошибка соединения";

        if (jsonDoc.isObject() && jsonDoc.object().contains("detail")) {
            errorMsg = jsonDoc.object()["detail"].toString();
        }
        QMessageBox::critical(this, u8"Ошибка", errorMsg);
    }

    reply->deleteLater();
} // <--- ИМЕННО ЭТА СКОБКА РАНЬШЕ БЫЛА ПОТЕРЯНА

void StormCloud::checkServerOnline() {
    QNetworkRequest request(QUrl(m_serverUrl + "/ping"));
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onPingReply(reply);
        });
}

void StormCloud::onPingReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() == QNetworkReply::NoError) {
        m_statusDot->setStyleSheet("color: #2ecc71; font-size: 14px;");
        m_statusTextLabel->setText(u8"В сети");
    }
    else {
        m_statusDot->setStyleSheet("color: #ff5f5f; font-size: 14px;");
        m_statusTextLabel->setText(u8"Нет связи");
    }
}

// ==========================================
// --- УТИЛИТЫ ---
// ==========================================
void StormCloud::toggleMode() {
    if (m_btnAuth->text() == u8"Войти в аккаунт") {
        m_btnAuth->setText(u8"Зарегистрироваться");
        m_modeToggle->setText(u8"Уже есть аккаунт? Войти");
        m_inviteInput->show();
    }
    else {
        m_btnAuth->setText(u8"Войти в аккаунт");
        m_modeToggle->setText(u8"Нет аккаунта? Создать");
        m_inviteInput->hide();
        m_inviteInput->clear();
    }
}

void StormCloud::logout() {
    m_settings.remove("sync/username");
    m_settings.remove("sync/password");
    m_settings.setValue("profile/is_logged_in", false);
    m_settings.remove("profile/username");
    m_currentUser = "";
    m_currentPassword = "";
    animateTransition(m_loginWidget);
}

void StormCloud::animateTransition(QWidget* nextWidget) {
    // Анимация выезда виджета сбоку, как в Python[cite: 7]
    nextWidget->setGeometry(0, 0, 800, 700);
    QPropertyAnimation* anim = new QPropertyAnimation(nextWidget, "pos", this);
    anim->setDuration(400);
    anim->setStartValue(QPoint(800, 0));
    anim->setEndValue(QPoint(0, 0));
    anim->setEasingCurve(QEasingCurve::OutQuint);

    m_stack->setCurrentWidget(nextWidget);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// Логика перетаскивания безрамочного окна[cite: 7]
void StormCloud::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_oldPos = event->globalPosition().toPoint();
    }
}

void StormCloud::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        QPoint delta = event->globalPosition().toPoint() - m_oldPos;
        move(x() + delta.x(), y() + delta.y());
        m_oldPos = event->globalPosition().toPoint();
    }
}

void StormCloud::requestBetaAccess() {}

void StormCloud::updateProfileUi() {
    QString displayName = m_settings.value("sync/display_name", m_currentUser).toString();

    // Если пользователь Premium, можно добавить коронку
    if (m_isPremium) {
        m_userNameLabel->setText(displayName + u8" 👑");
    }
    else {
        m_userNameLabel->setText(displayName);
    }

    // Запускаем асинхронный запрос за актуальными балансами
    fetchBillingInfo();
}

void StormCloud::fetchBillingInfo() {
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;

    QNetworkRequest request(QUrl(m_serverUrl + "/api/billing/info"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_networkManager->post(request, QJsonDocument(json).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onBillingInfoReply(reply);
        });
}

void StormCloud::onBillingInfoReply(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
        QJsonObject data = jsonDoc.object();

        // 1. Балансы SC и Токенов
        int balance = data.value("balance").toInt(0);
        int tokens = data.value("tokens").toInt(0);
        m_lblHeaderSc->setText(QString::number(balance) + " SC");
        m_lblHeaderAi->setText(u8"🧠 " + QString::number(tokens) + u8" Токенов");

        // 2. Статус Premium
        m_isPremium = data.value("is_premium").toInt(0) == 1;
        QString untilDate = data.value("premium_until").toString();

        // Кэшируем в тот же QSettings-ключ, что и StormCloudBridge::fetchBillingInfo() —
        // это два независимых пути получения биллинга, но виджеты вроде TodoWidget
        // должны видеть актуальный статус вне зависимости от того, через какой из них
        // пользователь последний раз его обновлял.
        m_settings.setValue("billing/is_premium", m_isPremium);
        m_settings.setValue("billing/premium_until", untilDate);

        if (m_isPremium) {
            m_lblHeaderPrem->setText(u8"👑 Premium до: " + untilDate.left(10));
            m_lblHeaderPrem->setStyleSheet("color: #ffd700; font-weight: bold; font-size: 13px; background: transparent; border: none;");
        }
        else {
            m_lblHeaderPrem->setText(u8"Стандарт");
            m_lblHeaderPrem->setStyleSheet("color: #cbd5e1; font-size: 13px; background: transparent; border: none;");
        }

        // 3. Реферальная система
        int invites = data.value("invites_left").toInt(0);
        QString code = data.value("invite_code").toString();
        m_refHeader->setText(QString(u8"🎁 <b>Пригласи друга (Осталось инвайтов: %1/3)</b>").arg(invites));

        if (invites > 0 && !code.isEmpty()) {
            m_refCodeLbl->setText(u8"Твой код для друга: <b>" + code + "</b>");
        }
        else {
            m_refCodeLbl->setText(u8"Вы исчерпали лимит в 3 приглашения. Спасибо!");
        }

        // 4. VPN Статистика
        int vpnActive = data.value("vpn_active").toInt(0);
        double vpnTrafficMb = data.value("vpn_traffic").toDouble(0) / (1024.0 * 1024.0);

        m_vpnProgress->setValue(static_cast<int>(vpnTrafficMb));
        m_vpnTrafficLbl->setText(QString(u8"Трафик: %1 МБ / 5000 МБ").arg(vpnTrafficMb, 0, 'f', 1));

        if (vpnActive == 1) {
            m_vpnStatusLbl->setText(u8"Статус: Доступен (Подписка Активна) 🔑");
            m_btnBuyVpn->setText(u8"Включить Storm VPN ⚡");
        }
        else {
            m_vpnStatusLbl->setText(u8"Статус: Не активен 🔴");
            m_btnBuyVpn->setText(u8"🛍️ Купить VPN в Магазине");
        }
    }
    reply->deleteLater();
}

void StormCloud::showBetaInfo() {
    QString infoText = u8"Добро пожаловать в Экосистему Storm! 🌪️\n\n"
        u8"Как работает закрытая экономика браузера:\n"
        u8"1. Storm Coins (SC) — это наша внутренняя валюта для активных участников.\n"
        u8"2. SC используются для покупки AI-токенов, которые нужны для работы умной нейросети.\n"
        u8"3. Чтобы получить стартовый капитал SC бесплатно, нажмите кнопку «Подать заявку на Beta-тест».\n\n"
        u8"⚠️ ВАЖНОЕ УСЛОВИЕ:\n"
        u8"Все заявки проверяются вручную. Начисление тестовых SC происходит в течение 24 часов!\n\n"
        u8"4. Как только SC поступят на ваш баланс, вы сможете обменять их на AI-токены в Магазине.";
    QMessageBox::information(this, u8"Инструкция: Экосистема и ИИ", infoText);
}

void StormCloud::showWalletDialog() {
    // Безопасное создание окна без атрибута прозрачности
    QDialog dlg(this);
    dlg.setWindowTitle(u8"💳 Storm Кошелек");
    dlg.setFixedSize(400, 480);
    dlg.setStyleSheet("QDialog { background-color: #0f172a; color: #e2e8f0; }");

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    QLabel* title = new QLabel(u8"💳 <b>Storm Кошелек</b>");
    title->setStyleSheet("color: #56d39b; font-size: 18px;");
    layout->addWidget(title, 0, Qt::AlignCenter);

    // Достаем актуальный баланс из шапки ЛК
    QString currentBalance = m_lblHeaderSc->text().split(" ")[0];
    QLabel* balanceLbl = new QLabel(QString(u8"<h2 style='text-align:center; margin:0;'>Ваш баланс:</h2><h1 style='color:#56d39b; text-align:center; margin:5px 0;'>%1 SC</h1><p style='color:gray; font-size:11px; text-align:center;'>SC - Storm Coins (Виртуальная валюта)</p>").arg(currentBalance));
    layout->addWidget(balanceLbl);

    QPushButton* btnTopup = new QPushButton(u8"Пополнить счет");
    btnTopup->setCursor(Qt::PointingHandCursor);
    btnTopup->setStyleSheet("padding: 12px; background: #a371f7; color: white; border-radius: 8px; font-weight: bold; font-size: 14px; border: none;");
    connect(btnTopup, &QPushButton::clicked, &dlg, [&]() {
        QMessageBox::information(&dlg, u8"Пополнение", u8"Шлюз оплаты находится в разработке.\n\nВ рамках закрытого Beta-тестирования администратор начисляет Storm Coins вручную.");
        });
    layout->addWidget(btnTopup);

    // Блок подписки Premium
    if (m_isPremium) {
        QLabel* premLbl = new QLabel(u8"<h3 style='color:#ffd700; text-align:center;'>👑 Storm Premium (Активирован)</h3>");
        layout->addWidget(premLbl);
    }
    else {
        QLabel* premTitle = new QLabel(u8"👑 Оформить подписку Storm Premium:");
        premTitle->setStyleSheet("font-weight: bold; color: #fbbf24; margin-top: 15px;");
        layout->addWidget(premTitle, 0, Qt::AlignCenter);

        QHBoxLayout* premLayout = new QHBoxLayout();

        auto addPremBtn = [&](const QString& text, const QString& packId, const QString& color) {
            QPushButton* btn = new QPushButton(text);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(QString("background-color: %1; color: white; font-weight: bold; padding: 10px; border-radius: 8px; border: none;").arg(color));
            connect(btn, &QPushButton::clicked, this, [this, packId, &dlg]() { processPurchase(packId, &dlg); });
            premLayout->addWidget(btn);
            };

        addPremBtn(u8"1 Месяц\n149 SC", "pack_premium_1m", "#3b82f6");
        addPremBtn(u8"6 Месяцев\n790 SC", "pack_premium_6m", "#8b5cf6");
        addPremBtn(u8"1 Год\n1490 SC", "pack_premium_1y", "#10b981");
        layout->addLayout(premLayout);
    }

    layout->addStretch();
    QPushButton* btnClose = new QPushButton(u8"Закрыть");
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setStyleSheet("padding: 8px; border-radius: 6px; background: #1e293b; color: #e2e8f0; border: 1px solid #334155;");
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout->addWidget(btnClose);

    dlg.exec();
}

void StormCloud::showStoreDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(u8"🛒 Магазин Storm Cloud");
    dlg.setFixedSize(650, 580);
    dlg.setStyleSheet("QDialog { background-color: #0f172a; color: #e2e8f0; }");

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(10);

    // Заголовок и балансы
    QHBoxLayout* header = new QHBoxLayout();
    QLabel* balanceLbl = new QLabel(QString(u8"<h2>Ваш баланс: <span style='color:#56d39b;'>%1</span></h2>").arg(m_lblHeaderSc->text()));

    // Очищаем строку токенов от лишних символов для магазина
    QString tokensRaw = m_lblHeaderAi->text();
    tokensRaw.replace(u8"🧠 ", "");
    QLabel* tokensLbl = new QLabel(QString(u8"AI Токены: <b>%1</b>").arg(tokensRaw));
    tokensLbl->setStyleSheet("color: #a371f7; font-size: 15px;");

    header->addWidget(balanceLbl);
    header->addStretch();
    header->addWidget(tokensLbl);
    layout->addLayout(header);

    // Сетка товаров
    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(15);

    auto addProductCard = [&](const QString& title, const QString& desc, const QString& price, const QString& packId, const QString& color, int row, int col, bool isAvailable = true) {
        QFrame* card = new QFrame(&dlg);
        card->setStyleSheet(QString("background: #1e293b; border: 1px solid %1; border-radius: 10px;").arg(color));
        QVBoxLayout* cLayout = new QVBoxLayout(card);

        QLabel* tLbl = new QLabel(QString("<b style='color:%1; font-size:15px;'>%2</b>").arg(color, title));
        QLabel* dLbl = new QLabel(desc);
        dLbl->setStyleSheet("color: #cbd5e1; font-size: 12px; border: none;");
        dLbl->setWordWrap(true);

        // Если пакет недоступен, меняем текст кнопки и выключаем её
        QPushButton* btn = new QPushButton(isAvailable ? u8"Купить (" + price + " SC)" : u8"Недоступно");
        btn->setCursor(isAvailable ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
        btn->setStyleSheet(QString("background: %1; color: white; font-weight: bold; padding: 8px; border-radius: 6px; border: none; %2")
            .arg(color, isAvailable ? "" : "opacity: 0.5;")); // Делаем кнопку полупрозрачной, если она отключена
        btn->setEnabled(isAvailable);

        if (isAvailable) {
            connect(btn, &QPushButton::clicked, this, [this, packId, &dlg]() { processPurchase(packId, &dlg); });
        }

        cLayout->addWidget(tLbl);
        cLayout->addWidget(dLbl);
        cLayout->addStretch();
        cLayout->addWidget(btn);
        grid->addWidget(card, row, col);
        };

    // Обновляем вызовы: добавляем надпись и передаем false, чтобы отрубить покупку
    addProductCard(u8"Пакет «Мини» (в разработке)", u8"20 000 AI Токенов. Оптимально для быстрого теста функций.", "150", "pack_20k", "#ff79c6", 0, 0, false);
    addProductCard(u8"Пакет «Старт» (в разработке)", u8"50 000 AI Токенов. Базовый набор для работы.", "299", "pack_50k", "#a371f7", 0, 1, false);
    addProductCard(u8"Пакет «Оптимум» (в разработке)", u8"100 000 AI Токенов. Максимальный объем слов.", "590", "pack_100k", "#4facfe", 1, 0, false);

    // VPN оставляем полностью рабочим (isAvailable = true по умолчанию)
    addProductCard(u8"Storm VPN", u8"Защищенный доступ. Выделенный лимит 5 ГБ трафика.", "390", "pack_vpn_5gb", "#56d39b", 1, 1);

    layout->addLayout(grid);

    // Блок Premium тарифов внизу витрины
    QLabel* premTitle = new QLabel(u8"👑 Оформить подписку Storm Premium:");
    premTitle->setStyleSheet("font-weight: bold; color: #fbbf24; margin-top: 10px;");
    layout->addWidget(premTitle);

    QHBoxLayout* premiumLayout = new QHBoxLayout();
    auto addStorePremBtn = [&](const QString& text, const QString& packId, const QString& color) {
        QPushButton* btn = new QPushButton(text);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString("background-color: %1; color: white; padding: 8px; border-radius: 6px; font-weight: bold; border: none;").arg(color));
        connect(btn, &QPushButton::clicked, this, [this, packId, &dlg]() { processPurchase(packId, &dlg); });
        premiumLayout->addWidget(btn);
        };

    addStorePremBtn(u8"1 Месяц\n149 SC", "pack_premium_1m", "#3b82f6");
    addStorePremBtn(u8"6 Месяцев\n790 SC", "pack_premium_6m", "#8b5cf6");
    addStorePremBtn(u8"1 Год\n1490 SC", "pack_premium_1y", "#10b981");
    layout->addLayout(premiumLayout);

    layout->addStretch();

    // Кнопка закрытия
    QPushButton* btnClose = new QPushButton(u8"Закрыть витрину");
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setStyleSheet("padding: 10px; background: #ff5f5f; color: white; border-radius: 6px; font-weight: bold; border: none;");
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout->addWidget(btnClose);

    dlg.exec();
}

void StormCloud::processPurchase(const QString& packageId, QDialog* parentDlg) {
    QJsonObject json;
    json["username"] = m_currentUser;
    json["password"] = m_currentPassword;
    json["package_id"] = packageId;

    QNetworkRequest request(QUrl(m_serverUrl + "/api/billing/buy"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // Отправляем асинхронный запрос на покупку к серверу FastAPI[cite: 7]
    QNetworkReply* reply = m_networkManager->post(request, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply, parentDlg]() {
        if (reply->error() == QNetworkReply::NoError) {
            QMessageBox::information(parentDlg, u8"Успех", u8"🎉 Покупка прошла успешно!");

            // Если покупка успешна - закрываем витрину и обновляем ЛК
            if (parentDlg) {
                parentDlg->accept();
            }
            updateProfileUi();
        }
        else {
            // Читаем сообщение об ошибке с сервера (например, "Недостаточно средств")
            QByteArray responseData = reply->readAll();
            QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
            QString errorMsg = u8"Ошибка покупки";

            if (jsonDoc.isObject() && jsonDoc.object().contains("detail")) {
                errorMsg = jsonDoc.object()["detail"].toString();
            }
            QMessageBox::warning(parentDlg, u8"Отказ", errorMsg);
        }
        reply->deleteLater();
        });
}

void StormCloud::handleVpnClick() {
    // Проверяем текст на кнопке. Если там "Купить" — открываем витрину магазина.
    if (m_btnBuyVpn->text().contains(u8"Купить")) {
        showStoreDialog();
    }
    else {
        // Если VPN уже куплен, здесь позже будет логика запуска Xray-прокси
        QMessageBox::information(this, u8"Storm VPN", u8"Запуск туннеля Xray скоро будет добавлен.");
    }
}