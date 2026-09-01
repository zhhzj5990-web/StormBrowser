#include "AdblockManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QRandomGenerator>
#include <QSettings>
#include <QRegularExpression>

AdblockManager& AdblockManager::instance() {
    static AdblockManager mgr;
    return mgr;
}

AdblockManager::AdblockManager(QObject* parent) : QObject(parent) {
    m_netManager = new QNetworkAccessManager(this);

    // Вшитые сверхбыстрые ключевые слова для мгновенного отсечения рекламы.
    // ИСПРАВЛЕНО: раньше этот список расходился с core_rules в interceptor.py
    // (там были ещё google-analytics.com, /ads.js, /banner.js — их не было
    // здесь). Домены, которые в Python задаются как чистые доменные правила
    // ("||an.yandex.ru^", "||doubleclick.net^" и т.д.), теперь тоже вынесены
    // ниже в m_blockedDomains — так они матчатся вместе с поддоменами (см.
    // matchesDomainOrSubdomain), а не только как подстрока URL.
    m_blockedKeywords = {
        "/ads/", "/analytics/", "yandex.ru/ads", "googleadservices",
        "/ads.js", "/banner.js"
    };

    // "||vk.com/ads_*" в core_rules Python — это wildcard-правило (путь с
    // маской), а не просто подстрока и не чистый домен. Компилируем regex
    // один раз при старте, а не на каждый isBlocked() (isBlocked дергается
    // на IO-потоке на каждый сетевой запрос — компилировать regex там же
    // было бы лишней нагрузкой).
    {
        QString escaped = QRegularExpression::escape(QStringLiteral("vk.com/ads_*"));
        escaped.replace(QStringLiteral("\\*"), QStringLiteral(".*"));
        m_blockedKeywordRegexes.append(
            QRegularExpression(escaped, QRegularExpression::CaseInsensitiveOption));
    }

    // Доменные core-правила из interceptor.py (там строки вида "||domain^",
    // без пути) — переносим их сюда как полноценные доменные правила, а не
    // ключевые слова, чтобы они матчились вместе с поддоменами, как и
    // положено семантике "||domain^" в uBlock/Rust-движке.
    // "yandex.ru/ads/^" и "vk.com/ads_*" сюда НЕ входят, т.к. это правила
    // с путём/маской, а не на весь домен целиком (блокировать весь yandex.ru
    // было бы явной регрессией — сломало бы поиск Яндекса).
    m_blockedDomains.unite({
        "an.yandex.ru",
        "doubleclick.net",
        "google-analytics.com",
        "adservice.google.com",
        "mc.yandex.ru",
        "googlesyndication.com"
        });

    // ВАЖНО: подключение делаем НЕ здесь на уровне m_netManager->finished(),
    // а per-reply в checkAndDownloadRules() — иначе один общий finished()
    // не может отличить ответ ads.txt от ответа malware-фида, и оба
    // пришли бы в один и тот же слот.
}

void AdblockManager::init(QWebEngineProfile* profile, QWebEngineProfile* privateProfile) {
    // Запоминаем профили, чтобы addCustomHideSelector()/removeCustomHideSelector()
    // могли позже переприменить обновлённый косметический скрипт без
    // перезапуска браузера.
    m_mainProfile = profile;
    m_privateProfile = privateProfile;

    loadLocalRules();
    applyCosmeticAndStealthScripts(profile);
    if (privateProfile) applyCosmeticAndStealthScripts(privateProfile);
    checkAndDownloadRules();
}

bool AdblockManager::matchesDomainOrSubdomain(const QString& host, const QSet<QString>& domains) {
    // uBlock/Rust-движок для правила "||domain^" блокирует и сам домен, и
    // ЛЮБОЙ его поддомен (ad.doubleclick.net матчится правилом на
    // doubleclick.net). Старая версия делала только domains.contains(host) —
    // точное совпадение всей строки хоста, поэтому реклама/malware с
    // поддоменов из списка проходила незамеченной. Идём по суффиксам
    // "a.b.c.com" -> "b.c.com" -> "c.com", каждый — O(1) хэш-проверка,
    // так что стоимость пропорциональна числу меток в хосте, а не размеру
    // списка доменов.
    int start = 0;
    while (true) {
        if (domains.contains(host.mid(start))) return true;
        int dot = host.indexOf(QLatin1Char('.'), start);
        if (dot < 0) break;
        start = dot + 1;
    }
    return false;
}

