#include <QApplication>
#include <QSettings>
#include <QIcon>
#include <QWebEngineUrlScheme>
#include "MainWindow.h"
#include "Logger.h"
#include "SingleInstanceGuard.h"
#include <QWebEngineProfile>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QProcess>
#include "UpdateManager.h"

int main(int argc, char* argv[])
{
    // =================================================================
    // 1. Инициализируем логирование САМЫМ ПЕРВЫМ ДЕЛОМ!
    // =================================================================
    Logger::init();
    Logger::logSystemInfo();

    // 2. Устанавливаем названия ДО создания приложения, 
    // чтобы QSettings сразу понимал, откуда брать настройки
    QCoreApplication::setApplicationName("StormBrowser");
    QCoreApplication::setOrganizationName("Shtorm Software");

    // 3. Проверяем настройку аппаратного ускорения
    QSettings settings;
    bool useHwAccel = settings.value("browser/hw_accel", true).toBool();

    // =================================================================
    // 3.1. SAFE-MODE / crash-guard.
    // Проблема: hw_accel лежит в QSettings, а выключить его пользователь
    // может только из Настроек ВНУТРИ уже открытого браузера. На слабых/
    // старых ПК (битые или отсутствующие GPU-драйверы, VM без проброса GPU)
    // Chromium-процесс WebEngine может падать при инициализации аппаратного
    // ускорения ДО того, как окно вообще успевает показаться — пользователь
    // физически не может зайти в Настройки, чтобы отключить ускорение
    // самому. Получается замкнутый круг.
    //
    // Решение: маркер-файл создаётся в начале каждого запуска и удаляется
    // только при штатном завершении (см. aboutToQuit ниже). Если при старте
    // такой файл уже существует — значит прошлый процесс не дошёл до
    // штатного выхода (упал или завис и был снят), и в этот раз мы сами,
    // без участия пользователя, принудительно уходим в программный рендеринг.
    QString guardDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(guardDir);
    const QString guardPath = guardDir + "/running.lock";
    bool previousRunCrashed = QFile::exists(guardPath);
    if (previousRunCrashed) {
        useHwAccel = false;
        qWarning() << "[SafeMode] Предыдущий запуск не завершился штатно — "
            "принудительно отключаю аппаратное ускорение GPU на этот раз.";
    }
    // Сам маркер создаём НЕ здесь, а только после того, как убедимся, что
    // это действительно единственный/главный процесс (см. ниже, после
    // проверки SingleInstanceGuard). Если создать его прямо тут, то
    // "вторичный" запуск (который лишь будит уже открытое окно и сразу
    // выходит через return 0, минуя app.exec() и aboutToQuit) навсегда
    // оставит маркер на диске — и следующий обычный одиночный запуск
    // ошибочно решит, что прошлый раз было падение.

    // Безопасные базовые флаги десктопного движка (БЕЗ отключателей web-security!):
    QString baseFlags = "--allow-file-access-from-files --no-sandbox --disable-blink-features=AutomationControlled ";

    // Отключаем Origin-keyed Agent Clusters и Sec-CH-UA заголовки.
    // UserAgentClientHint ОЧЕНЬ ВАЖЕН при маскировке под Firefox: если мы
    // надели маску Firefox, мы категорически не должны отправлять Client
    // Hints от Chrome!
    QString originClusterFix = "OriginAgentClusterDefaultEnable,UserAgentClientHint";

    if (!useHwAccel) {
        // Полный программный путь рендеринга: --disable-gpu только выключает
        // GPU-процесс, но композитинг и растеризация могут всё равно
        // пытаться зацепиться за видеодрайвер — гасим это явно.
        // --use-angle=d3d11warp принудительно сажает ANGLE на встроенный в
        // Windows (начиная с Win8) программный рендерер D3D11 WARP — он не
        // требует вообще никакого видеодрайвера и работает даже на ПК с
        // полностью отсутствующим/сломанным GPU-драйвером.
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            (baseFlags +
                "--disable-gpu --disable-gpu-compositing --disable-gpu-rasterization "
                "--use-angle=d3d11warp --disable-features=" + originClusterFix).toUtf8());
    }
    else {
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            (baseFlags + "--disable-features=AV1Decoder," + originClusterFix).toUtf8());
    }

    // =================================================================
    // 4. Регистрация кастомного протокола storm:// (ОБЯЗАТЕЛЬНО ДО QApplication)
    // =================================================================
    QWebEngineUrlScheme stormScheme("storm");
    stormScheme.setFlags(QWebEngineUrlScheme::SecureScheme |
        QWebEngineUrlScheme::LocalScheme |
        QWebEngineUrlScheme::LocalAccessAllowed);
    QWebEngineUrlScheme::registerScheme(stormScheme);

    // =================================================================
    // 5. Обязательный атрибут для QtWebEngine Widgets — должен быть установлен
    // ДО создания QApplication. Без него Chromium не может нормально шарить
    // OpenGL-контекст с Qt, что на практике проявляется как рваное, "слайд-шоу"
    // воспроизведение видео вместо плавного.
    // =================================================================
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // 6. Создаем само приложение и запускаем движок
    QApplication app(argc, argv);

    // Штатное завершение (пользователь закрыл окно / вышел через трей) —
    // убираем маркер safe-mode, чтобы следующий обычный запуск снова
    // пробовал аппаратное ускорение, если оно включено в настройках. Если
    // же процесс упадёт/зависнет, до этого сигнала дело не дойдёт, и файл
    // останется — это и есть сигнал для следующего запуска.
    QObject::connect(&app, &QApplication::aboutToQuit, [guardPath]() {
        QFile::remove(guardPath);
        });

    // Устанавливаем иконку приложения на панели задач
    app.setWindowIcon(QIcon(":/logo.png"));

    // =================================================================
    // 6.1. Защита от повторного запуска. QLockFile в UpdateManager уже
    // защищает общий временный файл обновления в %TEMP%, но это не мешает
    // пользователю случайно запустить ВТОРОЙ полноценный процесс браузера —
    // два трея, два профиля WebEngine, двойной расход памяти и т.д. Ставим
    // проверку максимально рано (сразу после конструирования QApplication,
    // до создания MainWindow и загрузки WebEngine-профиля) — если это
    // повторный запуск, не тратим время на инициализацию тяжёлых частей,
    // а просто "будим" уже работающее окно и сразу выходим.
    // =================================================================
    SingleInstanceGuard instanceGuard("StormBrowser_SingleInstance_9f3a7c2e");
    if (!instanceGuard.isPrimaryInstance()) {
        return 0;
    }

    // Только теперь, когда точно известно, что это единственный/главный
    // процесс и мы дойдём до app.exec(), создаём маркер safe-mode.
    {
        QFile guardFile(guardPath);
        guardFile.open(QIODevice::WriteOnly);
    }

    // =================================================================
    const QString dictPath = QCoreApplication::applicationDirPath() + "/qtwebengine_dictionaries";
    qputenv("QTWEBENGINE_DICTIONARIES_PATH", dictPath.toUtf8());

    int exitCode = 0;
    {
        MainWindow window;
        window.showMaximized();

        // Второй запуск (после проверки выше) молча "стучится" в этот процесс
        // через QLocalSocket — по этому сигналу поднимаем уже открытое окно
        // наверх, вместо того чтобы просто игнорировать попытку.
        QObject::connect(&instanceGuard, &SingleInstanceGuard::anotherInstanceStarted, &window, [&window]() {
            if (window.isMinimized()) {
                window.showNormal();
            }
            window.raise();
            window.activateWindow();
            });

        exitCode = app.exec();
        // 'window' и все её дети (вкладки/QWebEngineView, WebEngine-профиль)
        // ещё существуют как C++-объекты вплоть до конца этого блока { } —
        // ниже это гарантирует, что они будут ПОЛНОСТЬЮ разрушены обычным
        // выходом из области видимости ДО того, как мы позволим отложенному
        // установщику обновления что-либо трогать на диске.
    }

    // =================================================================
    // Отложенный запуск установщика фонового автообновления (если он был).
    // См. UpdateManager::cleanShutdownAndRunInstaller() — раньше установщик
    // запускался, пока браузер (и его Chromium-подпроцессы) были ещё
    // частично живы, и сам жёстко "убивал" ещё не остановленный процесс
    // (taskkill /F + CloseApplications=force в storm_browser_setup.iss) —
    // это и вызывало крэш вместо тихого обновления. Теперь установщик
    // запускается только здесь, когда MainWindow (и все detach-окна) уже
    // гарантированно разрушены строкой выше — устанавливать поверх себя
    // больше нечего принудительно закрывать.
    // =================================================================
    QString pendingInstallerPath;
    QStringList pendingInstallerArgs;
    if (UpdateManager::takePendingInstaller(pendingInstallerPath, pendingInstallerArgs)) {
        QProcess::startDetached(pendingInstallerPath, pendingInstallerArgs);
    }

    return exitCode;
}