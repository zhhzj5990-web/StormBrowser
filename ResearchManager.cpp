#include "ResearchManager.h"
#include "AiClient.h"
#include "ReaderWidget.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTextDocument>
#include <QPrinter>
#include <QPageSize>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QSettings>

// НЕ используем префикс u8"..." для строковых литералов, попадающих в
// QString/QNetworkRequest — см. подробное объяснение в конструкторе
// Sidebar.cpp: начиная с C++20 u8"..." — это const char8_t*, который не
// конвертируется в QString неявно. Файл сохранён в UTF-8, поэтому обычные
// литералы и так корректно читаются как UTF-8.

namespace {
    // "Быстро" — компромисс скорости: меньше источников и короче выдержки.
    // "Глубоко" — по ТЗ модуля: 10-15 сайтов (берём верхнюю половину диапазона).
    constexpr int kFastSourceCount = 6;
    constexpr int kDeepSourceCount = 14;

    constexpr int kMaxCharsPerSource = 1800; // сколько очищенного текста берём с одной страницы
    constexpr int kMaxTotalChars = 24000;    // общий бюджет текста, уходящего в промпт ИИ

    constexpr int kNetworkTimeoutMs = 15000;
}

ResearchManager::ResearchManager(QObject* parent) : QObject(parent) {
    // parent = this: AiClient — дочерний QObject, поэтому когда ResearchWidget
    // вызовет moveToThread(workerThread) на самом ResearchManager, Qt
    // автоматически перенесёт вместе с ним и m_aiClient (переезд родителя
    // рекурсивно переносит всех детей). Создавать его позже, уже "внутри"
    // потока воркера, не обязательно — как и с m_net, реальная сетевая
    // активность начнётся только после moveToThread(), когда объект и так
    // будет физически в нужном потоке.
    m_aiClient = new AiClient(this);
    connect(m_aiClient, &AiClient::replyReceived, this, &ResearchManager::onAiReplyReceived);
    connect(m_aiClient, &AiClient::replyFailed, this, &ResearchManager::onAiReplyFailed);
    connect(m_aiClient, &AiClient::quotaExceeded, this, &ResearchManager::onAiQuotaExceeded);
}

ResearchManager::~ResearchManager() {
    // m_net создан без родителя-виджета, но С родителем this (см. net()),
    // поэтому будет удалён автоматически как дочерний QObject — здесь
    // ничего специально чистить не нужно. Открытые QNetworkReply* тоже
    // дети m_net (Qt отдаёт их с parent = manager, создавшим запрос) и
    // уйдут вместе с ним.
}

QNetworkAccessManager* ResearchManager::net() {
    // Создаём здесь, а не в конструкторе: конструктор ResearchManager
    // выполняется ДО moveToThread() в ResearchWidget, а QNetworkAccessManager
    // должен жить в том потоке, где будет использоваться. Первый вызов net()
    // происходит уже внутри startResearch() — то есть гарантированно в
    // потоке воркера.
    if (!m_net) {
        m_net = new QNetworkAccessManager(this);
    }
    return m_net;
}

void ResearchManager::fail(const QString& message) {
    m_busy = false;
    emit researchFailed(message);
}

void ResearchManager::reportProgress(const QString& stageText, int percent) {
    emit progressChanged(stageText, qBound(0, percent, 100));
}

// ==========================================================================
// Точка входа
// ==========================================================================
void ResearchManager::startResearch(const QString& topic, const QString& depth, const QString& format) {
    if (m_busy) {
        // Виджет блокирует повторный клик по кнопке, пока идёт исследование,
        // но подстраховываемся и здесь — на случай гонки двух быстрых кликов.
        return;
    }
    if (topic.trimmed().isEmpty()) {
        fail(QStringLiteral("Введите тему исследования."));
        return;
    }

    m_busy = true;
    m_cancelled = false;
    m_topic = topic.trimmed();
    m_depth = depth;
    m_format = format;
    m_pendingUrls.clear();
    m_collectedChunks.clear();
    m_processedSources = 0;
    m_totalCollectedChars = 0;
    m_totalSources = (m_depth == QLatin1String("deep")) ? kDeepSourceCount : kFastSourceCount;

    checkQuotaThenProceed();
}

