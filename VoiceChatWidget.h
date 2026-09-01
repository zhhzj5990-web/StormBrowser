#pragma once
#ifndef VOICECHATWIDGET_H
#define VOICECHATWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QByteArray>
#include <QVariantMap>
#include <QMap>
#include <QVector>
#include <QListWidget>
#include <QAudioDevice>
#include "CryptoManager.h"

// TODO: подключить libopus и заменить сырой PCM на Opus-кодирование/декодирование
// E2EE (RSA + Fernet-совместимое шифрование комнаты) уже реализовано через
// CryptoManager (см. handlePacket(): request_room_key / room_key_response).

class QAudioSource;
class QAudioSink;
class QIODevice;
class QTimer;

// Одно сообщение в чате (или системная строка — join/leave/ошибка/файл).
// Хранится в VoiceChatWidget::messages и целиком перерисовывается в
// rebuildChatDisplay() — так проще всего корректно отразить edit/delete/
// реакции/закрепление, не патча HTML построчно. См. комментарий над
// rebuildChatDisplay() в .cpp про соответствие индекса записи и QTextBlock.
struct ChatMessageRecord {
    int id = -1;              // id сообщения в БД сервера; -1 у строк без edit/delete/реакций/закрепления
    QString user;
    QString time;
    QString text;              // для обычных сообщений — расшифрованный текст (экранируется при рендере);
    // для системных строк — уже готовый безопасный HTML (см. appendMessage());
    // для предложений файла — только имя файла (экранируется при рендере)
    QString downloadHref;      // непусто только у предложений файла — ссылка вида "stormfile:..."
    bool isSystem = false;
    bool deleted = false;
    bool edited = false;
    QMap<QString, QStringList> reactions; // эмодзи -> ники поставивших
};

// Данные об участнике комнаты, приходят в "joined"/"join"/"mic_status"/"status_update".
struct ParticipantInfo {
    QString rank = QStringLiteral("Player");
    bool mic = false;
    QString status = QStringLiteral("online");
};

class VoiceChatWidget : public QWidget {
    Q_OBJECT
public:
    explicit VoiceChatWidget(QWidget* parent = nullptr);
    ~VoiceChatWidget();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    // UI / ping
    void triggerPing();
    void checkServerStatus();
    void onPingConnected();
    void onPingError();


    // Подключение/отключение
    void startChat();
    void stopChat();
    void toggleMic();
    void sendTextMessage();

    // TCP протокол
    void onTcpConnected();
    void onTcpReadyRead();
    void onTcpDisconnected();
    void onTcpError(QAbstractSocket::SocketError err);

    // UDP голос
    void onUdpReadyRead();
    void onMicDataReady();

    // Настройки (устройства ввода/вывода звука, чувствительность микрофона)
    void openSettings();

    // Фолбэк для обмена ключом комнаты (E2EE): если через несколько секунд
    // после входа никто не прислал ключ в ответ на наш "request_room_key" —
    // мы либо первый/единственный участник комнаты, либо у всех остальных
    // ключа тоже нет (см. handlePacket()/CryptoManager::generateRoomKey()) —
    // генерируем ключ сами, а не ждём ответа вечно.
    void onRoomKeyTimeout();

    // Правый клик по сообщению в chatDisplay — меню "реакция/редактировать/
    // удалить/закрепить" (см. showChatContextMenu()).
    void showChatContextMenu(const QPoint& pos);
    // Клик по ссылке в chatDisplay — сейчас обрабатывает только ссылки
    // скачивания файла ("stormfile:..."), см. addFileOffer().
    void onChatAnchorClicked(const QUrl& url);

    // Индикатор "печатает": отправка троттлится (см. onMsgTextChanged()),
    // а onTypingTimerTick() раз в секунду скрывает ники, от которых давно
    // не было нового события "typing".
    void onMsgTextChanged();
    void onTypingTimerTick();

    // Прикрепление файла — выбор через диалог и отправка на сервер.
    void sendFile();

private:
    CryptoManager crypto;
    void updateServerLight(bool isOnline);
    void updateMicButtonUi(const QString& state);
    void resetUi();

    // Протокол: 4 байта длины (big-endian) + JSON UTF-8
    void sendPacket(const QVariantMap& data);
    void handlePacket(const QByteArray& json);
    // Системная/локальная строка (ошибка, join/leave, уведомление) — html
    // передаётся как есть, без повторного экранирования (см. .cpp).
    void appendMessage(const QString& html);

    // --- Модель сообщений чата (для edit/delete/реакций/закрепления) ---
    void pushMessageRecord(const ChatMessageRecord& rec);
    void rebuildChatDisplay();
    void addFileOffer(const QString& user, const QString& filename, const QString& fileId);
    void updateParticipantsList();
    void updatePinnedBanner(); // перерисовывает pinnedBanner по текущим pinnedMessageId/pinnedMessageText

    // Воспроизведение (спикер) — работает всё время, пока идёт чат
    void startAudio();
    void stopAudio();

    // Захват микрофона — включается/выключается вместе с кнопкой "🎤",
    // чтобы не писать звук с микрофона в фоне, когда он визуально выключен
    bool startMicCapture();
    void stopMicCapture();

