#pragma once
#include <QList>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QString>
#include <QJsonArray>

// Единая точка правды о доверенных корневых сертификатах TLS для Storm
// Browser. Используется в ТРЁХ местах:
//   1. Проверка цепочки сертификата при ошибке TLS в QtWebEngine (см.
//      интеграцию в местe, где создаются вкладки/QWebEnginePage —
//      сравнение сертификатов цепочки с allTrustedRoots()).
//   2. Раздел "Сертификаты" на странице storm://settings (список +
//      управление) через новые методы SettingsBridge.
//   3. Прямые сетевые запросы к серверам GigaChat/Сбера через
//      QNetworkAccessManager (SettingsBridge::testAiConnection,
//      AIAssistantWidget, HomeAIBridge, WebPageAgent) — это СОВСЕМ ДРУГОЙ
//      TLS-стек, чем QtWebEngine (см. trustedSslConfiguration() ниже), и
//      требует отдельного, явного подключения в каждом месте, где сейчас
//      стоит QSslSocket::VerifyNone — доверие из (1) на них не
//      распространяется автоматически.
//
// Три источника доверия, объединяются в allTrustedRoots():
//
//   • BuiltIn — сертификаты НУЦ (Национального удостоверяющего центра)
//     Минцифры России, "зашитые" в ресурсы приложения. Нужны потому, что
//     многие российские гос./банковские сайты (Госуслуги, Сбербанк и т.д.)
//     выпускают TLS-сертификаты через российский НУЦ вместо привычных
//     западных CA — без доверия этому корню такие сайты в браузере
//     показывают ERR_CERT_AUTHORITY_INVALID, даже если сам сертификат сайта
//     в остальном корректен. Пользователь не может их удалить — только
//     видит в списке как "встроенные".
//     ВАЖНО: сами файлы сертификатов НЕ входят в этот коммит — их нужно
//     скачать с официального портала Госуслуг и добавить в ресурсы проекта.
//     См. README_CERTIFICATES.md.
//
//   • System — сертификаты из системного хранилища Windows
//     (QSslSocket::systemCaCertificates()). Управляется тумблером
//     "Использовать сертификаты Windows" на странице настроек (по умолчанию
//     включено — то же поведение, что и у большинства Chromium-браузеров).
//
//   • Custom — сертификаты, которые пользователь установил вручную через
//     кнопку "Установить сертификат…" в настройках. Хранятся как отдельные
//     DER-файлы в AppDataLocation/custom_certs/<sha256-отпечаток>.cer —
//     сам список это просто содержимое папки, без дублирования в QSettings.
class CertificateManager {
public:
    enum class Source { BuiltIn, System, Custom };

    struct CertInfo {
        QString id;               // SHA-256 отпечаток (hex) — стабильный id для UI и удаления
        QString commonName;
        QString organization;
        QString issuerCommonName;
        QString validFrom;        // dd.MM.yyyy
        QString validTo;          // dd.MM.yyyy
        bool    expired = false;
        Source  source;
    };

    // Встроенные сертификаты Минцифры (root + все sub CA, сколько бы их ни
    // было), читаются один раз из ВСЕХ файлов, лежащих в ресурсах Qt под
    // префиксом :/certs/ (что добавлено в .qrc — то и загружено, имена
    // файлов роли не играют), и кэшируются в статике.
    static QList<QSslCertificate> builtInCertificates();

    // Сертификаты, установленные пользователем вручную через настройки.
    // Читаются заново при каждом вызове (папка обычно маленькая — единицы
    // файлов), кэш не нужен.
    static QList<QSslCertificate> customCertificates();

    // Полный список сертификатов, которым Storm Browser доверяет "сверх"
    // обычной цепочки Chromium — BuiltIn + Custom + (System, если включено
    // в настройках). Именно этот список сравнивается с цепочкой сертификата
    // при ошибке TLS.
    static QList<QSslCertificate> allTrustedRoots();

    // true, если хотя бы один сертификат из chain (обычно вся цепочка,
    // присланная Chromium при certificateError) совпадает побайтово
    // (QSslCertificate::operator==, сравнение по DER) с одним из
    // allTrustedRoots().
    static bool chainTrustedByUs(const QList<QSslCertificate>& chain);

    // Метаданные встроенных + пользовательских сертификатов в виде
    // JSON-массива — для отображения на странице настроек. Системные
    // (Windows) сертификаты сюда намеренно не попадают поштучно (их могут
    // быть десятки) — для них отдельно systemCertificateCount() и кнопка
    // "Открыть хранилище сертификатов Windows".
    static QJsonArray manageableCertificatesJson();

    // Сколько сертификатов сейчас лежит в системном хранилище Windows —
    // не зависит от тумблера "использовать" (просто информационная цифра).
    static int systemCertificateCount();

    // Импорт нового сертификата из файла (.cer/.crt/.pem — DER или PEM,
    // формат определяется автоматически). Если в файле несколько
    // сертификатов (например, экспортированная цепочка), импортируются все.
    // Возвращает пустую строку при успехе, иначе текст ошибки для показа
    // пользователю.
    static QString importCertificateFromFile(const QString& sourcePath);

    // Удаляет пользовательский сертификат по id (отпечатку). На встроенные
    // и системные сертификаты не действует — вернёт false.
    static bool removeCustomCertificate(const QString& id);

    // Тумблер "использовать сертификаты Windows" — QSettings
    // "shield-certs/use_system_store" (по умолчанию true).
    static bool useSystemStoreEnabled();
    static void setUseSystemStoreEnabled(bool enabled);

    // --- Для QNetworkAccessManager-запросов (GigaChat/Сбер), НЕ для QtWebEngine ---
    // QtWebEngine (вкладки браузера) и QNetworkAccessManager (прямые API-
    // запросы вроде обмена ключа GigaChat на access_token или отправки
    // сообщения в чат) — два независимых TLS-стека в Qt. Доверие,
    // настроенное для вкладок (allTrustedRoots()/chainTrustedByUs()), НИКАК
    // не влияет на QNetworkAccessManager — каждый вызов последнего должен
    // явно применить эту конфигурацию через req.setSslConfiguration(...).
    //
    // В отличие от вкладок браузера (где мы просто добавляем МОЖНО ли
    // доверять — прошлые сертификаты цепочки уже проверены Chromium
    // криптографически), здесь мы формируем ПОЛНУЮ конфигурацию проверки:
    // берём стандартный набор доверенных CA (defaultConfiguration —
    // обычно системные + встроенные в Qt/OpenSSL корни) и ДОБАВЛЯЕМ к нему
    // allTrustedRoots(), вместо того чтобы просто отключать проверку
    // (QSslSocket::VerifyNone), как это было временно сделано раньше —
    // тем самым запрос по-прежнему отклонит любой сертификат, который не
    // подтверждён ни обычным CA, ни нашими доверенными корнями, вместо
    // того чтобы принимать вообще любой (потенциально поддельный/MITM)
    // сертификат.
    static QSslConfiguration trustedSslConfiguration();

private:
    static QString customCertsDir();
    static CertInfo toCertInfo(const QSslCertificate& cert, Source source);
};