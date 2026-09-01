#pragma once
#include <QObject>
#include <QStringList>
#include <QVariant>

class QNetworkAccessManager;
class QNetworkReply;
class AiClient;
class ResearchManager : public QObject {
    Q_OBJECT
public:
    explicit ResearchManager(QObject* parent = nullptr);
    ~ResearchManager() override;

public slots:    
    void startResearch(const QString& topic, const QString& depth, const QString& format);
    void cancelResearch();

signals:    
    void progressChanged(const QString& stageText, int percent);
    void researchFinished(const QString& filePath, const QString& title);
    void researchFailed(const QString& errorMessage);
    void researchQuotaExceeded(const QString& message, int used, int limit);

private:
    // --- Этап 0: проверка квоты (только для "Личного кабинета") ---
    void checkQuotaThenProceed();
    void beginLinkGathering();   
    void beginLinkGatheringViaTavily(const QString& apiKey);
    void beginLinkGatheringViaDuckDuckGo();
    void extractLinksFromSearchHtml(const QString& html);
    void fetchNextPage();
    static QString stripHtmlToPlainText(const QString& html);

    // --- Этап 3: синтез через ИИ ---
    void synthesizeWithAi();
    void onAiReplyReceived(const QString& requestId, const QString& content, const QVariant& payload);
    void onAiReplyFailed(const QString& requestId, const QString& errorMessage, bool authError, const QVariant& payload);
    void onAiQuotaExceeded(const QString& requestId, const QString& detail, int used, int limit, const QVariant& payload);

    // --- Этап 4: сохранение отчёта и передача в Storm Reader ---
    void saveReportAndFinish(const QString& aiText);
    bool saveAsTxt(const QString& filePath, const QString& title, const QString& body, QString& errorOut) const;
    bool saveAsPdf(const QString& filePath, const QString& title, const QString& body, QString& errorOut) const;
    QString buildReportFilePath(const QString& title) const; // директорию берёт из ReaderWidget::getBooksDir()

    QNetworkAccessManager* net(); // ленивое создание — обязательно уже в потоке воркера, используется для сбора ссылок/чтения страниц (НЕ для запросов к ИИ — те идут через m_aiClient)

    void fail(const QString& message); // сброс состояния "занят" + сигнал ошибки
    void reportProgress(const QString& stageText, int percent);

    QNetworkAccessManager* m_net = nullptr;
    AiClient* m_aiClient = nullptr; // дочерний QObject — создаётся в конструкторе с parent=this, поэтому переезжает вместе с ResearchManager при moveToThread()

    // --- Состояние текущего запуска ---
    QString m_topic;
    QString m_depth;
    QString m_format;
    QStringList m_pendingUrls;     // ещё не прочитанные ссылки
    QStringList m_collectedChunks; // "Источник N (url):\n<очищенный текст>" по каждой прочитанной странице
    int m_totalSources = 0;
    int m_processedSources = 0;
    int m_totalCollectedChars = 0;
    bool m_cancelled = false;
    bool m_busy = false;
};