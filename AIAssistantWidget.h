#pragma once
#include <QWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>
#include <functional>

class QWebEngineView;
class WebPageAgent;

// Структура для хранения истории сообщений
struct ChatMessage {
    QString role;
    QString content;
};

class AIAssistantWidget : public QWidget {
    Q_OBJECT
public:
    explicit AIAssistantWidget(QWidget* parent = nullptr);
    void setPromptAndFocus(const QString& text);
    void submitPrompt(const QString& text);

    // Точка входа для действий из контекстного меню страницы (перевод, саммари,
    // объяснение и т.д. — см. BrowserWebView::contextMenuEvent). В отличие от обычного
    // sendChatMessage() эти действия работают с уже выделенным текстом и НИКОГДА не
    // требуют доступа к текущей странице, поэтому всегда идут напрямую в "простой" режим
    // (без классификации, без свечения/плашки на странице) и отправляются немедленно.
    void runContextMenuAction(const QString& actionType, const QString& selectedText);

signals:
    // Просит контейнер (обычно MainWindow) показать/поднять панель ИИ — нужно, когда
    // действие пришло не из самого чата (например, из контекстного меню страницы),
    // а панель ИИ может быть в этот момент свёрнута/не в фокусе.
    void panelActivationRequested();

private slots:
    void sendChatMessage();
    void onAgentNetworkReply(QNetworkReply* reply);
    void onQuickNetworkReply(QNetworkReply* reply); // Ответы классификации и "простого" чата (см. ниже) — отдельный менеджер, не пересекается с onAgentNetworkReply агента
    void onSearchNetworkReply(QNetworkReply* reply); // Ответ Tavily (см. performTavilySearch) — свой формат JSON, свой отдельный менеджер
    void attachFile();
    void clearAttachment();
    void onStopAgentClicked();  // Кнопка "Стоп" — прерывает текущую цепочку действий ИИ на странице

private:
    void renderChat();
    void setUIEnabled(bool enabled);
    bool checkAiConfigured(); // Проверяет, настроен ли выбранный бэкенд ИИ; если нет — печатает предупреждение в чат и возвращает false

    // --- Маршрутизация запроса: "поработать со страницей" vs "поискать в интернете" vs "просто ответить" ---
    // Раньше КАЖДОЕ сообщение в чате безусловно тянуло за собой снимок страницы и включало
    // визуальные эффекты агента (плашка "работаю", свечение элементов при клике) — даже для
    // обычных вопросов и просьб перевести/объяснить присланный текст. Теперь для обычных
    // текстовых сообщений сначала выполняется дешёвая классификация запроса самой моделью —
    // на один из трёх режимов ({"mode": "page"|"search"|"chat"}, см. classifierSystemPrompt()):
    //   - "page"   — нужен доступ к открытой сейчас странице → полноценный агентный режим
    //                с работой по странице (как раньше);
    //   - "search" — нужна актуальная информация из интернета, не связанная с открытой
    //                страницей → поиск через Tavily (см. performStandaloneSearch), а затем
    //                обычный ответ с результатами поиска как контекстом;
    //   - "chat"   — обычный вопрос, ни то ни другое не требуется → обычный короткий ответ,
    //                без снимка страницы, без поиска и без визуальных эффектов на вкладке.
    // Действия из контекстного меню (см. runContextMenuAction) в эту классификацию вообще
    // не идут — для них "простой" режим используется всегда напрямую.
    void classifyAndRoute(const QString& fullContent);       // Отправляет лёгкий классифицирующий запрос и решает, что делать дальше: page / search / chat
    // searchContext — необязательный блок с результатами поиска Tavily (см. performStandaloneSearch),
    // добавляется отдельным system-сообщением перед историей чата. Пустая строка — поиск не выполнялся.
    void sendSimpleChatRequest(const QString& fullContent, const QString& searchContext = QString());
    void startPageAgentFlow();                                // Запускает полноценный агентный цикл со страницей (снимок, свечение, действия) — как раньше
    void postQuickRequest(const QJsonArray& messages, bool wantJsonObjectFormat,
        const QString& requestKind, const QVariant& payload); // Общая отправка "одноразового" (неагентного) запроса выбранному провайдеру ИИ

