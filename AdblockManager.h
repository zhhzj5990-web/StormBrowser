#pragma once
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QWebEngineProfile>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QList>
#include <atomic>

class AdblockManager : public QObject {
    Q_OBJECT
public:
    static AdblockManager& instance();

    void init(QWebEngineProfile* profile, QWebEngineProfile* privateProfile);
    void checkAndDownloadRules();

    // Мгновенная проверка URL за O(1)
    bool isBlocked(const QString& host, const QString& urlStr, bool& outIsMalware);

    // Текущий общий счётчик блокировок за время жизни процесса.
    int totalBlockedCount() const;

    // Даёт приложению один раз при старте подставить сюда значение из
    // DatabaseManager::getBlockedThreatsCount(), чтобы счётчик не обнулялся
    // между запусками — паритет с security/total_blocked в QSettings
    // на стороне interceptor.py.
    void setInitialBlockedCount(int count);

    // Внедрение скриптов защиты
    void applyCosmeticAndStealthScripts(QWebEngineProfile* profile);

    // "Заблокировать элемент" из контекстного меню (см.
    // MainWindow::blockElementAt). Хранится как "host##selector" (формат
    // как в обычных adblock-фильтрах) — одно правило = один хост, чтобы
    // селектор с одного сайта не мог случайно задеть разметку другого.
    // Добавление/удаление сразу же переприменяет косметический скрипт на
    // уже запущенных профилях (см. m_mainProfile/m_privateProfile ниже),
    // без перезапуска браузера.
    void addCustomHideSelector(const QString& host, const QString& selector);
    void removeCustomHideSelector(const QString& host, const QString& selector);
    QStringList customHideRules() const;

signals:
    void rulesUpdated(int adCount, int malwareCount);

    // Паритет с security_threat_detected из interceptor.py — только для
    // malware/фишинга, не для обычной рекламы.
    void securityThreatDetected(const QString& url);

    // Паритет с blocked_count_changed из interceptor.py — на КАЖДУЮ
    // блокировку (рекламу и malware), с новым общим значением счётчика.
    // ВАЖНО ДЛЯ ИНТЕГРАЦИИ: там, где создаётся DatabaseManager (например,
    // в MainWindow), подключи:
    //   connect(&AdblockManager::instance(), &AdblockManager::blockedCountChanged,
    //           this, [dbManager](int total) { dbManager->setBlockedThreatsCount(total); });
    // иначе счётчик в БД по-прежнему будет жить отдельно от реальных блокировок.
    void blockedCountChanged(int totalBlocked);

private slots:
    void onAdRulesDownloaded(QNetworkReply* reply);
    void onMalwareRulesDownloaded(QNetworkReply* reply);

private:
    explicit AdblockManager(QObject* parent = nullptr);
    void loadLocalRules();
    void saveRulesToFile(const QString& filename, const QByteArray& data);
    static QSet<QString> parseHostsFile(const QByteArray& data);

    // uBlock-правило "||domain^" матчит домен И все его поддомены
    // (ad.doubleclick.net матчится правилом на doubleclick.net). Раньше
    // проверка была только domains.contains(host) — точное совпадение,
    // из-за чего реклама/malware с поддоменов списка проходила.
    static bool matchesDomainOrSubdomain(const QString& host, const QSet<QString>& domains);

    bool matchesKeywordRules(const QString& urlStr) const;

    // Общая точка инкремента счётчика + эмита сигналов при блокировке.
    void registerBlock(bool isMalware, const QString& urlStr);

    QSet<QString> m_blockedDomains;
    QStringList m_blockedKeywords;                    // обычные подстрочные правила
    QList<QRegularExpression> m_blockedKeywordRegexes; // wildcard-правила вида "vk.com/ads_*"
    QSet<QString> m_malwareDomains;
    QNetworkAccessManager* m_netManager;

    // Пользовательские правила "Заблокировать элемент", формат
    // "host##selector" — см. addCustomHideSelector()/customHideRules().
    // Персистятся в QSettings (ключ shield/custom_hide_rules), в память
    // подгружаются один раз в loadLocalRules().
    QStringList m_customHideRules;

    // Профили, куда нужно переприменить косметический скрипт при
    // добавлении/удалении пользовательского правила без перезапуска
    // браузера. Запоминаются в init() — тех же указателей, что туда
    // передаёт MainWindow::setupUi().
    QWebEngineProfile* m_mainProfile = nullptr;
    QWebEngineProfile* m_privateProfile = nullptr;

    // ЗАЩИЩАЕТ m_blockedDomains / m_blockedKeywords / m_malwareDomains /
    // m_customHideRules. isBlocked() вызывается на IO-потоке QtWebEngine,
    // а мутации сетов (loadLocalRules, onAdRulesDownloaded,
    // onMalwareRulesDownloaded, addCustomHideSelector/removeCustomHideSelector)
    // происходят на главном потоке — без блокировки это гонка потоков
    // на неатомарных QSet/QStringList. QReadWriteLock выбран вместо QMutex,
    // т.к. isBlocked() дергается на каждый сетевой запрос страницы и должен
    // оставаться дешёвым; читатели друг друга не блокируют.
    mutable QReadWriteLock m_rulesLock;

    std::atomic<int> m_totalBlocked{ 0 };
};