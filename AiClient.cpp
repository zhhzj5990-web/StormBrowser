#include "AiClient.h"
#include "CertificateManager.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>

// Как и во всём остальном коде проекта (см. AIAssistantWidget.cpp) — u8"..."
// для строк, идущих в QString, работает нормально: проект собирается как
// C++17 (см. CMakeLists.txt: CMAKE_CXX_STANDARD 17), где u8"..." — это ещё
// обычный const char*, а не char8_t*. Здесь тем не менее продолжаем без
// префикса ради единообразия с ResearchWidget/ResearchManager.

namespace {
    // Раньше называлась applyGigaChatSslBypass и делала
    // sslConf.setPeerVerifyMode(QSslSocket::VerifyNone) — полностью
    // отключала проверку сертификата (принимался бы вообще любой, в т.ч.
    // поддельный MITM-сертификат). Сервер GigaChat использует TLS-
    // сертификат, выпущенный НУЦ Минцифры, которому Qt/ОС не доверяют "из
    // коробки" — теперь, когда CertificateManager знает про встроенные
    // корневые сертификаты Минцифры, используем нормальную проверку с
    // расширенным списком доверенных CA вместо полного отключения. Имя
    // функции обновлено, чтобы не вводить в заблуждение — по факту это
    // больше не "обход", а "доверие конкретному корню".
    void applyGigaChatTrustedSsl(QNetworkRequest& req) {
        req.setSslConfiguration(CertificateManager::trustedSslConfiguration());
    }
}

AiClient::AiClient(QObject* parent) : QObject(parent) {
}

QNetworkAccessManager* AiClient::net() {
    // Ленивое создание — важно, если AiClient когда-нибудь создаётся ДО
    // moveToThread() своего владельца (см. ResearchManager): тогда сам
    // QNetworkAccessManager появится уже в правильном потоке, при первом
    // реальном вызове sendRequest(), а не в конструкторе.
    if (!m_net) {
        m_net = new QNetworkAccessManager(this);
    }
    return m_net;
}

void AiClient::cancel() {
    if (m_activeReply) {
        // abort() приводит к немедленному finished с OperationCanceledError —
        // обычная ветка обработки ошибок корректно завершит запрос (вышестоящий
        // код по флагу отмены превратит его в «Исследование отменено»).
        m_activeReply->abort();
    }
}

