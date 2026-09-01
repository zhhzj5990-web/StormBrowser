#include "ShieldInterceptor.h"
#include "AdblockManager.h"
#include <QSet>
#include <QMutexLocker>

ShieldInterceptor::ShieldInterceptor(QObject* parent, int initialBlockedCount)
    : QWebEngineUrlRequestInterceptor(parent), m_enabled(true), m_blockedCount(initialBlockedCount) {
}

void ShieldInterceptor::setEnabled(bool enabled) {
    m_enabled = enabled;
}

QString ShieldInterceptor::normalizeHost(const QString& host) {
    QString h = host.toLower().trimmed();
    if (h.startsWith("www.")) h = h.mid(4);
    return h;
}

void ShieldInterceptor::setExceptions(const QStringList& hosts) {
    QMutexLocker locker(&m_exceptionsMutex);
    m_exceptions.clear();
    for (const QString& h : hosts) {
        QString norm = normalizeHost(h);
        if (!norm.isEmpty()) m_exceptions.insert(norm);
    }
}

void ShieldInterceptor::addException(const QString& host) {
    QString norm = normalizeHost(host);
    if (norm.isEmpty()) return;
    QMutexLocker locker(&m_exceptionsMutex);
    m_exceptions.insert(norm);
}

void ShieldInterceptor::removeException(const QString& host) {
    QString norm = normalizeHost(host);
    QMutexLocker locker(&m_exceptionsMutex);
    m_exceptions.remove(norm);
}

QStringList ShieldInterceptor::exceptions() const {
    QMutexLocker locker(&m_exceptionsMutex);
    QStringList list(m_exceptions.begin(), m_exceptions.end());
    list.sort(Qt::CaseInsensitive);
    return list;
}

bool ShieldInterceptor::isExcepted(const QString& host) const {
    QString h = normalizeHost(host);
    if (h.isEmpty()) return false;
    QMutexLocker locker(&m_exceptionsMutex);
    if (m_exceptions.contains(h)) return true;
    for (const QString& ex : m_exceptions) {
        if (h.endsWith("." + ex)) return true;
    }
    return false;
}

void ShieldInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info) {
    if (!m_enabled) return;

    // 1. БЕЛЫЙ СПИСОК: Мгновенно пропускаем любые игровые ресурсы (SWF, WASM, Ruffle)
    QString urlStrLower = info.requestUrl().toString().toLower();
    if (urlStrLower.contains("ruffle") ||
        urlStrLower.contains(".swf") ||
        urlStrLower.contains(".wasm") ||
        urlStrLower.contains("unpkg.com")) {
        return; // Пропускаем без проверок!
    }

    QString scheme = info.requestUrl().scheme().toLower();
    if (scheme != "http" && scheme != "https") return;

    QWebEngineUrlRequestInfo::ResourceType resType = info.resourceType();
    QString host = info.requestUrl().host().toLower();

    // Хост в исключениях (см. addException/setExceptions) — полный обход Shield
    // для него, до любых проверок ниже: ни реклама, ни малварь/фишинг.
    if (isExcepted(host)) return;

    if (resType == QWebEngineUrlRequestInfo::ResourceTypeMainFrame) {
        // ВАЖНО: раньше здесь был безусловный return — MainFrame-переходы
        // (прямой клик по ссылке, набор URL) вообще не проверялись, а это
        // самый частый вектор фишинга. Рекламные правила по-прежнему
        // намеренно НЕ блокируют MainFrame (чтобы не ломать прямые переходы
        // на рекламные/промо-домены), но malware/phishing-проверка обязана
        // работать всегда, независимо от типа ресурса — иначе антифишинг
        // ловит только подресурсы уже открытой страницы, а не саму атаку.
        bool isMalware = false;
        if (AdblockManager::instance().isBlocked(host, urlStrLower, isMalware) && isMalware) {
            info.block(true);
            int currentTotal = ++m_blockedCount;
            emit blockedCountChanged(currentTotal);
            emit threatDetected(info.requestUrl().toString());
        }
        return;
    }

    // 2. КАТЕГОРИЧЕСКАЯ ГАРАНТИЯ: никогда не трогаем типы запросов, через
    // которые сайты обновляют/подтверждают сессию — XHR/fetch (99% всех
    // AJAX-запросов авторизации и поллинга QR-кодов идут именно так),
    // Service Worker, обычный Worker, CSP-отчёты, предзагрузку. Это ценой
    // небольшой части рекламных XHR-трекеров, зато полностью исключает
    // Shield как причину разрыва сессий на VK/Google/YouTube и т.п.
    static const QSet<int> NEVER_TOUCH_TYPES = {
        static_cast<int>(QWebEngineUrlRequestInfo::ResourceTypeXhr),
        static_cast<int>(QWebEngineUrlRequestInfo::ResourceTypePing),
        static_cast<int>(QWebEngineUrlRequestInfo::ResourceTypeServiceWorker),
        static_cast<int>(QWebEngineUrlRequestInfo::ResourceTypeWorker),
        static_cast<int>(QWebEngineUrlRequestInfo::ResourceTypeSharedWorker),
        static_cast<int>(QWebEngineUrlRequestInfo::ResourceTypeCspReport),
        static_cast<int>(QWebEngineUrlRequestInfo::ResourceTypePrefetch),
    };
    if (NEVER_TOUCH_TYPES.contains(static_cast<int>(resType))) {
        // ВАЖНО: раньше этот блок был безусловным "return" — не только рекламные
        // правила, но и антифишинг/малварь-проверка полностью пропускались для
        // XHR/fetch и Worker-запросов. Это давало трекерам лазейку (реклама и
        // так не блокируется в MainFrame намеренно, тут проблемы нет), но заодно
        // означало, что вредоносный/фишинговый хост, к которому страница
        // обращается через fetch()/XHR (например, отправка данных формы логина
        // на поддельный домен), проходил вообще без проверки. Как и в ветке
        // MainFrame выше, оставляем рекламный движок отключённым для этих типов
        // (чтобы не ломать сессии VK/Google/YouTube), но малварь обязана
        // проверяться всегда.
        bool isMalware = false;
        if (AdblockManager::instance().isBlocked(host, urlStrLower, isMalware) && isMalware) {
            info.block(true);
            int currentTotal = ++m_blockedCount;
            emit blockedCountChanged(currentTotal);
            emit threatDetected(info.requestUrl().toString());
        }
        return;
    }

    bool isMalware = false;
    if (AdblockManager::instance().isBlocked(host, urlStrLower, isMalware)) {
        info.block(true);

        int currentTotal = ++m_blockedCount;
        emit blockedCountChanged(currentTotal);

        if (isMalware) {
            emit threatDetected(info.requestUrl().toString());
        }
    }
}