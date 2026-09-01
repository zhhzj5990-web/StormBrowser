#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>

class HomeAIBridge : public QObject {
    Q_OBJECT
public:
    explicit HomeAIBridge(QObject* parent = nullptr);

    // Метод для обычного чата с ИИ
    Q_INVOKABLE void askAI(const QString& prompt);
    // Полностью сбрасывает память диалога на бэкенде
    Q_INVOKABLE void clearContext();

    // Подставляет на бэкенд контекст ранее сохранённого чата
    Q_INVOKABLE void restoreContext(const QString& historyJson);

    // Генерация медиа (изображения и видео)
    Q_INVOKABLE void generateMedia(const QString& prompt, const QString& type);

signals:
    // Сигнал, возвращающий ответ ИИ обратно в JavaScript
    void aiResponseReceived(const QString& replyHtml);

private slots:
    void onNetworkReply(QNetworkReply* reply);
    void onSearchNetworkReply(QNetworkReply* reply); // Ответ Tavily (см. performHomeSearch) — свой формат JSON, отдельный менеджер/слот, не пересекается с onNetworkReply

private:
    // isFinal помечает запрос как "финальный, обоснованный поиском" (см. sendGroundedFollowup) —
    // такой ответ в onNetworkReply уже не проверяется на префиксы NAVIGATE:/SEARCH:, чтобы не
    // зациклиться и не перепутать реальный ответ пользователю со служебной командой маршрутизации.
    void continueWithGigaChat(const QJsonObject& baseJson, const QString& gigaKey, const QString& gigaModel, bool isFinal = false);
    void sendChatRequest(QNetworkRequest request, const QByteArray& body, bool isFinal = false);
    void downloadGigaChatImage(const QString& fileId, const QString& token, const QString& originalText);

    // --- Поиск в интернете через Tavily (research/tavily_key — тот же ключ, что у модуля
    // "🔬 Глубокое исследование" и у бокового ИИ-агента AIAssistantWidget) ---
    // Раньше "поиск в интернете" с главной страницы был фиктивным: модель просто просила
    // открыть страницу выдачи поисковика (NAVIGATE:), а реального ответа с фактами не было.
    // Теперь модель, решив, что вопросу нужна свежая информация из сети, возвращает
    // 'SEARCH: запрос' — по нему выполняется реальный поиск через Tavily, а затем ещё один
    // (уже "финальный") запрос к тому же провайдеру ИИ с результатами поиска как контекстом,
    // чтобы получить содержательный ответ, а не просто ссылку на поисковик.
    QString buildSearchEngineUrl(const QString& query) const; // Общая логика с NAVIGATE: URL страницы выдачи выбранного в настройках поисковика — используется и как fallback, если ключ Tavily не задан или Tavily недоступен
    void performHomeSearch(const QString& query);              // Точка входа при 'SEARCH:' от модели — сам запрос к Tavily
    void sendGroundedFollowup(const QString& query, const QString& searchSummary); // Второй запрос к ИИ-провайдеру с результатами поиска — та же ветвление по aiMode, что и в askAI()

    QNetworkAccessManager* m_netManager;
    QNetworkAccessManager* m_authManager = nullptr;
    QNetworkAccessManager* m_searchManager = nullptr; // Отдельный менеджер для запросов к Tavily — формат ответа не похож на chat-completion, поэтому не смешивается с m_netManager
    QNetworkReply* m_pendingReply = nullptr;
    QJsonArray m_chatHistory;
    QString m_cachedGigaToken;
    qint64 m_gigaTokenExpireTime = 0;
};