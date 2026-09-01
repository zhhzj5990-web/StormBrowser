#include "VoiceChatWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QFrame>
#include <QMessageBox>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QDateTime>
#include <QKeyEvent>
#include <QDialog>
#include <QComboBox>
#include <QSlider>
#include <QDialogButtonBox>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QTextCursor>
#include <algorithm> // std::max
#include <cstdlib>   // std::abs(int)

// Qt Multimedia (Qt6). Для Qt5 замените QAudioSource/QAudioSink на
// QAudioInput/QAudioOutput и используйте QAudioDeviceInfo вместо QMediaDevices.
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QAudioDevice>

namespace {
    constexpr int kRate = 16000;
    constexpr int kChannels = 1;
    constexpr int kChunkBytes = 320 * 2; // 320 сэмплов * 2 байта (Int16), как CHUNK в Python
}

VoiceChatWidget::VoiceChatWidget(QWidget* parent) : QWidget(parent), micActive(false) {
    currentMicState = "off";

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(u8"<b>Чат (Голос + Текст)</b>", this));

    QSettings settings("Shtorm Software", "Storm Browser");
    QString savedIp = settings.value("chat/server_ip", "2.59.161.162").toString();
    QString savedPort = settings.value("chat/server_port", "55555").toString();

    vadThreshold = settings.value("chat/vad_threshold", 500).toInt();
    QString savedInputDesc = settings.value("chat/input_device").toString();
    QString savedOutputDesc = settings.value("chat/output_device").toString();
    if (!savedInputDesc.isEmpty()) {
        for (const QAudioDevice& dev : QMediaDevices::audioInputs()) {
            if (dev.description() == savedInputDesc) {
                selectedInputDevice = dev;
                hasCustomInputDevice = true;
                break;
            }
        }
    }
    if (!savedOutputDesc.isEmpty()) {
        for (const QAudioDevice& dev : QMediaDevices::audioOutputs()) {
            if (dev.description() == savedOutputDesc) {
                selectedOutputDevice = dev;
                hasCustomOutputDevice = true;
                break;
            }
        }
    }

    QHBoxLayout* serverLayout = new QHBoxLayout();
    ipInput = new QLineEdit(savedIp, this);
    ipInput->setPlaceholderText("IP или домен");
    portInput = new QLineEdit(savedPort, this);
    portInput->setPlaceholderText(u8"Порт");
    portInput->setFixedWidth(50);

    serverStatusLight = new QLabel(u8"⚪", this);
    serverLayout->addWidget(new QLabel(u8"🌐", this));
    serverLayout->addWidget(ipInput);
    serverLayout->addWidget(new QLabel(":", this));
    serverLayout->addWidget(portInput);
    serverLayout->addWidget(serverStatusLight);
    settingsBtn = new QPushButton(u8"⚙", this);
    settingsBtn->setFixedSize(30, 30);
    serverLayout->addWidget(settingsBtn);
    layout->addLayout(serverLayout);

    QFrame* line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet("background-color: #333; max-height: 1px; margin-top: 5px; margin-bottom: 5px;");
    layout->addWidget(line2);

    QHBoxLayout* authLayout = new QHBoxLayout();
    nickInput = new QLineEdit(this);
    nickInput->setPlaceholderText(u8"Никнейм");
    roomInput = new QLineEdit(u8"Гостиная", this);
    roomInput->setPlaceholderText(u8"Комната");
    authLayout->addWidget(nickInput);
    authLayout->addWidget(roomInput);
    layout->addLayout(authLayout);

    statusLabel = new QLabel(u8"Отключен 🔴", this);
    statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(statusLabel);

    QHBoxLayout* connLayout = new QHBoxLayout();
    connectBtn = new QPushButton(u8"Войти", this);
    connectBtn->setStyleSheet("background-color: #56d39b; color: #000; border-radius: 6px; padding: 6px; font-weight: bold;");
    disconnectBtn = new QPushButton(u8"Выйти", this);
    disconnectBtn->setStyleSheet("background-color: #ff5f5f; color: #fff; border-radius: 6px; padding: 6px; font-weight: bold;");
    disconnectBtn->setEnabled(false);
    connLayout->addWidget(connectBtn);
    connLayout->addWidget(disconnectBtn);
    layout->addLayout(connLayout);

    micBtn = new QPushButton(u8"🎤 Включить микрофон", this);
    micBtn->setEnabled(false);
    updateMicButtonUi("off");
    layout->addWidget(micBtn);

    // --- Участники комнаты (заполняется из "joined"/"join"/"leave"/
    // "mic_status"/"status_update", см. updateParticipantsList()) ---
    layout->addWidget(new QLabel(u8"👥 Участники:", this));
    participantsList = new QListWidget(this);
    participantsList->setFixedHeight(70);
    participantsList->setStyleSheet("QListWidget { border: 1px solid #333; border-radius: 4px; }");
    layout->addWidget(participantsList);

    // --- Баннер закреплённого сообщения (см. "pin"/"unpin" в handlePacket()) ---
    pinnedBannerBox = new QFrame(this);
    pinnedBannerBox->setStyleSheet("background-color: #2a2a2a; border-radius: 6px;");
    QHBoxLayout* pinnedLayout = new QHBoxLayout(pinnedBannerBox);
    pinnedLayout->setContentsMargins(8, 4, 4, 4);
    pinnedBannerText = new QLabel(pinnedBannerBox);
    pinnedBannerText->setWordWrap(true);
    pinnedUnpinBtn = new QPushButton(u8"✕", pinnedBannerBox);
    pinnedUnpinBtn->setFixedSize(22, 22);
    pinnedUnpinBtn->setToolTip(u8"Открепить сообщение");
    pinnedLayout->addWidget(pinnedBannerText, 1);
    pinnedLayout->addWidget(pinnedUnpinBtn);
    pinnedBannerBox->setVisible(false);
    layout->addWidget(pinnedBannerBox);

    chatDisplay = new QTextBrowser(this);
    chatDisplay->setPlaceholderText(u8"Здесь появятся сообщения...");
    chatDisplay->setMinimumHeight(400);
    // Ссылки внутри сообщений — служебные (см. rebuildChatDisplay()): "msg:id"
    // только для правого клика (showChatContextMenu()), "stormfile:..." — для
    // скачивания файла (onChatAnchorClicked()). Штатную навигацию по ним
    // отключаем и обрабатываем сами.
    chatDisplay->setOpenLinks(false);
    chatDisplay->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(chatDisplay, 1);

    typingLabel = new QLabel(this);
    typingLabel->setStyleSheet("color: #888; font-style: italic;");
    typingLabel->setVisible(false);

    QHBoxLayout* msgRow = new QHBoxLayout();
    msgInput = new QTextEdit(this);
    msgInput->setPlaceholderText(u8"Напишите сообщение...");
    msgInput->setFixedHeight(45);
    msgInput->setEnabled(false);
    fileBtn = new QPushButton(u8"📎", this);
    fileBtn->setFixedSize(45, 45);
    fileBtn->setEnabled(false);
    fileBtn->setToolTip(u8"Отправить файл");
    msgRow->addWidget(msgInput, 1);
    msgRow->addWidget(fileBtn);
    layout->addLayout(msgRow);
    layout->addWidget(typingLabel);

    connect(connectBtn, &QPushButton::clicked, this, &VoiceChatWidget::startChat);
    connect(disconnectBtn, &QPushButton::clicked, this, &VoiceChatWidget::stopChat);
    connect(micBtn, &QPushButton::clicked, this, &VoiceChatWidget::toggleMic);
    connect(ipInput, &QLineEdit::textChanged, this, &VoiceChatWidget::triggerPing);
    connect(portInput, &QLineEdit::textChanged, this, &VoiceChatWidget::triggerPing);
    connect(settingsBtn, &QPushButton::clicked, this, &VoiceChatWidget::openSettings);
    connect(fileBtn, &QPushButton::clicked, this, &VoiceChatWidget::sendFile);
    connect(msgInput, &QTextEdit::textChanged, this, &VoiceChatWidget::onMsgTextChanged);
    connect(chatDisplay, &QTextBrowser::customContextMenuRequested, this, &VoiceChatWidget::showChatContextMenu);
    connect(chatDisplay, &QTextBrowser::anchorClicked, this, &VoiceChatWidget::onChatAnchorClicked);
    connect(pinnedUnpinBtn, &QPushButton::clicked, this, [this]() {
        if (!connected) return;
        QVariantMap pkt;
        pkt["type"] = "unpin";
        pkt["room"] = room;
        pkt["username"] = username;
        sendPacket(pkt);
        });

    // --- Ping (проверка доступности сервера) ---
    pingSocket = new QTcpSocket(this);
    connect(pingSocket, &QTcpSocket::connected, this, &VoiceChatWidget::onPingConnected);
    connect(pingSocket, &QTcpSocket::errorOccurred, this, &VoiceChatWidget::onPingError);

    // БАГ (индикатор 🟢/🔴 не обновлялся сам по себе): раньше checkServerStatus()
    // вызывался только один раз при старте (см. singleShot ниже) и при ручном
    // изменении IP/порта (triggerPing()) — если сервер выключался в любой другой
    // момент, лампочка навсегда "застревала" в последнем известном состоянии.
    // Добавлен периодический опрос раз в 5 секунд.
    serverCheckTimer = new QTimer(this);
    serverCheckTimer->setInterval(5000);
    connect(serverCheckTimer, &QTimer::timeout, this, &VoiceChatWidget::checkServerStatus);
    serverCheckTimer->start();

    // --- Основной чат-сокет (TCP) ---
    chatSocket = new QTcpSocket(this);
    connect(chatSocket, &QTcpSocket::connected, this, &VoiceChatWidget::onTcpConnected);
    connect(chatSocket, &QTcpSocket::readyRead, this, &VoiceChatWidget::onTcpReadyRead);
    connect(chatSocket, &QTcpSocket::disconnected, this, &VoiceChatWidget::onTcpDisconnected);
    connect(chatSocket, &QTcpSocket::errorOccurred, this, &VoiceChatWidget::onTcpError);

    // --- UDP (голос) ---
    udpSocket = new QUdpSocket(this);
    connect(udpSocket, &QUdpSocket::readyRead, this, &VoiceChatWidget::onUdpReadyRead);

    // Отправка по Enter (без Shift), аналог ChatInput.keyPressEvent в Python
    msgInput->installEventFilter(this);

    // Раз в секунду скрываем ники, от которых давно не было нового события
    // "typing" (сервер не шлёт отдельное "перестал печатать" — см. handlePacket()).
    typingHideTimer = new QTimer(this);
    typingHideTimer->setInterval(1000);
    connect(typingHideTimer, &QTimer::timeout, this, &VoiceChatWidget::onTypingTimerTick);
    typingHideTimer->start();

    QTimer::singleShot(500, this, &VoiceChatWidget::checkServerStatus);
}