void ResearchManager::cancelResearch() {
    m_cancelled = true;
}

// ==========================================================================
// Этап 0: проверка квоты — ТОЛЬКО для режима "Личный кабинет" (aiMode==1).
// "Свой ключ" (OpenRouter/GigaChat, aiMode 0/2) пропускает этот шаг целиком —
// эти запросы никогда не идут через сервер Storm, значит и квоты к ним не
// применимы вообще, по построению, а не по отдельной проверке "заплатил/нет".
// ==========================================================================
void ResearchManager::checkQuotaThenProceed() {
    QSettings settings;
    int aiMode = settings.value(QStringLiteral("ai/mode"), 0).toInt();

    if (aiMode != 1) {
        beginLinkGathering();
        return;
    }

    QString user = settings.value(QStringLiteral("sync/username"), QString()).toString();
    QString pwd = settings.value(QStringLiteral("sync/password"), QString()).toString();
    if (user.isEmpty() || pwd.isEmpty()) {
        fail(QStringLiteral("Войдите в Личный кабинет в Настройки → Настройки ИИ, либо выберите «Свой API» или GigaChat."));
        return;
    }

    reportProgress(QStringLiteral("Проверяю лимит отчётов…"), 2);

    QJsonObject json;
    json[QStringLiteral("username")] = user;
    json[QStringLiteral("password")] = pwd;

    QNetworkRequest request(QUrl(QStringLiteral("https://storm-browser.online:8000/api/research/quota")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kNetworkTimeoutMs);

    QNetworkReply* reply = net()->post(request, QJsonDocument(json).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_cancelled) { fail(QStringLiteral("Исследование отменено.")); return; }

        if (reply->error() != QNetworkReply::NoError) {
            // Не удалось спросить сервер о квоте (сеть легла, сервер недоступен
            // и т.п.) — НЕ блокируем пользователя намертво из-за временной
            // проблемы связи: пропускаем в пайплайн, а авторитетную проверку
            // всё равно сделает сам сервер на шаге синтеза (см. meterKind в
            // synthesizeWithAi() / AiClient::quotaExceeded).
            beginLinkGathering();
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        int used = data.value(QStringLiteral("used")).toInt(0);
        int limit = data.value(QStringLiteral("limit")).toInt(4); // 4 — бесплатный лимит по умолчанию

        if (used >= limit) {
            QString detail = data.value(QStringLiteral("detail")).toString();
            if (detail.isEmpty()) {
                detail = QStringLiteral("Бесплатный лимит отчётов на этом тарифе исчерпан (%1/%2 в этом месяце). "
                    "Оформите пакет отчётов в Личном кабинете, чтобы продолжить.").arg(used).arg(limit);
            }
            m_busy = false;
            emit researchQuotaExceeded(detail, used, limit);
            return;
        }

        beginLinkGathering();
        });
}

// ==========================================================================
// Этап 1: сбор ссылок — диспетчер источника
// ==========================================================================
void ResearchManager::beginLinkGathering() {
    QString tavilyKey = QSettings().value(QStringLiteral("research/tavily_key"), QString()).toString().trimmed();
    if (!tavilyKey.isEmpty()) {
        beginLinkGatheringViaTavily(tavilyKey);
    }
    else {
        beginLinkGatheringViaDuckDuckGo();
    }
}