bool AdblockManager::matchesKeywordRules(const QString& urlStr) const {
    for (const QString& kw : m_blockedKeywords) {
        if (urlStr.contains(kw)) return true;
    }
    for (const QRegularExpression& re : m_blockedKeywordRegexes) {
        if (re.match(urlStr).hasMatch()) return true;
    }
    return false;
}

void AdblockManager::registerBlock(bool isMalware, const QString& urlStr) {
    // Паритет с interceptor.py::_increment_blocked(): там каждый блок
    // увеличивает и локальный blocked_count, и персистентный
    // security/total_blocked в QSettings, после чего эмитится
    // blocked_count_changed. Здесь эмитим тот же по смыслу сигнал; сам
    // persist в БД (DatabaseManager::setBlockedThreatsCount) должен быть
    // подключен снаружи (см. комментарий в .h) — AdblockManager не хранит
    // ссылку на DatabaseManager, чтобы не тащить лишнюю зависимость в
    // синглтон, который живёт с самого старта профиля.
    const int total = m_totalBlocked.fetch_add(1, std::memory_order_relaxed) + 1;
    emit blockedCountChanged(total);

    if (isMalware) {
        // Паритет с security_threat_detected — шлём ТОЛЬКО для malware/
        // фишинга, не для обычной рекламы (как и в Python).
        emit securityThreatDetected(urlStr);
    }
}

int AdblockManager::totalBlockedCount() const {
    return m_totalBlocked.load(std::memory_order_relaxed);
}

void AdblockManager::setInitialBlockedCount(int count) {
    m_totalBlocked.store(count, std::memory_order_relaxed);
}

bool AdblockManager::isBlocked(const QString& host, const QString& urlStr, bool& outIsMalware) {
    outIsMalware = false;

    // isBlocked() дергается на IO-потоке QtWebEngine на каждый сетевой
    // запрос страницы, а сеты ниже мутируются на главном потоке
    // (loadLocalRules / onAdRulesDownloaded / onMalwareRulesDownloaded).
    // Без лока это гонка на неатомарных QSet.
    QReadLocker locker(&m_rulesLock);

    // 1. Проверка на фишинг и вирусы, включая поддомены — O(число меток хоста).
    if (matchesDomainOrSubdomain(host, m_malwareDomains)) {
        outIsMalware = true;
        locker.unlock();
        registerBlock(true, urlStr);
        return true;
    }

    // 2. Проверка домена по хэш-таблице рекламы, тоже с поддоменами —
    // ИСПРАВЛЕНО: раньше был только m_blockedDomains.contains(host), что
    // пропускало рекламу с поддоменов заблокированных доменов.
    if (matchesDomainOrSubdomain(host, m_blockedDomains)) {
        locker.unlock();
        registerBlock(false, urlStr);
        return true;
    }

    // 3. Проверка по ключевым словам и wildcard-правилам (path-rules из
    // core_rules вроде "/ads.js" и "vk.com/ads_*").
    if (matchesKeywordRules(urlStr)) {
        locker.unlock();
        registerBlock(false, urlStr);
        return true;
    }

    return false;
}