VoiceChatWidget::~VoiceChatWidget() {
    stopAudio();
}

// =================== Ping / статус сервера ===================

void VoiceChatWidget::triggerPing() {
    serverStatusLight->setText(u8"⚪");
    QTimer::singleShot(300, this, &VoiceChatWidget::checkServerStatus);
}

void VoiceChatWidget::checkServerStatus() {
    if (connected) {
        updateServerLight(true);
        return;
    }
    QString ip = ipInput->text().trimmed();
    int port = portInput->text().toInt();
    if (ip.isEmpty() || port == 0) {
        updateServerLight(false);
        return;
    }
    pingSocket->abort();
    pingSocket->connectToHost(ip, static_cast<quint16>(port));
}

void VoiceChatWidget::onPingConnected() {
    updateServerLight(true);
    pingSocket->disconnectFromHost();
}

void VoiceChatWidget::onPingError() {
    updateServerLight(false);
}

void VoiceChatWidget::updateServerLight(bool isOnline) {
    serverStatusLight->setText(isOnline ? u8"🟢" : u8"🔴");
}

// =================== Подключение к чату ===================

void VoiceChatWidget::startChat() {
    QString nick = nickInput->text().trimmed();
    QString r = roomInput->text().trimmed();
    QString ip = ipInput->text().trimmed();
    QString portStr = portInput->text().trimmed();

    if (nick.isEmpty() || r.isEmpty() || ip.isEmpty() || portStr.isEmpty()) {
        QMessageBox::warning(this, u8"Ошибка", u8"Заполните все поля (IP, Порт, Никнейм, Комната)!");
        return;
    }

    QSettings settings("Shtorm Software", "Storm Browser");
    settings.setValue("chat/server_ip", ip);
    settings.setValue("chat/server_port", portStr);

    username = nick;
    room = r;
    serverHost = ip;
    serverPort = static_cast<quint16>(portStr.toInt());

    haveLen = false;
    recvBuffer.clear();
    udpToken.clear();
    usersInfo.clear();
    messages.clear();
    messageIndexById.clear();
    pinnedMessageId = -1;
    pinnedMessageUser.clear();
    pinnedMessageText.clear();
    typingUntilMs.clear();
    lastTypingSentMs = 0;
    crypto.clearRoomKey(); // ключ ПРЕДЫДУЩЕЙ комнаты/сессии не должен использоваться в новой (см. CryptoManager::clearRoomKey())

    chatDisplay->clear();
    participantsList->clear();
    pinnedBannerBox->setVisible(false);
    typingLabel->setVisible(false);
    statusLabel->setText(u8"Подключение... ⏳");

    chatSocket->abort();
    chatSocket->connectToHost(serverHost, serverPort);
}

void VoiceChatWidget::onTcpConnected() {
    // ВАЖНО: TCP-соединение установилось — это ещё не значит, что сервер принял
    // нас в комнату (например, ник мог быть занят). Раньше именно тут UI сразу
    // переключался в "Подключен" и включал элементы управления. Если сервер
    // сразу после этого разрывал соединение (см. handle_client() в server.py),
    // пользователь видел "Подключен", а через мгновение — "Отключен", и без
    // единого объяснения. Теперь UI переключается в подключенное состояние
    // только по факту получения от сервера пакета "joined" (см. handlePacket()).

    // Привязываем UDP к тому же локальному адресу, что и TCP, как в Python (bind перед join)
    udpSocket->bind(QHostAddress::AnyIPv4, 0);

    QVariantMap join;
    join["type"] = "join";
    join["username"] = username;
    join["room"] = room;
    sendPacket(join);

    connectBtn->setEnabled(false);
    statusLabel->setText(u8"Авторизация... ⏳");
}

void VoiceChatWidget::onTcpDisconnected() {
    connected = false;
    // Если разрыв соединения инициирован нами (stopChat()/onTcpError() уже
    // выполняются), не запускаем stopChat() повторно — раньше это определялось
    // хрупким сравнением текста statusLabel, что могло приводить к дублированию
    // сообщений/сбросу UI при синхронном сигнале disconnected() внутри abort().
    if (!stoppingChat) {
        stopChat();
    }
}

