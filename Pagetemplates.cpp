#include "PageTemplates.h"
#include "MainWindow.h"
#include "SettingsPageHtml.h"
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QCoreApplication>
#include <QUrl>
#include <QList>
#include <QPair>
#include <QByteArray>
#include <QLocale>

// ==========================================================================
// PageTemplates.cpp — конструктор и небольшие шаблоны
// storm://settings (делегирует в SettingsPageHtml), storm://newtab,
// инкогнито-страница и HTML для Arcade-игр (обёртка вокруг .swf).
// Большие шаблоны (Storm Home, Storm Cloud, Storm Talk) вынесены в
// PageTemplates_Home.cpp / PageTemplates_Cloud*.cpp / PageTemplates_Talk*.cpp
// — см. их шапки. Публичный интерфейс PageTemplates (PageTemplates.h)
// не менялся.
// ==========================================================================

PageTemplates::PageTemplates(MainWindow* mainWin) : mw(mainWin) {}

QString PageTemplates::getSettingsHtml() {
    // Разметка storm://settings вынесена в SettingsPageHtml, чтобы не раздувать
    // этот файл дальше (см. SettingsPageHtml.h). Здесь остаётся только делегирование.
    return SettingsPageHtml::build(mw);
}

QString PageTemplates::getNewTabHtml() {
    QString bgStyle = "background: radial-gradient(circle at center, #1e2638 0%, #070a12 100%);";

    QSettings settings;
    // Вшито в ресурсы .qrc и компилируется внутрь exe — больше не зависит от папки, куда установлен браузер.
    QString defaultBgPath = ":/resources/bg.jpg";
    QString bgPath = settings.value("browser/background_image", defaultBgPath).toString();

    QFile bgFile(bgPath);
    if (bgFile.open(QIODevice::ReadOnly)) {
        QByteArray imageData = bgFile.readAll();
        QString base64Image = QString::fromLatin1(imageData.toBase64());
        QString dataUrl = "data:image/jpeg;base64," + base64Image;
        bgStyle = QString("background: url('%1') center center / cover no-repeat fixed;").arg(dataUrl);
        bgFile.close();
    }

    return QString(R"HTML(
    <!DOCTYPE html>
    <html lang="ru">
    <head>    
        <meta charset="UTF-8">
        <title>Новая вкладка</title>
        <style>
            body { 
                margin: 0; 
                padding: 0; 
                width: 100%; 
                height: 100vh; 
                overflow: hidden;
                %1 
            }
        </style>
    </head>
    <body>
    </body>
    </html>
    )HTML").arg(bgStyle);
}