void AdblockManager::applyCosmeticAndStealthScripts(QWebEngineProfile* profile) {
    if (!profile) return;

    // 1. КОСМЕТИЧЕСКИЙ АНТИБАННЕР (Чистый CSS, скрывает пустые рамки)[cite: 20]
    QString cssHide = ".adsbygoogle, .ad-container, .banner-ads, [id^='google_ads_'], "
        ".yandex-rtb, [class*='popup-ad'], .ad-overlay, .promo-popup, "
        "[class*='cookie-banner'], #cookie-consent, .adv-banner "
        "{ display: none !important; width: 0 !important; height: 0 !important; }"
        // VK: карточка "Рекомендуем в VK Видео" (реклама myTarget/mradx.net).
        // Сама реклама и так блокируется на сетевом уровне в ShieldInterceptor —
        // это правило только прячет оставшийся после блокировки пустой контейнер
        // со сломанной иконкой. Селектор специально ограничен точной цепочкой
        // ПРЯМЫХ потомков (>), а не "где угодно внутри" — иначе широкий :has()
        // мог бы случайно поймать общую обёртку всей ленты (у неё те же
        // служебные классы vkuiRootComponent__host) и скрыть страницу целиком.
        "div.vkuiRootComponent__host:has(> div.vkuiRootComponent__host > "
        "div[class^='vkitSnippetAttachment__root']) "
        "{ display: none !important; height: 0 !important; }";

    // 1.1 Пользовательские правила "Заблокировать элемент" из контекстного
    // меню — хранятся как "host##selector" (формат как в обычных adblock-
    // фильтрах) и применяются только на своём хосте (см. matches() в JS
    // ниже), чтобы селектор с одного сайта не мог случайно задеть разметку
    // другого.
    QJsonArray customRulesJson;
    {
        QReadLocker locker(&m_rulesLock);
        for (const QString& rule : m_customHideRules) {
            int sep = rule.indexOf("##");
            if (sep < 0) continue;
            QJsonObject obj;
            obj["host"] = rule.left(sep);
            obj["selector"] = rule.mid(sep + 2);
            customRulesJson.append(obj);
        }
    }
    QString customRulesJs = QString::fromUtf8(QJsonDocument(customRulesJson).toJson(QJsonDocument::Compact));

    QString cosmeticJs = QString(u8R"JS(
        (function() {
            const style = document.createElement('style');
            style.id = 'storm-shield-cosmetic';
            style.textContent = `%1`;
            document.head.appendChild(style);

            const customRules = %2;
            const host = window.location.hostname;
            const matches = (h, ruleHost) => h === ruleHost || h.endsWith('.' + ruleHost);
            const applicable = customRules.filter(r => matches(host, r.host));
            if (applicable.length) {
                const userStyle = document.createElement('style');
                userStyle.id = 'storm-shield-custom';
                userStyle.textContent = applicable.map(r =>
                    r.selector + ' { display: none !important; }').join('\n');
                document.head.appendChild(userStyle);
            }
        })();
    )JS").arg(cssHide, customRulesJs);

    QWebEngineScript cosmeticScript;
    cosmeticScript.setName("StormCosmeticAdblock");
    cosmeticScript.setSourceCode(cosmeticJs);
    cosmeticScript.setInjectionPoint(QWebEngineScript::DocumentReady);
    cosmeticScript.setWorldId(QWebEngineScript::MainWorld);
    cosmeticScript.setRunsOnSubFrames(true);

    // Метод теперь может вызываться повторно (после добавления нового
    // правила через "Заблокировать элемент") — убираем старую версию,
    // иначе Chromium накопит несколько копий скрипта и вставит несколько
    // <style> при каждой загрузке страницы.
    // ПРИМЕЧАНИЕ: в актуальных версиях Qt 6 метод называется find() (а не
    // findScript() из более старых 5.x/6.x) и возвращает СПИСОК скриптов
    // с этим именем, а не один — QWebEngineScript::isNull() тут поэтому
    // не нужен, просто удаляем все найденные по имени.
    const QList<QWebEngineScript> existingScripts = profile->scripts()->find("StormCosmeticAdblock");
    for (const QWebEngineScript& s : existingScripts) {
        profile->scripts()->remove(s);
    }

    profile->scripts()->insert(cosmeticScript);

    // 2. СТЕЛС-РЕЖИМ (Anti-Fingerprinting для Canvas, WebGL и железа)[cite: 20]
    // ВАЖНО: шум генерируется ОДИН РАЗ и сохраняется в QSettings навсегда,
    // а не заново при каждом запуске. Раньше noise пересоздавался случайным
    // при каждом старте браузера — из-за этого Canvas-отпечаток менялся
    // каждый раз, и сайты вроде Google/VK/YouTube воспринимали это как
    // "новое устройство", требуя повторный вход даже после успешного
    // QR-логина в предыдущей сессии. Смысл защиты от фингерпринтинга
    // (сайт не видит РЕАЛЬНЫЙ canvas) при этом сохраняется — меняется
    // только то, что теперь этот "поддельный" отпечаток стабилен между
    // перезапусками, как и должно быть у нормального браузера.
    QSettings stealthSettings;
    double noise = stealthSettings.value("shield/canvas_noise", -1.0).toDouble();
    if (noise < 0.0) {
        noise = 0.000001 + QRandomGenerator::global()->generateDouble() * (0.000009 - 0.000001);
        stealthSettings.setValue("shield/canvas_noise", noise);
    }
    QString stealthJs = QString(u8R"JS(
        (function() {
            const noise = %1;
            const origToDataURL = HTMLCanvasElement.prototype.toDataURL;
            HTMLCanvasElement.prototype.toDataURL = function() {
                const ctx = this.getContext('2d');
                if (ctx) { ctx.fillStyle = `rgba(255,255,255,${noise})`; ctx.fillRect(0,0,1,1); }
                return origToDataURL.apply(this, arguments);
            };
            Object.defineProperty(navigator, 'hardwareConcurrency', { get: () => 8 });
            Object.defineProperty(navigator, 'deviceMemory', { get: () => 8 });
        })();
    )JS").arg(noise, 0, 'f', 8);

    QWebEngineScript stealthScript;
    stealthScript.setName("StormStealthMode");
    stealthScript.setSourceCode(stealthJs);
    stealthScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
    stealthScript.setWorldId(QWebEngineScript::MainWorld);
    stealthScript.setRunsOnSubFrames(true);
    profile->scripts()->insert(stealthScript);
}

