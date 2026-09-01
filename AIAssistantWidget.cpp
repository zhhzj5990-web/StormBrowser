#include "AIAssistantWidget.h"
#include "WebPageAgent.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QFile>
#include <QFileInfo>
#include <QTextCursor>
#include <QEventLoop>
#include <QUuid>
#include <QSslConfiguration>
#include <QDateTime>
#include <QTimer>
#include <QUrl>
#include <QHttpMultiPart>
#include "MainWindow.h"
#include <QWebEngineView>
#include <QWebEnginePage>
#include <memory>

// --- ОДНОРАЗОВАЯ МИГРАЦИЯ СТАРОГО КЛЮЧА ИИ ---
static void migrateLegacyAiKey() {
    QSettings s;
    if (!s.contains("ai/api_key")) return;

    QString legacyKey = s.value("ai/api_key", "").toString();
    if (!legacyKey.isEmpty() && s.value("ai/openrouter_key", "").toString().isEmpty()) {
        s.setValue("ai/openrouter_key", legacyKey);
    }
    s.remove("ai/api_key");
}

static void applyGigaChatSslBypass(QNetworkRequest& req) {
    QSslConfiguration sslConf = req.sslConfiguration();
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(sslConf);
}

// Системный промпт "агентного" режима — отправляется с КАЖДЫМ сообщением.
// Модель обязана отвечать строгим JSON: {"message": "...", "action": {...}}.
// "message" — то, что видит пользователь в чате (объяснение шага или итог).
// "action.type" — "click" / "type" / "scroll" / "navigate" / "done".
// Если действий на странице не требуется (обычный вопрос) — сразу "done".
static QString agentSystemPrompt() {
    return QString::fromUtf8(u8R"PROMPT(Ты Storm AI — ассистент, встроенный в браузер Storm Browser. Ты видишь текст текущей открытой пользователем страницы и список её видимых интерактивных элементов (ссылки, кнопки, поля ввода, чекбоксы), и можешь взаимодействовать с этой страницей, чтобы выполнить задачу пользователя: найти информацию, нажать кнопку, заполнить и отправить форму, перейти по ссылке и так далее.

Каждый интерактивный элемент приходит с числовым полем "id" — используй именно это число как "target" в своём действии, не пытайся придумывать CSS-селекторы или XPath.

Отвечай СТРОГО одним JSON-объектом, без какого-либо текста до или после него, в формате:
{"message": "текст для пользователя в чате: что ты сейчас делаешь и почему, либо итоговый результат", "action": {"type": "click | type | scroll | navigate | look | search | done", "target": <id элемента для click/type/scroll>, "target_text": "точный видимый текст элемента (запасной вариант для click)", "value": "текст для ввода при type", "url": "адрес при navigate", "query": "поисковый запрос при search"}}

Правила:
- Одно действие за один ответ. После выполнения тебе пришлют обновлённое состояние страницы, и ты сможешь продолжить.
- Для click в первую очередь используй "target" (id из списка elements). Если нужный элемент (например, карточка сообщества, товар, строка списка) виден в тексте страницы, но НЕ попал в список elements — всё равно верни "action":{"type":"click"}, но вместо "target" укажи "target_text" с точным видимым текстом этого элемента (или его отличительной частью) — система сама найдёт и нажмёт его на странице.
- "search" — ищет свежую информацию в реальном интернете (через Tavily), а не на текущей странице. Используй, когда для ответа или для шага задачи не хватает данных с самой страницы: нужна новость, курс, цена, факт, дата или любые актуальные сведения извне. Укажи короткий чёткий запрос в поле "query". Результаты поиска придут тебе вместе с состоянием на следующем шаге — сам по себе "search" ничего на странице не меняет. Если поиск окажется недоступен (например, не задан ключ Tavily в настройках), тебе придёт об этом сообщение — в этом случае отвечай по своим знаниям и предупреди об этом пользователя в "message".
- "look" — самый крайний и самый дорогой вариант: он делает снимок экрана страницы и присылает тебе его как картинку, чтобы ты сориентировался визуально. Используй его, только если несколько попыток через click/target_text/scroll не помогли разобраться в странице по тексту — не в начале задачи и не при каждом шаге. Если "look" по какой-то причине недоступен (например, для текущего бэкенда не настроена vision-модель), тебе придёт об этом сообщение — в этом случае просто продолжай обычными способами.
- Никогда не угадывай адрес для "navigate" внутри того же сайта (не выдумывай /ссылки/, /id, /слаги и т.п.). "navigate" используй только если точный адрес есть в предоставленных данных (например, в поле href элемента) или это переход на заведомо известный внешний сайт по прямой просьбе пользователя. Для перехода внутри сайта (открыть карточку, раздел, профиль) всегда предпочитай click/target_text, а не navigate.
- Если предыдущее действие завершилось со статусом "не удалось" ИЛИ проверка показала, что страница не изменилась — не повторяй его в том же виде ещё раз. Попробуй другой способ: click по target_text вместо id, прокрути страницу (scroll), либо посмотри на обновлённый список elements и выбери другой подходящий элемент.
- Прежде чем написать "мы уже на нужной странице/разделе" — ВСЕГДА сверяйся со строками "Текущий адрес (URL)" и "Заголовок вкладки" в присланном состоянии страницы, а не только с текстом на странице. Упоминание, репост или ссылка на что-либо в тексте страницы (например, пост на стене пользователя со ссылкой на сообщество) — это НЕ доказательство, что ты уже находишься на этой странице. Если URL/заголовок не соответствуют цели — ты туда не попал, продолжай (клик по ссылке/карточке, либо новая попытка).
- Если для ответа не нужно ничего делать на странице (обычный вопрос, объяснение и т.п.) — сразу верни "action": {"type": "done"}, а весь ответ помести в "message".
- Если задача выполнена, невозможна или зашла в тупик после нескольких разных попыток — верни "action": {"type": "done"} и понятно объясни итог в "message".
- Пиши в "message" по-русски, кратко и по-человечески — это читает пользователь в чате в реальном времени.
- Никогда не выдумывай данные, которых нет на странице. Не совершай необратимые или чувствительные действия (оплата, удаление данных, отправка личной информации, подтверждение необратимых операций) без явной прямой просьбы пользователя именно об этом шаге.)PROMPT");
}