// --------------------------------------------------------------------------
// Tavily — платный (1000 запросов/мес бесплатно, без карты) поисковый API,
// заточенный именно под ИИ-агентов: в отличие от DuckDuckGo, отдаёт не
// HTML-страницу выдачи для парсинга, а готовый JSON с уже очищенным текстом
// каждой найденной страницы (include_raw_content) — поэтому здесь не нужен
// отдельный проход fetchNextPage()/stripHtmlToPlainText(): результат сразу
// собирается в m_collectedChunks, и мы идём прямиком к synthesizeWithAi().
// --------------------------------------------------------------------------
void ResearchManager::beginLinkGatheringViaTavily(const QString& apiKey) {
    reportProgress(QStringLiteral("Сбор источников (Tavily)…"), 5);

    QJsonObject json;
    json[QStringLiteral("query")] = m_topic;
    json[QStringLiteral("max_results")] = m_totalSources;
    // "basic" — 1 кредит за запрос вместо 2 у "advanced". Для задачи "собрать
    // выдержки под дальнейший синтез отчёта" advanced не даёт ощутимого
    // выигрыша, а кредиты бесплатного тарифа тратит вдвое быстрее.
    json[QStringLiteral("search_depth")] = QStringLiteral("basic");
    // Именно этот флаг и даёт нам чистый текст страниц одним запросом — без
    // него в "content" пришёл бы только короткий сниппет, и всё равно
    // пришлось бы самим скачивать и чистить каждую страницу, как для DDG.
    json[QStringLiteral("include_raw_content")] = true;

    QNetworkRequest request(QUrl(QStringLiteral("https://api.tavily.com/search")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
    request.setTransferTimeout(kNetworkTimeoutMs);

    QNetworkReply* reply = net()->post(request, QJsonDocument(json).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_cancelled) { fail(QStringLiteral("Исследование отменено.")); return; }

        if (reply->error() != QNetworkReply::NoError) {
            // Не проваливаем всё исследование из-за проблемы с Tavily (истёк
            // лимит на бесплатном тарифе, невалидный ключ, сеть) — просто
            // откатываемся на бесплатный DuckDuckGo, чтобы пользователь всё
            // равно получил отчёт, пусть и без преимуществ платного поиска.
            reportProgress(QStringLiteral("Tavily недоступен, пробую DuckDuckGo…"), 5);
            beginLinkGatheringViaDuckDuckGo();
            return;
        }

        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonArray results = root.value(QStringLiteral("results")).toArray();

        QStringList seenHosts;
        for (const QJsonValue& v : results) {
            QJsonObject r = v.toObject();
            QString urlStr = r.value(QStringLiteral("url")).toString();
            if (urlStr.isEmpty()) continue;

            QString host = QUrl(urlStr).host().toLower();
            if (host.isEmpty() || seenHosts.contains(host)) continue; // та же логика разнообразия источников, что и у DuckDuckGo
            seenHosts.append(host);

            // raw_content может отсутствовать (например, страница оказалась
            // недоступна для извлечения на стороне Tavily) — тогда берём
            // обычный "content" (короткий сниппет) вместо того, чтобы
            // выбрасывать источник целиком.
            QString text = r.value(QStringLiteral("raw_content")).toString();
            if (text.trimmed().isEmpty()) {
                text = r.value(QStringLiteral("content")).toString();
            }
            text = text.trimmed();
            if (text.isEmpty()) continue;

            if (text.size() > kMaxCharsPerSource) {
                text = text.left(kMaxCharsPerSource) + QStringLiteral("…");
            }

            QString chunk = QStringLiteral("Источник %1 (%2):\n%3")
                .arg(m_collectedChunks.size() + 1)
                .arg(urlStr, text);
            m_collectedChunks.append(chunk);
            m_totalCollectedChars += chunk.size();
            m_processedSources++;

            if (m_totalCollectedChars >= kMaxTotalChars) break;
        }

        if (m_collectedChunks.isEmpty()) {
            // И здесь тоже не сдаёмся сразу — вдруг у DuckDuckGo по этой же
            // теме результаты найдутся (например, Tavily просто не сумел
            // вытащить raw_content ни для одной страницы).
            beginLinkGatheringViaDuckDuckGo();
            return;
        }

        m_totalSources = m_collectedChunks.size();
        reportProgress(QStringLiteral("Источники получены (%1). Синтезирую отчёт…").arg(m_totalSources), 65);
        synthesizeWithAi();
        });
}