void VoiceChatWidget::onTcpError(QAbstractSocket::SocketError) {
    if (stoppingChat) return; // уже останавливаем чат, не дублируем сообщение об ошибке
    appendMessage(u8"<span style='color: #ff5f5f;'>Ошибка соединения: " + chatSocket->errorString() + "</span>");
    statusLabel->setText(u8"Ошибка 🔴");
    stopChat();
}

void VoiceChatWidget::stopChat() {
    if (stoppingChat) return; // защита от реентерабельного вызова
    stoppingChat = true;

    connected = false;
    stopAudio();
    udpSocket->close();

    // Блокируем сигналы на время принудительного разрыва: abort() может
    // синхронно эмитить disconnected()/errorOccurred(), что без блокировки
    // привело бы к повторному входу в stopChat() ещё до выхода из этой функции.
    chatSocket->blockSignals(true);
    chatSocket->abort();
    chatSocket->blockSignals(false);

    resetUi();
    participantsList->clear();
    pinnedBannerBox->setVisible(false);
    typingLabel->setVisible(false);
    typingUntilMs.clear();
    if (roomKeyTimeoutTimer) roomKeyTimeoutTimer->stop();
    statusLabel->setText(u8"Отключен 🔴");
    appendMessage(u8"<span style='color: #ff5f5f;'><b>Вы отключились от чата.</b></span>");

    stoppingChat = false;

    // Пока шёл чат, checkServerStatus() всегда рано возвращал "онлайн" (см. её
    // начало: if (connected) return true) без реального пинга — так что сразу
    // после выхода лампочка ещё показывает старое "🟢". Перепроверяем сейчас же,
    // не дожидаясь следующего тика serverCheckTimer (до 5с).
    checkServerStatus();
}

void VoiceChatWidget::resetUi() {
    connectBtn->setEnabled(true);
    disconnectBtn->setEnabled(false);
    micBtn->setEnabled(false);
    msgInput->setEnabled(false);
    fileBtn->setEnabled(false);
    micActive = false;
    updateMicButtonUi("off");
    nickInput->setEnabled(true);
    roomInput->setEnabled(true);
    ipInput->setEnabled(true);
    portInput->setEnabled(true);
}

// =================== TCP протокол: 4 байта длины + JSON ===================
// Идентично send_packet()/receive_loop() в voice_chat.py

void VoiceChatWidget::sendPacket(const QVariantMap& data) {
    if (chatSocket->state() != QAbstractSocket::ConnectedState) return;
    QJsonDocument doc = QJsonDocument::fromVariant(data);
    QByteArray json = doc.toJson(QJsonDocument::Compact);

    QByteArray header(4, 0);
    quint32 len = static_cast<quint32>(json.size());
    header[0] = static_cast<char>((len >> 24) & 0xFF);
    header[1] = static_cast<char>((len >> 16) & 0xFF);
    header[2] = static_cast<char>((len >> 8) & 0xFF);
    header[3] = static_cast<char>(len & 0xFF);

    chatSocket->write(header + json);
}

void VoiceChatWidget::onTcpReadyRead() {
    recvBuffer.append(chatSocket->readAll());

    while (true) {
        if (!haveLen) {
            if (recvBuffer.size() < 4) return;
            const uchar* b = reinterpret_cast<const uchar*>(recvBuffer.constData());
            expectedLen = (quint32(b[0]) << 24) | (quint32(b[1]) << 16) | (quint32(b[2]) << 8) | quint32(b[3]);

            // Защита от переполнения (лимит 10 МБ на пакет)
            if (expectedLen > 10485760) {
                haveLen = false;
                recvBuffer.clear();
                chatSocket->disconnectFromHost();
                return;
            }

            recvBuffer.remove(0, 4);
            haveLen = true;
        }
        if (static_cast<quint32>(recvBuffer.size()) < expectedLen) return;

        QByteArray packet = recvBuffer.left(expectedLen);
        recvBuffer.remove(0, expectedLen);
        haveLen = false;

        handlePacket(packet);
    }
}

