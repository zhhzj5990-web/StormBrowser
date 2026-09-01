#include "HomeAIBridge.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QUrl>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QFile>
#include <QCoreApplication>
#include <QUuid>
#include <QDateTime>
#include <QTimer>
#include <QRegularExpression>

namespace {
    const QString STORM_CLOUD_AI_URL = QStringLiteral("https://storm-browser.online:8000/api/ai/chat");
    // Отдельный эндпоинт для генерации картинок через Личный кабинет — сервер
    // проксирует его в OpenRouter Image API (см. server.py: /api/ai/image),
    // а не в /api/ai/chat, так как формат запроса/ответа у картинок другой.
    const QString STORM_CLOUD_AI_IMAGE_URL = QStringLiteral("https://storm-browser.online:8000/api/ai/image");
    constexpr int kNetworkTimeoutMs = 30000;

    void applyRussianTrustedCa(QNetworkRequest& req) {
        QSslConfiguration sslConf = req.sslConfiguration();
        sslConf.setPeerVerifyMode(QSslSocket::VerifyPeer);

        static const QList<QSslCertificate> extraCerts = [] {
            QList<QSslCertificate> certs;
            const QStringList candidatePaths = {
                QCoreApplication::applicationDirPath() + "/certs/russian_trusted_root_ca.pem",
                QCoreApplication::applicationDirPath() + "/certs/russian_trusted_sub_ca.pem",
                ":/certs/russian_trusted_root_ca.pem",
                ":/certs/russian_trusted_sub_ca.pem",
            };
            for (const QString& path : candidatePaths) {
                QFile f(path);
                if (f.open(QIODevice::ReadOnly)) {
                    certs += QSslCertificate::fromData(f.readAll(), QSsl::Pem);
                }
            }
            return certs;
            }();

        if (!extraCerts.isEmpty()) {
            QList<QSslCertificate> chain = sslConf.caCertificates();
            chain += extraCerts;
            sslConf.setCaCertificates(chain);
        }

        req.setSslConfiguration(sslConf);
    }
}

HomeAIBridge::HomeAIBridge(QObject* parent) : QObject(parent) {
    m_netManager = new QNetworkAccessManager(this);
    connect(m_netManager, &QNetworkAccessManager::finished, this, &HomeAIBridge::onNetworkReply);
}