void AdblockManager::loadLocalRules() {
    QString rulesDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Rules";

    // Читаем файлы ДО захвата лока — I/O не должно держать writer-блокировку
    // дольше необходимого и задерживать конкурирующие isBlocked() на IO-потоке.
    QSet<QString> newAdDomains;
    QFile adFile(rulesDir + "/ads.json");
    if (adFile.open(QIODevice::ReadOnly)) {
        QJsonObject obj = QJsonDocument::fromJson(adFile.readAll()).object();
        QJsonArray arr = obj["blocked_domains"].toArray();
        for (const auto& val : arr) newAdDomains.insert(val.toString().toLower());
    }

    // Симметричная загрузка malware.json — без неё антифишинг оставался бы
    // пуст до первого успешного скачивания (или навсегда при отсутствии сети).
    QSet<QString> newMalwareDomains;
    QFile malwareFile(rulesDir + "/malware.json");
    if (malwareFile.open(QIODevice::ReadOnly)) {
        QJsonObject obj = QJsonDocument::fromJson(malwareFile.readAll()).object();
        QJsonArray arr = obj["malware_domains"].toArray();
        for (const auto& val : arr) newMalwareDomains.insert(val.toString().toLower());
    }

    // Пользовательские "Заблокировать элемент" — грузим один раз при
    // старте из QSettings, дальше живут только в памяти + QSettings,
    // без сети (в отличие от ads.json/malware.json выше).
    QStringList savedCustomRules;
    {
        QSettings s;
        savedCustomRules = s.value("shield/custom_hide_rules").toStringList();
    }

    QWriteLocker locker(&m_rulesLock);
    m_blockedDomains.unite(newAdDomains);
    m_malwareDomains.unite(newMalwareDomains);
    m_customHideRules = savedCustomRules;
}

void AdblockManager::checkAndDownloadRules() {
    // Фоновая асинхронная загрузка свежих баз[cite: 19]
    // Оба запроса, как и в interceptor.py: ads.txt и urlhaus (malware/phishing).
    //
    // Каждый reply подключается к СВОЕМУ собственному finished(), а не
    // к общему QNetworkAccessManager::finished(). Так каждый обработчик
    // гарантированно получает "свой" ответ независимо от порядка
    // завершения запросов — никакой путаницы между рекламным и
    // malware-списком быть не может.
    QNetworkReply* adReply = m_netManager->get(QNetworkRequest(QUrl("https://adaway.org/hosts.txt")));
    connect(adReply, &QNetworkReply::finished, this, [this, adReply]() {
        onAdRulesDownloaded(adReply);
        });

    QNetworkReply* malwareReply = m_netManager->get(QNetworkRequest(QUrl("https://urlhaus.abuse.ch/downloads/hostfile/")));
    connect(malwareReply, &QNetworkReply::finished, this, [this, malwareReply]() {
        onMalwareRulesDownloaded(malwareReply);
        });
}