QString PageTemplates::getIncognitoHtml() {
    return u8R"HTML(
    <!DOCTYPE html>
    <html lang="ru">
    <head>
        <meta charset="UTF-8">
        <title>Инкогнито</title>
        <style>
            body {
                margin: 0; padding: 0; width: 100%; height: 100vh;
                background-color: #0d1117;
                color: #eef3ff; font-family: 'Segoe UI', sans-serif;
                display: flex; flex-direction: column;
                align-items: center; justify-content: center;
            }
            .container { max-width: 600px; text-align: center; }
            .icon { font-size: 100px; margin-bottom: 5px; text-shadow: 0 0 30px rgba(177, 128, 255, 0.8); }
            h1 { font-size: 36px; font-weight: 600; margin-bottom: 20px; color: #b180ff; }
            p { font-size: 16px; color: #8b949e; line-height: 1.6; }
            .features {
                text-align: left; background: rgba(255,255,255,0.03);
                padding: 25px 40px; border-radius: 12px; margin-top: 35px;
                border: 1px solid rgba(177, 128, 255, 0.3);
                box-shadow: 0 10px 30px rgba(0,0,0,0.5);
            }
            .features p { color: #eef3ff; font-size: 18px; margin-top: 0; margin-bottom: 15px;}
            ul { padding-left: 20px; margin: 0; }
            li { margin-bottom: 12px; color: #8b949e; font-size: 15px; }
        </style>
    </head>
    <body>
        <div class="container">
            <div class="icon">🕶️</div>
            <h1>Режим Инкогнито</h1>
            <p>Вы перешли в приватный режим. Ваша история браузера, поисковые запросы и файлы cookie не будут сохранены.</p>
            <div class="features">
                <p><b>Storm Browser не сохранит:</b></p>
                <ul>
                    <li>Историю посещенных страниц</li>
                    <li>Файлы cookie и кэш сайтов</li>
                    <li>Данные, вводимые в формы (логины, пароли)</li>
                </ul>
            </div>
        </div>
    </body>
    </html>
    )HTML";
}

QString PageTemplates::getArcadeGameHtml(const QString& swfPath, const QString& gameName) {
    QFileInfo swfInfo(swfPath);
    if (!swfInfo.exists() || !swfInfo.isReadable()) {
        qWarning().noquote() << "🕹️ [Arcade Widget] ❌ Файл игры не найден или не читается:" << swfPath;
        return QString(u8"<html><body style='background:#070a12;color:#ff5f5f;display:flex;justify-content:center;align-items:center;height:100vh;font-family:sans-serif;'><h2>❌ Ошибка: Не удалось открыть файл игры на диске:<br>%1</h2></body></html>").arg(swfPath);
    }
    qWarning().noquote() << "🕹️ [Arcade Widget] Файл игры найден:" << swfPath << "(" << swfInfo.size() << "байт)";

    // ВАЖНО: раньше SWF читался в память и вшивался в HTML как base64 через
    // setHtml(). Но Qt/Chromium превращает содержимое setHtml() в data:-URL,
    // а такие URL жёстко ограничены 2 МБ (см. документацию Qt: "Content larger
    // than 2 MB cannot be displayed"). Любой SWF весом больше пары мегабайт
    // (наш тестовый — 18 МБ) приводил к тому, что страница вообще НЕ грузилась —
    // ни одна строка JS не выполнялась, отсюда и черный экран без единой ошибки.
    // Теперь мы отдаём Ruffle прямой file:// путь, и игра стримится с диска
    // напрямую, как и было задумано изначально — без раздувания в base64.
    QString swfUrl = QUrl::fromLocalFile(swfPath).toString();

    // Используем стабильный CDN jsDelivr с точным указанием папки.
    // Если ты захочешь вернуть свой удалённый сервер, просто замени обе строки ниже на:
    // "http://2.59.161.162:8045/" и "http://2.59.161.162:8045/ruffle.js"
    QString rufflePath = "https://cdn.jsdelivr.net/npm/@ruffle-rs/ruffle/";
    QString ruffleScript = rufflePath + "ruffle.js";

    return QString(u8R"HTML(
    <!DOCTYPE html>
    <html lang="ru">
    <head>
        <meta charset="UTF-8">
        <title>%1</title>
        <style>
            * { margin: 0; padding: 0; overflow: hidden; }
            html, body { 
                width: 100%; height: 100%; 
                background-color: #070a12; color: white;
                font-family: 'Segoe UI', sans-serif;
                display: flex; flex-direction: column; 
                justify-content: center; align-items: center; 
            }
            .header {
                position: absolute; top: 15px; left: 20px;
                font-size: 18px; font-weight: bold; color: #56d39b;
                z-index: 1000; text-shadow: 0 2px 10px rgba(0,0,0,0.8);
            }
            #game-container { 
                width: 95vw; height: 90vh; 
                display: flex; justify-content: center; align-items: center;
            }
            #status {
                position: absolute; color: #ffc857; font-size: 18px; font-weight: bold; z-index: 500;
                background: rgba(10, 14, 23, 0.9); padding: 15px 25px; border-radius: 10px;
                border: 1px solid rgba(255, 200, 87, 0.3); text-align: center;
            }
            ruffle-player { width: 100% !important; height: 100% !important; }
        </style>

        <script>
            window.RufflePlayer = window.RufflePlayer || {};
            window.RufflePlayer.config = {
                "autoplay": "on",
                "unmuteOverlay": "hidden",
                "publicPath": "%3",
                "logLevel": "warn",
                // ВАЖНО: QtWebEngine (встроенный Chromium) часто работает без полноценного
                // GPU-ускорения (WebGPU/WebGL2), и Ruffle в этом случае не всегда корректно
                // откатывается на запасной рендерер — вместо ошибки получаем черный экран.
                // "canvas" — самый медленный, но самый совместимый бэкенд, который точно
                // работает без аппаратного ускорения.
                "preferredRenderer": "canvas"
            };
        </script>
    </head>
    <body>
        <div class="header">🕹️ Storm Arcade: %1</div>
        <div id="status">⏳ Загрузка игрового движка Ruffle...</div>
        <div id="game-container"></div>

        <script src="%4" onerror="console.error('[ARCADE] Не удалось загрузить ruffle.js с CDN (сеть/CORS/блокировка): %4');"></script>

        <script>
            // Единая точка логирования: ВСЁ, что происходит с виджетом аркады,
            // проходит через console.error/console.log с тегом [ARCADE], чтобы
            // гарантированно долететь до файлового лога через
            // StormWebPage::javaScriptConsoleMessage (см. StormWebPage.cpp).
            // Раньше ошибки только меняли текст на странице и терялись, если
            // человек не успевал его увидеть или экран был просто черным.
            console.log("[ARCADE] Страница игры загружена (file://), публичный путь Ruffle: %3");

            // Если движок рендеринга (WebGL/WebGPU/wasm) упадёт уже ПОСЛЕ успешной
            // загрузки SWF, статус-текст обычно уже скрыт (мы прячем его в .then()),
            // и человек видит просто черный экран без объяснения причины.
            // Ловим такие ошибки глобально и возвращаем видимое сообщение + лог.
            window.addEventListener("error", (e) => {
                const msg = "[ARCADE] Необработанная ошибка рендеринга: " + (e.message || e) +
                    " @ " + (e.filename || "?") + ":" + (e.lineno || "?");
                console.error(msg);
                const statusEl = document.getElementById("status");
                if (statusEl) {
                    statusEl.style.display = "block";
                    statusEl.style.color = "#ff5f5f";
                    statusEl.innerText = "❌ Ошибка рендеринга: " + (e.message || e);
                }
            });

            window.addEventListener("DOMContentLoaded", () => {
                const statusEl = document.getElementById("status");
                let initAttempts = 0;

                let initInterval = setInterval(() => {
                    initAttempts++;
                    if (window.RufflePlayer && typeof window.RufflePlayer.newest === "function") {
                        clearInterval(initInterval);
                        console.log("[ARCADE] Скрипт ruffle.js загружен через " + (initAttempts * 100) + " мс, создаём плеер...");
                        statusEl.innerText = "⏳ Инициализация Flash-игры...";

                        try {
                            const ruffle = window.RufflePlayer.newest();
                            const player = ruffle.createPlayer();
                            document.getElementById("game-container").appendChild(player);

                            const swfUrl = "%2";
                            console.log("[ARCADE] Плеер создан. Загружаем SWF напрямую с диска: " + swfUrl);

                            // Отдаём Ruffle прямую file:// ссылку — он сам сделает fetch()
                            // и отрисует игру, без раздувания в base64/data-URL.
                            player.load(swfUrl).then(() => {
                                console.log("[ARCADE] player.load() успешно разрешился, SWF запущен.");
                                statusEl.style.display = "none";
                            }).catch((err) => {
                                console.error("[ARCADE] Ошибка в player.load(): " + err);
                                statusEl.style.color = "#ff5f5f";
                                statusEl.style.borderColor = "#ff5f5f";
                                statusEl.innerText = "❌ Ошибка запуска SWF: " + err;
                            });
                        } catch (e) {
                            console.error("[ARCADE] Исключение при создании плеера: " + e.message);
                            statusEl.style.color = "#ff5f5f";
                            statusEl.innerText = "❌ Ошибка создания плеера: " + e.message;
                        }
                    } else if (initAttempts > 150) {
                        clearInterval(initInterval);
                        console.error("[ARCADE] Таймаут: window.RufflePlayer.newest так и не появился за 15 секунд. " +
                            "Скорее всего, не загрузился ruffle.js с " + "%4" + " (сеть/CDN/блокировка).");
                        statusEl.style.color = "#ff5f5f";
                        statusEl.innerText = "❌ Ошибка: Не удалось загрузить скрипт Ruffle по сети.";
                    }
                }, 100);
            });
        </script>
    </body>
    </html>
    )HTML").arg(gameName, swfUrl, rufflePath, ruffleScript);
}