void VoiceChatWidget::handlePacket(const QByteArray& json) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    QJsonObject msg = doc.object();
    QString mtype = msg.value("type").toString();

    if (mtype == "udp_token") {
        // Сервер присылает токен для UDP ОТДЕЛЬНЫМ пакетом, до пакета "joined"
        // (см. handle_client() в server.py: send_to_client(..., {"type":"udp_token", ...})
        // происходит раньше send_to_client(..., {"type":"joined", ...})).
        // Раньше клиент читал поле "udp_token" из пакета "joined", где его
        // никогда не было — udpToken всегда оставался пустым, и голос по факту
        // не работал (см. onMicDataReady(): "if (!micActive || udpToken.isEmpty()) return;").
        udpToken = msg.value("token").toString().toUtf8();
    }
    else if (mtype == "joined") {
        // Настоящее подтверждение входа сервером — только теперь считаем себя
        // подключенными и включаем UI чата/микрофона.
        connected = true;

        // Сервер присылает полный список участников комнаты прямо в пакете
        // "joined" (поле "users", см. build_users_list() в server.py) —
        // элементы вида {"user": ..., "rank": ..., "mic": ..., "status": ...}.
        usersInfo.clear();
        for (const QJsonValue& v : msg.value("users").toArray()) {
            QJsonObject u = v.toObject();
            QString user = u.value("user").toString();
            if (user.isEmpty()) continue;
            ParticipantInfo info;
            info.rank = u.value("rank").toString(u8"Player");
            info.mic = u.value("mic").toBool();
            info.status = u.value("status").toString(u8"online");
            usersInfo[user] = info;
        }
        updateParticipantsList();

        // Только Host (сервер выдаёт этот ранг единственному участнику,
        // вошедшему в пустую комнату — см. server.py: `rank = "Host" if
        // len(rooms[room]) == 0 else "Player"`) имеет право самостоятельно
        // сгенерировать ключ комнаты, если никто не ответит на запрос
        // (см. onRoomKeyTimeout()). Остальные при таймауте просто
        // повторяют запрос, а не генерируют свой ключ.
        isRoomKeyOwner = (usersInfo.value(username).rank == QStringLiteral("Host"));
        roomKeyRequestAttempts = 0;

        disconnectBtn->setEnabled(true);
        micBtn->setEnabled(true);
        msgInput->setEnabled(true);
        fileBtn->setEnabled(true);
        nickInput->setEnabled(false);
        roomInput->setEnabled(false);
        ipInput->setEnabled(false);
        portInput->setEnabled(false);

        statusLabel->setText(u8"Подключен 🟢");
        updateServerLight(true);
        startAudio();

        // Обмен ключами: клиент генерирует RSA-пару и отправляет публичный ключ серверу
        if (crypto.generateRsaKeys()) {
            QVariantMap rsaRequest;
            rsaRequest["type"] = "request_room_key";
            rsaRequest["public_key"] = QString::fromUtf8(crypto.getPublicKeyPem());
            rsaRequest["room"] = room;
            sendPacket(rsaRequest);

            // Сервер только пересылает request_room_key/room_key_response между
            // клиентами (сам ключей не хранит, см. server.py) — то есть ответить
            // может ТОЛЬКО кто-то из уже подключённых, у кого ключ уже есть. Если
            // мы первый/единственный участник комнаты — отвечать некому, и без
            // этого таймера ключ не появился бы никогда (см. onRoomKeyTimeout()).
            if (!roomKeyTimeoutTimer) {
                roomKeyTimeoutTimer = new QTimer(this);
                roomKeyTimeoutTimer->setSingleShot(true);
                connect(roomKeyTimeoutTimer, &QTimer::timeout, this, &VoiceChatWidget::onRoomKeyTimeout);
            }
            roomKeyTimeoutTimer->start(4000);
        }
        else {
            appendMessage(u8"<span style='color: #ff5f5f;'>⚠️ Не удалось сгенерировать ключи шифрования. "
                u8"Сообщения в этой сессии не будут защищены сквозным шифрованием.</span>");
        }
    }
    // Получение зашифрованного ключа комнаты от сервера
    else if (mtype == "room_key_response") {
        QByteArray encryptedRoomKey = QByteArray::fromBase64(msg.value("encrypted_key").toString().toUtf8());
        QByteArray fernetKey = crypto.decryptRsa(encryptedRoomKey);
        if (fernetKey.isEmpty()) {
            appendMessage(u8"<span style='color: #ff5f5f;'>⚠️ Не удалось расшифровать ключ комнаты. "
                u8"E2EE не активировано.</span>");
        }
        else {
            if (roomKeyTimeoutTimer) roomKeyTimeoutTimer->stop(); // ключ пришёл вовремя — фолбэк не нужен
            crypto.setRoomKey(fernetKey);
            appendMessage(u8"<span style='color: #56d39b;'><i>🔒 Сквозное шифрование (E2EE) активировано.</i></span>");
        }
    }
    // Кто-то ЕЩЁ входит в комнату и просит у нас ключ. Раньше клиент вообще не
    // слушал этот тип пакета (только отправлял его сам при своём входе и ждал
    // ответ) — то есть даже участник, у которого ключ уже был, никогда не
    // отвечал новеньким, и ключ комнаты у новых людей не устанавливался
    // практически никогда.
    else if (mtype == "request_room_key") {
        if (!crypto.hasRoomKey()) return; // самим нечем поделиться — пусть ответит кто-то другой (или сработает его собственный таймаут)
        QByteArray pubKeyPem = msg.value("public_key").toString().toUtf8();
        if (pubKeyPem.isEmpty()) return;

        QByteArray encrypted = crypto.encryptRsaWithPublicKey(pubKeyPem, crypto.getRoomKeyBase64Url());
        if (encrypted.isEmpty()) return;

        QVariantMap pkt;
        pkt["type"] = "room_key_response";
        pkt["encrypted_key"] = QString::fromLatin1(encrypted.toBase64());
        pkt["room"] = room;
        sendPacket(pkt);
    }
    else if (mtype == "text") {
        QString user = msg.value("user").toString("?");
        QByteArray rawEncryptedText = msg.value("msg").toString().toUtf8();

        // РАСШИФРОВЫВАЕМ СООБЩЕНИЕ
        QString text = crypto.decryptFernet(rawEncryptedText);
        if (text.isEmpty() && !rawEncryptedText.isEmpty()) {
            text = u8"⚠️ [не удалось расшифровать сообщение]";
        }

        QString tm = msg.value("time").toString(QDateTime::currentDateTime().toString("HH:mm"));

        // "id" нужен, чтобы дальше можно было отредактировать/удалить/лайкнуть
        // реакцией/закрепить именно это сообщение (см. showChatContextMenu()).
        ChatMessageRecord rec;
        rec.id = msg.value("id").toInt(-1);
        rec.user = user;
        rec.time = tm;
        rec.text = text;
        pushMessageRecord(rec);
        rebuildChatDisplay();
    }
    else if (mtype == "history") {
        // защита от падения, если массив отсутствует
        if (!msg.contains("messages") || !msg.value("messages").isArray()) return;

        for (const QJsonValue& v : msg.value("messages").toArray()) {
            QJsonArray item = v.toArray();
            if (item.size() != 4) continue;
            QString user = item[0].toString();

            // РАСШИФРОВЫВАЕМ ИСТОРИЮ
            QByteArray rawEncryptedText = item[1].toString().toUtf8();
            QString text = crypto.decryptFernet(rawEncryptedText);
            if (text.isEmpty() && !rawEncryptedText.isEmpty()) {
                text = u8"⚠️ [не удалось расшифровать сообщение]";
            }

            QString tm = item[2].toString();
            // Порядок полей — (user, message, time, id), см. server.py:
            // "messages": [(u, text, tm, mid) for (mid, u, text, tm) in history_rows].
            int id = item[3].toInt(-1);

            ChatMessageRecord rec;
            rec.id = id;
            rec.user = user;
            rec.time = tm;
            rec.text = text;
            pushMessageRecord(rec);
        }
        rebuildChatDisplay();
    }
    // Полный набор реакций комнаты, присылается один раз сразу после "history"
    // при входе (см. server.py: send_to_client(..., {"type":"all_reactions", ...})).
    else if (mtype == "all_reactions") {
        QJsonObject allReactions = msg.value("reactions").toObject();
        bool any = false;
        for (auto it = allReactions.begin(); it != allReactions.end(); ++it) {
            bool ok = false;
            int id = it.key().toInt(&ok);
            if (!ok) continue;
            int idx = messageIndexById.value(id, -1);
            if (idx < 0 || idx >= messages.size()) continue;

            QMap<QString, QStringList> reacts;
            QJsonObject robj = it.value().toObject();
            for (auto rit = robj.begin(); rit != robj.end(); ++rit) {
                QStringList users;
                for (const QJsonValue& uv : rit.value().toArray()) users << uv.toString();
                if (!users.isEmpty()) reacts[rit.key()] = users;
            }
            messages[idx].reactions = reacts;
            any = true;
        }
        if (any) rebuildChatDisplay();
    }
    // Изменение реакций на ОДНО сообщение (после клика по эмодзи в контекстном
    // меню — см. showChatContextMenu()); сервер шлёт это всем в комнате,
    // включая отправителя, так что свою же реакцию отдельно не рисуем.
    else if (mtype == "reaction_update") {
        int id = msg.value("target_id").toInt(-1);
        int idx = messageIndexById.value(id, -1);
        if (idx < 0 || idx >= messages.size()) return;

        QMap<QString, QStringList> reacts;
        QJsonObject robj = msg.value("reactions").toObject();
        for (auto it = robj.begin(); it != robj.end(); ++it) {
            QStringList users;
            for (const QJsonValue& uv : it.value().toArray()) users << uv.toString();
            if (!users.isEmpty()) reacts[it.key()] = users;
        }
        messages[idx].reactions = reacts;
        rebuildChatDisplay();
    }
    else if (mtype == "edit") {
        int id = msg.value("id").toInt(-1);
        int idx = messageIndexById.value(id, -1);
        if (idx < 0 || idx >= messages.size()) return;

        QByteArray raw = msg.value("new").toString().toUtf8();
        QString text = crypto.decryptFernet(raw);
        if (text.isEmpty() && !raw.isEmpty()) text = u8"⚠️ [не удалось расшифровать сообщение]";

        messages[idx].text = text;
        messages[idx].edited = true;
        rebuildChatDisplay();

        if (pinnedMessageId == id) { // закреплённое сообщение тоже могли отредактировать
            pinnedMessageText = text;
            updatePinnedBanner();
        }
    }
    else if (mtype == "delete") {
        int id = msg.value("id").toInt(-1);
        int idx = messageIndexById.value(id, -1);
        if (idx < 0 || idx >= messages.size()) return;

        messages[idx].deleted = true;
        messages[idx].reactions.clear();
        rebuildChatDisplay();

        if (pinnedMessageId == id) {
            pinnedMessageId = -1;
            pinnedMessageUser.clear();
            pinnedMessageText.clear();
            updatePinnedBanner();
        }
    }
    else if (mtype == "pin") {
        QJsonObject m = msg.value("msg").toObject();
        QByteArray raw = m.value("text").toString().toUtf8();
        QString text = crypto.decryptFernet(raw);
        if (text.isEmpty() && !raw.isEmpty()) text = u8"⚠️ [не удалось расшифровать сообщение]";

        pinnedMessageId = m.value("id").toInt(-1);
        pinnedMessageUser = m.value("user").toString();
        pinnedMessageText = text;
        updatePinnedBanner();
    }
    else if (mtype == "unpin") {
        pinnedMessageId = -1;
        pinnedMessageUser.clear();
        pinnedMessageText.clear();
        updatePinnedBanner();
    }
    else if (mtype == "mic_status") {
        QString user = msg.value("user").toString();
        if (user.isEmpty()) return;
        usersInfo[user].mic = msg.value("mic").toBool();
        updateParticipantsList();
    }
    else if (mtype == "status_update") {
        QString user = msg.value("user").toString();
        if (user.isEmpty()) return;
        usersInfo[user].status = msg.value("status").toString(u8"online");
        updateParticipantsList();
    }
    else if (mtype == "stats") {
        int online = msg.value("online").toInt(-1);
        if (online >= 0 && connected) {
            statusLabel->setText(QString(u8"Подключен 🟢 · %1 онлайн").arg(online));
        }
    }
    else if (mtype == "typing") {
        QString user = msg.value("user").toString();
        if (user.isEmpty() || user == username) return;
        // Сервер не шлёт отдельное событие "перестал печатать" — индикатор
        // сам гаснет через 3с без нового "typing" от этого ника (см.
        // onTypingTimerTick(), тикает раз в секунду).
        typingUntilMs[user] = QDateTime::currentMSecsSinceEpoch() + 3000;
        onTypingTimerTick();
    }
    else if (mtype == "file_offer") {
        QString fileId = msg.value("file_id").toString();
        QString filename = msg.value("filename").toString();
        QString user = msg.value("user").toString();
        if (fileId.isEmpty() || filename.isEmpty()) return;
        addFileOffer(user, filename, fileId);
    }
    else if (mtype == "file_data") {
        QString filename = msg.value("filename").toString();
        if (filename.isEmpty()) filename = pendingDownloadFilename;
        QByteArray data = QByteArray::fromBase64(msg.value("data").toString().toUtf8());

        QString savePath = QFileDialog::getSaveFileName(this, u8"Сохранить файл", filename);
        if (savePath.isEmpty()) return;

        QFile f(savePath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(data);
            f.close();
            appendMessage(QString(u8"<span style='color:#56d39b;'>✅ Файл «%1» сохранён.</span>")
                .arg(filename.toHtmlEscaped()));
        }
        else {
            appendMessage(QString(u8"<span style='color:#ff5f5f;'>⚠️ Не удалось сохранить файл «%1».</span>")
                .arg(filename.toHtmlEscaped()));
        }
    }
    // Список участников комнаты заполняется из массива "users" в пакете
    // "joined" (см. выше) при входе, а дальше обновляется этими двумя
    // ветками. Раньше здесь стояли типы "user_list"/"user_joined"/"user_left"
    // с полем "username" — сервер такие вообще не шлёт. В server.py эти
    // события называются "join"/"leave" и несут поле "user" (см.
    // broadcast(room, {"type": "join", "user": ...}, ...) и
    // broadcast(room, {"type": "leave", "user": ...}) в handle_client()) —
    // из-за несовпадения имён вход/выход других участников никак не отражался
    // в usersInfo.
    else if (mtype == "join") {
        QString user = msg.value("user").toString();
        if (user.isEmpty()) return;
        ParticipantInfo info;
        info.rank = msg.value("rank").toString(u8"Player");
        info.mic = msg.value("mic").toBool();
        info.status = msg.value("status").toString(u8"online");
        usersInfo[user] = info;
        updateParticipantsList();
        appendMessage(QString(u8"<span style='color:#888;'><i>%1 присоединился к комнате</i></span>")
            .arg(user.toHtmlEscaped()));
    }
    else if (mtype == "leave") {
        QString user = msg.value("user").toString();
        if (user.isEmpty()) return;
        usersInfo.remove(user);
        updateParticipantsList();
        appendMessage(QString(u8"<span style='color:#888;'><i>%1 покинул комнату</i></span>")
            .arg(user.toHtmlEscaped()));
    }
    // Служебные сообщения сервера: результат "auth_admin" (верный/неверный
    // пароль), уведомление о бане ("ban_user"), глобальное объявление
    // ("global_broadcast") — см. server.py. Раньше клиент их вообще не читал,
    // так что ответы на эти команды нигде не показывались.
    else if (mtype == "system_msg") {
        QString text = msg.value("text").toString();
        if (text.isEmpty()) return;
        appendMessage(QString(u8"<span style='color:#f1c40f;'><i>%1</i></span>").arg(text.toHtmlEscaped()));
    }
}