void AdblockManager::saveRulesToFile(const QString& filename, const QByteArray& data) {
    // Была объявлена в .h, но нигде не вызывалась — скачанные правила жили
    // только в памяти текущей сессии и терялись при перезапуске: следующий
    // старт снова читал только то, что лежит в AppData/Rules/*.json (то есть
    // ничего нового, пока сеть опять не скачает базы). В interceptor.py
    // (SecurityUpdaterThread.run()) свежескачанные ads.json/malware.json
    // всегда пишутся на диск — приводим C++-часть к тому же поведению.
    QString rulesDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Rules";
    QDir().mkpath(rulesDir);
    QFile file(rulesDir + "/" + filename);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(data);
    }
}

QSet<QString> AdblockManager::parseHostsFile(const QByteArray& data) {
    QSet<QString> domains;
    QString text = QString::fromUtf8(data);
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;
        QStringList parts = trimmed.split(' ', Qt::SkipEmptyParts);
        if (parts.size() >= 2 && (parts[0] == "127.0.0.1" || parts[0] == "0.0.0.0")) {
            domains.insert(parts[1].toLower());
        }
    }
    return domains;
}

void AdblockManager::onAdRulesDownloaded(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) return;

    // Парсинг — вне лока (чистая функция над только что скачанными байтами,
    // не трогает member-состояние). Лок берём только на сам unite() и на
    // чтение размеров для сигнала, чтобы окно блокировки было минимальным.
    QSet<QString> parsed = parseHostsFile(reply->readAll());

    // Пишем на диск ДО захвата лока — I/O не должно держать writer-блокировку.
    // Сохраняем именно то, что реально пришло с сервера (parsed), а не
    // объединённый m_blockedDomains — захардкоженные core-правила и так
    // всегда заново добавляются в конструкторе при следующем запуске.
    {
        QJsonObject obj;
        QJsonArray arr;
        for (const QString& domain : parsed) arr.append(domain);
        obj["blocked_domains"] = arr;
        saveRulesToFile("ads.json", QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

    int adCount, malwareCount;
    {
        QWriteLocker locker(&m_rulesLock);
        m_blockedDomains.unite(parsed);
        adCount = m_blockedDomains.size();
        malwareCount = m_malwareDomains.size();
    }
    emit rulesUpdated(adCount, malwareCount);
}

void AdblockManager::onMalwareRulesDownloaded(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) return;

    QSet<QString> parsed = parseHostsFile(reply->readAll());

    {
        QJsonObject obj;
        QJsonArray arr;
        for (const QString& domain : parsed) arr.append(domain);
        obj["malware_domains"] = arr;
        saveRulesToFile("malware.json", QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

    int adCount, malwareCount;
    {
        QWriteLocker locker(&m_rulesLock);
        m_malwareDomains.unite(parsed);
        adCount = m_blockedDomains.size();
        malwareCount = m_malwareDomains.size();
    }
    emit rulesUpdated(adCount, malwareCount);
}

// ==========================================================================
// "Заблокировать элемент" — правила из контекстного меню (см.
// MainWindow::blockElementAt). Хранятся как "host##selector", привязаны к
// хосту, персистятся в QSettings и переприменяются на лету без перезапуска
// браузера через повторный вызов applyCosmeticAndStealthScripts().
// ==========================================================================

void AdblockManager::addCustomHideSelector(const QString& host, const QString& selector) {
    if (host.isEmpty() || selector.isEmpty()) return;
    const QString rule = host.toLower() + "##" + selector;

    {
        QWriteLocker locker(&m_rulesLock);
        if (m_customHideRules.contains(rule)) return;
        m_customHideRules.append(rule);
    }

    QSettings s;
    s.setValue("shield/custom_hide_rules", m_customHideRules);

    if (m_mainProfile) applyCosmeticAndStealthScripts(m_mainProfile);
    if (m_privateProfile) applyCosmeticAndStealthScripts(m_privateProfile);
}

void AdblockManager::removeCustomHideSelector(const QString& host, const QString& selector) {
    const QString rule = host.toLower() + "##" + selector;
    {
        QWriteLocker locker(&m_rulesLock);
        m_customHideRules.removeAll(rule);
    }

    QSettings s;
    s.setValue("shield/custom_hide_rules", m_customHideRules);

    if (m_mainProfile) applyCosmeticAndStealthScripts(m_mainProfile);
    if (m_privateProfile) applyCosmeticAndStealthScripts(m_privateProfile);
}

QStringList AdblockManager::customHideRules() const {
    QReadLocker locker(&m_rulesLock);
    return m_customHideRules;
}