// Лёгкий "классификатор" намерения — отправляется ОТДЕЛЬНО от основной агентной логики,
// без снимка страницы (дёшево и быстро). Задача — выбрать один из трёх режимов обработки
// ПОСЛЕДНЕГО сообщения пользователя: работа с открытой страницей, поиск в интернете через
// Tavily (см. performStandaloneSearch), либо обычный ответ без того и другого.
static QString classifierSystemPrompt() {
    return QString::fromUtf8(u8R"PROMPT(Ты — маршрутизатор запросов ассистента Storm AI, встроенного в браузер. Единственная задача — определить, какой режим обработки нужен ПОСЛЕДНЕМУ сообщению пользователя.

Отвечай СТРОГО одним JSON-объектом без какого-либо текста до или после: {"mode": "page"} или {"mode": "search"} или {"mode": "chat"}.

mode = "page" — пользователь просит найти/показать/объяснить что-то на ОТКРЫТОЙ СЕЙЧАС странице или сайте, нажать кнопку, заполнить форму, прокрутить, перейти по ссылке, оформить заказ, авторизоваться, или иным образом взаимодействовать с текущей вкладкой браузера.
mode = "search" — вопрос требует АКТУАЛЬНОЙ информации из интернета, не связанной с открытой сейчас страницей: новости, текущие события, курсы, цены, факты, которые могли устареть или измениться, недавние даты/версии/должности людей и т.п. — то, на что лучше ответить, реально поискав в сети, а не полагаясь только на "память" модели.
mode = "chat" — обычный вопрос общего характера: объяснение понятия, помощь с кодом, творческая задача, перевод/объяснение/переписывание/суммирование уже присланного в сообщении текста и т.п. — не требует ни доступа к странице, ни свежего поиска.

Если сомневаешься между "search" и "chat" — выбирай "chat". Если сомневаешься между "page" и "search" — выбирай "page", когда пользователь явно ссылается на что-то видимое сейчас на экране, иначе "search".)PROMPT");
}

// Системный промпт для "простого" режима — обычный ответ без снимка страницы и без
// агентного JSON-протокола {"message","action"}. Используется, когда классификатор решил,
// что доступ к странице не нужен, а также всегда для действий из контекстного меню.
static QString simpleChatSystemPrompt() {
    return QString::fromUtf8(u8"Ты Storm AI — ассистент, встроенный в браузер Storm Browser. Отвечай кратко, ёмко, полезно и красиво форматируй текст на русском языке (или на языке пользователя, если он пишет не по-русски). Если пользователь прислал текст для перевода, объяснения, саммари, улучшения грамматики или анализа кода — работай именно с этим текстом напрямую, без лишних уточняющих вопросов.");
}

// Готовые формулировки задачи для пунктов контекстного меню "🤖 Storm AI" — то же самое
// действие (actionName), что передаётся в BrowserWebView::contextMenuEvent.
static QString buildQuickActionPrompt(const QString& actionType, const QString& text) {
    if (actionType == "translate") {
        return u8"Переведи следующий текст (если он на русском — переведи на английский, если на любом другом языке — переведи на русский). Верни только перевод, без пояснений:\n\n" + text;
    }
    if (actionType == "explain") {
        return u8"Объясни простыми словами, что означает следующий текст/фрагмент:\n\n" + text;
    }
    if (actionType == "summarize") {
        return u8"Сделай краткую выжимку (саммари) следующего текста, выдели главные мысли по пунктам:\n\n" + text;
    }
    if (actionType == "write_post") {
        return u8"На основе следующего текста напиши пост или ответ — грамотно, живо и по существу, сохранив смысл и ключевые факты:\n\n" + text;
    }
    if (actionType == "fix_grammar") {
        return u8"Исправь грамматические, орфографические и пунктуационные ошибки в следующем тексте, сохранив стиль и смысл. Верни только исправленный текст:\n\n" + text;
    }
    if (actionType == "explain_code") {
        return u8"Объясни, что делает следующий код, и укажи возможные баги или проблемы, если они есть:\n\n" + text;
    }
    return text;
}

AIAssistantWidget::AIAssistantWidget(QWidget* parent) : QWidget(parent) {
    migrateLegacyAiKey();

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(10, 10, 10, 10);

    // Шапка
    QLabel* titleLbl = new QLabel(u8"<b>🤖 Storm AI</b>", this);
    titleLbl->setStyleSheet("font-size: 16px; color: #a371f7;");
    layout->addWidget(titleLbl);

    QLabel* infoLbl = new QLabel(u8"👁 ИИ видит текущую открытую страницу и может сам выполнять на ней действия по вашей просьбе (нажимать кнопки, заполнять формы, переходить по ссылкам). Многошаговые задачи расходуют больше AI-токенов.", this);
    infoLbl->setStyleSheet("color: #8b949e; font-size: 11px;");
    infoLbl->setWordWrap(true);
    layout->addWidget(infoLbl);

    // Кнопка остановки текущей цепочки действий — видна только пока ИИ работает на странице
    stopAgentBtn = new QPushButton(u8"🛑 Остановить", this);
    stopAgentBtn->setStyleSheet("background-color: #ff5f5f; color: white; border-radius: 6px; padding: 8px; font-weight: bold;");
    stopAgentBtn->setCursor(Qt::PointingHandCursor);
    connect(stopAgentBtn, &QPushButton::clicked, this, &AIAssistantWidget::onStopAgentClicked);
    stopAgentBtn->hide();
    layout->addWidget(stopAgentBtn);

    // Окно чата
    resultBrowser = new QTextBrowser(this);
    resultBrowser->setPlaceholderText(u8"Напишите свой вопрос или задачу в чат ниже!");
    resultBrowser->setStyleSheet("font-size: 14px; line-height: 1.5; background-color: rgba(0,0,0,0.2); border: 1px solid #444; border-radius: 6px; padding: 5px;");
    resultBrowser->setOpenLinks(false);
    layout->addWidget(resultBrowser, 1);

    // Виджет прикрепленного файла
    attachWidget = new QWidget(this);
    QHBoxLayout* attLayout = new QHBoxLayout(attachWidget);
    attLayout->setContentsMargins(0, 0, 0, 0);
    attachLabel = new QLabel(this);
    attachLabel->setStyleSheet("color: #56d39b; font-size: 12px; font-weight: bold;");
    attachClearBtn = new QPushButton(u8"❌", this);
    attachClearBtn->setFixedSize(20, 20);
    attachClearBtn->setStyleSheet("background: #ff5f5f; color: white; border-radius: 4px; border: none;");
    connect(attachClearBtn, &QPushButton::clicked, this, &AIAssistantWidget::clearAttachment);
    attLayout->addWidget(attachLabel);
    attLayout->addWidget(attachClearBtn);
    attLayout->addStretch();
    attachWidget->hide();
    layout->addWidget(attachWidget);

    // Блок ввода и кнопок
    QHBoxLayout* chatLayout = new QHBoxLayout();
    chatInput = new QTextEdit(this);
    chatInput->setFixedHeight(65);
    chatInput->setPlaceholderText(u8"Спросить или поставить задачу...");
    chatInput->setStyleSheet("background: rgba(0,0,0,0.6); border: 1px solid #444; border-radius: 6px; color: white; padding: 8px;");

    QVBoxLayout* btnsLayout = new QVBoxLayout();
    btnsLayout->setSpacing(5);

    attachBtn = new QPushButton(u8"📎", this);
    attachBtn->setFixedSize(40, 30);
    attachBtn->setStyleSheet("background-color: #28a745; color: white; border-radius: 6px; font-size: 14px;");
    attachBtn->setCursor(Qt::PointingHandCursor);
    connect(attachBtn, &QPushButton::clicked, this, &AIAssistantWidget::attachFile);

    sendBtn = new QPushButton(u8"➤", this);
    sendBtn->setFixedSize(40, 30);
    sendBtn->setStyleSheet("background-color: #0078D7; color: white; border-radius: 6px; font-size: 14px;");
    sendBtn->setCursor(Qt::PointingHandCursor);
    connect(sendBtn, &QPushButton::clicked, this, &AIAssistantWidget::sendChatMessage);

    btnsLayout->addWidget(attachBtn);
    btnsLayout->addWidget(sendBtn);

    chatLayout->addWidget(chatInput, 1);
    chatLayout->addLayout(btnsLayout);
    layout->addLayout(chatLayout);

    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &AIAssistantWidget::onAgentNetworkReply);

    m_quickManager = new QNetworkAccessManager(this);
    connect(m_quickManager, &QNetworkAccessManager::finished, this, &AIAssistantWidget::onQuickNetworkReply);

    m_searchManager = new QNetworkAccessManager(this);
    connect(m_searchManager, &QNetworkAccessManager::finished, this, &AIAssistantWidget::onSearchNetworkReply);

    m_pageAgent = new WebPageAgent([this]() -> QWebEngineView* {
        MainWindow* mw = qobject_cast<MainWindow*>(window());
        if (!mw || !mw->getTabWidget()) return nullptr;
        return qobject_cast<QWebEngineView*>(mw->getTabWidget()->currentWidget());
        }, this);
}

