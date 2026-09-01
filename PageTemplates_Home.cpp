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
// PageTemplates_Home.cpp — главная страница storm://home
// Приветствие, статистика (заблокированные угрозы, трафик, время) и
// быстрые ссылки (избранное). Выделено из Pagetemplates.cpp без
// изменений в содержимом шаблона.
// ==========================================================================

QString PageTemplates::getHomePageHtml() {
    QSettings settings;

    bool isLoggedIn = settings.value("profile/is_logged_in", false).toBool();
    QString username = settings.value("profile/username", "").toString();

    QString greetingText;
    if (isLoggedIn && !username.isEmpty()) {
        greetingText = u8"Привет, " + username + u8"!";
    }
    else {
        greetingText = u8"Приветствую тебя, странник!";
    }

    qint64 threats = mw->getDatabaseManager().getBlockedThreatsCount();

    double savedMb = (threats * 50.0) / 1024.0;
    qint64 savedSec = static_cast<qint64>(threats * 1.5);

    QString blockedThreats = QString::number(threats);

    QString savedTraffic;
    if (savedMb < 1.0) {
        savedTraffic = QString::number(savedMb * 1024.0, 'f', 0) + u8" КБ";
    }
    else if (savedMb < 1000.0) {
        savedTraffic = QString::number(savedMb, 'f', 1) + u8" МБ";
    }
    else {
        savedTraffic = QString::number(savedMb / 1024.0, 'f', 2) + u8" ГБ";
    }

    QString savedTime;
    if (savedSec < 60) {
        savedTime = QString::number(savedSec) + u8" сек";
    }
    else if (savedSec < 3600) {
        savedTime = QString::number(savedSec / 60) + u8" мин";
    }
    else {
        savedTime = QString::number(savedSec / 3600) + u8" ч " + QString::number((savedSec % 3600) / 60) + u8" мин";
    }

    QString bgStyle = "background: radial-gradient(circle at center, #1e2638 0%, #070a12 100%);";
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

    QList<QPair<QString, QString>> favorites = mw->getDatabaseManager().getFavorites();
    QString quickLinksHtml;

    if (favorites.isEmpty()) {
        quickLinksHtml =
            "<a href='https://ya.ru' class='fav-link'>Яндекс</a>"
            "<a href='https://youtube.com' class='fav-link'>YouTube</a>";
    }
    else {
        for (const auto& fav : favorites) {
            QString title = fav.first.isEmpty() ? fav.second : fav.first;
            if (title.length() > 15) title = title.left(15) + "...";
            quickLinksHtml += QString("<a href='%1' class='fav-link'>%2</a>").arg(fav.second, title);
        }
    }

    QString qwebchannelJs = "";
    QFile qwcFile(":/qtwebchannel/qwebchannel.js");
    if (qwcFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qwebchannelJs = QTextStream(&qwcFile).readAll();
        qwcFile.close();
    }

    QString html = R"HTML(
    <!DOCTYPE html>
    <html lang="ru">
    <head>    
        <meta charset="UTF-8">
        <script>
            %QWEBCHANNEL_JS%
        </script>
        <style>
            * { box-sizing: border-box; }
            html, body { margin: 0; padding: 0; }
            body { 
                min-height: 100vh; font-family: 'Segoe UI', sans-serif; color: white; 
                %BG_STYLE% 
                display: flex; flex-direction: column; align-items: center;
                padding: 30px 0 50px;
                overflow-x: hidden;
            }
            .top-zone { text-align: center; margin-bottom: 26px; }
            #time { font-size: 85px; font-weight: bold; margin: 0; text-shadow: 0 4px 15px rgba(0,0,0,0.6); letter-spacing: 2px; }
            #greeting { font-size: 22px; color: #56d39b; margin-top: -15px; text-shadow: 0 2px 8px rgba(0,0,0,0.8); }

            /* ==========================================================
               Кнопка "История" (слева) + выдвижная панель
               ========================================================== */
            .history-tab {
                position: fixed; top: 24px; left: 24px; z-index: 500;
                background: rgba(20, 25, 30, 0.75); color: #eef3ff;
                border: 1px solid rgba(163, 113, 247, 0.5);
                padding: 10px 16px; border-radius: 14px; cursor: pointer;
                font-size: 13.5px; font-weight: bold; backdrop-filter: blur(10px);
                box-shadow: 0 6px 20px rgba(0,0,0,0.4); transition: 0.2s;
            }
            .history-tab:hover { background: rgba(163, 113, 247, 0.35); border-color: #a371f7; transform: translateY(-2px); }

            .drawer-overlay {
                display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.5); z-index: 900;
            }
            .drawer-overlay.open { display: block; }

            .history-drawer {
                position: fixed; top: 0; left: 0; height: 100vh; width: 320px; max-width: 85vw;
                background: rgba(14, 17, 24, 0.97); border-right: 1px solid rgba(163, 113, 247, 0.4);
                backdrop-filter: blur(18px); z-index: 950; transform: translateX(-100%);
                transition: transform 0.3s ease; display: flex; flex-direction: column;
                box-shadow: 10px 0 40px rgba(0,0,0,0.6);
            }
            .history-drawer.open { transform: translateX(0); }
            .history-header {
                display: flex; align-items: center; justify-content: space-between;
                padding: 18px 18px 14px; border-bottom: 1px solid rgba(255,255,255,0.1); flex-shrink: 0;
            }
            .history-header span:first-child { font-weight: bold; color: #a371f7; font-size: 16px; }
            .history-close { cursor: pointer; color: #ff5f5f; font-weight: bold; font-size: 18px; }
            .new-chat-btn {
                margin: 14px 18px; padding: 10px; border-radius: 10px;
                border: 1px dashed rgba(86, 211, 155, 0.5); background: rgba(86, 211, 155, 0.1);
                color: #56d39b; cursor: pointer; font-size: 14px; font-weight: bold; text-align: center;
                flex-shrink: 0; transition: 0.2s;
            }
            .new-chat-btn:hover { background: rgba(86, 211, 155, 0.2); }
            .history-list { flex: 1; overflow-y: auto; padding: 0 12px 18px; }
            .history-item {
                position: relative; display: flex; align-items: center; justify-content: space-between;
                gap: 6px; padding: 10px 8px 10px 12px; border-radius: 10px; margin-bottom: 6px;
                background: rgba(255,255,255,0.04); transition: 0.2s; font-size: 13.5px;
            }
            .history-item:hover { background: rgba(163, 113, 247, 0.15); }
            .history-item.active { background: rgba(163, 113, 247, 0.25); border: 1px solid rgba(163, 113, 247, 0.5); }
            .history-item-title { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; flex: 1; cursor: pointer; }
            .history-item-dots {
                background: none; border: none; color: rgba(255,255,255,0.6); font-size: 18px;
                cursor: pointer; padding: 0 6px; flex-shrink: 0; line-height: 1;
            }
            .history-item-dots:hover { color: #fff; }
            .history-item-menu {
                position: absolute; right: 8px; top: 40px; background: #181c26;
                border: 1px solid rgba(255,255,255,0.15); border-radius: 10px;
                box-shadow: 0 10px 25px rgba(0,0,0,0.5); z-index: 20; overflow: hidden;
                min-width: 190px; display: none;
            }
            .history-item-menu.open { display: block; }
            .history-item-menu button {
                display: block; width: 100%; text-align: left; padding: 10px 14px;
                background: none; border: none; color: #eef3ff; font-size: 13px; cursor: pointer;
            }
            .history-item-menu button:hover { background: rgba(255,255,255,0.08); }
            .history-item-menu button.danger { color: #ff5f5f; }
            .history-empty { color: rgba(255,255,255,0.4); font-size: 13px; text-align: center; padding: 30px 10px; }

            /* ==========================================================
               Основная сетка: чат по центру, виджеты — справа
               ========================================================== */
            .page-body {
                width: 100%; max-width: 1480px; display: flex; gap: 30px;
                align-items: flex-start; justify-content: center; padding: 0 24px;
            }
            .center-column { flex: 1 1 620px; max-width: 660px; display: flex; flex-direction: column; align-items: center; }
            /* Правая зона теперь в два столбца — новости идут во второй (крайний правый) */
            .right-sidebar { flex: 0 0 660px; max-width: 660px; display: flex; gap: 20px; }
            .sidebar-col { flex: 1 1 0; min-width: 0; display: flex; flex-direction: column; gap: 20px; }

            @media (max-width: 1280px) {
                .page-body { flex-direction: column; align-items: center; }
                .center-column, .right-sidebar { max-width: 660px; width: 100%; }
                .right-sidebar { flex-direction: column; }
            }

            /* --- Чат --- */
            .ai-container { width: 100%; display: flex; flex-direction: column; align-items: center; }

            .ai-input-wrapper { 
                display: flex; width: 100%; background: rgba(10, 12, 18, 0.85); 
                border: 1px solid rgba(163, 113, 247, 0.5); border-radius: 30px; 
                padding: 5px 15px; backdrop-filter: blur(12px);
                box-shadow: 0 8px 25px rgba(0,0,0,0.5); transition: 0.3s;
            }
            .ai-input-wrapper:focus-within { border-color: #56d39b; box-shadow: 0 0 20px rgba(86, 211, 155, 0.3); }
            .ai-input { flex: 1; padding: 12px 10px; border: none; background: transparent; color: white; outline: none; font-size: 16px; }
            .ai-btn { background: transparent; border: none; color: #a371f7; font-size: 20px; cursor: pointer; transition: 0.2s; }
            .ai-btn:hover { color: #56d39b; transform: scale(1.1); }

            /* --- Панель маленьких кнопок под строкой ввода --- */
            .ai-mode-bar { display: flex; flex-wrap: wrap; gap: 8px; width: 100%; margin-top: 10px; }
            .mode-btn {
                background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.15);
                color: #eef3ff; padding: 6px 13px; border-radius: 14px; font-size: 12.5px;
                cursor: pointer; transition: 0.2s;
            }
            .mode-btn:hover { background: rgba(163, 113, 247, 0.2); }
            .mode-btn.active { background: rgba(163, 113, 247, 0.3); border-color: #a371f7; color: #fff; font-weight: bold; }
            .mode-btn.clear-btn { margin-left: auto; border-color: rgba(255, 95, 95, 0.4); color: #ff9a9a; }
            .mode-btn.clear-btn:hover { background: rgba(255, 95, 95, 0.2); }

            /* --- Лента сообщений --- */
            #chat-messages {
                display: none; width: 100%; margin-top: 14px; flex-direction: column; gap: 12px;
                max-height: 440px; overflow-y: auto; padding: 16px 10px 16px 16px;
                background: rgba(20, 25, 30, 0.95); border: 1px solid #a371f7; border-radius: 18px;
                backdrop-filter: blur(15px); box-shadow: 0 10px 30px rgba(0,0,0,0.6);
            }
            #chat-messages.visible { display: flex; }

            .msg-row { display: flex; width: 100%; }
            .msg-row.user { justify-content: flex-end; }
            .msg-row.ai { justify-content: flex-start; }
            .msg-bubble {
                position: relative; max-width: 84%; padding: 10px 34px 10px 14px;
                border-radius: 14px; font-size: 14.5px; line-height: 1.55; word-wrap: break-word;
            }
            .msg-row.user .msg-bubble {
                background: rgba(163, 113, 247, 0.22); border: 1px solid rgba(163, 113, 247, 0.5);
                border-bottom-right-radius: 4px;
            }
            .msg-row.ai .msg-bubble {
                background: rgba(86, 211, 155, 0.12); border: 1px solid rgba(86, 211, 155, 0.35);
                border-bottom-left-radius: 4px;
            }
            .msg-copy-btn {
                position: absolute; top: 6px; right: 6px; background: none; border: none;
                cursor: pointer; color: rgba(255,255,255,0.45); font-size: 12px;
                padding: 2px 4px; border-radius: 4px; transition: 0.15s;
            }
            .msg-copy-btn:hover { color: #fff; background: rgba(255,255,255,0.12); }
            .msg-copy-btn.copied { color: #56d39b; }
            .msg-text { white-space: normal; }

            /* --- Анимация ожидания (4 бегающие точки) --- */
            .typing-indicator {
                display: inline-flex; align-items: center; gap: 4px; padding: 4px 0;
            }
            .typing-indicator span {
                width: 7px; height: 7px; background-color: #56d39b; border-radius: 50%;
                animation: bounce 1.4s infinite ease-in-out both;
            }
            .typing-indicator span:nth-child(1) { animation-delay: -0.32s; }
            .typing-indicator span:nth-child(2) { animation-delay: -0.16s; }
            .typing-indicator span:nth-child(3) { animation-delay: 0s; }
            .typing-indicator span:nth-child(4) { animation-delay: 0.16s; background-color: #a371f7; }

            @keyframes bounce {
                0%, 80%, 100% { transform: scale(0); }
                40% { transform: scale(1); }
            }

            /* --- Виджеты (теперь колонкой справа) --- */
            .widget { 
                width: 100%; background: rgba(20, 25, 30, 0.7); border-radius: 20px; padding: 22px; 
                backdrop-filter: blur(15px); border: 1px solid rgba(255,255,255,0.15);
                box-shadow: 0 8px 25px rgba(0,0,0,0.4);
            }
            .widget-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; color: #a371f7; font-size: 16px; font-weight: bold; }
            .widget-action { cursor: pointer; font-size: 14px; opacity: 0.65; transition: 0.2s; }
            .widget-action:hover { opacity: 1; transform: scale(1.15); }
            .stat-row { display: flex; justify-content: space-between; padding: 5px 0; font-size: 14px; }
            .stat-val { color: #56d39b; font-weight: bold; }
            .quick-links { display: flex; gap: 10px; flex-wrap: wrap; }
            .fav-link { text-decoration: none; color: white; padding: 8px 15px; background: rgba(255,255,255,0.15); border-radius: 12px; font-size: 14px; transition: 0.2s; }
            .fav-link:hover { background: rgba(255,255,255,0.3); transform: translateY(-2px); }

            /* --- Дата + мотивационная цитата (теперь верхний виджет в правой колонке) --- */
            .date-quote-row { display: flex; flex-direction: column; gap: 12px; }
            .date-day { font-size: 20px; font-weight: bold; color: #a371f7; }
            .date-full { font-size: 13px; color: rgba(255,255,255,0.6); margin-top: 2px; }
            .quote-block { border-top: 2px solid rgba(163,113,247,0.4); padding-top: 12px; }
            .quote-text { font-size: 15px; font-style: italic; color: #eef3ff; line-height: 1.5; }

            /* --- Погода --- */
            .weather-loading, .weather-error, .news-loading, .news-error {
                font-size: 13px; color: rgba(255,255,255,0.5); padding: 10px 0;
            }
            .weather-main { display: flex; align-items: baseline; gap: 12px; }
            .weather-icon { font-size: 40px; line-height: 1; }
            .weather-temp { font-size: 32px; font-weight: bold; }
            .weather-city { font-size: 14px; color: rgba(255,255,255,0.75); margin-top: 4px; }
            .weather-details { display: flex; gap: 16px; margin-top: 10px; font-size: 13px; color: #56d39b; }

            /* --- Новости --- */
            .news-item {
                display: flex; align-items: flex-start; gap: 8px; padding: 8px 0;
                text-decoration: none; color: #eef3ff; font-size: 13.5px; line-height: 1.4;
                border-bottom: 1px solid rgba(255,255,255,0.06); transition: 0.2s;
            }
            .news-item:last-child { border-bottom: none; }
            .news-item:hover { color: #56d39b; padding-left: 4px; }
            .news-dot { color: #a371f7; font-weight: bold; flex-shrink: 0; }
            .news-source { color: rgba(255,255,255,0.35); font-size: 11px; margin-left: 4px; }

            /* --- Модальные окна (Быстрый доступ / RSS-ленты) --- */
            .modal-overlay { display:none; position:fixed; inset:0; background:rgba(0,0,0,0.6);
                             align-items:center; justify-content:center; z-index:1000; }
            .modal-overlay.open { display:flex; }
            .modal-box { background:#12161f; border:1px solid rgba(255,255,255,0.15); border-radius:16px;
                         padding:22px; width:420px; max-width:90vw; max-height:80vh; overflow-y:auto;
                         box-shadow: 0 20px 50px rgba(0,0,0,0.6); }
            .modal-title { font-size:16px; font-weight:bold; color:#a371f7; margin-bottom:14px; }
            .modal-close-btn { display:block; width:100%; padding:10px; margin-top:14px; background:rgba(255,255,255,0.06);
                               color:#eef3ff; border:1px solid rgba(255,255,255,0.15); border-radius:8px; cursor:pointer; }
            .modal-close-btn:hover { background:rgba(255,95,95,0.15); color:#ff5f5f; border-color:#ff5f5f; }

            /* --- Кастомное окно подтверждения (замена системного confirm() —
               у него белая полоса заголовка и он не стилизуется под тему браузера) --- */
            .confirm-modal-box { width:360px; text-align:center; }
            .confirm-modal-text { font-size:14px; color:#eef3ff; line-height:1.5; margin-bottom:18px; }
            .confirm-modal-actions { display:flex; gap:10px; }
            .confirm-modal-cancel-btn, .confirm-modal-ok-btn {
                flex:1; padding:10px; border-radius:8px; cursor:pointer; font-size:13.5px; font-weight:bold;
                border:1px solid rgba(255,255,255,0.15); background:rgba(255,255,255,0.06); color:#eef3ff; transition:0.2s;
            }
            .confirm-modal-cancel-btn:hover { background:rgba(255,255,255,0.12); }
            .confirm-modal-ok-btn.danger { background:rgba(255,95,95,0.15); border-color:rgba(255,95,95,0.4); color:#ff9a9a; }
            .confirm-modal-ok-btn.danger:hover { background:#ff5f5f; border-color:#ff5f5f; color:#fff; }

            .fav-item-row, .feed-item-row {
                display:flex; align-items:center; justify-content:space-between; gap:8px;
                padding:8px 10px; background:rgba(255,255,255,0.04); border-radius:8px; margin-bottom:6px; font-size:13px;
            }
            .fav-item-row span, .feed-item-row span { overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
            .remove-btn { background:none; border:none; color:#ff5f5f; cursor:pointer; font-size:15px; flex-shrink:0; }
            .remove-btn:hover { transform:scale(1.2); }

            .add-fav-row { display:flex; gap:6px; margin-top:10px; }
            .add-fav-row input {
                flex:1; padding:8px 10px; border-radius:6px; border:1px solid rgba(255,255,255,0.15);
                background:rgba(255,255,255,0.05); color:#eef3ff; font-size:13px; outline:none; min-width:0;
            }
            .add-fav-row input:focus { border-color:#a371f7; }
            .add-fav-row button {
                padding:8px 14px; border-radius:6px; border:none; background:#a371f7; color:#fff;
                font-weight:bold; cursor:pointer; font-size:13px; white-space:nowrap;
            }
            .add-fav-row button:hover { background:#b48cfb; }

            .limit-note { font-size:11px; color:rgba(255,255,255,0.4); margin-top:6px; }
            .feed-status { font-size:12px; margin-top:8px; min-height:16px; }
            .feed-status.ok { color:#56d39b; }
            .feed-status.err { color:#ff5f5f; }

            /* --- Картинка от ИИ: обёртка + кнопка "Скачать" --- */
            .ai-media-wrap { display:flex; flex-direction:column; }
            .ai-media-download-btn {
                display:inline-flex; align-items:center; gap:6px; margin-top:8px; align-self:flex-start;
                padding:7px 14px; border-radius:8px; background:rgba(163,113,247,0.15);
                border:1px solid rgba(163,113,247,0.4); color:#d9c8fc; text-decoration:none;
                font-size:12.5px; font-weight:bold; cursor:pointer; transition:0.2s;
            }
            .ai-media-download-btn:hover { background:#a371f7; color:#fff; border-color:#a371f7; }

            /* --- Промежуточный статус в пузыре "печатает" (например, загрузка картинки) --- */
            .typing-status-text { font-size:13px; color:rgba(255,255,255,0.65); font-style:italic; padding:2px 0; }
        </style>
    </head>
    <body>
        <button class="history-tab" onclick="toggleHistory()">📜 История</button>
        <div class="drawer-overlay" id="drawer-overlay" onclick="toggleHistory(false)"></div>
        <div class="history-drawer" id="history-drawer">
            <div class="history-header">
                <span>📜 История чатов</span>
                <span class="history-close" onclick="toggleHistory(false)">✕</span>
            </div>
            <div class="new-chat-btn" onclick="startNewChat()">+ Новый чат</div>
            <div class="history-list" id="history-list"></div>
        </div>

        <div class="top-zone">
            <div id="time">00:00</div>
            <div id="greeting">%GREETING%</div>
        </div>

        <div class="page-body">
            <div class="center-column">
                <div class="ai-container">
                    <div class="ai-input-wrapper">
                        <input type="text" id="ai-input" class="ai-input" placeholder="Спросить Storm AI о чем угодно...">
                        <button id="ai-send-btn" class="ai-btn">➤</button>
                    </div>

                    <div class="ai-mode-bar">
                        <button class="mode-btn active" data-mode="chat" onclick="setAIMode('chat')">💬 Обычный чат</button>
                        <button class="mode-btn" data-mode="image" onclick="setAIMode('image')">🖼️ Создать картинку</button>
                        <button class="mode-btn" style="margin-left: auto; border-color: rgba(86, 211, 155, 0.4); background: rgba(86, 211, 155, 0.15); color: #56d39b;" onclick="startNewChat()">✨ Новый чат</button>
                        <button class="mode-btn clear-btn" style="margin-left: 8px; background: rgba(255, 95, 95, 0.15);" onclick="clearCurrentChat()">🧹 Очистить</button>
                    </div>

                    <div id="chat-messages"></div>
                </div>
            </div>

            <div class="right-sidebar">
                <div class="sidebar-col">
                    <div class="widget" id="date-widget">
                        <div class="date-quote-row">
                            <div class="date-block">
                                <div class="date-day" id="date-day">—</div>
                                <div class="date-full" id="date-full">—</div>
                            </div>
                            <div class="quote-block">
                                <div class="quote-text" id="quote-text">—</div>
                            </div>
                        </div>
                    </div>

                    <div class="widget">
                        <div class="widget-header"><span>🛡️ Storm Shield</span></div>
                        <div class="stat-row"><span>Заблокировано угроз</span><span class="stat-val">%THREATS%</span></div>
                        <div class="stat-row"><span>Трафик</span><span class="stat-val">%TRAFFIC%</span></div>
                        <div class="stat-row"><span>Ускорение</span><span class="stat-val">%TIME%</span></div>
                    </div>

                    <div class="widget" id="weather-widget">
                        <div class="widget-header">
                            <span>🌤 Погода</span>
                            <span class="widget-action" onclick="changeWeatherCity()" title="Изменить город">📍</span>
                        </div>
                        <div id="weather-content"><div class="weather-loading">Загрузка погоды...</div></div>
                    </div>
                </div>

                <div class="sidebar-col">
                    <div class="widget">
                        <div class="widget-header">
                            <span>🚀 Быстрый доступ</span>
                            <span class="widget-action" onclick="openFavoritesModal()" title="Настроить">⚙️</span>
                        </div>
                        <div class="quick-links" id="quick-links-container">
                            %QUICK_LINKS%
                        </div>
                    </div>

                    <div class="widget" id="news-widget">
                        <div class="widget-header">
                            <span>📰 Новости</span>
                            <span class="widget-action" onclick="openFeedsModal()" title="Настроить ленты">⚙️</span>
                        </div>
                        <div id="news-content"><div class="news-loading">Загрузка новостей...</div></div>
                    </div>
                </div>
            </div>
        </div>

        <!-- ===== Модалка: Быстрый доступ ===== -->
        <div class="modal-overlay" id="favorites-modal">
            <div class="modal-box">
                <div class="modal-title">🚀 Быстрый доступ (до 6 ссылок)</div>
                <div id="favorites-list"></div>
                <div class="add-fav-row" id="fav-add-block">
                    <input type="text" id="fav-title-input" placeholder="Название" style="max-width:110px;">
                    <input type="text" id="fav-url-input" placeholder="example.com">
                    <button onclick="submitAddFavorite()">Добавить</button>
                </div>
                <div class="limit-note" id="fav-limit-note" style="display:none;">Достигнут лимит в 6 ссылок — удалите одну, чтобы добавить новую.</div>
                <button class="modal-close-btn" onclick="closeModal('favorites-modal')">Закрыть</button>
            </div>
        </div>

        <!-- ===== Модалка: RSS-ленты ===== -->
        <div class="modal-overlay" id="feeds-modal">
            <div class="modal-box">
                <div class="modal-title">📰 RSS-ленты (до 5)</div>
                <div id="feeds-list"></div>
                <div class="add-fav-row">
                    <input type="text" id="feed-url-input" placeholder="https://example.com/rss.xml">
                    <button onclick="submitAddFeed()">Добавить</button>
                </div>
                <div id="feed-status" class="feed-status"></div>
                <button class="modal-close-btn" onclick="closeModal('feeds-modal')">Закрыть</button>
            </div>
        </div>

        <!-- ===== Модалка: Кастомное подтверждение (замена системного confirm()) ===== -->
        <div class="modal-overlay" id="confirm-modal">
            <div class="modal-box confirm-modal-box">
                <div class="modal-title" id="confirm-modal-title">⚠️ Подтверждение</div>
                <div class="confirm-modal-text" id="confirm-modal-text"></div>
                <div class="confirm-modal-actions">
                    <button class="confirm-modal-cancel-btn" onclick="resolveConfirm(false)">Отмена</button>
                    <button class="confirm-modal-ok-btn danger" id="confirm-modal-ok-btn" onclick="resolveConfirm(true)">Подтвердить</button>
                </div>
            </div>
        </div>

        <script>
            function updateClock() {
                document.getElementById('time').innerText = new Date().toLocaleTimeString('ru-RU', {hour: '2-digit', minute:'2-digit'});
            }
            setInterval(updateClock, 1000); updateClock();

            function openModal(id) { document.getElementById(id).classList.add('open'); }
            function closeModal(id) { document.getElementById(id).classList.remove('open'); }

            // Кастомная замена системного confirm() — тот рисуется движком со
            // своей белой полосой заголовка и не подчиняется теме страницы.
            // Использование: if (!(await customConfirm('Текст?'))) return;
            let _confirmResolve = null;
            function customConfirm(message, opts) {
                opts = opts || {};
                document.getElementById('confirm-modal-title').textContent = opts.title || '⚠️ Подтверждение';
                document.getElementById('confirm-modal-text').textContent = message;
                document.getElementById('confirm-modal-ok-btn').textContent = opts.confirmText || 'Подтвердить';
                openModal('confirm-modal');
                return new Promise(function(resolve) {
                    _confirmResolve = resolve;
                });
            }
            function resolveConfirm(result) {
                closeModal('confirm-modal');
                if (_confirmResolve) {
                    const r = _confirmResolve;
                    _confirmResolve = null;
                    r(result);
                }
            }

            // ==========================================================
            // 🔧 Небольшие текстовые утилиты
            // ==========================================================
            function escapeHtml(s) {
                return String(s == null ? '' : s)
                    .replace(/&/g, '&amp;')
                    .replace(/</g, '&lt;')
                    .replace(/>/g, '&gt;')
                    .replace(/"/g, '&quot;')
                    .replace(/'/g, '&#39;');
            }
            function renderBubbleHtml(text) {
                return escapeHtml(text).replace(/\n/g, '<br>');
            }
            // Ответ от бэкенда уже приходит как HTML (экранированный текст + <br>).
            // Разворачиваем его обратно в обычный текст — для копирования и для
            // сохранения в localStorage в одном и том же "сыром" виде, что и
            // сообщения пользователя.
            function htmlReplyToPlainText(html) {
                const withNewlines = String(html).replace(/<br\s*\/?>/gi, '\n');
                const ta = document.createElement('textarea');
                ta.innerHTML = withNewlines;
                return ta.value;
            }

            // ==========================================================
            // 🤖 Storm AI — чат
            // ==========================================================
            let backendAI = null;
            let homeBridge = null;
            let currentMode = 'chat';
            let pendingTypingRow = null;
            let aiTimeoutId = null;
            let webChannelStarted = false;

            // ВАЖНО: один QWebChannel на один transport. Раньше здесь (и ниже,
            // для homeBridge) создавалось НЕСКОЛЬКО отдельных QWebChannel поверх
            // одного и того же qt.webChannelTransport — они перебивали друг
            // друга при обработке сообщений, из-за чего ответ ИИ мог вообще не
            // дойти до JS, и чат "вечно грузился". Теперь канал создаётся
            // ровно один раз и отдаёт оба объекта — homeAI и homeBridge.
            function initWebChannel() {
                if (webChannelStarted) return;
                if (typeof QWebChannel === 'undefined' || typeof qt === 'undefined' || !qt.webChannelTransport) {
                    console.warn('[StormHome] qt.webChannelTransport ещё не готов, жду...');
                    setTimeout(initWebChannel, 200);
                    return;
                }
                webChannelStarted = true;
                console.warn('[StormHome] qt.webChannelTransport найден, создаём QWebChannel...');

                new QWebChannel(qt.webChannelTransport, function(channel) {
                    backendAI = channel.objects.homeAI;
                    homeBridge = channel.objects.homeBridge;

                    if (backendAI) {
                        console.warn('[StormHome] homeAI подключён успешно');
                        backendAI.aiResponseReceived.connect(function(htmlReply) {
                            if (htmlReply.startsWith("NAVIGATE_CMD:")) {
                                window.location.href = htmlReply.substring(13).trim();
                                return;
                            }

                            // Промежуточный статус (например, "картинка сгенерирована,
                            // идёт загрузка файла") — НЕ финальный ответ. Не трогаем
                            // pendingTypingRow/таймаут, просто обновляем текст в уже
                            // открытом пузыре "печатает" и ждём настоящего ответа следом.
                            if (htmlReply.startsWith("STATUS:")) {
                                if (pendingTypingRow) {
                                    const bubble = pendingTypingRow.querySelector('.msg-bubble');
                                    if (bubble) {
                                        bubble.innerHTML = '<div class="typing-status-text">' + escapeHtml(htmlReply.substring(7)) + '</div>';
                                    }
                                }
                                return;
                            }

                            clearAiTimeout();
                            setSendingState(false);
                            
                            let isMedia = false;
                            let plainText = "";

                            // Проверяем флаг медиа, чтобы не испортить HTML тег
                            if (htmlReply.startsWith("MEDIA_HTML:")) {
                                isMedia = true;
                                plainText = htmlReply.substring(11); // отрезаем префикс
                            } else {
                                plainText = htmlReplyToPlainText(htmlReply);
                            }

                            if (pendingTypingRow) {
                                resolveTypingBubble(pendingTypingRow, plainText, isMedia);
                                pendingTypingRow = null;
                            } else {
                                appendBubble('ai', plainText, isMedia);
                            }
                            persistCurrentChat();
                        });
                    } else {
                        console.warn('[StormHome] ОШИБКА: channel.objects.homeAI не найден! Проверьте регистрацию homeChannel->registerObject("homeAI", ...) в MainWindow_Tabs.cpp');
                    }

                    if (homeBridge) {
                        console.warn('[StormHome] homeBridge подключён успешно');
                        wireHomeBridgeSignals();
                        homeBridge.getFavorites();
                        homeBridge.refreshNews();
                    } else {
                        console.warn('[StormHome] ОШИБКА: channel.objects.homeBridge не найден!');
                    }
                });
            }
            initWebChannel();

            function setSendingState(isSending) {
                const btn = document.getElementById('ai-send-btn');
                btn.style.opacity = isSending ? '0.4' : '1';
                btn.style.pointerEvents = isSending ? 'none' : 'auto';
            }
            function clearAiTimeout() {
                if (aiTimeoutId) { clearTimeout(aiTimeoutId); aiTimeoutId = null; }
            }

            // --- Режимы (чат / картинка / видео) ---
            function setAIMode(mode) {
                currentMode = mode;
                document.querySelectorAll('.mode-btn[data-mode]').forEach(function(b) {
                    b.classList.toggle('active', b.dataset.mode === mode);
                });
                const inp = document.getElementById('ai-input');
                if (mode === 'image') inp.placeholder = 'Опишите картинку, которую хотите создать...';
                else inp.placeholder = 'Спросить Storm AI о чем угодно...';
                inp.focus();
            }

            // --- Копирование сообщения ---
            function copyMsg(btn) {
                const bubble = btn.closest('.msg-bubble');
                const textEl = bubble.querySelector('.msg-text');
                const raw = textEl.getAttribute('data-raw') || textEl.innerText;

                const showCopied = function() {
                    const old = btn.textContent;
                    btn.textContent = '✓';
                    btn.classList.add('copied');
                    setTimeout(function() { btn.textContent = old; btn.classList.remove('copied'); }, 1200);
                };
                const showFailed = function() {
                    const old = btn.textContent;
                    btn.textContent = '⚠';
                    setTimeout(function() { btn.textContent = old; }, 1200);
                };
                // Запасной способ через execCommand — в QtWebEngine асинхронный
                // Clipboard API часто недоступен без специальных настроек
                // (JavascriptCanAccessClipboard/JavascriptCanPaste), а этот
                // старый способ обычно работает без них.
                const fallbackCopy = function() {
                    try {
                        const ta = document.createElement('textarea');
                        ta.value = raw;
                        ta.style.position = 'fixed';
                        ta.style.top = '-9999px';
                        ta.style.left = '-9999px';
                        document.body.appendChild(ta);
                        ta.focus();
                        ta.select();
                        const ok = document.execCommand('copy');
                        document.body.removeChild(ta);
                        if (ok) showCopied(); else showFailed();
                    } catch (e) {
                        showFailed();
                    }
                };

                if (navigator.clipboard && navigator.clipboard.writeText && window.isSecureContext !== false) {
                    navigator.clipboard.writeText(raw).then(showCopied).catch(fallbackCopy);
                } else {
                    fallbackCopy();
                }
            }

            // --- Рендер пузырьков сообщений ---
            // Добавили параметр isMedia
            function appendBubble(role, text, isMedia) {
                const box = document.getElementById('chat-messages');
                box.classList.add('visible');
                const row = document.createElement('div');
                row.className = 'msg-row ' + (role === 'user' ? 'user' : 'ai');
                
                let htmlContent = isMedia ? text : renderBubbleHtml(text);
                let rawAttr = isMedia ? '[Медиафайл]' : escapeHtml(text);
                
                row.innerHTML =
                    '<div class="msg-bubble">' +
                        '<button class="msg-copy-btn" onclick="copyMsg(this)" title="Копировать">📋</button>' +
                        '<div class="msg-text" data-raw="' + rawAttr + '">' + htmlContent + '</div>' +
                    '</div>';
                box.appendChild(row);
                box.scrollTop = box.scrollHeight;
                return row;
            }

            function appendTypingBubble() {
                const box = document.getElementById('chat-messages');
                box.classList.add('visible');
                const row = document.createElement('div');
                row.className = 'msg-row ai typing';
                row.innerHTML =
                    '<div class="msg-bubble">' +
                        '<div class="typing-indicator"><span></span><span></span><span></span><span></span></div>' +
                    '</div>';
                box.appendChild(row);
                box.scrollTop = box.scrollHeight;
                return row;
            }

            // Добавили параметр isMedia
            function resolveTypingBubble(row, text, isMedia) {
                row.classList.remove('typing');
                let htmlContent = isMedia ? text : renderBubbleHtml(text);
                let rawAttr = isMedia ? '[Медиафайл]' : escapeHtml(text);

                row.querySelector('.msg-bubble').innerHTML =
                    '<button class="msg-copy-btn" onclick="copyMsg(this)" title="Копировать">📋</button>' +
                    '<div class="msg-text" data-raw="' + rawAttr + '">' + htmlContent + '</div>';
                const box = document.getElementById('chat-messages');
                box.scrollTop = box.scrollHeight;
            }

            // --- Отправка сообщения ---
            function sendToAI() {
                const inp = document.getElementById('ai-input');
                const text = inp.value.trim();
                if (!text) return;
                if (pendingTypingRow) return; 

                appendBubble('user', text);
                persistCurrentChat();
                inp.value = '';

                const typingRow = appendTypingBubble();
                pendingTypingRow = typingRow;
                setSendingState(true);

                clearAiTimeout();
                // Для генерации картинки даем 45 секунд
                let timeoutTime = (currentMode === 'image') ? 45000 : 32000;
                
                aiTimeoutId = setTimeout(function() {
                    if (pendingTypingRow === typingRow) {
                        resolveTypingBubble(typingRow, '⚠️ Не удалось получить ответ — истекло время ожидания.');
                        pendingTypingRow = null;
                        setSendingState(false);
                        persistCurrentChat();
                    }
                }, timeoutTime);

                // Распределение запросов
                if (currentMode === 'image') {
                    askMediaWhenReady(text, currentMode);
                } else {
                    askAIWhenReady(text);
                }
            }

            function askMediaWhenReady(text, mode) {
                if (backendAI) {
                    if (backendAI.generateMedia) {
                        backendAI.generateMedia(text, mode);
                    } else {
                        resolveTypingBubble(pendingTypingRow, '⚠️ Обновите программу: модуль медиа не найден.');
                        pendingTypingRow = null;
                        setSendingState(false);
                    }
                } else {
                    setTimeout(function() { askMediaWhenReady(text, mode); }, 300);
                }
            }

            // Ждём готовности моста, НЕ создавая новых QWebChannel
            function askAIWhenReady(text) {
                if (backendAI) {
                    console.warn('[StormHome] Вызываем backendAI.askAI()');
                    backendAI.askAI(text);
                } else {
                    setTimeout(function() { askAIWhenReady(text); }, 300);
                }
            }

            document.getElementById('ai-send-btn').addEventListener('click', sendToAI);
            document.getElementById('ai-input').addEventListener('keypress', function(e) {
                if (e.key === 'Enter') sendToAI();
            });

            // ==========================================================
            // 📜 История чатов (хранится в localStorage на этой странице)
            // ==========================================================
            const CHATS_KEY = 'storm_ai_chats_v1';
            const ACTIVE_CHAT_KEY = 'storm_ai_active_chat_v1';
            const MAX_BACKEND_HISTORY = 10; // совпадает с лимитом в HomeAIBridge.cpp
            let activeChatId = null;

            function loadAllChats() {
                try {
                    const raw = JSON.parse(localStorage.getItem(CHATS_KEY));
                    return Array.isArray(raw) ? raw : [];
                } catch (e) { return []; }
            }
            function saveAllChats(chats) {
                localStorage.setItem(CHATS_KEY, JSON.stringify(chats));
            }
            function makeChatTitle(text) {
                text = (text || '').trim();
                if (!text) return 'Новый чат';
                return text.length > 34 ? text.slice(0, 34) + '…' : text;
            }
            function newChatId() {
                return 'c_' + Date.now() + '_' + Math.random().toString(36).slice(2, 7);
            }

            function collectMessagesFromDom() {
                const rows = document.querySelectorAll('#chat-messages .msg-row:not(.typing)');
                const arr = [];
                rows.forEach(function(r) {
                    const role = r.classList.contains('user') ? 'user' : 'ai';
                    const textEl = r.querySelector('.msg-text');
                    if (textEl) arr.push({ role: role, text: textEl.getAttribute('data-raw') || '' });
                });
                return arr;
            }

            function persistCurrentChat() {
                const messages = collectMessagesFromDom();
                if (!messages.length) return;
                const chats = loadAllChats();
                let chat = chats.find(function(c) { return c.id === activeChatId; });
                const firstUserMsg = messages.find(function(m) { return m.role === 'user'; });
                if (!chat) {
                    chat = { id: activeChatId, title: makeChatTitle(firstUserMsg ? firstUserMsg.text : ''), customTitle: false, messages: [], updatedAt: 0 };
                    chats.unshift(chat);
                }
                chat.messages = messages;
                chat.updatedAt = Date.now();
                if (!chat.customTitle && firstUserMsg) chat.title = makeChatTitle(firstUserMsg.text);
                saveAllChats(chats);
                renderHistoryList();
            }

            function renderHistoryList() {
                const list = document.getElementById('history-list');
                const chats = loadAllChats().slice().sort(function(a, b) { return b.updatedAt - a.updatedAt; });
                if (!chats.length) {
                    list.innerHTML = '<div class="history-empty">Пока нет сохранённых чатов</div>';
                    return;
                }
                list.innerHTML = chats.map(function(c) {
                    const activeCls = c.id === activeChatId ? ' active' : '';
                    return '<div class="history-item' + activeCls + '" data-id="' + c.id + '">' +
                        '<span class="history-item-title" onclick="openChat(\'' + c.id + '\')">' + escapeHtml(c.title) + '</span>' +
                        '<button class="history-item-dots" onclick="toggleItemMenu(event, \'' + c.id + '\')">⋮</button>' +
                        '<div class="history-item-menu" id="menu-' + c.id + '">' +
                            '<button onclick="openChat(\'' + c.id + '\')">↩️ Войти</button>' +
                            '<button onclick="renameChat(\'' + c.id + '\')">✏️ Редактировать название</button>' +
                            '<button class="danger" onclick="deleteChat(\'' + c.id + '\')">🗑️ Удалить чат</button>' +
                        '</div>' +
                    '</div>';
                }).join('');
            }

            function toggleItemMenu(evt, id) {
                evt.stopPropagation();
                const menu = document.getElementById('menu-' + id);
                const wasOpen = menu.classList.contains('open');
                document.querySelectorAll('.history-item-menu.open').forEach(function(m) { m.classList.remove('open'); });
                if (!wasOpen) menu.classList.add('open');
            }
            document.addEventListener('click', function() {
                document.querySelectorAll('.history-item-menu.open').forEach(function(m) { m.classList.remove('open'); });
            });

            function openChat(id) {
                const chat = loadAllChats().find(function(c) { return c.id === id; });
                if (!chat) return;
                activeChatId = id;
                localStorage.setItem(ACTIVE_CHAT_KEY, id);

                clearAiTimeout();
                pendingTypingRow = null;
                setSendingState(false);

                const box = document.getElementById('chat-messages');
                box.innerHTML = '';
                box.classList.remove('visible');
                chat.messages.forEach(function(m) { appendBubble(m.role, m.text); });

                if (backendAI && backendAI.restoreContext) {
                    const payload = chat.messages.slice(-MAX_BACKEND_HISTORY).map(function(m) {
                        return { role: m.role === 'user' ? 'user' : 'assistant', content: m.text };
                    });
                    backendAI.restoreContext(JSON.stringify(payload));
                }

                renderHistoryList();
                toggleHistory(false);
            }

            function renameChat(id) {
                const chats = loadAllChats();
                const chat = chats.find(function(c) { return c.id === id; });
                if (!chat) return;
                const newTitle = prompt('Новое название чата:', chat.title);
                if (newTitle && newTitle.trim()) {
                    chat.title = makeChatTitle(newTitle.trim());
                    chat.customTitle = true;
                    saveAllChats(chats);
                    renderHistoryList();
                }
            }

            async function deleteChat(id) {
                const ok = await customConfirm('Удалить этот чат безвозвратно?', { confirmText: 'Удалить' });
                if (!ok) return;
                const chats = loadAllChats().filter(function(c) { return c.id !== id; });
                saveAllChats(chats);
                if (id === activeChatId) {
                    startNewChat();
                } else {
                    renderHistoryList();
                }
            }

            function startNewChat() {
                activeChatId = newChatId();
                localStorage.setItem(ACTIVE_CHAT_KEY, activeChatId);

                clearAiTimeout();
                pendingTypingRow = null;
                setSendingState(false);

                const box = document.getElementById('chat-messages');
                box.innerHTML = '';
                box.classList.remove('visible');
                if (backendAI && backendAI.clearContext) backendAI.clearContext();
                renderHistoryList();
            }

            // "Очистить" на панели ввода — стирает диалог насовсем, без сохранения в историю
            async function clearCurrentChat() {
                const ok = await customConfirm('Очистить чат полностью? В историю он сохранён не будет.', { confirmText: 'Очистить' });
                if (!ok) return;
                const chats = loadAllChats().filter(function(c) { return c.id !== activeChatId; });
                saveAllChats(chats);
                startNewChat();
            }

            function toggleHistory(forceState) {
                const drawer = document.getElementById('history-drawer');
                const overlay = document.getElementById('drawer-overlay');
                const open = (typeof forceState === 'boolean') ? forceState : !drawer.classList.contains('open');
                drawer.classList.toggle('open', open);
                overlay.classList.toggle('open', open);
                if (open) renderHistoryList();
            }

            function initChatState() {
                const chats = loadAllChats();
                const savedActive = localStorage.getItem(ACTIVE_CHAT_KEY);
                const found = chats.find(function(c) { return c.id === savedActive; });
                if (found) {
                    activeChatId = found.id;
                    found.messages.forEach(function(m) { appendBubble(m.role, m.text); });
                } else {
                    activeChatId = newChatId();
                    localStorage.setItem(ACTIVE_CHAT_KEY, activeChatId);
                }
                renderHistoryList();
            }
            initChatState();

            // ==========================================================
            // 🔗 HomeBridge: Быстрый доступ + RSS-ленты
            // (сам объект homeBridge инициализируется в initWebChannel() выше)
            // ==========================================================
            function wireHomeBridgeSignals() {
                homeBridge.favoritesReceived.connect(function(jsonStr) {
                    const favs = JSON.parse(jsonStr);
                    renderQuickLinks(favs);
                    renderFavoritesModalList(favs);
                });
                homeBridge.favoriteAddBlocked.connect(function(reason) {
                    alert(reason);
                });
                homeBridge.feedsReceived.connect(function(jsonStr) {
                    renderFeedsModalList(JSON.parse(jsonStr));
                });
                homeBridge.newsReceived.connect(function(jsonStr) {
                    renderNews(JSON.parse(jsonStr));
                });
                homeBridge.feedAddResult.connect(function(success, message) {
                    const statusEl = document.getElementById('feed-status');
                    statusEl.textContent = message;
                    statusEl.className = 'feed-status ' + (success ? 'ok' : 'err');
                    if (success) document.getElementById('feed-url-input').value = '';
                });
            }

            // ---------------- Быстрый доступ ----------------
            function renderQuickLinks(favs) {
                const container = document.getElementById('quick-links-container');
                if (!favs.length) {
                    container.innerHTML =
                        "<a href='https://ya.ru' class='fav-link'>Яндекс</a>" +
                        "<a href='https://youtube.com' class='fav-link'>YouTube</a>";
                    return;
                }
                container.innerHTML = favs.map(function(f) {
                    let title = f.title.length > 15 ? f.title.slice(0, 15) + '...' : f.title;
                    return '<a href="' + f.url + '" class="fav-link">' + title + '</a>';
                }).join('');
            }

            function openFavoritesModal() {
                openModal('favorites-modal');
                if (homeBridge) homeBridge.getFavorites();
            }

            function renderFavoritesModalList(favs) {
                const list = document.getElementById('favorites-list');
                if (!favs.length) {
                    list.innerHTML = '<div class="limit-note">Ссылок пока нет — добавьте первую ниже.</div>';
                } else {
                    list.innerHTML = favs.map(function(f) {
                        const safeUrl = f.url.replace(/'/g, "\\'");
                        return '<div class="fav-item-row"><span>' + f.title + '</span>' +
                               '<button class="remove-btn" onclick="removeFavoriteClick(\'' + safeUrl + '\')">✕</button></div>';
                    }).join('');
                }
                const atLimit = favs.length >= 6;
                document.getElementById('fav-add-block').style.display = atLimit ? 'none' : 'flex';
                document.getElementById('fav-limit-note').style.display = atLimit ? 'block' : 'none';
            }

            function submitAddFavorite() {
                const title = document.getElementById('fav-title-input').value.trim();
                const url = document.getElementById('fav-url-input').value.trim();
                if (!url) return;
                if (homeBridge) homeBridge.addFavorite(title, url);
                document.getElementById('fav-title-input').value = '';
                document.getElementById('fav-url-input').value = '';
            }

            function removeFavoriteClick(url) {
                if (homeBridge) homeBridge.removeFavorite(url);
            }

            // ---------------- RSS-ленты / Новости ----------------
            function openFeedsModal() {
                openModal('feeds-modal');
                document.getElementById('feed-status').textContent = '';
                document.getElementById('feed-status').className = 'feed-status';
                if (homeBridge) homeBridge.getFeeds();
            }

            function renderFeedsModalList(feeds) {
                const list = document.getElementById('feeds-list');
                list.innerHTML = feeds.map(function(f) {
                    const safeUrl = f.replace(/'/g, "\\'");
                    return '<div class="feed-item-row"><span title="' + f + '">' + f + '</span>' +
                           '<button class="remove-btn" onclick="removeFeedClick(\'' + safeUrl + '\')">✕</button></div>';
                }).join('');
            }

            function submitAddFeed() {
                const url = document.getElementById('feed-url-input').value.trim();
                if (!url) return;
                document.getElementById('feed-status').textContent = 'Проверяем ленту...';
                document.getElementById('feed-status').className = 'feed-status';
                if (homeBridge) homeBridge.addFeed(url);
            }

            function removeFeedClick(url) {
                if (homeBridge) homeBridge.removeFeed(url);
            }

            function renderNews(items) {
                const content = document.getElementById('news-content');
                if (!items.length) {
                    content.innerHTML = '<div class="news-error">Новости недоступны</div>';
                    return;
                }
                content.innerHTML = items.map(function(item) {
                    let title = item.title.length > 60 ? item.title.slice(0, 60) + '…' : item.title;
                    return '<a href="' + item.link + '" class="news-item">' +
                           '<span class="news-dot">›</span>' +
                           '<span class="news-title">' + title + '<span class="news-source"> · ' + item.source + '</span></span></a>';
                }).join('');
            }

            // ==========================================================
            // 📅 ДАТА + МОТИВАЦИОННАЯ ЦИТАТА
            // ==========================================================
            const STORM_QUOTES = [
                "Гроза не разрушает горы — она открывает их истинную форму.",
                "Лучшие идеи рождаются там, где заканчивается зона комфорта.",
                "Шторм проходит. Тот, кто выстоял, становится сильнее.",
                "Маленький шаг сегодня — большая буря завтра.",
                "Не бойся начать заново — у грозы тоже нет черновиков.",
                "Спокойствие после бури стоит того, чтобы через неё пройти.",
                "Настоящая сила — не в отсутствии страха, а в движении вопреки ему.",
                "Каждый день — новая возможность переписать вчерашний сценарий.",
                "Молния бьёт туда, где меньше всего сопротивления. Будь гибким, а не хрупким.",
                "Тот, кто ищет тихую гавань, никогда не узнает, куда может завести ветер.",
                "Дисциплина — это мост между целью и результатом.",
                "Даже самый сильный шторм рано или поздно стихает.",
                "Сомнения убивают больше мечт, чем неудачи.",
                "Однажды начатое движение сложнее остановить, чем разогнать.",
                "Гром пугает, но именно молния меняет пейзаж.",
                "Твой лучший день ещё не наступил — он ждёт твоего следующего шага.",
                "Учись у бури: она не извиняется за свою силу.",
                "Прогресс редко выглядит как прямая линия.",
                "Однажды ты оглянешься и поймёшь: трудности были тренировкой.",
                "Уверенность строится действием, а не ожиданием готовности.",
                "Ветер не выбирает, куда дуть — ты выбираешь, куда плыть.",
                "Маленькие привычки создают большие бури перемен.",
                "Не сравнивай свою главу первую с чужой двадцатой.",
                "Самая тёмная туча несёт самый сильный дождь — и самый чистый воздух после.",
                "Отдых — это тоже часть пути, а не его остановка.",
                "Каждая молния начинается с едва заметной искры.",
                "Лучше действовать несовершенно, чем ждать идеального момента.",
                "Штиль после бури — награда, а не гарантия.",
                "Не бойся выделяться — гроза тоже не старается быть незаметной.",
                "Сегодняшние усилия — это фундамент завтрашнего затишья.",
                "Границы комфорта расширяются только под давлением.",
                "Всё, что тебя пугает, однажды станет частью истории твоей силы.",
                "Буря учит смотреть на препятствия как на топливо, а не как на стену.",
                "Ясное небо ценится именно потому, что бывает не всегда.",
                "Ты не обязан видеть всю дорогу — достаточно видеть следующий шаг."
            ];

            function loadDateQuote() {
                const now = new Date();
                const days = ['Воскресенье','Понедельник','Вторник','Среда','Четверг','Пятница','Суббота'];
                const months = ['января','февраля','марта','апреля','мая','июня','июля','августа','сентября','октября','ноября','декабря'];

                document.getElementById('date-day').textContent = days[now.getDay()];
                document.getElementById('date-full').textContent =
                    now.getDate() + ' ' + months[now.getMonth()] + ' ' + now.getFullYear();

                const start = new Date(now.getFullYear(), 0, 0);
                const dayOfYear = Math.floor((now - start) / 86400000);
                const quote = STORM_QUOTES[dayOfYear % STORM_QUOTES.length];
                document.getElementById('quote-text').textContent = '«' + quote + '»';
            }
            loadDateQuote();

            // ==========================================================
            // 🌤 ПОГОДА (Open-Meteo — бесплатно, без ключа)
            // ==========================================================
            const WMO_ICONS = {
                0: '☀️', 1: '🌤', 2: '⛅', 3: '☁️',
                45: '🌫', 48: '🌫',
                51: '🌦', 53: '🌦', 55: '🌧',
                61: '🌧', 63: '🌧', 65: '🌧️',
                71: '🌨', 73: '🌨', 75: '❄️',
                80: '🌦', 81: '🌧', 82: '⛈',
                95: '⛈', 96: '⛈', 99: '⛈'
            };
            const WMO_DESC = {
                0: 'Ясно', 1: 'Малооблачно', 2: 'Облачно с прояснениями', 3: 'Пасмурно',
                45: 'Туман', 48: 'Изморозь',
                51: 'Морось', 53: 'Морось', 55: 'Сильная морось',
                61: 'Небольшой дождь', 63: 'Дождь', 65: 'Сильный дождь',
                71: 'Небольшой снег', 73: 'Снег', 75: 'Сильный снег',
                80: 'Ливень', 81: 'Ливень', 82: 'Сильный ливень',
                95: 'Гроза', 96: 'Гроза с градом', 99: 'Сильная гроза'
            };

            async function loadWeather(forceCity) {
                const content = document.getElementById('weather-content');
                try {
                    let city = forceCity || localStorage.getItem('storm_weather_city') || 'Москва';
                    let lat = localStorage.getItem('storm_weather_lat');
                    let lon = localStorage.getItem('storm_weather_lon');

                    if (forceCity || !lat || !lon) {
                        const geoResp = await fetch('https://geocoding-api.open-meteo.com/v1/search?name=' +
                            encodeURIComponent(city) + '&count=1&language=ru');
                        const geoData = await geoResp.json();
                        if (!geoData.results || !geoData.results.length) {
                            content.innerHTML = '<div class="weather-error">Город не найден</div>';
                            return;
                        }
                        const place = geoData.results[0];
                        lat = place.latitude; lon = place.longitude;
                        city = place.name;
                        localStorage.setItem('storm_weather_city', city);
                        localStorage.setItem('storm_weather_lat', lat);
                        localStorage.setItem('storm_weather_lon', lon);
                    }

                    const wResp = await fetch('https://api.open-meteo.com/v1/forecast?latitude=' + lat +
                        '&longitude=' + lon + '&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&timezone=auto');
                    const wData = await wResp.json();
                    const c = wData.current;
                    const icon = WMO_ICONS[c.weather_code] || '🌡';
                    const desc = WMO_DESC[c.weather_code] || '';
                    const temp = Math.round(c.temperature_2m);

                    content.innerHTML =
                        '<div class="weather-main">' +
                            '<span class="weather-icon">' + icon + '</span>' +
                            '<span class="weather-temp">' + (temp > 0 ? '+' : '') + temp + '°</span>' +
                        '</div>' +
                        '<div class="weather-city">' + city + ' · ' + desc + '</div>' +
                        '<div class="weather-details">' +
                            '<span>💧 ' + c.relative_humidity_2m + '%</span>' +
                            '<span>💨 ' + Math.round(c.wind_speed_10m) + ' км/ч</span>' +
                        '</div>';
                } catch (e) {
                    content.innerHTML = '<div class="weather-error">Не удалось загрузить погоду</div>';
                }
            }

            function changeWeatherCity() {
                const city = prompt('Введите город:', localStorage.getItem('storm_weather_city') || 'Москва');
                if (city && city.trim()) {
                    localStorage.removeItem('storm_weather_lat');
                    localStorage.removeItem('storm_weather_lon');
                    document.getElementById('weather-content').innerHTML = '<div class="weather-loading">Загрузка...</div>';
                    loadWeather(city.trim());
                }
            }
            loadWeather();
        </script>
    </body>
    </html>
    )HTML";

    html.replace("%QWEBCHANNEL_JS%", qwebchannelJs);
    html.replace("%BG_STYLE%", bgStyle);
    html.replace("%GREETING%", greetingText);
    html.replace("%THREATS%", blockedThreats);
    html.replace("%TRAFFIC%", savedTraffic);
    html.replace("%TIME%", savedTime);
    html.replace("%QUICK_LINKS%", quickLinksHtml);

    return html;
}