    // --- Поиск в интернете через Tavily (research/tavily_key — тот же ключ, что и у модуля
    // "🔬 Глубокое исследование", см. SettingsBridge::saveAI) ---
    // Раньше боковой ИИ-ассистент не имел собственного доступа в интернет: "обычные" вопросы
    // (mode=="chat") отвечались только по знаниям модели, а классификатор мог направить запрос
    // только на текущую открытую страницу (mode=="page") либо в чат. Теперь классификатор умеет
    // определять и третий случай — mode=="search": вопрос требует актуальной информации из
    // интернета, не связанной с открытой страницей (новости, курсы, факты и т.п.).
    //
    // query — поисковый запрос. mode — "standalone" (обычный вопрос вне агентного цикла: после
    // получения результатов вызывается sendSimpleChatRequest с этими результатами как контекст)
    // либо "agent" (поиск понадобился ИИ-агенту прямо в процессе работы со страницей — см. action
    // "search" в agentSystemPrompt(); после получения результатов агентный цикл просто продолжается
    // дальше через scheduleNextAgentStep, как после любого другого шага). payload транзитом
    // возвращается в onSearchNetworkReply — для "standalone" это исходный текст вопроса пользователя.
    void performTavilySearch(const QString& query, const QString& mode, const QVariant& payload = QVariant());
    void performStandaloneSearch(const QString& fullContent); // Обёртка performTavilySearch(mode="standalone") для классификатора
    // Ключ Tavily не задан или пустой запрос — тот же результат, что и обычный ответ Tavily
    // "ничего не найдено", но без сетевого запроса.
    void onTavilySearchUnavailable(const QString& query, const QString& mode, const QVariant& payload, const QString& reason);

    // --- Работа ИИ-агента с текущей открытой веб-страницей ---
    // Общая идея: КАЖДОЕ сообщение в чате (а не только "умный поиск", как раньше)
    // сопровождается свежим срезом активной вкладки (видимый текст + список
    // интерактивных элементов). ИИ отвечает строгим JSON {"message", "action"} —
    // "message" сразу показывается пользователю в чате, а "action" (если это не
    // "done") тут же выполняется на странице (клик/ввод текста/скролл/переход по
    // ссылке), после чего цикл повторяется автоматически с обновлённым состоянием
    // страницы — пока ИИ не вернёт "done" или не будет достигнут лимит шагов.
    //
    // Сама работа со страницей (снимок, клики, свечение, плашка) вынесена в отдельный
    // класс WebPageAgent (см. WebPageAgent.h/.cpp) — здесь остаётся только оркестрация
    // переписки с ИИ и чат.
    void sendAgentRequest();                     // Собирает и отправляет очередной запрос (первый шаг задачи или продолжение цепочки)
    void finishAgentTask();                      // Общая очистка после завершения/остановки цепочки действий
    bool ensureGigaChatToken(const QString& gigaKey, QString& errorOut); // Общая логика получения/обновления токена GigaChat
    void scheduleNextAgentStep(int fallbackDelayMs);  // Ждёт "оседания" страницы после действия (либо реальной загрузки, либо таймаут) и продолжает цепочку

    // --- "Последний вариант": ИИ смотрит на скриншот страницы, а не только на текст/DOM ---
    // Используется, только когда сама модель решает, что дешёвые способы (клик по id/тексту)
    // не сработали. Работает с любым бэкендом, для которого настроена vision-модель — не
    // привязано к одному конкретному провайдеру ИИ.
    void performVisionLook();
    bool uploadImageToGigaChat(const QByteArray& pngBytes, QString& fileIdOut, QString& errorOut); // GigaChat не принимает картинки инлайн — их нужно сперва загрузить в файловое хранилище и сослаться на id

    QTextBrowser* resultBrowser;
    QTextEdit* chatInput;
    QPushButton* sendBtn;
    QPushButton* attachBtn;
    QPushButton* stopAgentBtn;      // Видна только пока идёт цепочка действий на странице

    QWidget* attachWidget;
    QLabel* attachLabel;
    QPushButton* attachClearBtn;

    QString attachedFileText;
    QString attachedFilename;
    QString cachedGigaToken;
    qint64 gigaTokenExpireTime = 0;

    QList<ChatMessage> chatHistory;
    QNetworkAccessManager* networkManager;      // Только для агентного цикла (onAgentNetworkReply) — снимок страницы + действия
    QNetworkAccessManager* m_quickManager;      // Отдельный менеджер для классификации и "простых" ответов (onQuickNetworkReply) — намеренно отдельный от networkManager, чтобы их сигналы finished не пересекались
    QNetworkAccessManager* m_searchManager;     // Отдельный менеджер для запросов к Tavily (onSearchNetworkReply) — формат ответа не похож на chat-completion провайдеров ИИ, поэтому не смешивается с остальными менеджерами
    WebPageAgent* m_pageAgent; // "Руки и глаза" на странице — снимки, клики, визуальные эффекты (см. WebPageAgent)

    // --- Состояние текущей цепочки действий агента ---
    bool m_busy = false;        // Идёт ЛЮБОЙ запрос — классификация, простой ответ или полный агентный цикл (блокирует повторную отправку)
    bool m_agentRunning = false;
    int m_agentStepCount = 0;
    QString m_lastActionSummary;         // Короткое описание последнего выполненного шага — передаётся ИИ как контекст следующего запроса
    QString m_lastSeenUrl;                // URL страницы на предыдущем шаге — для проверки "действие реально что-то изменило"
    QString m_lastSeenPageText;           // Видимый текст страницы на предыдущем шаге — та же проверка
    static constexpr int MAX_AGENT_STEPS = 15; // Предохранитель от зацикливания на сложных/непредвиденных сайтах
};