void AIAssistantWidget::setPromptAndFocus(const QString& text) {
    chatInput->setPlainText(text);
    chatInput->setFocus();
    QTextCursor cursor = chatInput->textCursor();
    cursor.movePosition(QTextCursor::End);
    chatInput->setTextCursor(cursor);
}

void AIAssistantWidget::submitPrompt(const QString& text) {
    setPromptAndFocus(text);
    sendChatMessage();
}

void AIAssistantWidget::attachFile() {
    QString filepath = QFileDialog::getOpenFileName(this, u8"Прикрепить файл", "", "Text files (*.txt *.md *.json *.csv *.html);;All files (*.*)");
    if (filepath.isEmpty()) return;

    QFile file(filepath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        attachedFileText = QString::fromUtf8(file.readAll());
        attachedFilename = QFileInfo(filepath).fileName();
        attachLabel->setText(u8"📎 Прикреплен: " + attachedFilename);
        attachWidget->show();
        file.close();
    }
    else {
        QMessageBox::critical(this, u8"Ошибка", u8"Не удалось прочитать файл!");
    }
}

void AIAssistantWidget::clearAttachment() {
    attachedFileText.clear();
    attachedFilename.clear();
    attachWidget->hide();
}

void AIAssistantWidget::renderChat() {
    resultBrowser->clear();
    for (const auto& msg : chatHistory) {
        QString formattedText = msg.content.toHtmlEscaped();
        formattedText.replace("\n", "<br>");

        if (msg.role == "user") {
            resultBrowser->append(u8"<div style='background: rgba(88,166,255,0.15); padding: 10px; border-radius: 8px; margin: 10px 0;'><b style='color:#58a6ff;'>Вы:</b><br>" + formattedText + u8"</div>");
        }
        else {
            resultBrowser->append(u8"<div style='background: rgba(163,113,247,0.15); padding: 10px; border-radius: 8px; margin: 10px 0;'><b style='color:#a371f7;'>Storm AI:</b><br>" + formattedText + u8"</div>");
        }
    }
}

void AIAssistantWidget::setUIEnabled(bool enabled) {
    sendBtn->setEnabled(enabled);
    chatInput->setEnabled(enabled);
    if (enabled) chatInput->setFocus();
}

bool AIAssistantWidget::ensureGigaChatToken(const QString& gigaKey, QString& errorOut) {
    qint64 currentTime = QDateTime::currentSecsSinceEpoch();
    if (!cachedGigaToken.isEmpty() && currentTime < gigaTokenExpireTime) return true;

    QNetworkRequest authReq(QUrl("https://ngw.devices.sberbank.ru:9443/api/v2/oauth"));
    authReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    authReq.setRawHeader("Accept", "application/json");
    authReq.setRawHeader("RqUID", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
    authReq.setRawHeader("Authorization", ("Basic " + gigaKey).toUtf8());
    applyGigaChatSslBypass(authReq);

    QNetworkAccessManager authManager;
    QNetworkReply* authReply = authManager.post(authReq, "scope=GIGACHAT_API_PERS");
    QEventLoop loop;
    QObject::connect(authReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool ok = (authReply->error() == QNetworkReply::NoError);
    if (ok) {
        QJsonDocument authDoc = QJsonDocument::fromJson(authReply->readAll());
        cachedGigaToken = authDoc.object().value("access_token").toString();
        gigaTokenExpireTime = currentTime + 1740;
    }
    else {
        errorOut = authReply->errorString();
    }
    authReply->deleteLater();
    return ok;
}

bool AIAssistantWidget::uploadImageToGigaChat(const QByteArray& pngBytes, QString& fileIdOut, QString& errorOut) {
    QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart purposePart;
    purposePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"purpose\""));
    purposePart.setBody("general");
    multiPart->append(purposePart);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("image/png"));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"file\"; filename=\"screenshot.png\""));
    filePart.setBody(pngBytes);
    multiPart->append(filePart);

    QNetworkRequest request(QUrl("https://api.giga.chat/v1/files"));
    request.setRawHeader("Authorization", ("Bearer " + cachedGigaToken).toUtf8());
    applyGigaChatSslBypass(request);

    QNetworkAccessManager uploadManager;
    QNetworkReply* reply = uploadManager.post(request, multiPart);
    multiPart->setParent(reply); // удалится вместе с ответом

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool ok = (reply->error() == QNetworkReply::NoError);
    if (ok) {
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        fileIdOut = obj.value("id").toString();
        ok = !fileIdOut.isEmpty();
        if (!ok) errorOut = u8"сервер не вернул id загруженного файла";
    }
    else {
        errorOut = reply->errorString();
    }
    reply->deleteLater();
    return ok;
}

// =========================================================================================
// --- ОСНОВНОЙ ЦИКЛ ЧАТА/АГЕНТА ---
// =========================================================================================

bool AIAssistantWidget::checkAiConfigured() {
    QSettings settings;
    int aiMode = settings.value("ai/mode", 0).toInt();
    bool useCustomApi = (aiMode == 0);
    bool useGigaChat = (aiMode == 2);

    QString apiKey = settings.value("ai/openrouter_key", "").toString();
    QString gigaKey = settings.value("ai/gigachat_key", "").toString();

    if (useCustomApi && apiKey.isEmpty()) {
        resultBrowser->append(u8"<br><b style='color:#ff5f5f;'>⚠️ Введите API-ключ OpenRouter в Настройки → ⚙️ → 🤖 Настройки ИИ и нажмите «Сохранить».</b>");
        return false;
    }
    if (useGigaChat && gigaKey.isEmpty()) {
        resultBrowser->append(u8"<br><b style='color:#ff5f5f;'>⚠️ Введите ключ авторизации GigaChat в Настройки → ⚙️ → 🤖 Настройки ИИ.</b>");
        return false;
    }

    QString user = settings.value("sync/username", "").toString();
    QString pwd = settings.value("sync/password", "").toString();
    if (!useCustomApi && !useGigaChat && (user.isEmpty() || pwd.isEmpty())) {
        resultBrowser->append(u8"<br><b style='color:#ff5f5f;'>⚠️ Войдите в Личный кабинет в Настройки → ⚙️ → 🤖 Настройки ИИ, либо выберите «Свой API» или GigaChat.</b>");
        return false;
    }
    return true;
}