// --------------------------------------------------------------------------
// DuckDuckGo — бесплатный запасной вариант, без ключа и без лимитов, но
// требует отдельного прохода по каждой ссылке (см. fetchNextPage ниже),
// т.к. отдаёт только HTML-страницу выдачи для парсинга, а не готовый текст.
// --------------------------------------------------------------------------
void ResearchManager::beginLinkGatheringViaDuckDuckGo() {
    reportProgress(QStringLiteral("Сбор ссылок…"), 5);

    // html.duckduckgo.com/html — облегчённая (без JS) HTML-версия выдачи,
    // рассчитанная как раз на простых клиентов без браузерного движка.
    // Никакого API-ключа не требуется.
    QUrl searchUrl(QStringLiteral("https://html.duckduckgo.com/html/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), m_topic);
    searchUrl.setQuery(query);

    QNetworkRequest request(searchUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (StormBrowser DeepResearch)"));
    request.setTransferTimeout(kNetworkTimeoutMs);

    QNetworkReply* reply = net()->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_cancelled) { fail(QStringLiteral("Исследование отменено.")); return; }

        if (reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("Не удалось получить результаты поиска: %1").arg(reply->errorString()));
            return;
        }

        QString html = QString::fromUtf8(reply->readAll());
        extractLinksFromSearchHtml(html);

        if (m_pendingUrls.isEmpty()) {
            fail(QStringLiteral("По теме «%1» не удалось найти ни одной ссылки.").arg(m_topic));
            return;
        }

        m_totalSources = m_pendingUrls.size(); // могло оказаться меньше желаемого — берём фактическое число
        reportProgress(QStringLiteral("Найдено источников: %1. Начинаю чтение…").arg(m_totalSources), 10);
        fetchNextPage();
        });
}

void ResearchManager::extractLinksFromSearchHtml(const QString& html) {
    // Ссылки результатов помечены классом result__a и указывают на
    // редиректор DuckDuckGo (/l/?uddg=<percent-encoded реальный URL>&...),
    // а не напрямую на сайт — реальный адрес нужно достать из параметра uddg.
    static const QRegularExpression linkRe(
        QStringLiteral("class=\"result__a\"[^>]*href=\"([^\"]+)\""),
        QRegularExpression::CaseInsensitiveOption);

    QStringList seenHosts;
    auto it = linkRe.globalMatch(html);
    while (it.hasNext() && m_pendingUrls.size() < m_totalSources) {
        QRegularExpressionMatch m = it.next();
        QString rawHref = m.captured(1).replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        if (rawHref.startsWith(QStringLiteral("//"))) {
            rawHref.prepend(QStringLiteral("https:"));
        }

        QUrl redirectUrl(rawHref);
        QUrlQuery redirectQuery(redirectUrl);
        QString target = redirectQuery.queryItemValue(QStringLiteral("uddg"));
        if (target.isEmpty()) continue;

        QString decoded = QUrl::fromPercentEncoding(target.toUtf8());
        QUrl targetUrl(decoded);
        if (!targetUrl.isValid() || targetUrl.host().isEmpty()) continue;
        if (targetUrl.scheme() != QStringLiteral("http") && targetUrl.scheme() != QStringLiteral("https")) continue;

        // Не берём два результата с одного и того же домена — так отчёт
        // синтезируется из более разнообразных источников, а не, скажем,
        // из десяти разных статей одного и того же сайта.
        QString host = targetUrl.host().toLower();
        if (seenHosts.contains(host)) continue;
        seenHosts.append(host);

        m_pendingUrls.append(targetUrl.toString());
    }
}