void VoiceChatWidget::onRoomKeyTimeout() {
    // Ключ мог успеть прийти буквально перед самым срабатыванием таймера —
    // на всякий случай перепроверяем, а не полагаемся только на timer->stop()
    // в обработчике "room_key_response".
    if (crypto.hasRoomKey()) return;

    if (isRoomKeyOwner) {
        // Мы Host — по правилам сервера это значит, что мы вошли в ПУСТУЮ
        // комнату, так что ответить нам в принципе некому. Единственный
        // безопасный генератор ключа — это мы.
        crypto.generateRoomKey();
        appendMessage(u8"<span style='color:#56d39b;'><i>🔑 Никто не ответил ключом комнаты (вы, похоже, первый "
            u8"участник) — сгенерирован новый ключ шифрования.</i></span>");
        return;
    }

    // Мы НЕ Host — самостоятельно генерировать ключ нельзя: если бы каждый
    // участник по своему таймауту делал это сам, два человека, вошедшие в
    // комнату почти одновременно, получили бы два разных ключа и не смогли
    // бы читать сообщения друг друга (именно это и происходило раньше).
    // Значит, тот, у кого ключ есть (Host или любой другой участник, уже
    // получивший его), просто не успел ответить — повторяем запрос.
    if (roomKeyRequestAttempts >= 5) {
        appendMessage(u8"<span style='color:#ff5f5f;'><i>⚠️ Не удалось получить ключ комнаты от других "
            u8"участников. Попробуйте переподключиться.</i></span>");
        return;
    }
    ++roomKeyRequestAttempts;

    QVariantMap rsaRequest;
    rsaRequest["type"] = "request_room_key";
    rsaRequest["public_key"] = QString::fromUtf8(crypto.getPublicKeyPem());
    rsaRequest["room"] = room;
    sendPacket(rsaRequest);

    if (!roomKeyTimeoutTimer) {
        roomKeyTimeoutTimer = new QTimer(this);
        roomKeyTimeoutTimer->setSingleShot(true);
        connect(roomKeyTimeoutTimer, &QTimer::timeout, this, &VoiceChatWidget::onRoomKeyTimeout);
    }
    roomKeyTimeoutTimer->start(4000);
}

// =================== Модель сообщений чата ===================
// Реакции/редактирование/удаление/закрепление требуют уметь находить и
// изменять уже показанное сообщение по его серверному id — вместо точечного
// патча HTML внутри chatDisplay проще держать сообщения в этой модели и
// полностью перерисовывать chatDisplay при любом изменении (rebuildChatDisplay()).
// История ограничена 100 сообщениями на комнату (см. get_last_messages() в
// server.py), так что полный перерендер остаётся дешёвым.

