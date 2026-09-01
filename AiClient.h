#pragma once
#include <QObject>
#include <QJsonArray>
#include <QVariant>

class QNetworkAccessManager;

// ==========================================================================
// AiClient — общая точка отправки "одноразовых" (неагентных) запросов к
// выбранному в настройках AI-бэкенду (OpenRouter / GigaChat / личный
// кабинет Storm). Раньше эта логика была продублирована дважды:
//   - AIAssistantWidget::postQuickRequest / onQuickNetworkReply (классификация
//     запроса и "простой" чат-ответ без снимка страницы),
//   - ResearchManager (синтез отчёта в модуле Deep Research Report).
// Вынесена сюда один раз, чтобы оба места читали одни и те же настройки
// (ai/mode, ai/openrouter_key, ai/gigachat_key...) и одинаково парсили ответ.
//
// НЕ привязан к GUI-потоку: не содержит ни одного QWidget/чата, только сеть
// и кэш токена GigaChat, поэтому безопасно создаётся и используется как из
// AIAssistantWidget (GUI-поток), так и из ResearchManager (поток воркера) —
// при условии, что и AiClient, и вызывающий его объект живут в одном и том
// же потоке (см. пример использования в ResearchManager.cpp: AiClient
// создаётся как дочерний QObject ResearchManager'а, поэтому переезжает в
// worker-поток вместе с ним при moveToThread()).
//
// НЕ используется для агентного цикла работы со страницей (sendAgentRequest/
// performVisionLook в AIAssistantWidget) — там своя, более сложная логика
// (снимок страницы, JSON-протокол action, vision-запросы), которую пока не
// имеет смысла сюда тащить.
// ==========================================================================
class AiClient : public QObject {
    Q_OBJECT
public:
    explicit AiClient(QObject* parent = nullptr);

    // requestId — метка запроса ("classify", "simple", "research-synthesis"...),
    // приходит обратно в сигналах, чтобы вызывающий код понимал, к какому его
    // собственному запросу относится ответ.
    // payload — произвольные данные вызывающей стороны, транзитом возвращаются
    // в replyReceived/replyFailed без изменений — прямой аналог
    // reply->setProperty("stormPayload", ...) из старого postQuickRequest, но
    // не завязанный на QNetworkReply (и поэтому одинаково работающий что в
    // GUI-потоке, что в потоке воркера).
    // meterKind — необязательная метка ТАРИФИЦИРУЕМОЙ фичи ("research_report"
    // и т.п.). Работает ТОЛЬКО в ветке "Личный кабинет" (aiMode == 1): при
    // непустом значении добавляется полем "purpose" в тело запроса к
    // storm-browser.online, чтобы сервер знал, какую квоту проверять/списывать.
    // Для "свой ключ" (OpenRouter/GigaChat, aiMode 0/2) — не используется и не
    // передаётся вообще: сервер эти запросы даже не видит, они летят напрямую
    // провайдеру с ключом самого пользователя, поэтому никаких квот там в
    // принципе быть не может — это и есть "свой ключ бесплатно" по построению.
    void sendRequest(const QString& requestId, const QJsonArray& messages,
        bool wantJsonObjectFormat = false, const QVariant& payload = QVariant(),
        const QString& meterKind = QString());

signals:
    void replyReceived(const QString& requestId, const QString& content, const QVariant& payload);

    // authError = true — запрос даже не улетел на сервер (не заполнен нужный
    // ключ в настройках, либо не удалось получить/обновить токен GigaChat).
    // authError = false — запрос дошёл до сервера, но тот ответил ошибкой
    // (HTTP-код, таймаут, обрыв сети и т.п.). Разделение сохраняет то же
    // поведение, что было в оригинале: AIAssistantWidget при "classify" на
    // обычную сетевую ошибку молча уходит в полноценный агентный режим
    // (безопасный запасной вариант), а на ошибку авторизации — показывает
    // сообщение об ошибке, не подменяя её сообщением "не удалось разобраться,
    // включаю агента".
    void replyFailed(const QString& requestId, const QString& errorMessage, bool authError, const QVariant& payload);

    // Сработавшая квота тарифицируемой фичи (см. meterKind в sendRequest) —
    // сервер ответил HTTP 402. used/limit — -1, если сервер их не прислал
    // (тогда UI показывает только текст detail без чисел). Отдельный сигнал,
    // а не частный случай replyFailed — вызывающая сторона (ResearchManager)
    // должна показать это не как обычную ошибку, а как предложение оформить
    // подписку/пакет в Личном кабинете.
    void quotaExceeded(const QString& requestId, const QString& detail, int used, int limit, const QVariant& payload);

private:
    QNetworkAccessManager* net();
    bool ensureGigaChatToken(const QString& gigaKey, QString& errorOut);

    QNetworkAccessManager* m_net = nullptr;
    QString m_cachedGigaToken;
    qint64 m_gigaTokenExpireTime = 0;
};