// ==========================================================================
// Этап 2: чтение и очистка страниц
// ==========================================================================
void ResearchManager::fetchNextPage() {
    if (m_cancelled) { fail(QStringLiteral("Исследование отменено.")); return; }

    if (m_pendingUrls.isEmpty() || m_totalCollectedChars >= kMaxTotalChars) {
        if (m_collectedChunks.isEmpty()) {
            fail(QStringLiteral("Ни одну из найденных страниц не удалось прочитать."));
            return;
        }
        synthesizeWithAi();
        return;
    }

    QString urlStr = m_pendingUrls.takeFirst();
    QUrl url(urlStr);

    int percent = 10 + static_cast<int>((static_cast<double>(m_processedSources) / qMax(1, m_totalSources)) * 60.0);
    reportProgress(QStringLiteral("Чтение (%1/%2): %3").arg(m_processedSources + 1).arg(m_totalSources).arg(url.host()), percent);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (StormBrowser DeepResearch)"));
    request.setTransferTimeout(kNetworkTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = net()->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, urlStr]() {
        reply->deleteLater();
        if (m_cancelled) { fail(QStringLiteral("Исследование отменено.")); return; }

        m_processedSources++;

        // Одна неудачная страница не должна рушить весь отчёт — просто
        // пропускаем её и переходим к следующей ссылке.
        if (reply->error() == QNetworkReply::NoError) {
            QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
            if (contentType.isEmpty() || contentType.contains(QStringLiteral("text/html"), Qt::CaseInsensitive) ||
                contentType.contains(QStringLiteral("text/plain"), Qt::CaseInsensitive)) {

                QString html = QString::fromUtf8(reply->readAll());
                QString plainText = stripHtmlToPlainText(html);

                if (plainText.size() > kMaxCharsPerSource) {
                    plainText = plainText.left(kMaxCharsPerSource) + QStringLiteral("…");
                }

                if (!plainText.trimmed().isEmpty()) {
                    QString chunk = QStringLiteral("Источник %1 (%2):\n%3")
                        .arg(m_collectedChunks.size() + 1)
                        .arg(urlStr, plainText.trimmed());
                    m_collectedChunks.append(chunk);
                    m_totalCollectedChars += chunk.size();
                }
            }
        }

        fetchNextPage();
        });
}