void VoiceChatWidget::pushMessageRecord(const ChatMessageRecord& rec) {
    messages.append(rec);
    if (rec.id >= 0) messageIndexById[rec.id] = messages.size() - 1;
}

void VoiceChatWidget::rebuildChatDisplay() {
    // ВАЖНО: каждая запись рендерится РОВНО одним HTML-параграфом (<p>...</p>),
    // без пустых строк между записями. Это принципиально для
    // showChatContextMenu(): чтобы по позиции клика можно было понять, по
    // какому сообщению кликнули, мы кладём id прямо в ссылку "msg:id" внутри
    // параграфа и достаём его через chatDisplay->anchorAt(pos) — специально
    // подобранная схема, не завязанная на номер строки/блока, поэтому порядок
    // записей в messages может быть любым, лишь бы каждая была одним <p>.
    QString html;
    html.reserve(messages.size() * 96);

    for (const ChatMessageRecord& m : messages) {
        if (!m.downloadHref.isEmpty()) {
            html += QString("<p style='margin:2px 0; color:#dcdcdc;'>📎 <a href='%1' style='color:#56d39b;'>%2 (нажмите, чтобы скачать)</a></p>")
                .arg(m.downloadHref, m.text.toHtmlEscaped());
            continue;
        }
        if (m.isSystem) {
            // m.text для системных строк — уже готовый безопасный HTML (см.
            // appendMessage()), повторно не экранируем.
            html += QString("<p style='margin:2px 0;'>%1</p>").arg(m.text);
            continue;
        }

        QString body = m.deleted
            ? QString(u8"<i style='color:#888;'>сообщение удалено</i>")
            : m.text.toHtmlEscaped();

        QString extra;
        if (m.edited && !m.deleted) {
            extra += u8" <span style='color:#888; font-size:small;'>(ред.)</span>";
        }
        if (!m.reactions.isEmpty()) {
            QStringList parts;
            for (auto it = m.reactions.constBegin(); it != m.reactions.constEnd(); ++it) {
                if (!it.value().isEmpty()) parts << QString("%1 %2").arg(it.key()).arg(it.value().size());
            }
            if (!parts.isEmpty()) {
                extra += QString(u8" <span style='color:#888; font-size:small;'>[%1]</span>").arg(parts.join("  "));
            }
        }

        if (m.id >= 0) {
            // Оборачиваем в <a href='msg:id'> ТОЛЬКО ради правого клика (см.
            // выше) — переход по клику отключён (chatDisplay->setOpenLinks(false)
            // в конструкторе), левый клик ничего не делает. Побочный эффект:
            // курсор при наведении на сообщение становится "рукой" — это
            // встроенное поведение QTextBrowser для якорей, штатно не отключается.
            //
            // ЦВЕТ ЗАДАЁМ ЯВНО (#dcdcdc), а не 'inherit': встроенный в Qt разбор
            // CSS не понимает ключевое слово 'inherit' и в этом случае тихо
            // подставляет чёрный — на тёмной теме сообщения становились
            // нечитаемыми (сливались с фоном). Раньше, до перехода на
            // setHtml()-перерисовку (нужна для правки/удаления/реакций),
            // текст добавлялся через chatDisplay->append() и просто наследовал
            // цвет темы виджета, поэтому явный цвет не требовался.
            html += QString("<p style='margin:2px 0; color:#dcdcdc;'><a href='msg:%1' style='color:#dcdcdc; text-decoration:none;'>"
                "[%2] <b>%3</b>: %4</a>%5</p>")
                .arg(m.id).arg(m.time.toHtmlEscaped(), m.user.toHtmlEscaped(), body, extra);
        }
        else {
            html += QString("<p style='margin:2px 0; color:#dcdcdc;'>[%1] <b>%2</b>: %3%4</p>")
                .arg(m.time.toHtmlEscaped(), m.user.toHtmlEscaped(), body, extra);
        }
    }

    chatDisplay->setHtml(html);
    QTextCursor c = chatDisplay->textCursor();
    c.movePosition(QTextCursor::End);
    chatDisplay->setTextCursor(c);
    chatDisplay->ensureCursorVisible();
}

void VoiceChatWidget::addFileOffer(const QString& user, const QString& filename, const QString& fileId) {
    QUrl u;
    u.setScheme("stormfile");
    u.setHost("download");
    QUrlQuery q;
    q.addQueryItem("id", fileId);
    q.addQueryItem("name", filename);
    u.setQuery(q);

    ChatMessageRecord rec;
    rec.isSystem = true; // у файла нет id сообщения в БД — reactions/edit/delete/pin к нему не применимы
    rec.user = user;
    rec.text = filename;
    rec.downloadHref = u.toString(QUrl::FullyEncoded);
    pushMessageRecord(rec);
    rebuildChatDisplay();
}

void VoiceChatWidget::updateParticipantsList() {
    participantsList->clear();
    for (auto it = usersInfo.constBegin(); it != usersInfo.constEnd(); ++it) {
        const ParticipantInfo& info = it.value();
        QString statusIcon = (info.status == u8"online") ? u8"🟢" : u8"🌙";
        QString line = QString("%1 %2 (%3)%4")
            .arg(statusIcon, it.key(), info.rank, info.mic ? u8" 🎤" : QString());
        participantsList->addItem(line);
    }
}

void VoiceChatWidget::updatePinnedBanner() {
    if (pinnedMessageId < 0) {
        pinnedBannerBox->setVisible(false);
        return;
    }
    pinnedBannerText->setText(QString(u8"📌 <b>%1</b>: %2")
        .arg(pinnedMessageUser.toHtmlEscaped(), pinnedMessageText.toHtmlEscaped()));
    pinnedBannerBox->setVisible(true);
}

void VoiceChatWidget::appendMessage(const QString& html) {
    // Системная/локальная строка без id — не может быть отредактирована,
    // удалена, лайкнута реакцией или закреплена (у неё нет id сообщения на
    // сервере). html передаётся КАК ЕСТЬ, без повторного экранирования —
    // вызывающий код уже сам решает, что в нём безопасно (ники/время в
    // остальных вызовах уже экранированы через toHtmlEscaped() на месте).
    ChatMessageRecord rec;
    rec.isSystem = true;
    rec.text = html;
    pushMessageRecord(rec);
    rebuildChatDisplay();
}

// =================== Контекстное меню сообщения / ссылки ===================

