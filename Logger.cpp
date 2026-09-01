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

    // Защита от спама: если один и тот же текст сообщения прилетает повторно
    // (например, баг в цикле долбит одной и той же ошибкой), не пишем его
    // в лог чаще, чем раз в 5 секунд. Первое вхождение всегда попадает в лог.
    bool shouldThrottle(const QString& msg) {
        static QHash<QString, qint64> lastSeen;
        static QElapsedTimer timer;
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
// Всё остальное - Debug/Info любого происхождения, немаркированный
// Warning/Critical от Chromium/Qt, JS-консоль сайтов, сетевые обрывы,
// шумовые сообщения движка и т.п. - отбрасывается целиком и безусловно.
// Списки "известного шума" и отдельная логика для сетевых ошибок больше
// не нужны: раз сообщение не крах и не помечено как своё - оно не пишется.
void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    Q_UNUSED(context);

    const bool isFatal = (type == QtFatalMsg);
    const bool isOwn = isOwnFeatureDiagnostic(msg);

    if (!isFatal) {
        if (!isOwn) return;
        if (shouldThrottle(msg)) return;
    }

    QString levelStr;
    if (isFatal) {
        levelStr = "[FATAL]";
    } else if (type == QtCriticalMsg) {
        levelStr = "[CRITICAL]";
    } else {
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