QString ResearchManager::stripHtmlToPlainText(const QString& html) {
    QString text = html;

    // Сначала убираем содержимое script/style/noscript целиком — иначе их
    // текст (JS-код, CSS) попадёт в "плоский" текст вместе со статьёй.
    static const QRegularExpression scriptRe(
        QStringLiteral("<(script|style|noscript)[^>]*>.*?</\\1>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    text.remove(scriptRe);

    static const QRegularExpression commentRe(QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption);
    text.remove(commentRe);

    // Блочные теги заменяем переводом строки ДО того, как срезать все теги
    // вообще — иначе "<p>Раз</p><p>Два</p>" склеится в нечитаемое "РазДва".
    static const QRegularExpression blockBreakRe(
        QStringLiteral("</(p|div|li|h1|h2|h3|h4|h5|h6|tr|br)\\s*>|<br\\s*/?>"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(blockBreakRe, QStringLiteral("\n"));

    static const QRegularExpression tagRe(QStringLiteral("<[^>]+>"));
    text.remove(tagRe);

    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));

    static const QRegularExpression blankLinesRe(QStringLiteral("\\n{3,}"));
    text.replace(blankLinesRe, QStringLiteral("\n\n"));
    static const QRegularExpression spacesRe(QStringLiteral("[ \\t]{2,}"));
    text.replace(spacesRe, QStringLiteral(" "));

    return text.trimmed();
}

// ==========================================================================
// Этап 3: синтез через ИИ — через общий AiClient (см. AiClient.h/.cpp).
// Выбор бэкенда (OpenRouter / GigaChat / личный кабинет), сборка запроса,
// авторизация GigaChat и разбор ответа теперь в одном месте, используемом
// и здесь, и в AIAssistantWidget — вместо двух копий одной и той же логики.
// ==========================================================================
void ResearchManager::synthesizeWithAi() {
    if (m_cancelled) { fail(QStringLiteral("Исследование отменено.")); return; }
    reportProgress(QStringLiteral("Синтез ИИ…"), 72);

    QString sources = m_collectedChunks.join(QStringLiteral("\n\n---\n\n"));

    QString prompt = QStringLiteral(
        "Ты — аналитик-исследователь. На основе приведённых ниже выдержек из %1 разных источников "
        "составь связный аналитический отчёт по теме: \"%2\".\n\n"
        "Структура отчёта:\n"
        "1) Краткое резюме (2-4 предложения).\n"
        "2) Ключевые тезисы (списком).\n"
        "3) Подробный разбор по подтемам, если тема это предполагает.\n"
        "4) Противоречия и разногласия между источниками, если они есть.\n"
        "5) Список источников в конце (просто перечисли использованные URL).\n\n"
        "Пиши по-русски, ёмко и по делу, без \"воды\". Не выдумывай факты, которых нет в выдержках.\n\n"
        "=== ВЫДЕРЖКИ ИЗ ИСТОЧНИКОВ ===\n%3")
        .arg(m_collectedChunks.size())
        .arg(m_topic, sources);

    QJsonArray messages;
    QJsonObject sys;
    sys[QStringLiteral("role")] = QStringLiteral("system");
    sys[QStringLiteral("content")] = QStringLiteral(
        "Ты Storm AI в режиме составления исследовательских отчётов. Отвечай только готовым текстом "
        "отчёта, без вступлений вроде \"Конечно, вот отчёт\".");
    messages.append(sys);

    QJsonObject user;
    user[QStringLiteral("role")] = QStringLiteral("user");
    user[QStringLiteral("content")] = prompt;
    messages.append(user);

    // requestId "synthesis" вернётся в onAiReplyReceived/onAiReplyFailed —
    // сейчас у нас в полёте всегда ровно один AI-запрос за раз (защищено
    // m_busy в startResearch), но метка всё равно нужна, т.к. это часть
    // сигнатуры AiClient (общей для всех вызывающих, включая AIAssistantWidget,
    // у которого одновременных запросов больше одного не бывает, но кэш токена
    // GigaChat в AiClient общий на все запросы этого клиента).
    //
    // meterKind "research_report" — авторитетная серверная проверка квоты
    // (см. AiClient::sendRequest). checkQuotaThenProceed() уже проверил её
    // заранее для отзывчивого UX, это — подстраховка на случай гонки между
    // тем предварительным чеком и этим моментом. Для "своего ключа" AiClient
    // это поле проигнорирует (добавляется только в ветке "Личный кабинет").
    m_aiClient->sendRequest(QStringLiteral("synthesis"), messages, /*wantJsonObjectFormat=*/false,
        QVariant(), QStringLiteral("research_report"));
}

void ResearchManager::onAiReplyReceived(const QString& requestId, const QString& content, const QVariant& payload) {
    Q_UNUSED(requestId);
    Q_UNUSED(payload);
    if (m_cancelled) { fail(QStringLiteral("Исследование отменено.")); return; }

    if (content.trimmed().isEmpty()) {
        fail(QStringLiteral("ИИ вернул пустой ответ. Попробуйте другую тему или другой AI-бэкенд в настройках."));
        return;
    }
    saveReportAndFinish(content.trimmed());
}

void ResearchManager::onAiReplyFailed(const QString& requestId, const QString& errorMessage, bool authError, const QVariant& payload) {
    Q_UNUSED(requestId);
    Q_UNUSED(authError); // здесь, в отличие от AIAssistantWidget::classifyAndRoute, разницы в реакции нет — в обоих случаях просто сообщаем об ошибке
    Q_UNUSED(payload);
    fail(QStringLiteral("Ошибка ИИ: %1").arg(errorMessage));
}

void ResearchManager::onAiQuotaExceeded(const QString& requestId, const QString& detail, int used, int limit, const QVariant& payload) {
    Q_UNUSED(requestId);
    Q_UNUSED(payload);
    // Сработало именно на этапе синтеза (а не на предварительном чеке в
    // checkQuotaThenProceed()) — редкий случай гонки, но результат для
    // пользователя должен выглядеть так же: предложение оформить подписку,
    // а не рассыпанная в мусор работа по сбору источников без объяснения.
    m_busy = false;
    emit researchQuotaExceeded(detail, used, limit);
}

// ==========================================================================
// Этап 4: сохранение отчёта
// ==========================================================================
QString ResearchManager::buildReportFilePath(const QString& title) const {
    QString safeName = title;
    // Убираем символы, недопустимые в именах файлов на Windows/macOS/Linux.
    static const QRegularExpression forbiddenRe(QStringLiteral("[\\\\/:*?\"<>|]"));
    safeName.replace(forbiddenRe, QStringLiteral("_"));
    safeName = safeName.trimmed();
    if (safeName.isEmpty()) safeName = QStringLiteral("Отчёт");
    if (safeName.size() > 60) safeName = safeName.left(60);

    QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    QString ext = (m_format == QLatin1String("txt")) ? QStringLiteral("txt") : QStringLiteral("pdf");
    QString fileName = QStringLiteral("%1 [%2].%3").arg(safeName, stamp, ext);

    // Тот же путь, что использует Storm Reader — берём его метод напрямую
    // (public static), а не пересчитываем сами, чтобы пути не могли
    // разойтись. getBooksDir() сам создаёт папку, если её ещё нет.
    return QDir(ReaderWidget::getBooksDir()).filePath(fileName);
}

void ResearchManager::saveReportAndFinish(const QString& aiText) {
    if (m_cancelled) { fail(QStringLiteral("Исследование отменено.")); return; }
    reportProgress(QStringLiteral("Сохраняю отчёт…"), 95);

    // Заголовок отчёта — тема исследования (обрезанная для читаемости в
    // списке истории и как имя файла).
    QString title = m_topic.size() > 80 ? m_topic.left(80) + QStringLiteral("…") : m_topic;
    QString filePath = buildReportFilePath(title);

    QString errorOut;
    bool ok = (m_format == QLatin1String("txt"))
        ? saveAsTxt(filePath, title, aiText, errorOut)
        : saveAsPdf(filePath, title, aiText, errorOut);

    m_busy = false;

    if (!ok) {
        emit researchFailed(QStringLiteral("Не удалось сохранить файл отчёта: %1").arg(errorOut));
        return;
    }

    reportProgress(QStringLiteral("Отчёт готов ✅"), 100);
    emit researchFinished(filePath, title);
}

bool ResearchManager::saveAsTxt(const QString& filePath, const QString& title, const QString& body, QString& errorOut) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorOut = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << title << "\n";
    out << QStringLiteral("Сгенерировано Storm Deep Research: ") << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    out << QStringLiteral("========================================\n\n");
    out << body << "\n";
    file.close();
    return true;
}

bool ResearchManager::saveAsPdf(const QString& filePath, const QString& title, const QString& body, QString& errorOut) const {
    // QTextDocument + QPrinter — стандартный способ получить многостраничный
    // PDF с автоматической разбивкой на страницы в Qt (модуль QtPrintSupport).
    // ПРИМЕЧАНИЕ ДЛЯ ИНТЕГРАЦИИ: этот код рендерит документ, находясь в
    // потоке воркера, а не в GUI-потоке. На практике QTextDocument/QPrinter
    // с PdfFormat в этом сценарии (запись сразу в файл, без экрана) не
    // требуют GUI-потока — но так как это единственное место во всём модуле,
    // где мы выходим за пределы чисто сетевого/текстового кода, стоит
    // проверить это на целевой платформе перед продакшеном; при проблемах
    // самое простое решение — выполнить именно эту функцию через
    // QMetaObject::invokeMethod(..., Qt::BlockingQueuedConnection) в GUI-потоке.
    QTextDocument doc;
    doc.setDocumentMargin(36);

    QString html = QStringLiteral(
        "<h1 style=\"color:#a371f7;\">%1</h1>"
        "<p style=\"color:#8b949e; font-size:11px;\">Сгенерировано Storm Deep Research — %2</p>"
        "<hr/>"
        "<div style=\"font-size:13px; white-space:pre-wrap;\">%3</div>")
        .arg(title.toHtmlEscaped(),
            QDateTime::currentDateTime().toString(Qt::ISODate),
            body.toHtmlEscaped());
    doc.setHtml(html);

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setOutputFileName(filePath);

    doc.print(&printer);

    if (!QFileInfo::exists(filePath) || QFileInfo(filePath).size() == 0) {
        errorOut = QStringLiteral("PDF-принтер не создал файл (проверьте модуль QtPrintSupport в сборке).");
        return false;
    }
    return true;
}