void HomeAIBridge::askAI(const QString& prompt) {
    QString cleanPrompt = prompt.trimmed();
    if (cleanPrompt.isEmpty()) return;

    if (m_pendingReply) {
        m_pendingReply->abort();
        m_pendingReply = nullptr;
    }

    QJsonObject userMsg{ {"role", "user"}, {"content", cleanPrompt} };
    m_chatHistory.append(userMsg);

    while (m_chatHistory.size() > 10) {
        m_chatHistory.removeFirst();
    }

    QSettings settings;
    int aiMode = settings.value("ai/mode", 0).toInt();
    bool useCustomApi = (aiMode == 0);
    bool useGigaChat = (aiMode == 2);

    QString apiKey = settings.value("ai/openrouter_key", "").toString();
    QString gigaKey = settings.value("ai/gigachat_key", "").toString();
    QString gigaModel = settings.value("ai/gigachat_model", "GigaChat-2").toString();
    QString user = settings.value("sync/username", "").toString();
    QString pwd = settings.value("sync/password", "").toString();

    if (useCustomApi && apiKey.isEmpty()) {
        emit aiResponseReceived(u8"⚠️ <b>Введите API-ключ OpenRouter в Настройки → 🤖 Настройки ИИ</b>");
        return;
    }
    if (useGigaChat && gigaKey.isEmpty()) {
        emit aiResponseReceived(u8"⚠️ <b>Введите ключ авторизации GigaChat в Настройки → 🤖 Настройки ИИ</b>");
        return;
    }
    if (!useCustomApi && !useGigaChat && (user.isEmpty() || pwd.isEmpty())) {
        emit aiResponseReceived(u8"⚠️ <b>Войдите в Личный кабинет или выберите другой режим ИИ в настройках</b>");
        return;
    }

    QJsonObject json;
    QJsonArray messagesArray;
    messagesArray.append(QJsonObject{
        {"role", "system"},
        {"content", u8"Ты быстрый ассистент главной страницы Storm Browser. Если пользователь прямо просит открыть сайт по названию или адресу (например 'открой вк', 'перейди на youtube', 'зайди на ozon.ru') — верни СТРОГО одну строку: 'NAVIGATE: запрос'. Если для ответа нужна актуальная информация из интернета (новости, курсы, цены, погода, факты, которые могли устареть или измениться, и т.п.) — верни СТРОГО одну строку: 'SEARCH: запрос' с коротким чётким поисковым запросом; результаты живого поиска придут тебе отдельным сообщением, и уже тогда ты дашь пользователю полноценный ответ. В остальных случаях отвечай на вопрос кратко и полезно сразу, без служебных префиксов."}
        });

    for (const auto& val : m_chatHistory) {
        messagesArray.append(val);
    }
    json["messages"] = messagesArray;

    QNetworkRequest request;
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setTransferTimeout(kNetworkTimeoutMs);

    if (useCustomApi) {
        json["model"] = QString("openrouter/auto");
        request.setUrl(QUrl("https://openrouter.ai/api/v1/chat/completions"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
        request.setRawHeader("HTTP-Referer", "https://storm-browser.ru/");
        request.setRawHeader("X-Title", "Storm Browser");
        sendChatRequest(request, QJsonDocument(json).toJson());
    }
    else if (useGigaChat) {
        continueWithGigaChat(json, gigaKey, gigaModel);
    }
    else {
        json["username"] = user;
        json["password"] = pwd;
        request.setUrl(QUrl(STORM_CLOUD_AI_URL));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        sendChatRequest(request, QJsonDocument(json).toJson());
    }
}

void HomeAIBridge::generateMedia(const QString& prompt, const QString& type) {
    QString cleanPrompt = prompt.trimmed();
    // Сразу отсекаем пустые запросы и попытки сгенерировать видео
    if (cleanPrompt.isEmpty() || type != "image") return;

    if (m_pendingReply) {
        m_pendingReply->abort();
        m_pendingReply = nullptr;
    }

    m_chatHistory.append(QJsonObject{ {"role", "user"}, {"content", u8"[Генерация картинки] " + cleanPrompt} });
    while (m_chatHistory.size() > 10) m_chatHistory.removeFirst();

    QSettings settings;
    int aiMode = settings.value("ai/mode", 0).toInt();
    bool useCustomApi = (aiMode == 0);
    bool useGigaChat = (aiMode == 2);

    // --- ЛОГИКА ДЛЯ GIGACHAT ---
    if (useGigaChat) {
        QString gigaKey = settings.value("ai/gigachat_key", "").toString();
        QString gigaModel = settings.value("ai/gigachat_model", "GigaChat-2").toString();

        if (gigaKey.isEmpty()) {
            emit aiResponseReceived(u8"⚠️ <b>Ключ GigaChat не найден. Укажите его в Настройках ИИ.</b>");
            return;
        }

        // Не дублируем триггер-слово, если пользователь уже сам начал с "нарисуй"
        QString imagePrompt = cleanPrompt.startsWith(u8"нарисуй", Qt::CaseInsensitive)
            ? cleanPrompt
            : u8"Нарисуй " + cleanPrompt;

        QJsonObject json;
        QJsonArray messagesArray;
        messagesArray.append(QJsonObject{ {"role", "user"}, {"content", imagePrompt} });
        json["messages"] = messagesArray;
        json["model"] = gigaModel;
        // ВАЖНО: без этого параметра GigaChat не вызывает встроенную функцию
        // text2image (Kandinsky) и часто отдаёт пустой content — отсюда и была
        // ошибка "Сервер вернул пустой ответ". По докам Sber это обязательный
        // параметр для авто-вызова встроенных функций (см. developers.sber.ru,
        // раздел "Создание изображений" / "Обращение к встроенным функциям").
        json["function_call"] = "auto";

        continueWithGigaChat(json, gigaKey, gigaModel);
        return;
    }

    // --- ЛОГИКА ДЛЯ ЛИЧНОГО КАБИНЕТА STORM CLOUD ---
    // Раньше этой ветки не было вовсе: aiMode==1 молча проваливался в блок
    // OpenRouter ниже и пытался использовать ai/openrouter_key, которого у
    // пользователей личного кабинета обычно нет — отсюда и "пустой ответ /
    // неизвестный формат JSON". Теперь бьём в тот же Storm Cloud сервер, что
    // и текстовый чат, но в его отдельный эндпоинт для картинок (см. server.py: /api/ai/image).
    if (!useCustomApi) {
        QString user = settings.value("sync/username", "").toString();
        QString pwd = settings.value("sync/password", "").toString();

        if (user.isEmpty() || pwd.isEmpty()) {
            emit aiResponseReceived(u8"⚠️ <b>Войдите в Личный кабинет или выберите другой режим ИИ в настройках</b>");
            return;
        }

        QJsonObject json;
        json["username"] = user;
        json["password"] = pwd;
        json["prompt"] = cleanPrompt;
        json["model"] = settings.value("ai/image_model", "google/gemini-2.5-flash-image").toString();

        QNetworkRequest request;
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        // Даем 45 секунд на генерацию картинки
        request.setTransferTimeout(45000);
        request.setUrl(QUrl(STORM_CLOUD_AI_IMAGE_URL));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        sendChatRequest(request, QJsonDocument(json).toJson());
        return;
    }

    // --- ЛОГИКА ДЛЯ OPENROUTER (свой ключ, aiMode==0) ---
    // Точечный ключ "только для картинок" (ai/image_key) убрали из интерфейса —
    // раз поля для него больше нет, картинки всегда идут через основной ключ
    // OpenRouter и его Image API. Поведение для всех, кто настраивал ключи
    // через интерфейс, от этого не меняется: пустой ai/image_key и раньше
    // всегда приводил именно к этой ветке.
    QString apiKey = settings.value("ai/openrouter_key", "").toString();

    if (apiKey.isEmpty()) {
        emit aiResponseReceived(u8"⚠️ <b>Ключ API не найден. Укажите его в Настройках ИИ.</b>");
        return;
    }

    QNetworkRequest request;
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    // Даем 45 секунд на генерацию картинки
    request.setTransferTimeout(45000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
    // Генерация картинок у OpenRouter — отдельный Image API (POST /v1/images),
    // а не /chat/completions. Модель конфигурируется через ai/image_model.
    request.setUrl(QUrl("https://openrouter.ai/api/v1/images"));
    // Заголовки атрибуции — не обязательны для этого эндпоинта, но не мешают
    request.setRawHeader("HTTP-Referer", "https://storm-browser.ru/");
    request.setRawHeader("X-Title", "Storm Browser");

    QJsonObject json;
    json["model"] = settings.value("ai/image_model", "google/gemini-2.5-flash-image").toString();
    json["prompt"] = cleanPrompt;

    sendChatRequest(request, QJsonDocument(json).toJson());
}

void HomeAIBridge::downloadGigaChatImage(const QString& fileId, const QString& token, const QString& originalText) {
    QNetworkRequest req(QUrl("https://api.giga.chat/v1/files/" + fileId + "/content"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/jpg");
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    applyRussianTrustedCa(req);

    // ВАЖНО: НЕ используем m_netManager — он подключён к onNetworkReply() через
    // QNetworkAccessManager::finished для ВСЕХ своих запросов, и бинарный ответ
    // (байты картинки) параллельно улетал бы и туда, где его пытались бы
    // распарсить как JSON чата — в итоге в чат прилетало лишнее сообщение
    // "Сервер вернул пустой ответ" перед настоящей картинкой. m_authManager
    // к onNetworkReply не подключён, поэтому используем его.
    if (!m_authManager) {
        m_authManager = new QNetworkAccessManager(this);
    }
    QNetworkReply* reply = m_authManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, originalText]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray imgData = reply->readAll();
            QString base64 = QString::fromLatin1(imgData.toBase64());
            QString dataUrl = "data:image/jpeg;base64," + base64;
            QString html = QString(
                "MEDIA_HTML:<div class=\"ai-media-wrap\">"
                "<img src=\"%1\" style=\"max-width: 100%; border-radius: 8px; margin-top: 5px; display:block;\" />"
                "<a href=\"%1\" download=\"storm-ai-image.jpg\" class=\"ai-media-download-btn\">⬇️ Скачать картинку</a>"
                "</div>").arg(dataUrl);

            m_chatHistory.append(QJsonObject{ {"role", "assistant"}, {"content", u8"[Изображение от GigaChat]"} });
            emit aiResponseReceived(html);
        }
        else {
            emit aiResponseReceived(originalText.toHtmlEscaped().replace("\n", "<br>"));
        }
        });
}

void HomeAIBridge::continueWithGigaChat(const QJsonObject& baseJson, const QString& gigaKey, const QString& gigaModel, bool isFinal) {
    qint64 currentTime = QDateTime::currentSecsSinceEpoch();

    if (!m_cachedGigaToken.isEmpty() && currentTime < m_gigaTokenExpireTime) {
        QJsonObject json = baseJson;
        json["model"] = gigaModel;

        QNetworkRequest request;
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        request.setTransferTimeout(kNetworkTimeoutMs);
        request.setUrl(QUrl("https://api.giga.chat/v1/chat/completions"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", ("Bearer " + m_cachedGigaToken).toUtf8());
        applyRussianTrustedCa(request);

        sendChatRequest(request, QJsonDocument(json).toJson(), isFinal);
        return;
    }

    if (!m_authManager) {
        m_authManager = new QNetworkAccessManager(this);
    }

    QNetworkRequest authReq(QUrl("https://ngw.devices.sberbank.ru:9443/api/v2/oauth"));
    authReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    authReq.setRawHeader("Accept", "application/json");
    authReq.setRawHeader("RqUID", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
    authReq.setRawHeader("Authorization", ("Basic " + gigaKey).toUtf8());
    authReq.setTransferTimeout(kNetworkTimeoutMs);
    applyRussianTrustedCa(authReq);

    QNetworkReply* authReply = m_authManager->post(authReq, "scope=GIGACHAT_API_PERS");
    m_pendingReply = authReply;

    connect(authReply, &QNetworkReply::finished, this, [this, authReply, baseJson, gigaKey, gigaModel, isFinal]() {
        if (m_pendingReply == authReply) m_pendingReply = nullptr;
        authReply->deleteLater();

        if (authReply->error() == QNetworkReply::OperationCanceledError) {
            return;
        }
        if (authReply->error() != QNetworkReply::NoError) {
            emit aiResponseReceived(u8"❌ Ошибка авторизации GigaChat: " + authReply->errorString());
            return;
        }

        QJsonDocument authDoc = QJsonDocument::fromJson(authReply->readAll());
        m_cachedGigaToken = authDoc.object().value("access_token").toString();
        m_gigaTokenExpireTime = QDateTime::currentSecsSinceEpoch() + 1740;

        if (m_cachedGigaToken.isEmpty()) {
            emit aiResponseReceived(u8"❌ GigaChat не вернул токен доступа — проверьте ключ авторизации");
            return;
        }

        continueWithGigaChat(baseJson, gigaKey, gigaModel, isFinal);
        });
}

void HomeAIBridge::sendChatRequest(QNetworkRequest request, const QByteArray& body, bool isFinal) {
    QNetworkReply* reply = m_netManager->post(request, body);
    if (isFinal) reply->setProperty("stormFinal", true);
    m_pendingReply = reply;
}

// =========================================================================================
// --- ПОИСК В ИНТЕРНЕТЕ ЧЕРЕЗ TAVILY ---
// =========================================================================================

// Та же логика построения адреса страницы выдачи, что и в обработке NAVIGATE: ниже (см.
// onNetworkReply) — вынесена сюда, чтобы использоваться и как основной путь для NAVIGATE
// (когда запрос не похож на доменное имя), и как fallback для SEARCH: если ключ Tavily не
// задан или сам Tavily недоступен — тогда хотя бы открываем поисковик, как это было раньше.
QString HomeAIBridge::buildSearchEngineUrl(const QString& query) const {
    QSettings settings;
    QString engine = settings.value("browser/search_engine", "DuckDuckGo").toString();
    QString searchUrl = "https://duckduckgo.com/?q=";
    if (engine == "Yandex") searchUrl = "https://yandex.ru/search/?text=";
    else if (engine == "Google") searchUrl = "https://www.google.com/search?q=";
    else if (engine == "Bing") searchUrl = "https://www.bing.com/search?q=";
    return searchUrl + QUrl::toPercentEncoding(query);
}

void HomeAIBridge::performHomeSearch(const QString& query) {
    QString trimmedQuery = query.trimmed();
    if (trimmedQuery.isEmpty()) return;

    QSettings settings;
    QString tavilyKey = settings.value("research/tavily_key", "").toString().trimmed();

    if (tavilyKey.isEmpty()) {
        // Ключ не настроен — тот же результат, что и раньше был для ЛЮБОГО поиска с главной
        // страницы: открываем страницу выдачи выбранного поисковика вместо содержательного ответа.
        emit aiResponseReceived("NAVIGATE_CMD:" + buildSearchEngineUrl(trimmedQuery));
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
    request.setTransferTimeout(kNetworkTimeoutMs);

    if (!m_searchManager) {
        m_searchManager = new QNetworkAccessManager(this);
        connect(m_searchManager, &QNetworkAccessManager::finished, this, &HomeAIBridge::onSearchNetworkReply);
    }

    QNetworkReply* reply = m_searchManager->post(request, QJsonDocument(body).toJson());
    reply->setProperty("stormSearchQuery", trimmedQuery);
}

void HomeAIBridge::onSearchNetworkReply(QNetworkReply* reply) {
    reply->deleteLater();
    QString query = reply->property("stormSearchQuery").toString();

    if (reply->error() != QNetworkReply::NoError) {
        // Сетевая ошибка/просроченный лимит Tavily — не оставляем пользователя без ответа,
        // откатываемся на тот же fallback, что и при отсутствующем ключе.
        emit aiResponseReceived("NAVIGATE_CMD:" + buildSearchEngineUrl(query));
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
        // Tavily ответил без ошибки, но пусто по существу — тот же fallback на поисковик.
        emit aiResponseReceived("NAVIGATE_CMD:" + buildSearchEngineUrl(query));
        return;
    }

    sendGroundedFollowup(query, summary);
}

// Второй ("финальный", см. isFinal в sendChatRequest/continueWithGigaChat) запрос к тому же
// провайдеру ИИ, что и обычный чат, — с результатами поиска как контекстом вместо истории
// системного промпта NAVIGATE/SEARCH. Ветвление по aiMode намеренно дублирует askAI(): это
// отдельный, самодостаточный запрос (своя система сообщений), а не продолжение того же.
void HomeAIBridge::sendGroundedFollowup(const QString& query, const QString& searchSummary) {
    QSettings settings;
    int aiMode = settings.value("ai/mode", 0).toInt();
    bool useCustomApi = (aiMode == 0);
    bool useGigaChat = (aiMode == 2);

    QString apiKey = settings.value("ai/openrouter_key", "").toString();
    QString gigaKey = settings.value("ai/gigachat_key", "").toString();
    QString gigaModel = settings.value("ai/gigachat_model", "GigaChat-2").toString();
    QString user = settings.value("sync/username", "").toString();
    QString pwd = settings.value("sync/password", "").toString();

    if (useCustomApi && apiKey.isEmpty()) {
        emit aiResponseReceived(u8"⚠️ <b>Введите API-ключ OpenRouter в Настройки → 🤖 Настройки ИИ</b>");
        return;
    }
    if (useGigaChat && gigaKey.isEmpty()) {
        emit aiResponseReceived(u8"⚠️ <b>Введите ключ авторизации GigaChat в Настройки → 🤖 Настройки ИИ</b>");
        return;
    }
    if (!useCustomApi && !useGigaChat && (user.isEmpty() || pwd.isEmpty())) {
        emit aiResponseReceived(u8"⚠️ <b>Войдите в Личный кабинет или выберите другой режим ИИ в настройках</b>");
        return;
    }

    QJsonArray messagesArray;
    messagesArray.append(QJsonObject{
        {"role", "system"},
        {"content", u8"Ты быстрый ассистент главной страницы Storm Browser. Ниже даны результаты живого поиска в интернете (Tavily) по запросу «" + query + u8"», который ты сам только что попросил выполнить. Ответь на исходный вопрос пользователя из истории переписки кратко и по существу, опираясь на эти результаты, и по возможности укажи источники (адреса сайтов). Если результаты не по теме — отвечай на основе своих знаний, не упоминая сам факт поиска. Отвечай обычным текстом, БЕЗ 'NAVIGATE:'/'SEARCH:' и без каких-либо служебных префиксов."}
        });
    messagesArray.append(QJsonObject{
        {"role", "system"},
        {"content", u8"[Результаты поиска Tavily]\n" + searchSummary}
        });
    for (const auto& val : m_chatHistory) {
        messagesArray.append(val);
    }

    QJsonObject json;
    json["messages"] = messagesArray;

    QNetworkRequest request;
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setTransferTimeout(kNetworkTimeoutMs);

    if (useCustomApi) {
        json["model"] = QString("openrouter/auto");
        request.setUrl(QUrl("https://openrouter.ai/api/v1/chat/completions"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
        request.setRawHeader("HTTP-Referer", "https://storm-browser.ru/");
        request.setRawHeader("X-Title", "Storm Browser");
        sendChatRequest(request, QJsonDocument(json).toJson(), /*isFinal=*/true);
    }
    else if (useGigaChat) {
        continueWithGigaChat(json, gigaKey, gigaModel, /*isFinal=*/true);
    }
    else {
        json["username"] = user;
        json["password"] = pwd;
        request.setUrl(QUrl(STORM_CLOUD_AI_URL));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        sendChatRequest(request, QJsonDocument(json).toJson(), /*isFinal=*/true);
    }
}

void HomeAIBridge::clearContext() {
    if (m_pendingReply) {
        m_pendingReply->abort();
        m_pendingReply = nullptr;
    }
    m_chatHistory = QJsonArray();
}

void HomeAIBridge::restoreContext(const QString& historyJson) {
    if (m_pendingReply) {
        m_pendingReply->abort();
        m_pendingReply = nullptr;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(historyJson.toUtf8(), &parseError);
    QJsonArray restored = (parseError.error == QJsonParseError::NoError && doc.isArray())
        ? doc.array()
        : QJsonArray();

    while (restored.size() > 10) {
        restored.removeFirst();
    }

    m_chatHistory = restored;
}

void HomeAIBridge::onNetworkReply(QNetworkReply* reply) {
    if (m_pendingReply == reply) m_pendingReply = nullptr;
    // isFinal — этот ответ уже "обоснован" результатами поиска Tavily (см. sendGroundedFollowup):
    // его нельзя повторно разбирать на NAVIGATE:/SEARCH:, иначе рискуем зациклиться или
    // случайно перенаправить пользователя вместо содержательного ответа.
    bool isFinal = reply->property("stormFinal").toBool();
    reply->deleteLater();

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        return;
    }

    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject rootObj = doc.object();
        QString content;

        // Перехватчик 2: Синхронные картинки (OpenAI GPT-image / OpenRouter Image API / DALL-E)
        if (rootObj.contains("data") && rootObj.value("data").isArray()) {
            QJsonArray data = rootObj.value("data").toArray();
            if (!data.isEmpty()) {
                QJsonObject mediaObj = data[0].toObject();
                QString mediaHtml;

                // GPT-image-модели (OpenAI) и Image API OpenRouter всегда отдают
                // base64 (response_format=url для них не поддерживается вовсе)
                if (mediaObj.contains("b64_json")) {
                    QString b64 = mediaObj.value("b64_json").toString();
                    QString mediaType = mediaObj.value("media_type").toString();
                    if (mediaType.isEmpty()) mediaType = "image/png";
                    QString ext = mediaType.section('/', 1);
                    if (ext.isEmpty()) ext = "png";
                    QString dataUrl = QString("data:%1;base64,%2").arg(mediaType, b64);
                    mediaHtml = QString(
                        "MEDIA_HTML:<div class=\"ai-media-wrap\">"
                        "<img src=\"%1\" style=\"max-width: 100%; border-radius: 8px; margin-top: 5px; display:block;\" />"
                        "<a href=\"%1\" download=\"storm-ai-image.%2\" class=\"ai-media-download-btn\">⬇️ Скачать картинку</a>"
                        "</div>").arg(dataUrl, ext);
                }
                // Старый формат DALL-E/совместимых прокси (response_format=url)
                else if (mediaObj.contains("url")) {
                    QString url = mediaObj.value("url").toString();
                    if (url.contains(".mp4") || url.contains(".webm")) {
                        mediaHtml = QString("MEDIA_HTML:<video src=\"%1\" controls autoplay loop style=\"max-width: 100%; border-radius: 8px; margin-top: 5px;\"></video>").arg(url.toHtmlEscaped());
                    }
                    else {
                        // ВАЖНО: атрибут download у <a> не гарантирует принудительное
                        // сохранение для чужого домена (кросс-origin) — тут это лучшее,
                        // что можно сделать без прокси картинки через сам браузер;
                        // для base64-путей выше (основной случай сейчас) download работает как надо.
                        mediaHtml = QString(
                            "MEDIA_HTML:<div class=\"ai-media-wrap\">"
                            "<a href=\"%1\" target=\"_blank\"><img src=\"%1\" style=\"max-width: 100%; border-radius: 8px; margin-top: 5px; cursor: pointer; display:block;\" /></a>"
                            "<a href=\"%1\" download=\"storm-ai-image.png\" class=\"ai-media-download-btn\">⬇️ Скачать картинку</a>"
                            "</div>").arg(url.toHtmlEscaped());
                    }
                }

                if (!mediaHtml.isEmpty()) {
                    m_chatHistory.append(QJsonObject{ {"role", "assistant"}, {"content", u8"[Сгенерированный медиафайл]"} });
                    emit aiResponseReceived(mediaHtml);
                    return;
                }
            }
        }

        // Стандартная обработка текстового ответа
        if (rootObj.contains("choices")) {
            QJsonArray choices = rootObj.value("choices").toArray();
            if (!choices.isEmpty()) {
                content = choices[0].toObject().value("message").toObject().value("content").toString().trimmed();
            }
        }
        else if (rootObj.contains("reply")) {
            content = rootObj.value("reply").toString().trimmed();
        }

        if (content.isEmpty()) content = u8"⚠️ Сервер вернул пустой ответ или неизвестный формат JSON.";

        // Перехватчик 3: Картинки от GigaChat (Kandinsky)
        QRegularExpression imgRegex("<img\\s+src=\"([^\"]+)\"");
        QRegularExpressionMatch match = imgRegex.match(content);
        if (match.hasMatch()) {
            QString fileId = match.captured(1);
            if (!fileId.startsWith("http")) {
                // Промежуточный статус, НЕ финальный ответ — отдельный префикс STATUS:,
                // чтобы JS не считал его завершением и не чистил pendingTypingRow/таймаут
                // раньше времени (раньше это оставляло в чате зависший пузырь с сырыми
                // <i> тегами вместо курсива, а настоящая картинка прилетала уже отдельным
                // новым сообщением).
                emit aiResponseReceived(u8"STATUS:⏳ Картинка от GigaChat (Kandinsky) сгенерирована, идёт загрузка файла...");
                downloadGigaChatImage(fileId, m_cachedGigaToken, content);
                return;
            }
        }

        // Обработка навигации
        if (!isFinal && content.startsWith("NAVIGATE:")) {
            QString query = content.mid(9).trimmed();
            QString urlStr = query;

            if (!urlStr.startsWith("http://") && !urlStr.startsWith("https://") && !urlStr.startsWith("storm://")) {
                if (urlStr.contains(".") && !urlStr.contains(" ")) {
                    urlStr = "https://" + urlStr;
                }
                else {
                    urlStr = buildSearchEngineUrl(query);
                }
            }

            emit aiResponseReceived("NAVIGATE_CMD:" + urlStr);
            return;
        }

        // Обработка поиска в интернете — модель попросила проверить актуальную информацию
        // через Tavily, прежде чем отвечать пользователю (см. performHomeSearch).
        if (!isFinal && content.startsWith("SEARCH:")) {
            QString query = content.mid(7).trimmed();
            performHomeSearch(query);
            return;
        }

        m_chatHistory.append(QJsonObject{ {"role", "assistant"}, {"content", content} });
        QString formattedHtml = content.toHtmlEscaped().replace("\n", "<br>");
        emit aiResponseReceived(formattedHtml);
    }
    else {
        QByteArray errBody = reply->readAll();
        QJsonDocument errDoc = QJsonDocument::fromJson(errBody);
        QString apiErrMsg;

        if (!errDoc.isNull() && errDoc.isObject() && errDoc.object().contains("error")) {
            QJsonValue eVal = errDoc.object().value("error");
            apiErrMsg = eVal.isObject() ? eVal.toObject().value("message").toString() : eVal.toString();
        }

        if (!apiErrMsg.isEmpty()) {
            emit aiResponseReceived(u8"❌ <b>Отказ сервера:</b> " + apiErrMsg.toHtmlEscaped());
        }
        else {
            // Если сервер не дал красивый JSON, берем сырой текст ответа
            QString rawError = QString::fromUtf8(errBody).trimmed();
            if (rawError.isEmpty()) {
                rawError = reply->errorString();
            }
            emit aiResponseReceived(u8"❌ <b>Ошибка сети/сервера:</b> " + rawError.toHtmlEscaped());
        }
    }
}