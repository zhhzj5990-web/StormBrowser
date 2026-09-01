#pragma once
#include <QObject>
#include <QString>
#include <QList>

class WebPageAgent;
class QNetworkAccessManager;
class QNetworkReply;

// Headless-вариант того же агентного цикла, что AIAssistantWidget использует
// для чата: снимок страницы (WebPageAgent::capturePageContext) -> запрос к
// выбранному бэкенду ИИ (OpenRouter/GigaChat/собственный сервер — те же
// ключи QSettings ai/*, что читает AIAssistantWidget::sendAgentRequest) ->
// разбор ответа {"message","action"} -> executeAction на WebPageAgent ->
// повтор, пока action не станет "done" или не будет достигнут лимит шагов.
//
// Сознательно НЕ зависит от AIAssistantWidget (не тянет сюда чат-виджет ради
// одного цикла) — та же логика, что уже применена в
// SettingsBridge::testAiConnection() к проверке ключа: минимальное
// дублирование уже существующего запроса вместо межвиджетной зависимости.
// Системный промпт здесь — адаптация agentSystemPrompt() из
// AIAssistantWidget.cpp под задачу без живого чата: помимо обычных правил
// агента, модель обязана в финальном action указать "success": true/false —
// в чате это не нужно (человек сам читает "message" и судит), а здесь нужен
// явный структурированный сигнал, чтобы решить markPublished vs markFailed.
//
// НЕ поддерживает "look" (снимок экрана + vision-модель, см.
// AIAssistantWidget::performVisionLook) — это отдельный, довольно объёмный
// путь (скриншот, для GigaChat ещё и отдельная загрузка файла). Системный
// промпт прямо предупреждает модель, что "look" недоступен, и просит
// продолжать обычными способами — это штатное поведение, заложенное в сам
// протокол (см. AIAssistantWidget::agentSystemPrompt(), пункт про "look").
class AiAgentTaskRunner : public QObject {
    Q_OBJECT
public:
    // agent — уже созданный и зафиксированный на нужной вкладке WebPageAgent
    // (см. SmmPublishController::startAgentSequence). Раннер не владеет им и
    // не вызывает beginTask()/endTask() — это ответственность вызывающего
    // кода, как и с showWorking()/updateStatus() до/после run().
    explicit AiAgentTaskRunner(WebPageAgent* agent, QObject* parent = nullptr);

    // taskDescription — текстовое описание задачи одним предложением/абзацем
    // (то, что в чате было бы сообщением пользователя). Раннер сам добавляет
    // к нему актуальный снимок страницы на каждом шаге.
    // maxSteps — лимит шагов агента именно для этой задачи (см.
    // PlatformInfo::maxAgentSteps в SmmTypes.h — у разных площадок разная
    // объективная "длина" честного сценария публикации). По умолчанию —
    // общий MAX_STEPS ниже, на случай если вызывающий код его не задаёт.
    void run(const QString& taskDescription, int maxSteps = MAX_STEPS);

signals:
    // Промежуточные "message" от ИИ по ходу выполнения — необязательны к
    // обработке, можно использовать для логов/статуса.
    void progress(const QString& message);
    // Финал: success — явный флаг из последнего action ("done", success=…),
    // finalMessage — последнее "message" от ИИ (человекочитаемое объяснение
    // результата или причина неудачи/остановки по лимиту шагов/ошибке сети).
    void finished(bool success, const QString& finalMessage);

private:
    struct AgentMsg { QString role; QString content; };

    void sendStep();
    void onNetworkReply(QNetworkReply* reply);
    void scheduleNextStep(int fallbackDelayMs);
    bool ensureGigaChatToken(const QString& gigaKey, QString& errorOut);
    void finishWith(bool success, const QString& message);

    WebPageAgent* m_agent;
    QNetworkAccessManager* m_networkManager;

    QString m_taskDescription;
    QList<AgentMsg> m_history; // роли/содержимое — свой, независимый от AIAssistantWidget::chatHistory аналог
    int m_stepCount = 0;
    // Лимит для ТЕКУЩЕЙ задачи — приходит параметром в run() (см. выше),
    // подставляется в проверку "лимит шагов" в sendStep() вместо статического
    // MAX_STEPS напрямую, чтобы разные площадки могли иметь разный бюджет.
    int m_maxSteps = MAX_STEPS;
    // Не даёт зациклиться на "странице, которая никогда не догрузится" —
    // повторный снимок из-за подозрительно пустого текста делается ровно
    // один раз за всю задачу, см. sendStep().
    bool m_settleRetryUsed = false;
    bool m_running = false;
    QString m_lastActionSummary;
    QString m_lastSeenUrl;
    QString m_lastSeenPageText;
    // Сигнатура (тип+target+text+value+url) последнего ПРОВАЛИВШЕГОСЯ
    // действия — если модель на следующем шаге пришлёт точно то же самое,
    // не отправляем это в WebPageAgent повторно (заведомо провалится тем же
    // образом), а сразу просим модель попробовать иначе. Пусто — либо ещё
    // не было провалов, либо последнее действие удалось.
    QString m_lastFailedActionSignature;
    // Сколько раз подряд уже повторили текущий шаг из-за временного
    // сетевого сбоя (таймаут/5xx/429) — сбрасывается при любом успешном
    // ответе сервера, см. onNetworkReply().
    int m_networkRetryCount = 0;

    QString m_cachedGigaToken;
    qint64 m_gigaTokenExpireTime = 0;

    static constexpr int MAX_STEPS = 15;
    static constexpr int kMaxNetworkRetries = 2;
};