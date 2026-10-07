#include "Logger.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QSysInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QtGlobal>
#include <QHash>
#include <QElapsedTimer>
#include <QList>

// Глобальная переменная пути к логу и мьютекс для потокобезопасности
static QString g_logFilePath;
static QMutex g_logMutex;

QString Logger::getLogDir() {
    // Получаем %LOCALAPPDATA% или стандартную папку приложения
    QString appData = qEnvironmentVariable("LOCALAPPDATA");
    if (appData.isEmpty()) {
        appData = QDir::homePath() + "/AppData/Local";
    }
    return appData + "/StormBrowser/Logs";
}

namespace {

    // Единственный признак "своего" сообщения - эмодзи-метка нашей функции
    // в начале строки. Проверяется независимо от QtMsgType (Warning или
    // Critical) - важно происхождение сообщения (это наш код: Talk Widget,
    // Password Capture, Arcade Widget), а не то, каким вызовом
    // (qWarning/qCritical) оно было отправлено.
    bool isOwnFeatureDiagnostic(const QString& msg) {
        return msg.contains(QString::fromUtf8(u8"📹")) ||  // Talk Widget
            msg.contains(QString::fromUtf8(u8"🔑")) ||  // Password Capture
            msg.contains(QString::fromUtf8(u8"🕹️"));   // Arcade Widget
    }

    // P2-4: сообщения подсистем браузера помечены тегом "[Имя подсистемы]".
    // Раньше в лог попадали ТОЛЬКО строки с эмодзи — и, например, все
    // предупреждения "[Storm Updater]" (включая настоящие причины, по
    // которым фоновое обновление не состоялось: 404, несовпавший хеш,
    // ошибки диска) молча выбрасывались, из-за чего "не обновляется в
    // фоне" было невозможно продиагностировать по логу. Теги
    // разработческой отладки ([DIAG]) в лог по-прежнему НЕ пишутся —
    // они слишком многословны.
    bool isSubsystemDiagnostic(const QString& msg) {
        static const QList<QString> tags = {
            QStringLiteral("[Storm Updater]"),
            QStringLiteral("[Storm Tabs]"),
            QStringLiteral("[Storm Shield"),
            QStringLiteral("[MainWindow]"),
            QStringLiteral("[SingleInstance]"),
            QStringLiteral("[PasswordManager]"),
            QStringLiteral("[CertificateManager]"),
            QStringLiteral("[SafeMode]"),
            QStringLiteral("[Torrent")
        };
        for (const QString& tag : tags) {
            if (msg.contains(tag, Qt::CaseInsensitive)) return true;
        }
        return false;
    }

    // Защита от спама: если один и тот же текст сообщения прилетает повторно
    // (например, баг в цикле долбит одной и той же ошибкой), не пишем его
    // в лог чаще, чем раз в 5 секунд. Первое вхождение всегда попадает в лог.
    // S-2: обработчик сообщений Qt вызывается из ЛЮБЫХ потоков (сетевые,
    // WebEngine, торрент) — статический QHash без мьютекса был гонкой с
    // риском повреждения кучи при многопоточном логировании.
    bool shouldThrottle(const QString& msg) {
        static QMutex s_throttleMutex;
        static QHash<QString, qint64> lastSeen;
        static QElapsedTimer timer;
        QMutexLocker lock(&s_throttleMutex);
        if (!timer.isValid()) {
            timer.start();
        }

        const qint64 now = timer.elapsed();
        const qint64 throttleWindowMs = 5000;

        auto it = lastSeen.find(msg);
        if (it != lastSeen.end() && (now - it.value()) < throttleWindowMs) {
            return true; // подавляем повтор
        }
        lastSeen[msg] = now;
        // Не даём таблице расти бесконечно на длинных сессиях с большим
        // количеством уникальных сообщений (утечка памяти в самом логгере).
        if (lastSeen.size() > 512) {
            lastSeen.clear();
        }
        return false;
    }

    // Общая запись строки в файл лога, с ротацией. Используется и
    // перехватчиком сообщений Qt, и Logger::logInfo() для намеренной
    // диагностики (баннер запуска и т.п.), которая не проходит через
    // фильтр "критическая ошибка своей функции".
    void writeLogLine(const QString& levelStr, const QString& msg) {
        QMutexLocker locker(&g_logMutex);

        QDir dir(Logger::getLogDir());
        if (!dir.exists()) {
            dir.mkpath(".");
        }

        // Ротация логов: если файл > 1 МБ, делаем бэкап browser.log.1
        QFile currentFile(g_logFilePath);
        if (currentFile.exists() && currentFile.size() > 1024 * 1024) {
            QString backupPath = g_logFilePath + ".1";
            if (QFile::exists(backupPath)) {
                QFile::remove(backupPath);
            }
            QFile::rename(g_logFilePath, backupPath);
        }

        if (currentFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&currentFile);
            out.setEncoding(QStringConverter::Utf8);

            QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            out << timestamp << " " << levelStr << " " << msg << "\n";
            currentFile.close();
        }
    }

} // namespace

// Кастомный обработчик: строгий allow-list, никакого "лишнего".
// В лог попадает ТОЛЬКО:
//   1. QtFatalMsg - настоящий краш: после этого колбэка Qt вызовет abort().
//                   Пишем ВСЕГДА, без каких-либо исключений.
//   2. Любое сообщение (Warning ИЛИ Critical), помеченное эмодзи нашей
//      функции (📹 Talk Widget, 🔑 Password Capture, 🕹️ Arcade Widget) -
//      это и есть "реальные критические ошибки от функций браузера".
//   3. Сообщения подсистем с тегом [Storm Updater]/[MainWindow]/... -
//      жизненно важная диагностика обновлений, вкладок и безопасности
//      (P2-4: раньше они выбрасывались, и сбои обновления были невидимы).
// Всё остальное - Debug/Info любого происхождения, немаркированный
// Warning/Critical от Chromium/Qt, JS-консоль сайтов, сетевые обрывы,
// шумовые сообщения движка и т.п. - отбрасывается целиком и безусловно.
void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    Q_UNUSED(context);

    const bool isFatal = (type == QtFatalMsg);
    const bool isOwn = isOwnFeatureDiagnostic(msg);
    const bool isSubsystem = isSubsystemDiagnostic(msg);

    if (!isFatal) {
        if (!isOwn && !isSubsystem) return;
        if (shouldThrottle(msg)) return;
    }

    QString levelStr;
    if (isFatal) {
        levelStr = "[FATAL]";
    }
    else if (type == QtCriticalMsg) {
        levelStr = "[CRITICAL]";
    }
    else {
        levelStr = "[WARNING]";
    }

    writeLogLine(levelStr, msg);
}

void Logger::init() {
    g_logFilePath = getLogDir() + "/browser.log";

    // Включаем наш перехватчик сообщений Qt
    qInstallMessageHandler(customMessageHandler);
}

void Logger::logInfo(const QString& msg) {
    writeLogLine("[INFO]", msg);
}

void Logger::logSystemInfo() {
    // Пишем стартовый блок напрямую, в обход фильтра сообщений Qt -
    // это не ошибка, а намеренная диагностика.
    logInfo("==================================================");
    logInfo(QString("Storm Browser Started at %1")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")));
    logInfo(QString("OS: %1 (%2)")
        .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()));
    logInfo(QString("Qt Version: %1").arg(qVersion()));
    logInfo("==================================================");
}