void VoiceChatWidget::showChatContextMenu(const QPoint& pos) {
    QString anchor = chatDisplay->anchorAt(pos);
    if (!anchor.startsWith("msg:")) return; // клик мимо сообщения (или по ссылке файла) — своё меню не показываем

    bool ok = false;
    int id = anchor.mid(4).toInt(&ok);
    if (!ok) return;
    int idx = messageIndexById.value(id, -1);
    if (idx < 0 || idx >= messages.size()) return;
    if (messages[idx].deleted || !connected) return;

    const QString msgUser = messages[idx].user;
    const QString msgText = messages[idx].text;
    const bool isPinned = (pinnedMessageId == id);

    QMenu menu(this);
    QMenu* reactMenu = menu.addMenu(u8"😀 Реакция");
    static const QStringList kEmojis = { u8"👍", u8"❤️", u8"😂", u8"😮", u8"😢" };
    for (const QString& emoji : kEmojis) {
        QAction* a = reactMenu->addAction(emoji);
        connect(a, &QAction::triggered, this, [this, id, emoji]() {
            QVariantMap pkt;
            pkt["type"] = "reaction";
            pkt["target_id"] = id;
            pkt["emoji"] = emoji;
            pkt["room"] = room;
            pkt["username"] = username;
            sendPacket(pkt);
            });
    }

    if (msgUser == username) {
        QAction* editAction = menu.addAction(u8"✏️ Редактировать");
        connect(editAction, &QAction::triggered, this, [this, id, msgText]() {
            bool ok2 = false;
            QString newText = QInputDialog::getText(this, u8"Редактировать сообщение",
                u8"Новый текст:", QLineEdit::Normal, msgText, &ok2);
            newText = newText.trimmed();
            if (!ok2 || newText.isEmpty() || !connected) return;
            QVariantMap pkt;
            pkt["type"] = "edit";
            pkt["id"] = id;
            pkt["new"] = crypto.encryptFernet(newText);
            pkt["room"] = room;
            pkt["username"] = username;
            sendPacket(pkt);
            });

        QAction* delAction = menu.addAction(u8"🗑 Удалить");
        connect(delAction, &QAction::triggered, this, [this, id]() {
            if (!connected) return;
            if (QMessageBox::question(this, u8"Удалить сообщение",
                u8"Удалить это сообщение для всех участников комнаты?") != QMessageBox::Yes) return;
            QVariantMap pkt;
            pkt["type"] = "delete";
            pkt["id"] = id;
            pkt["room"] = room;
            pkt["username"] = username;
            sendPacket(pkt);
            });
    }

    QAction* pinAction = menu.addAction(isPinned ? u8"📌 Открепить" : u8"📌 Закрепить");
    connect(pinAction, &QAction::triggered, this, [this, id, isPinned]() {
        if (!connected) return;
        QVariantMap pkt;
        if (isPinned) {
            pkt["type"] = "unpin";
        }
        else {
            pkt["type"] = "pin";
            pkt["id"] = id;
        }
        pkt["room"] = room;
        pkt["username"] = username;
        sendPacket(pkt);
        });

    menu.exec(chatDisplay->mapToGlobal(pos));
}

void VoiceChatWidget::onChatAnchorClicked(const QUrl& url) {
    // "msg:" — служебная ссылка только для правого клика (см. showChatContextMenu()
    // и rebuildChatDisplay()), по левому клику по сообщению намеренно ничего не делаем.
    if (url.scheme() != "stormfile") return;
    if (!connected) return;

    QUrlQuery q(url);
    QString fileId = q.queryItemValue("id");
    QString filename = q.queryItemValue("name", QUrl::FullyDecoded);
    if (fileId.isEmpty()) return;

    pendingDownloadFilename = filename;
    QVariantMap pkt;
    pkt["type"] = "file_request";
    pkt["file_id"] = fileId;
    pkt["filename"] = filename;
    pkt["room"] = room;
    sendPacket(pkt);
    appendMessage(QString(u8"<span style='color:#888;'><i>⏳ Запрашиваю файл «%1»…</i></span>")
        .arg(filename.toHtmlEscaped()));
}

// =================== Индикатор "печатает" ===================

void VoiceChatWidget::onMsgTextChanged() {
    if (!connected || msgInput->toPlainText().trimmed().isEmpty()) return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastTypingSentMs < 2000) return; // троттлинг: не чаще раза в ~2 секунды
    lastTypingSentMs = now;

    QVariantMap pkt;
    pkt["type"] = "typing";
    pkt["room"] = room;
    pkt["username"] = username;
    sendPacket(pkt);
}

void VoiceChatWidget::onTypingTimerTick() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList active;
    for (auto it = typingUntilMs.begin(); it != typingUntilMs.end(); ) {
        if (it.value() <= now) it = typingUntilMs.erase(it);
        else { active << it.key(); ++it; }
    }

    if (active.isEmpty()) {
        typingLabel->clear();
        typingLabel->setVisible(false);
    }
    else {
        QString text = active.size() == 1
            ? QString(u8"%1 печатает…").arg(active.first())
            : QString(u8"%1 печатают…").arg(active.join(", "));
        typingLabel->setText(text);
        typingLabel->setVisible(true);
    }
}

// =================== Текстовые сообщения ===================

void VoiceChatWidget::sendTextMessage() {
    QString text = msgInput->toPlainText().trimmed();
    if (text.isEmpty() || !connected) return;

    QVariantMap pkt;
    pkt["type"] = "text";

    // ШИФРУЕМ ИСХОДЯЩЕЕ СООБЩЕНИЕ
    pkt["msg"] = crypto.encryptFernet(text);

    pkt["user"] = username;
    pkt["room"] = room;
    sendPacket(pkt);

    msgInput->clear();
}

// =================== Передача файлов ===================

void VoiceChatWidget::sendFile() {
    if (!connected) return;
    QString path = QFileDialog::getOpenFileName(this, u8"Выберите файл для отправки");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, u8"Ошибка", u8"Не удалось открыть файл.");
        return;
    }
    QByteArray data = f.readAll();
    f.close();

    // Ограничение по размеру: приём пакета ограничен 10 МБ (см.
    // onTcpReadyRead()), а base64 увеличивает объём примерно на треть —
    // берём запас и режем на ~7 МБ исходного файла.
    const qint64 kMaxFileBytes = 7 * 1024 * 1024;
    if (data.size() > kMaxFileBytes) {
        QMessageBox::warning(this, u8"Файл слишком большой",
            u8"Максимальный размер файла для отправки — 7 МБ.");
        return;
    }

    QVariantMap pkt;
    pkt["type"] = "file_upload";
    pkt["filename"] = QFileInfo(path).fileName();
    pkt["data"] = QString::fromLatin1(data.toBase64());
    pkt["room"] = room;
    pkt["username"] = username;
    sendPacket(pkt);
}

// =================== Микрофон / индикатор состояния ===================

void VoiceChatWidget::toggleMic() {
    bool wantActive = !micActive;

    if (wantActive) {
        // Захват микрофона запускается именно здесь, а не при подключении к
        // чату — раньше startAudio() включал запись сразу при коннекте, и
        // микрофон физически писал звук в фоне даже когда кнопка показывала
        // "выключен". Теперь запись идёт, только когда пользователь реально
        // включил микрофон.
        if (!startMicCapture()) {
            return; // не удалось открыть микрофон — оставляем выключенным, сообщение уже показано
        }
    }
    else {
        stopMicCapture();
    }

    micActive = wantActive;

    QVariantMap pkt;
    pkt["type"] = "mic_status";
    pkt["mic"] = micActive;
    pkt["room"] = room;
    pkt["username"] = username;
    sendPacket(pkt);

    currentMicState = micActive ? "silent" : "off"; // "silent" = жду речь, как в Python record_loop
    updateMicButtonUi(currentMicState);
}

void VoiceChatWidget::updateMicButtonUi(const QString& state) {
    if (state == "off") {
        micBtn->setText(u8"🎤 Включить микрофон");
        micBtn->setStyleSheet("QPushButton { background-color: #CC0000; color: white; border-radius: 6px; padding: 10px; font-weight: bold; }");
    }
    else if (state == "silent") {
        micBtn->setText(u8"⏳ Анализ тишины...");
        micBtn->setStyleSheet("QPushButton { background-color: #f39c12; color: white; border-radius: 6px; padding: 10px; font-weight: bold; }");
    }
    else if (state == "speaking") {
        micBtn->setText(u8"🔊 Идет передача!");
        micBtn->setStyleSheet("QPushButton { background-color: #2FA572; color: white; border-radius: 6px; padding: 10px; font-weight: bold; }");
    }
}