    QLineEdit* ipInput;
    QLineEdit* portInput;
    QLabel* serverStatusLight;
    QLineEdit* nickInput;
    QLineEdit* roomInput;
    QLabel* statusLabel;

    QPushButton* connectBtn;
    QPushButton* disconnectBtn;
    QPushButton* micBtn;
    QPushButton* settingsBtn;
    QPushButton* fileBtn; // 📎 — прикрепить и отправить файл

    QTextBrowser* chatDisplay;
    QTextEdit* msgInput;
    QLabel* typingLabel;      // "ник печатает…", скрыт по умолчанию
    QWidget* pinnedBannerBox; // контейнер закреплённого сообщения, скрыт когда ничего не закреплено
    QLabel* pinnedBannerText;
    QPushButton* pinnedUnpinBtn;
    QListWidget* participantsList;

    bool micActive;
    QString currentMicState; // off / silent / speaking

    // --- сетевые объекты ---
    QTcpSocket* pingSocket;      // быстрая проверка "жив ли сервер"
    // Периодически (раз в 5с) перезапускает checkServerStatus(), пока чат не
    // подключен. БЕЗ этого таймера индикатор 🟢/🔴 проверялся только один раз
    // при старте виджета и при ручном редактировании IP/порта (triggerPing()) —
    // если сервер выключался в любой другой момент, лампочка навсегда
    // оставалась в последнем известном состоянии, ничего не пересчитывая.
    QTimer* serverCheckTimer = nullptr;
    QTcpSocket* chatSocket;      // основное TCP-соединение чата
    QUdpSocket* udpSocket;       // голос
    QByteArray  udpToken;        // токен, выдаваемый сервером для UDP

    // буфер приёма TCP-пакетов (длина + JSON), как в receive_loop()
    QByteArray  recvBuffer;
    quint32     expectedLen = 0;
    bool        haveLen = false;

    QString username;
    QString room;
    QString serverHost;
    quint16 serverPort = 0;

    bool connected = false;
    bool stoppingChat = false; // защита от реентерабельного вызова stopChat() (см. onTcpDisconnected/onTcpError)

    // --- аудио ---
    QAudioSource* audioInput = nullptr;
    QAudioSink* audioOutput = nullptr;
    QIODevice* micDevice = nullptr;  // поток чтения с микрофона
    QIODevice* spkDevice = nullptr;  // поток записи в динамики

    // Выбранные пользователем устройства (см. openSettings()). Если не выбраны —
    // используются устройства по умолчанию (QMediaDevices::defaultAudioInput/Output).
    QAudioDevice selectedInputDevice;
    QAudioDevice selectedOutputDevice;
    bool hasCustomInputDevice = false;
    bool hasCustomOutputDevice = false;
    int vadThreshold = 500; // порог простого VAD по амплитуде, настраивается в openSettings()

    QMap<QString, ParticipantInfo> usersInfo; // username -> ранг/мик/статус

    // --- Сообщения чата ---
    QVector<ChatMessageRecord> messages;  // порядок отображения = порядок в chatDisplay
    QMap<int, int> messageIndexById;      // id сообщения на сервере -> индекс в messages
    int pinnedMessageId = -1;
    QString pinnedMessageUser;
    QString pinnedMessageText; // расшифрованный текст закреплённого сообщения

    // --- "Печатает…" ---
    QMap<QString, qint64> typingUntilMs; // ник -> момент (epoch ms), до которого показываем индикатор
    QTimer* typingHideTimer = nullptr;   // тикает раз в секунду, скрывает истёкшие ники
    qint64 lastTypingSentMs = 0;         // троттлинг исходящего "typing" (не чаще раза в ~2с)

    // --- Обмен ключом комнаты (E2EE), см. onRoomKeyTimeout() ---
    QTimer* roomKeyTimeoutTimer = nullptr;
    // true, только если наш собственный rank в комнате == "Host" (сервер
    // выдаёт этот ранг ровно одному участнику — тому, кто вошёл в ПУСТУЮ
    // комнату, см. server.py). Только Host разрешено самостоятельно
    // генерировать ключ комнаты по таймауту в onRoomKeyTimeout() — иначе,
    // если два новых участника входят в комнату почти одновременно, ни у
    // кого из них ещё нет ключа, чтобы ответить друг другу, и оба по
    // таймауту генерируют РАЗНЫЕ ключи, после чего не могут читать
    // сообщения друг друга уже никогда в рамках этой комнаты.
    bool isRoomKeyOwner = false;
    // Счётчик повторных отправок "request_room_key" для участников,
    // не являющихся Host (см. onRoomKeyTimeout()).
    int roomKeyRequestAttempts = 0;

    // --- Передача файлов ---
    // Сервер отвечает на "file_request" пакетом "file_data" без file_id (только
    // filename+data), поэтому просто запоминаем имя последнего запрошенного
    // файла. Ограничение: если запустить скачивание второго файла до прихода
    // ответа на первый — имя может перепутаться (крайне маловероятный кейс
    // при обычном использовании из UI).
    QString pendingDownloadFilename;
};

#endif // VOICECHATWIDGET_H