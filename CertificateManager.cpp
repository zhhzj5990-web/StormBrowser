#include "CertificateManager.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QDebug>

// Пытаемся прочитать файл сертификата и как PEM, и как DER — оба формата
// встречаются "в дикой природе" под расширением .cer (Микрософт обычно
// раздаёт DER, но официальный архив Минцифры для Linux/macOS даёт PEM), так
// что сначала пробуем PEM, а если ничего не нашли — DER.
static QList<QSslCertificate> loadCertsFromPath(const QString& path) {
    QList<QSslCertificate> certs = QSslCertificate::fromPath(path, QSsl::Pem);
    if (certs.isEmpty()) {
        certs = QSslCertificate::fromPath(path, QSsl::Der);
    }
    return certs;
}

QList<QSslCertificate> CertificateManager::builtInCertificates() {
    static QList<QSslCertificate> cache;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        // Раньше грузили ровно два жёстко заданных имени файла
        // (russian_trusted_root_ca.cer / russian_trusted_sub_ca.cer) — но у
        // Минцифры больше одного действующего "sub CA" (например, отдельный
        // 2024 года, выпущенный при ротации ключа), и жёсткие имена не дают
        // просто добавить третий файл без правки кода. Теперь читаем ВСЕ
        // файлы, лежащие в ресурсах под префиксом :/certs/ — что туда
        // добавили через .qrc (сколько угодно файлов, любые имена/алиасы),
        // то и будет доверенным встроенным корнем/промежуточным CA.
        QDir dir(":/certs");
        const QStringList files = dir.entryList(QDir::Files);
        QSet<QByteArray> seenFingerprints; // на случай, если один и тот же сертификат случайно добавили и как .cer, и как .pem — не показываем дубликат дважды
        for (const QString& fileName : files) {
            for (const QSslCertificate& cert : loadCertsFromPath(dir.filePath(fileName))) {
                QByteArray fp = cert.digest(QCryptographicHash::Sha256);
                if (seenFingerprints.contains(fp)) continue;
                seenFingerprints.insert(fp);
                cache += cert;
            }
        }
        if (cache.isEmpty()) {
            qWarning() << "[CertificateManager] Встроенные сертификаты Минцифры не найдены "
                "в ресурсах (папка :/certs/ пуста или не скомпилирована в .qrc) — "
                "см. README_CERTIFICATES.md";
        }
    }
    return cache;
}

QString CertificateManager::customCertsDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/custom_certs";
    QDir().mkpath(dir);
    return dir;
}

QList<QSslCertificate> CertificateManager::customCertificates() {
    QList<QSslCertificate> result;
    QDir dir(customCertsDir());
    const QStringList files = dir.entryList(QStringList() << "*.cer", QDir::Files);
    for (const QString& fileName : files) {
        result += loadCertsFromPath(dir.filePath(fileName));
    }
    return result;
}

bool CertificateManager::useSystemStoreEnabled() {
    QSettings s;
    return s.value("shield-certs/use_system_store", true).toBool();
}

void CertificateManager::setUseSystemStoreEnabled(bool enabled) {
    QSettings s;
    s.setValue("shield-certs/use_system_store", enabled);
}

int CertificateManager::systemCertificateCount() {
    // QSslSocket::systemCaCertificates() существовал в Qt5 (и был там же
    // deprecated), но в Qt6 этот статический метод переехал в
    // QSslConfiguration — в QSslSocket его больше нет вовсе.
    return QSslConfiguration::systemCaCertificates().size();
}

QList<QSslCertificate> CertificateManager::allTrustedRoots() {
    QList<QSslCertificate> result = builtInCertificates() + customCertificates();
    if (useSystemStoreEnabled()) {
        result += QSslConfiguration::systemCaCertificates();
    }
    return result;
}

bool CertificateManager::chainTrustedByUs(const QList<QSslCertificate>& chain) {
    if (chain.isEmpty()) return false;
    const QList<QSslCertificate> trusted = allTrustedRoots();
    if (trusted.isEmpty()) return false;
    for (const QSslCertificate& c : chain) {
        // QSslCertificate::operator== сравнивает по содержимому DER — точное
        // байтовое совпадение, а не только по имени/отпечатку "на вид".
        if (trusted.contains(c)) return true;
    }
    return false;
}