// =================== Аудио (захват/воспроизведение) ===================
// ВАЖНО: здесь передаётся СЫРОЙ PCM16/16kHz/mono по UDP, без Opus и без
// шифрования — это рабочий, но "тяжёлый" канал (больше трафика, чем в Python).
// Замените onMicDataReady()/onUdpReadyRead() на Opus encode/decode, когда
// подключите libopus, чтобы соответствовать Python 1:1.

void VoiceChatWidget::startAudio() {
    // Запускаем только воспроизведение (спикер) — чтобы слышать других
    // участников сразу после подключения. Захват микрофона запускается
    // отдельно, в startMicCapture(), только когда пользователь включает "🎤".
    QAudioFormat format;
    format.setSampleRate(kRate);
    format.setChannelCount(kChannels);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice outDev = hasCustomOutputDevice ? selectedOutputDevice : QMediaDevices::defaultAudioOutput();

    audioOutput = new QAudioSink(outDev, format, this);
    spkDevice = audioOutput->start();

    if (!spkDevice) {
        appendMessage(u8"<span style='color:#ff5f5f;'>⚠️ Не удалось запустить воспроизведение звука "
            u8"(нет устройства вывода или оно занято).</span>");
    }
}

void VoiceChatWidget::stopAudio() {
    stopMicCapture();
    if (audioOutput) { audioOutput->stop(); audioOutput->deleteLater(); audioOutput = nullptr; }
    spkDevice = nullptr;
}

bool VoiceChatWidget::startMicCapture() {
    if (audioInput) return true; // уже запущен

    QAudioFormat format;
    format.setSampleRate(kRate);
    format.setChannelCount(kChannels);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice inDev = hasCustomInputDevice ? selectedInputDevice : QMediaDevices::defaultAudioInput();

    audioInput = new QAudioSource(inDev, format, this);
    micDevice = audioInput->start();

    // ВАЖНО: start() может вернуть nullptr, если микрофон недоступен (нет
    // устройства, отказано в доступе ОС и т.п.). connect() с nullptr вместо
    // sender'а — это краш, поэтому проверяем перед подпиской на readyRead.
    if (!micDevice) {
        appendMessage(u8"<span style='color:#ff5f5f;'>⚠️ Микрофон недоступен: нет устройства ввода "
            u8"или доступ запрещён операционной системой.</span>");
        audioInput->deleteLater();
        audioInput = nullptr;
        return false;
    }

    connect(micDevice, &QIODevice::readyRead, this, &VoiceChatWidget::onMicDataReady);
    return true;
}

void VoiceChatWidget::stopMicCapture() {
    if (audioInput) { audioInput->stop(); audioInput->deleteLater(); audioInput = nullptr; }
    micDevice = nullptr;
}

void VoiceChatWidget::onMicDataReady() {
    if (!micDevice) return;
    QByteArray pcm = micDevice->readAll();
    if (pcm.isEmpty()) return;

    // Захват запускается/останавливается вместе с micActive (см. toggleMic/
    // stopMicCapture), так что этот случай — защитный, на случай гонки сигналов.
    if (!micActive || udpToken.isEmpty()) return;

    // Простейший VAD по амплитуде (замена webrtcvad); порог настраивается в openSettings()
    const qint16* samples = reinterpret_cast<const qint16*>(pcm.constData());
    int n = pcm.size() / 2;
    int peak = 0;
    for (int i = 0; i < n; ++i) peak = std::max(peak, std::abs(int(samples[i])));

    if (peak < vadThreshold) {
        if (currentMicState != "silent") {
            currentMicState = "silent";
            updateMicButtonUi("silent");
        }
        return;
    }

    if (currentMicState != "speaking") {
        currentMicState = "speaking";
        updateMicButtonUi("speaking");
    }

    // TODO: encoded = opus_encode(pcm); затем cipher.encrypt(encoded), если E2EE
    QByteArray packet = udpToken + "|" + pcm; // "|" разделитель, как в Python udp_receive_loop
    udpSocket->writeDatagram(packet, QHostAddress(serverHost), serverPort);
}

void VoiceChatWidget::onUdpReadyRead() {
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(int(udpSocket->pendingDatagramSize()));
        udpSocket->readDatagram(data.data(), data.size());

        int sep = data.indexOf('|');
        if (sep < 0) continue;
        QString fromUser = QString::fromUtf8(data.left(sep));
        if (fromUser == username) continue;

        QByteArray audio = data.mid(sep + 1);
        // TODO: cipher.decrypt(audio), затем opus_decode(audio) -> pcm
        if (spkDevice) spkDevice->write(audio);
    }
}

// =================== Настройки ===================

void VoiceChatWidget::openSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(u8"Настройки голосового чата");
    QVBoxLayout* v = new QVBoxLayout(&dlg);

    v->addWidget(new QLabel(u8"Микрофон (вход):", &dlg));
    QComboBox* inCombo = new QComboBox(&dlg);
    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
    int inCurrent = 0;
    for (int i = 0; i < inputs.size(); ++i) {
        inCombo->addItem(inputs[i].description());
        if (hasCustomInputDevice && inputs[i].description() == selectedInputDevice.description()) {
            inCurrent = i;
        }
    }
    if (!inputs.isEmpty()) inCombo->setCurrentIndex(inCurrent);
    else inCombo->addItem(u8"Устройства не найдены");
    v->addWidget(inCombo);

    v->addWidget(new QLabel(u8"Динамики (выход):", &dlg));
    QComboBox* outCombo = new QComboBox(&dlg);
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    int outCurrent = 0;
    for (int i = 0; i < outputs.size(); ++i) {
        outCombo->addItem(outputs[i].description());
        if (hasCustomOutputDevice && outputs[i].description() == selectedOutputDevice.description()) {
            outCurrent = i;
        }
    }
    if (!outputs.isEmpty()) outCombo->setCurrentIndex(outCurrent);
    else outCombo->addItem(u8"Устройства не найдены");
    v->addWidget(outCombo);

    v->addWidget(new QLabel(u8"Чувствительность микрофона (порог VAD, меньше = чувствительнее):", &dlg));
    QSlider* vadSlider = new QSlider(Qt::Horizontal, &dlg);
    vadSlider->setRange(50, 5000);
    vadSlider->setValue(vadThreshold);
    v->addWidget(vadSlider);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    if (!inputs.isEmpty()) {
        selectedInputDevice = inputs[inCombo->currentIndex()];
        hasCustomInputDevice = true;
    }
    if (!outputs.isEmpty()) {
        selectedOutputDevice = outputs[outCombo->currentIndex()];
        hasCustomOutputDevice = true;
    }
    vadThreshold = vadSlider->value();

    QSettings settings("Shtorm Software", "Storm Browser");
    settings.setValue("chat/vad_threshold", vadThreshold);
    if (hasCustomInputDevice) settings.setValue("chat/input_device", selectedInputDevice.description());
    if (hasCustomOutputDevice) settings.setValue("chat/output_device", selectedOutputDevice.description());

    // Если чат уже идёт — перезапускаем аудио с новыми устройствами
    if (connected) {
        bool wasMicActive = micActive;
        stopAudio();
        startAudio();
        if (wasMicActive) {
            if (!startMicCapture()) {
                micActive = false;
                currentMicState = "off";
                updateMicButtonUi("off");
            }
        }
    }
}

bool VoiceChatWidget::eventFilter(QObject* watched, QEvent* event) {
    // Перехватываем нажатие Enter в поле ввода сообщений
    if (watched == msgInput && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                return false; // Shift+Enter оставляет перенос строки
            }
            else {
                sendTextMessage();
                return true; // Отправляем сообщение
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}