void AIAssistantWidget::sendChatMessage() {
    if (m_busy) {
        // Пока не завершился текущий запрос (классификация / простой ответ / цепочка действий
        // на странице) — новую задачу не запускаем, чтобы не отправить два параллельных запроса.
        resultBrowser->append(u8"<br>⏳ <b style='color:#8b949e;'>Дождитесь завершения текущего запроса или нажмите «Остановить».</b>");
        return;
    }

    QString userText = chatInput->toPlainText().trimmed();
    bool hasFile = !attachedFileText.isEmpty();

    if (userText.isEmpty() && !hasFile) return;
    if (!checkAiConfigured()) return;

    QString fullContent = userText;
    if (hasFile) {
        if (!userText.isEmpty()) {
            fullContent = userText + "\n\n--- [Файл: " + attachedFilename + "] ---\n" + attachedFileText;
        }
        else {
            fullContent = u8"Проанализируй файл:\n\n--- [Файл: " + attachedFilename + "] ---\n" + attachedFileText;
        }
        clearAttachment();
    }

    chatInput->clear();
    chatHistory.append(ChatMessage{ "user", fullContent });
    if (chatHistory.size() > 10) chatHistory.removeFirst();
    renderChat();

    m_busy = true;
    setUIEnabled(false);
    resultBrowser->append(u8"<br><i>⏳ Storm AI думает...</i>");

    if (hasFile) {
        // Анализ прикреплённого файла не требует текущей открытой страницы — сразу простой
        // ответ, без классификации и без снимка/эффектов на вкладке.
        sendSimpleChatRequest(fullContent);
    }
    else {
        // Решаем, нужен ли вообще доступ к открытой странице, ПЕРЕД тем как включать
        // полноценный агентный режим (снимок страницы, свечение, плашка "работаю").
        classifyAndRoute(fullContent);
    }
}

void AIAssistantWidget::classifyAndRoute(const QString& fullContent) {
    QJsonArray messages;
    QJsonObject sys; sys["role"] = "system"; sys["content"] = classifierSystemPrompt();
    messages.append(sys);

    // Немного недавнего контекста диалога, а не только последнее сообщение — помогает
    // правильно понять уточняющие/продолжающие запросы вроде "а переведи это же".
    int histStart = qMax(0, chatHistory.size() - 4);
    for (int i = histStart; i < chatHistory.size(); ++i) {
        QJsonObject m; m["role"] = chatHistory[i].role; m["content"] = chatHistory[i].content;
        messages.append(m);
    }

    postQuickRequest(messages, /*wantJsonObjectFormat=*/true, "classify", fullContent);
}

void AIAssistantWidget::sendSimpleChatRequest(const QString& fullContent, const QString& searchContext) {
    Q_UNUSED(fullContent); // содержимое уже добавлено в chatHistory вызывающей стороной

    QJsonArray messages;
    QJsonObject sys; sys["role"] = "system"; sys["content"] = simpleChatSystemPrompt();
    messages.append(sys);

    // Результаты поиска Tavily (если он выполнялся, см. performStandaloneSearch) —
    // отдельным system-сообщением ПЕРЕД историей чата, а не подмешиванием в сам вопрос
    // пользователя, чтобы не искажать то, что реально написал пользователь в chatHistory.
    if (!searchContext.isEmpty()) {
        QJsonObject searchMsg;
        searchMsg["role"] = "system";
        searchMsg["content"] = searchContext
            + u8"\n\nИспользуй эти результаты поиска, если они относятся к вопросу пользователя, и по возможности указывай источники (адреса сайтов), на которые опираешься. Если результаты не относятся к делу — отвечай на основе своих знаний, не упоминая сам факт неудачного поиска.";
        messages.append(searchMsg);
    }

    for (const auto& msg : chatHistory) {
        QJsonObject m; m["role"] = msg.role; m["content"] = msg.content;
        messages.append(m);
    }

    postQuickRequest(messages, /*wantJsonObjectFormat=*/false, "simple", QVariant());
}

// =========================================================================================
// --- ПОИСК В ИНТЕРНЕТЕ ЧЕРЕЗ TAVILY ---
// Один и тот же метод обслуживает два случая: обычный вопрос вне агентного цикла
// (mode="standalone", см. performStandaloneSearch — классификатор решил, что нужен свежий
// поиск, но не сама открытая страница) и поиск, запрошенный ИИ-агентом прямо посреди
// работы со страницей (mode="agent", см. action "search" в agentSystemPrompt() и его
// обработку в onAgentNetworkReply). Формат ответа Tavily не похож на chat-completion
// провайдеров ИИ, поэтому запросы идут через отдельный m_searchManager/onSearchNetworkReply,
// а не через networkManager/m_quickManager.
// =========================================================================================

void AIAssistantWidget::performStandaloneSearch(const QString& fullContent) {
    // payload = fullContent — исходный вопрос пользователя, он же станет query для Tavily и
    // вернётся обратно в onSearchNetworkReply, чтобы после поиска отправить его в
    // sendSimpleChatRequest вместе с результатами как контекстом.
    performTavilySearch(fullContent, QStringLiteral("standalone"), fullContent);
}