CertificateManager::CertInfo CertificateManager::toCertInfo(const QSslCertificate& cert, Source source) {
    CertInfo info;
    info.id = QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());
    QStringList cn = cert.subjectInfo(QSslCertificate::CommonName);
    info.commonName = cn.isEmpty() ? QObject::tr("(без имени)") : cn.join(", ");
    QStringList org = cert.subjectInfo(QSslCertificate::Organization);
    info.organization = org.join(", ");
    QStringList issuerCn = cert.issuerInfo(QSslCertificate::CommonName);
    info.issuerCommonName = issuerCn.join(", ");
    info.validFrom = cert.effectiveDate().toString("dd.MM.yyyy");
    info.validTo = cert.expiryDate().toString("dd.MM.yyyy");
    info.expired = QDateTime::currentDateTime() > cert.expiryDate();
    info.source = source;
    return info;
}

QJsonArray CertificateManager::manageableCertificatesJson() {
    QJsonArray arr;
    auto append = [&arr](const QSslCertificate& cert, Source source) {
        CertInfo info = toCertInfo(cert, source);
        QJsonObject obj;
        obj["id"] = info.id;
        obj["commonName"] = info.commonName;
        obj["organization"] = info.organization;
        obj["issuerCommonName"] = info.issuerCommonName;
        obj["validFrom"] = info.validFrom;
        obj["validTo"] = info.validTo;
        obj["expired"] = info.expired;
        obj["source"] = (source == Source::BuiltIn) ? "built-in" : "custom";
        arr.append(obj);
        };
    for (const QSslCertificate& c : builtInCertificates()) append(c, Source::BuiltIn);
    for (const QSslCertificate& c : customCertificates()) append(c, Source::Custom);
    return arr;
}

QString CertificateManager::importCertificateFromFile(const QString& sourcePath) {
    if (sourcePath.isEmpty() || !QFile::exists(sourcePath)) {
        return QObject::tr("Файл не найден.");
    }

    QList<QSslCertificate> certs = loadCertsFromPath(sourcePath);
    if (certs.isEmpty()) {
        return QObject::tr("Не удалось распознать файл как сертификат "
            "(поддерживаются .cer/.crt/.pem в формате DER или PEM).");
    }

    QDir dir(customCertsDir());
    int imported = 0;
    for (const QSslCertificate& cert : certs) {
        QString fingerprint = QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());
        QString destPath = dir.filePath(fingerprint + ".cer");
        if (QFile::exists(destPath)) {
            continue; // уже импортирован ранее — не ошибка, просто пропускаем
        }
        QFile out(destPath);
        if (!out.open(QIODevice::WriteOnly)) {
            return QObject::tr("Не удалось сохранить сертификат в профиль браузера.");
        }
        out.write(cert.toDer());
        out.close();
        ++imported;
    }

    if (imported == 0 && certs.size() > 0) {
        // Все найденные сертификаты уже были импортированы раньше — не ошибка.
        return QString();
    }
    return QString();
}

bool CertificateManager::removeCustomCertificate(const QString& id) {
    // Простая защита от path traversal — id должен быть чистым hex-отпечатком.
    static const QRegularExpression hexOnly("^[0-9a-fA-F]+$");
    if (!hexOnly.match(id).hasMatch()) return false;

    QString path = QDir(customCertsDir()).filePath(id + ".cer");
    if (!QFile::exists(path)) return false;
    return QFile::remove(path);
}

QSslConfiguration CertificateManager::trustedSslConfiguration() {
    QSslConfiguration conf = QSslConfiguration::defaultConfiguration();
    // ДОБАВЛЯЕМ наши корни к уже имеющемуся стандартному набору (не
    // заменяем!) — так запрос по-прежнему проверяет сертификат нормально
    // (не QSslSocket::VerifyNone), просто список доверенных CA шире.
    conf.setCaCertificates(conf.caCertificates() + allTrustedRoots());
    return conf;
}