void AiClient::sendRequest(const QString& requestId, const QJsonArray& messages,
    bool wantJsonObjectFormat, const QVariant& payload, const QString& meterKind) {

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
    // P2-3: без таймаута зависший ответ LLM-сервера навсегда оставлял
    // запрос "в полёте". 2 минуты с запасом на длинные генерации.
    request.setTransferTimeout(120 * 1000);
    QNetworkReply* reply = nullptr;

    if (useCustomApi) {
        json["model"] = QString("openrouter/auto");
        if (wantJsonObjectFormat) json["response_format"] = QJsonObject{ {"type", "json_object"} };
        request.setUrl(QUrl("https://openrouter.ai/api/v1/chat/completions"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
        request.setRawHeader("HTTP-Referer", "https://storm-browser.ru/");
        request.setRawHeader("X-Title", "Storm Browser");
        reply = net()->post(request, QJsonDocument(json).toJson());
    }
    else if (useGigaChat) {
        QString authErr;
        if (!ensureGigaChatToken(gigaKey, authErr)) {
            emit replyFailed(requestId, QString("Ошибка авторизации GigaChat: %1").arg(authErr), /*authError=*/true, payload);
            return;
        }
        json["model"] = gigaModel;
        request.setUrl(QUrl("https://api.giga.chat/v1/chat/completions"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", ("Bearer " + m_cachedGigaToken).toUtf8());
        applyGigaChatTrustedSsl(request);
        reply = net()->post(request, QJsonDocument(json).toJson());
    }
    else {
        if (user.isEmpty() || pwd.isEmpty()) {
            emit replyFailed(requestId, QString("Войдите в Личный кабинет в Настройки, либо выберите «Свой API» или GigaChat."), /*authError=*/true, payload);
            return;
        }
        json["username"] = user;
        json["password"] = pwd;
        json["user_api_key"] = QString("");
        // "purpose" — ТОЛЬКО в этой ветке (Личный кабинет): сервер по этому
        // полю понимает, какую тарифицируемую квоту проверить/списать перед
        // тем как генерировать ответ. Для "своего ключа" (OpenRouter/GigaChat
        // выше) это поле никогда не добавляется — сервер storm-browser.online
        // эти запросы вообще не видит.
        if (!meterKind.isEmpty()) {
            json["purpose"] = meterKind;
        }
        request.setUrl(QUrl("https://storm-browser.online:8000/api/ai/chat"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = net()->post(request, QJsonDocument(json).toJson());
    }

    if (!reply) {
        // net()->post() вернул nullptr (например, запрос отклонён ещё до
        // отправки) — раньше это навсегда оставляло m_busy у вызывающего кода:
        // ни один сигнал не эмитился, UI «Синтез ИИ…» вис до перезапуска.
        emit replyFailed(requestId, QStringLiteral("Не удалось отправить запрос (пустой ответ сети)"), /*authError=*/false, payload);
        return;
    }
    m_activeReply = reply;

    // messages/flags захватываются в лямбду по значению: они нужны для
    // единственного повтора запроса при протухшем токене GigaChat (401).
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, requestId, payload, messages, wantJsonObjectFormat, meterKind]() {
        reply->deleteLater();
        if (m_activeReply == reply) m_activeReply = nullptr;

        if (reply->error() != QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

            // GigaChat: 401 — кэшированный токен протух между получением и
            // этим запросом (живёт 1740 с). Без этой ветки один просроченный
            // токен убивал всю собранную работу Deep Research на финальном
            // синтезе. Сбрасываем токен и повторяем запрос ровно один раз
            // (метка-предохранитель в payload не даёт зациклиться).
            if (statusCode == 401 && !m_cachedGigaToken.isEmpty()
                && QSettings().value("ai/mode", 0).toInt() == 2
                && payload != QVariant(QStringLiteral("__giga401retried__"))) {
                m_cachedGigaToken.clear();
                m_gigaTokenExpireTime = 0;
                sendRequest(requestId, messages, wantJsonObjectFormat,
                    QVariant(QStringLiteral("__giga401retried__")), meterKind);
                return;
            }

            QJsonObject errObj = QJsonDocument::fromJson(reply->readAll()).object();
            // "detail" — то же поле, которым StormCloudBridge везде сообщает
            // ошибки сервера (login/register/purchase/...), поэтому сервер
            // может использовать один и тот же формат ответа для квоты.
            QString detail = errObj.value("detail").toString();

            if (statusCode == 402) {
                // Оплата/лимит требуется — сервер обязан вернуть 402 именно
                // для этого случая (а не 400/403), чтобы клиент мог отличить
                // "закончилась квота" от обычной сетевой/серверной ошибки.
                int used = errObj.contains("used") ? errObj.value("used").toInt() : -1;
                int limit = errObj.contains("limit") ? errObj.value("limit").toInt() : -1;
                emit quotaExceeded(requestId,
                    detail.isEmpty() ? QString("Лимит запросов на этом тарифе исчерпан.") : detail,
                    used, limit, payload);
                return;
            }

            QString errorMsg = (statusCode == 0)
                ? QString("Нет подключения к серверу. Проверьте интернет.")
                : (!detail.isEmpty() ? detail : QString("Ошибка %1: %2").arg(statusCode).arg(reply->errorString()));
            emit replyFailed(requestId, errorMsg, /*authError=*/false, payload);
            return;
        }

        QJsonObject rootObj = QJsonDocument::fromJson(reply->readAll()).object();
        QString content;
        if (rootObj.contains("choices")) {
            QJsonArray choices = rootObj.value("choices").toArray();
            if (!choices.isEmpty()) {
                content = choices[0].toObject().value("message").toObject().value("content").toString().trimmed();
            }
        }
        else if (rootObj.contains("reply")) {
            content = rootObj.value("reply").toString().trimmed();
        }

        // Учёт токенов — теперь для ОБОИХ режимов без своего ключа:
        // Личный кабинет (ai/mode == 1) И GigaChat (ai/mode == 2).
        if (rootObj.contains("usage")) {
            QSettings tokenSettings;
            int mode = tokenSettings.value("ai/mode", 0).toInt();
            if (mode == 1 || mode == 2) {
                qint64 totalTokens = rootObj.value("usage").toObject().value("total_tokens").toVariant().toLongLong();
                qint64 usedSoFar = tokenSettings.value("ai/tokens_used", 0).toLongLong();
                tokenSettings.setValue("ai/tokens_used", usedSoFar + totalTokens);
            }
        }

        emit replyReceived(requestId, content, payload);
        });
}

bool AiClient::ensureGigaChatToken(const QString& gigaKey, QString& errorOut) {
    qint64 currentTime = QDateTime::currentSecsSinceEpoch();
    if (!m_cachedGigaToken.isEmpty() && currentTime < m_gigaTokenExpireTime) return true;

    QNetworkRequest authReq(QUrl("https://ngw.devices.sberbank.ru:9443/api/v2/oauth"));
    authReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    authReq.setRawHeader("Accept", "application/json");
    authReq.setRawHeader("RqUID", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
    authReq.setRawHeader("Authorization", ("Basic " + gigaKey).toUtf8());
    applyGigaChatTrustedSsl(authReq);
    // P1-5: у запроса токена и у цикла ожидания ниже НЕ БЫЛО таймаута —
    // зависший endpoint Сбербанка навсегда вешал UI во вложенном цикле
    // (браузер выглядел "зависшим намертво"). Теперь и запрос, и ожидание
    // ограничены по времени.
    authReq.setTransferTimeout(15 * 1000);

    // Временный локальный менеджер на стеке — как и в оригинальном
    // AIAssistantWidget::ensureGigaChatToken. Это синхронный вызов через
    // вложенный QEventLoop, поэтому намеренно НЕ используем net() (общий
    // персистентный менеджер этого AiClient): не хотим, чтобы вложенный
    // loop.exec() случайно начал обрабатывать finished() от каких-то других,
    // не связанных с этим запросом ответов, которые могли бы прилететь на
    // net() в это же время.
    QNetworkAccessManager authManager;
    QNetworkReply* authReply = authManager.post(authReq, QByteArray("scope=GIGACHAT_API_PERS"));
    QEventLoop loop;
    bool authFinished = false;
    QObject::connect(authReply, &QNetworkReply::finished, &loop, [&loop, &authFinished]() {
        authFinished = true;
        loop.quit();
        });
    // Страховочный таймаут на случай, если ответ так и не придёт
    // (setTransferTimeout тоже оборвёт запрос, но событие finished после
    // abort() гарантированно завершит цикл и здесь).
    QTimer::singleShot(20 * 1000, &loop, &QEventLoop::quit);
    loop.exec();

    bool ok = authFinished && (authReply->error() == QNetworkReply::NoError);
    if (!ok) {
        errorOut = authFinished ? authReply->errorString()
            : QString::fromUtf8(u8"Таймаут запроса токена GigaChat (сервер не ответил за 20 с)");
    }
    else {
        QJsonObject authDoc = QJsonDocument::fromJson(authReply->readAll()).object();
        m_cachedGigaToken = authDoc.value("access_token").toString();
        m_gigaTokenExpireTime = currentTime + 1740;
        ok = !m_cachedGigaToken.isEmpty();
        if (!ok) {
            errorOut = QString::fromUtf8(u8"Сервер GigaChat вернул пустой токен доступа");
        }
    }
    authReply->deleteLater();
    return ok;
}