void AIAssistantWidget::performTavilySearch(const QString& query, const QString& mode, const QVariant& payload) {
    QString trimmedQuery = query.trimmed();

    QSettings settings;
    QString tavilyKey = settings.value("research/tavily_key", "").toString().trimmed();

    if (tavilyKey.isEmpty()) {
        onTavilySearchUnavailable(trimmedQuery, mode, payload, u8"не задан ключ Tavily (Настройки → ⚙️ → 🤖 Настройки ИИ → «Поиск источников»)");
        return;
    }
    if (trimmedQuery.isEmpty()) {
        onTavilySearchUnavailable(trimmedQuery, mode, payload, u8"пустой поисковый запрос");
        return;
    }

    QJsonObject body;
    body["api_key"] = tavilyKey;
    body["query"] = trimmedQuery;
    body["search_depth"] = QString("basic");
    body["include_answer"] = true;
    body["max_results"] = 5;

    QNetworkRequest request(QUrl("https://api.tavily.com/search"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QNetworkReply* reply = m_searchManager->post(request, QJsonDocument(body).toJson());
    reply->setProperty("stormSearchQuery", trimmedQuery);
    reply->setProperty("stormSearchMode", mode);
    reply->setProperty("stormSearchPayload", payload);
}

// Ключ не задан или пустой запрос — обрабатываем как обычный "неудачный" исход поиска, но
// без единого сетевого запроса: тот же путь, что и при реальном ответе Tavily без результатов.
void AIAssistantWidget::onTavilySearchUnavailable(const QString& query, const QString& mode, const QVariant& payload, const QString& reason) {
    if (mode == "agent") {
        if (!m_agentRunning) return;
        m_lastActionSummary = u8"[search] по запросу «" + query + u8"» — поиск недоступен: " + reason + u8". Отвечай на основе своих знаний и упомяни в message, что живой поиск сейчас недоступен.";
        scheduleNextAgentStep(200);
        return;
    }

    // mode == "standalone" — payload это исходный текст вопроса пользователя; тихо
    // откатываемся на обычный ответ без результатов поиска (пользователю не за что
    // извиняться — ключ Tavily необязателен, это просто улучшение точности при наличии).
    sendSimpleChatRequest(payload.toString());
}

void AIAssistantWidget::onSearchNetworkReply(QNetworkReply* reply) {
    reply->deleteLater();

    QString query = reply->property("stormSearchQuery").toString();
    QString mode = reply->property("stormSearchMode").toString();
    QVariant payload = reply->property("stormSearchPayload");

    if (mode == "agent" && !m_agentRunning) return; // цепочка действий уже остановлена/завершена

    if (reply->error() != QNetworkReply::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString reason = (statusCode == 401 || statusCode == 403)
            ? u8"ключ Tavily отклонён (неверный или истёк)"
            : (statusCode == 429)
            ? u8"исчерпан месячный лимит запросов Tavily"
            : (u8"ошибка сети (" + QString::number(statusCode) + u8"): " + reply->errorString());
        onTavilySearchUnavailable(query, mode, payload, reason);
        return;
    }

    QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    QString answer = root.value("answer").toString().trimmed();
    QJsonArray results = root.value("results").toArray();

    QString summary;
    if (!answer.isEmpty()) {
        summary += u8"Краткий ответ (по данным Tavily): " + answer + u8"\n\n";
    }
    if (!results.isEmpty()) {
        summary += u8"Источники:\n";
        int n = 0;
        for (const QJsonValue& v : results) {
            if (n >= 5) break;
            QJsonObject r = v.toObject();
            QString title = r.value("title").toString();
            QString url = r.value("url").toString();
            QString snippet = r.value("content").toString();
            if (snippet.length() > 500) snippet = snippet.left(500) + u8"…";
            summary += QString(u8"%1. %2\n%3\n%4\n\n").arg(++n).arg(title, url, snippet);
        }
    }
    if (summary.trimmed().isEmpty()) {
        onTavilySearchUnavailable(query, mode, payload, u8"по этому запросу ничего не нашлось");
        return;
    }

    if (mode == "agent") {
        m_lastActionSummary = u8"[search] по запросу «" + query + u8"» — результаты поиска в интернете (Tavily):\n" + summary;
        scheduleNextAgentStep(300);
    }
    else {
        QString searchContext = u8"[Результаты поиска в интернете по запросу «" + query + u8"» через Tavily]\n" + summary;
        sendSimpleChatRequest(payload.toString(), searchContext);
    }
}

void AIAssistantWidget::startPageAgentFlow() {
    m_agentStepCount = 0;
    m_lastActionSummary = u8"Действие ещё не выполнялось.";
    m_lastSeenUrl.clear();
    m_lastSeenPageText.clear();
    m_pageAgent->beginTask(); // Фиксирует активную вкладку под эту задачу и подчищает возможные старые эффекты на ней

    m_agentRunning = true;
    stopAgentBtn->show();

    resultBrowser->append(u8"<br><i>⏳ Storm AI смотрит на страницу...</i>");
    m_pageAgent->showWorking(true);

    sendAgentRequest();
}

void AIAssistantWidget::runContextMenuAction(const QString& actionType, const QString& selectedText) {
    emit panelActivationRequested(); // Просим контейнер показать/поднять панель ИИ

    if (m_busy) {
        resultBrowser->append(u8"<br>⏳ <b style='color:#8b949e;'>Дождитесь завершения текущего запроса или нажмите «Остановить».</b>");
        return;
    }
    if (selectedText.trimmed().isEmpty()) {
        resultBrowser->append(u8"<br><b style='color:#ff5f5f;'>⚠️ Сначала выделите текст на странице, затем выберите действие в меню 🤖 Storm AI.</b>");
        return;
    }
    if (!checkAiConfigured()) return;

    QString fullContent = buildQuickActionPrompt(actionType, selectedText);

    chatHistory.append(ChatMessage{ "user", fullContent });
    if (chatHistory.size() > 10) chatHistory.removeFirst();
    renderChat();

    m_busy = true;
    setUIEnabled(false);
    resultBrowser->append(u8"<br><i>⏳ Storm AI думает...</i>");

    sendSimpleChatRequest(fullContent);
}

void AIAssistantWidget::postQuickRequest(const QJsonArray& messages, bool wantJsonObjectFormat,
    const QString& requestKind, const QVariant& payload) {

    QSettings settings;
    int aiMode = settings.value("ai/mode", 0).toInt();
    bool useCustomApi = (aiMode == 0);
    bool useGigaChat = (aiMode == 2);
    QString apiKey = settings.value("ai/openrouter_key", "").toString();
    QString gigaKey = settings.value("ai/gigachat_key", "").toString();
    QString gigaModel = settings.value("ai/gigachat_model", "GigaChat-2").toString();
    QString user = settings.value("sync/username", "").toString();
    QString pwd = settings.value("sync/password", "").toString();

    QJsonObject json;
    json["messages"] = messages;

    QNetworkRequest request;
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    QNetworkReply* reply = nullptr;

    if (useCustomApi) {
        json["model"] = QString("openrouter/auto");
        if (wantJsonObjectFormat) json["response_format"] = QJsonObject{ {"type", "json_object"} };
        request.setUrl(QUrl("https://openrouter.ai/api/v1/chat/completions"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
        request.setRawHeader("HTTP-Referer", "https://storm-browser.ru/");
        request.setRawHeader("X-Title", "Storm Browser");
        reply = m_quickManager->post(request, QJsonDocument(json).toJson());
    }
    else if (useGigaChat) {
        QString authErr;
        if (!ensureGigaChatToken(gigaKey, authErr)) {
            chatHistory.append(ChatMessage{ "assistant", u8"❌ Ошибка авторизации GigaChat: " + authErr });
            renderChat();
            m_busy = false;
            setUIEnabled(true);
            return;
        }
        json["model"] = gigaModel;
        request.setUrl(QUrl("https://api.giga.chat/v1/chat/completions"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", ("Bearer " + cachedGigaToken).toUtf8());
        applyGigaChatSslBypass(request);
        reply = m_quickManager->post(request, QJsonDocument(json).toJson());
    }
    else {
        json["username"] = user;
        json["password"] = pwd;
        json["user_api_key"] = QString("");
        request.setUrl(QUrl("https://storm-browser.online:8000/api/ai/chat")); // актуальный адрес (было http://147.45.178.149:8000)
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = m_quickManager->post(request, QJsonDocument(json).toJson());
    }

    if (reply) {
        reply->setProperty("stormKind", requestKind);
        reply->setProperty("stormPayload", payload);
    }
}

void AIAssistantWidget::onQuickNetworkReply(QNetworkReply* reply) {
    reply->deleteLater();
    QString kind = reply->property("stormKind").toString();

    if (reply->error() != QNetworkReply::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errorStr = reply->errorString();

        if (kind == "classify") {

            startPageAgentFlow();
            return;
        }

        QString errorMsg = (statusCode == 0)
            ? u8"❌ Нет подключения к серверу. Проверьте интернет."
            : u8"❌ Ошибка " + QString::number(statusCode) + u8": " + errorStr;
        chatHistory.append(ChatMessage{ "assistant", errorMsg });
        renderChat();
        m_busy = false;
        setUIEnabled(true);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject rootObj = doc.object();
    QString content;
    if (rootObj.contains("choices")) {
        QJsonArray choices = rootObj.value("choices").toArray();
        if (!choices.isEmpty()) content = choices[0].toObject().value("message").toObject().value("content").toString().trimmed();
    }
    else if (rootObj.contains("reply")) {
        content = rootObj.value("reply").toString().trimmed();
    }

    if (rootObj.contains("usage")) {
        QSettings tokenSettings;
        int mode = tokenSettings.value("ai/mode", 0).toInt();
        if (mode == 1 || mode == 2) {
            qint64 totalTokens = rootObj.value("usage").toObject().value("total_tokens").toVariant().toLongLong();
            qint64 usedSoFar = tokenSettings.value("ai/tokens_used", 0).toLongLong();
            tokenSettings.setValue("ai/tokens_used", usedSoFar + totalTokens);
        }
    }

    if (kind == "classify") {
        QString fullContent = reply->property("stormPayload").toString();

        // Модель обязана вернуть строгий {"mode": "page"|"search"|"chat"}, но на случай, если она
        // всё же обернула ответ лишним текстом, достаём первую фигурную скобку — как и в
        // основном агентном парсере.
        QString jsonPart = content;
        int jsonStart = content.indexOf('{');
        int jsonEnd = content.lastIndexOf('}');
        if (jsonStart != -1 && jsonEnd != -1 && jsonEnd > jsonStart) {
            jsonPart = content.mid(jsonStart, jsonEnd - jsonStart + 1);
        }
        QJsonParseError parseErr;
        QJsonObject parsed = QJsonDocument::fromJson(jsonPart.toUtf8(), &parseErr).object();

        QString mode = QStringLiteral("page"); // безопасный запасной вариант, если ответ не удалось разобрать
        if (parseErr.error == QJsonParseError::NoError && parsed.contains("mode")) {
            mode = parsed.value("mode").toString(QStringLiteral("page"));
        }

        if (mode == "search") performStandaloneSearch(fullContent);
        else if (mode == "chat") sendSimpleChatRequest(fullContent);
        else startPageAgentFlow();
        return;
    }

    // kind == "simple"
    if (content.isEmpty()) content = u8"⚠️ Сервер вернул пустой ответ или неизвестный формат JSON.";
    chatHistory.append(ChatMessage{ "assistant", content });
    if (chatHistory.size() > 10) chatHistory.removeFirst();
    renderChat();
    m_busy = false;
    setUIEnabled(true);
}

void AIAssistantWidget::sendAgentRequest() {
    if (!m_agentRunning) return;

    if (m_agentStepCount >= MAX_AGENT_STEPS) {
        chatHistory.append(ChatMessage{ "assistant", u8"⚠️ Достигнут лимит шагов (" + QString::number(MAX_AGENT_STEPS) + u8") на этой странице — останавливаюсь, чтобы не зациклиться. Уточните задачу или продолжите вручную." });
        renderChat();
        finishAgentTask();
        return;
    }
    m_agentStepCount++;

    m_pageAgent->capturePageContext([this](const QJsonObject& page) {
        if (!m_agentRunning) return;

        QSettings settings;
        int aiMode = settings.value("ai/mode", 0).toInt();
        bool useCustomApi = (aiMode == 0);
        bool useGigaChat = (aiMode == 2);
        QString apiKey = settings.value("ai/openrouter_key", "").toString();
        QString gigaKey = settings.value("ai/gigachat_key", "").toString();
        QString gigaModel = settings.value("ai/gigachat_model", "GigaChat-2").toString();
        QString user = settings.value("sync/username", "").toString();
        QString pwd = settings.value("sync/password", "").toString();

        QJsonObject sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = agentSystemPrompt();
        QJsonArray messagesArray;
        messagesArray.append(sysMsg);

        for (const auto& msg : chatHistory) {
            QJsonObject m; m["role"] = msg.role; m["content"] = msg.content;
            messagesArray.append(m);
        }

        QString contextContent;
        if (page.value("hasPage").toBool()) {
            QString url = page.value("url").toString();
            QString title = page.value("title").toString();
            QString bodyText = page.value("text").toString();

            QString verificationNote;
            if (m_agentStepCount > 1) {
                bool changed = (url != m_lastSeenUrl) || (bodyText != m_lastSeenPageText);
                verificationNote = changed
                    ? u8"\nПроверка: страница изменилась после последнего действия."
                    : u8"\nПроверка: страница НЕ изменилась после последнего действия — вероятно, клик пришёлся мимо, элемент не тот, или нужен другой способ.";
            }
            m_lastSeenUrl = url;
            m_lastSeenPageText = bodyText;

            QJsonDocument pageDoc(page);
            contextContent = u8"[Текущее состояние открытой страницы]\n"
                u8"Текущий адрес (URL): " + url + u8"\n"
                u8"Заголовок вкладки: " + title + u8"\n\n"
                u8"Полный срез страницы (JSON — текст и интерактивные элементы):\n"
                + QString::fromUtf8(pageDoc.toJson(QJsonDocument::Compact))
                + u8"\n\nПоследнее выполненное действие: " + m_lastActionSummary
                + verificationNote;
        }
        else {
            contextContent = u8"[У пользователя сейчас нет открытой вкладки сайта — действия click/type/scroll/navigate/look выполнить не получится. Доступны только \"search\" (поиск в интернете через Tavily) и \"done\".]";
        }
        QJsonObject ctxMsg; ctxMsg["role"] = "user"; ctxMsg["content"] = contextContent;
        messagesArray.append(ctxMsg);

        QJsonObject json;
        json["messages"] = messagesArray;

        QNetworkRequest request;
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

        if (useCustomApi) {
            json["model"] = QString("openrouter/auto");
            json["route"] = QString("fallback");
            json["response_format"] = QJsonObject{ {"type", "json_object"} };
            request.setUrl(QUrl("https://openrouter.ai/api/v1/chat/completions"));
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
            request.setRawHeader("HTTP-Referer", "https://storm-browser.ru/");
            request.setRawHeader("X-Title", "Storm Browser");
            networkManager->post(request, QJsonDocument(json).toJson());
        }
        else if (useGigaChat) {
            QString authErr;
            if (!ensureGigaChatToken(gigaKey, authErr)) {
                chatHistory.append(ChatMessage{ "assistant", u8"❌ Ошибка авторизации GigaChat: " + authErr });
                renderChat();
                finishAgentTask();
                return;
            }
            json["model"] = gigaModel;
            request.setUrl(QUrl("https://api.giga.chat/v1/chat/completions"));
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            request.setRawHeader("Authorization", ("Bearer " + cachedGigaToken).toUtf8());
            applyGigaChatSslBypass(request);
            networkManager->post(request, QJsonDocument(json).toJson());
        }
        else {
            json["username"] = user;
            json["password"] = pwd;
            json["user_api_key"] = QString("");
            request.setUrl(QUrl("https://storm-browser.online:8000/api/ai/chat")); // актуальный адрес (было http://147.45.178.149:8000)
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            networkManager->post(request, QJsonDocument(json).toJson());
        }
        });
}

void AIAssistantWidget::performVisionLook() {
    if (!m_agentRunning) return;

    if (m_agentStepCount >= MAX_AGENT_STEPS) {
        chatHistory.append(ChatMessage{ "assistant", u8"⚠️ Достигнут лимит шагов (" + QString::number(MAX_AGENT_STEPS) + u8") — останавливаюсь, чтобы не зациклиться." });
        renderChat();
        finishAgentTask();
        return;
    }
    m_agentStepCount++;

    QSettings settings;
    int aiMode = settings.value("ai/mode", 0).toInt();
    bool useCustomApi = (aiMode == 0);
    bool useGigaChat = (aiMode == 2);

    QString visionModel;
    if (useCustomApi) visionModel = settings.value("ai/vision_model", "").toString().trimmed();
    else if (useGigaChat) visionModel = settings.value("ai/gigachat_vision_model", "GigaChat-2-Max").toString().trimmed();

    // Свой ключ есть только у OpenRouter/GigaChat — у собственного бэкенда Storm нет
    // подтверждённой поддержки vision, поэтому туда не отправляем.
    if (visionModel.isEmpty() || (!useCustomApi && !useGigaChat)) {
        m_lastActionSummary = u8"[look] — недоступно: для текущего AI-бэкенда не настроена vision-модель (Настройки → 🤖 Настройки ИИ). Продолжаю обычными способами.";
        scheduleNextAgentStep(200);
        return;
    }

    m_lastActionSummary = u8"[look] — посмотрел на снимок страницы.";

    m_pageAgent->captureScreenshotBase64([this, useCustomApi, useGigaChat, visionModel](const QString& base64Png) {
        if (!m_agentRunning) return;

        if (base64Png.isEmpty()) {
            m_lastActionSummary = u8"[look] — не удалось сделать снимок страницы.";
            scheduleNextAgentStep(200);
            return;
        }

        // Свежий QSettings прямо здесь, а не захват внешнего: тот, что был объявлен в
        // performVisionLook() выше, — локальная переменная стека этой функции, которая уже
        // разрушится к моменту, когда сработает этот асинхронный колбэк (captureScreenshotBase64
        // выполняется позже, уже после возврата из performVisionLook). Тот же приём, что и в
        // sendAgentRequest().
        QSettings settings;

        QJsonObject sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = agentSystemPrompt();
        QJsonArray messagesArray;
        messagesArray.append(sysMsg);

        for (const auto& msg : chatHistory) {
            QJsonObject m; m["role"] = msg.role; m["content"] = msg.content;
            messagesArray.append(m);
        }

        QString promptText = u8"[Скриншот текущей страницы]";
        if (!m_lastSeenUrl.isEmpty()) promptText += u8"\nТекущий адрес (URL): " + m_lastSeenUrl;
        promptText += u8"\nПосмотри на изображение и реши, что делать дальше. Отвечай как обычно — строго JSON (message + action).";

        QNetworkRequest request;
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

        if (useCustomApi) {
            // OpenRouter/OpenAI-совместимый формат: картинка — инлайн base64 внутри content.
            QJsonObject imgUrl; imgUrl["url"] = QString("data:image/png;base64,") + base64Png;
            QJsonObject imgBlock; imgBlock["type"] = "image_url"; imgBlock["image_url"] = imgUrl;
            QJsonObject textBlock; textBlock["type"] = "text"; textBlock["text"] = promptText;
            QJsonArray contentArr; contentArr.append(textBlock); contentArr.append(imgBlock);
            QJsonObject userMsg; userMsg["role"] = "user"; userMsg["content"] = contentArr;
            messagesArray.append(userMsg);

            QJsonObject json;
            json["model"] = visionModel; // конкретная vision-модель, а не "openrouter/auto" — авто-роутинг не гарантирует картинки
            json["messages"] = messagesArray;
            json["response_format"] = QJsonObject{ {"type", "json_object"} };

            QString apiKey = settings.value("ai/openrouter_key", "").toString();
            request.setUrl(QUrl("https://openrouter.ai/api/v1/chat/completions"));
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
            request.setRawHeader("HTTP-Referer", "https://storm-browser.ru/");
            request.setRawHeader("X-Title", "Storm Browser");
            networkManager->post(request, QJsonDocument(json).toJson());
        }
        else if (useGigaChat) {
            // GigaChat не принимает картинку инлайн — сперва грузим её в хранилище и
            // ссылаемся на полученный id через "attachments" (см. uploadImageToGigaChat).
            QString gigaKey = settings.value("ai/gigachat_key", "").toString();
            QString authErr;
            if (!ensureGigaChatToken(gigaKey, authErr)) {
                chatHistory.append(ChatMessage{ "assistant", u8"❌ Ошибка авторизации GigaChat: " + authErr });
                renderChat();
                finishAgentTask();
                return;
            }

            QByteArray pngBytes = QByteArray::fromBase64(base64Png.toLatin1());
            QString fileId, uploadErr;
            if (!uploadImageToGigaChat(pngBytes, fileId, uploadErr)) {
                m_lastActionSummary = u8"[look] — не удалось загрузить снимок в GigaChat: " + uploadErr;
                scheduleNextAgentStep(200);
                return;
            }

            QJsonObject userMsg;
            userMsg["role"] = "user";
            userMsg["content"] = promptText;
            QJsonArray attachments; attachments.append(fileId);
            userMsg["attachments"] = attachments;
            messagesArray.append(userMsg);

            QJsonObject json;
            json["model"] = visionModel;
            json["messages"] = messagesArray;

            request.setUrl(QUrl("https://api.giga.chat/v1/chat/completions"));
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            request.setRawHeader("Authorization", ("Bearer " + cachedGigaToken).toUtf8());
            applyGigaChatSslBypass(request);
            networkManager->post(request, QJsonDocument(json).toJson());
        }
        });
}

void AIAssistantWidget::onAgentNetworkReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (!m_agentRunning) return;

    if (reply->error() != QNetworkReply::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errorStr = reply->errorString();

        QSettings errSettings;
        bool errUsingOwnKey = (errSettings.value("ai/mode", 0).toInt() == 0);
        bool errUsingGigaChat = (errSettings.value("ai/mode", 0).toInt() == 2);

        QString errorMsg;
        if (statusCode == 502) errorMsg = errUsingOwnKey ? u8"❌ Ошибка 502: OpenRouter недоступен." : (errUsingGigaChat ? u8"❌ Ошибка 502: GigaChat недоступен." : u8"❌ Ошибка 502: Все AI-серверы перегружены.");
        else if (statusCode == 403) errorMsg = errUsingGigaChat ? u8"❌ Ошибка 403: Доступ к GigaChat запрещен." : u8"❌ Недостаточно AI-токенов.";
        else if (statusCode == 401) errorMsg = errUsingOwnKey ? u8"❌ Ошибка 401: Неверный API-ключ OpenRouter." : (errUsingGigaChat ? u8"❌ Ошибка 401: Неверный токен GigaChat." : u8"❌ Ошибка 401: Неверный логин или пароль ЛК.");
        else if (statusCode == 0) errorMsg = u8"❌ Нет подключения к серверу. Проверьте интернет.";
        else errorMsg = u8"❌ Ошибка " + QString::number(statusCode) + u8": " + errorStr;

        chatHistory.append(ChatMessage{ "assistant", errorMsg });
        renderChat();
        finishAgentTask();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject rootObj = doc.object();
    QString content;

    if (rootObj.contains("choices")) {
        QJsonArray choices = rootObj.value("choices").toArray();
        if (!choices.isEmpty()) {
            content = choices[0].toObject().value("message").toObject().value("content").toString();
        }
    }
    else if (rootObj.contains("reply")) {
        content = rootObj.value("reply").toString();
    }

    if (rootObj.contains("usage")) {
        QSettings tokenSettings;
        int mode = tokenSettings.value("ai/mode", 0).toInt();
        if (mode == 1 || mode == 2) {
            qint64 totalTokens = rootObj.value("usage").toObject().value("total_tokens").toVariant().toLongLong();
            qint64 usedSoFar = tokenSettings.value("ai/tokens_used", 0).toLongLong();
            tokenSettings.setValue("ai/tokens_used", usedSoFar + totalTokens);
        }
    }

    // Модель обязана вернуть {"message":..., "action":...}, но на случай, если она
    // всё же обернула JSON лишним текстом (или вообще его не вернула), достаём
    // первую фигурную скобку — так же, как раньше делал парсер "умного поиска".
    QString jsonPart = content;
    int jsonStart = content.indexOf('{');
    int jsonEnd = content.lastIndexOf('}');
    if (jsonStart != -1 && jsonEnd != -1 && jsonEnd > jsonStart) {
        jsonPart = content.mid(jsonStart, jsonEnd - jsonStart + 1);
    }

    QJsonParseError parseErr;
    QJsonObject parsed = QJsonDocument::fromJson(jsonPart.toUtf8(), &parseErr).object();

    QString message;
    QJsonObject action;
    if (parseErr.error == QJsonParseError::NoError && parsed.contains("message")) {
        message = parsed.value("message").toString();
        action = parsed.value("action").toObject();
    }
    else {
        // Модель не выполнила формат ответа — показываем как обычный текст и завершаем цепочку,
        // чтобы не выполнять действия "вслепую" по неразобранному ответу.
        message = content.isEmpty() ? u8"⚠️ Сервер вернул пустой ответ или неизвестный формат JSON." : content;
        action["type"] = "done";
    }

    if (!message.isEmpty()) {
        chatHistory.append(ChatMessage{ "assistant", message });
        if (chatHistory.size() > 10) chatHistory.removeFirst();
        renderChat();
    }

    QString actionType = action.value("type").toString(u8"done");
    if (actionType.isEmpty() || actionType == "done") {
        finishAgentTask();
        return;
    }

    if (actionType == "look") {
        m_pageAgent->updateStatus(u8"смотрю на снимок страницы…");
        performVisionLook();
        return;
    }

    if (actionType == "search") {
        QString query = action.value("query").toString();
        m_pageAgent->updateStatus(query.isEmpty() ? u8"ищу в интернете…" : (u8"ищу в интернете: " + query));
        // Не идёт через m_pageAgent->executeAction() — это не действие НА странице, а сетевой
        // запрос к Tavily; результат вернётся текстом в m_lastActionSummary (см. onSearchNetworkReply)
        // и агентный цикл продолжится обычным путём через scheduleNextAgentStep.
        performTavilySearch(query, QStringLiteral("agent"), QVariant());
        return;
    }

    QString stepLabel;
    if (actionType == "click") stepLabel = u8"нажимаю на элемент страницы…";
    else if (actionType == "type") stepLabel = u8"ввожу текст в поле…";
    else if (actionType == "scroll") stepLabel = u8"прокручиваю страницу…";
    else if (actionType == "navigate") stepLabel = u8"перехожу по ссылке…";
    else stepLabel = u8"выполняю действие…";
    m_pageAgent->updateStatus(stepLabel);

    m_pageAgent->executeAction(action, [this, actionType](bool success) {
        if (!m_agentRunning) return;

        m_lastActionSummary = QString(u8"[%1] — %2").arg(actionType, success ? u8"выполнено" : u8"не удалось (элемент не найден или устарел, список элементов будет обновлён)");

        int fallbackDelay = (actionType == "navigate") ? 8000 : 900;
        scheduleNextAgentStep(fallbackDelay);
        });
}

void AIAssistantWidget::scheduleNextAgentStep(int fallbackDelayMs) {
    if (!m_agentRunning) return;

    // После действия ждём либо реальной загрузки страницы (если клик привёл к переходу),
    // либо fallback-таймаут — что наступит раньше. Флаг proceeded защищает от двойного вызова.
    auto proceeded = std::make_shared<bool>(false);
    auto conn = std::make_shared<QMetaObject::Connection>();

    QWebEngineView* view = m_pageAgent->targetView();
    if (view) {
        *conn = connect(view->page(), &QWebEnginePage::loadFinished, this, [this, proceeded, conn](bool) {
            if (*proceeded) return;
            *proceeded = true;
            QObject::disconnect(*conn);
            if (m_agentRunning) sendAgentRequest();
            });
    }

    QTimer::singleShot(fallbackDelayMs, this, [this, proceeded, conn]() {
        if (*proceeded) return;
        *proceeded = true;
        if (*conn) QObject::disconnect(*conn);
        if (m_agentRunning) sendAgentRequest();
        });
}

void AIAssistantWidget::finishAgentTask() {
    if (!m_agentRunning) return;
    stopAgentBtn->hide();
    m_pageAgent->endTask(); // Снимает свечение/плашку с зафиксированной вкладки задачи и отпускает её
    setUIEnabled(true);
    m_agentRunning = false;
    m_busy = false;
}

void AIAssistantWidget::onStopAgentClicked() {
    if (!m_agentRunning) return;
    chatHistory.append(ChatMessage{ "assistant", u8"🛑 Остановлено пользователем." });
    renderChat();
    finishAgentTask();
}