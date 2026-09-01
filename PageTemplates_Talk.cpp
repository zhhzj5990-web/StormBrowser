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

#include "PageTemplates_Internal.h"

// ==========================================================================
// PageTemplates_Talk.cpp — storm://talk, часть 1 из 2
// getTalkHtml() просто склеивает две половины шаблона (у Talk, в
// отличие от Cloud, нет .replace(...) — шаблон статический). Первая
// половина текста — здесь же, вторая — в PageTemplates_Talk2.cpp.
//
// ЭТА РЕВИЗИЯ:
//  1) Исправлены найденные скрытые баги (см. пометки "БАГФИКС" ниже).
//  2) Звонок 1-на-1 переделан в ГРУППОВУЮ комнату: несколько участников
//     могут одновременно находиться в одной комнате (mesh: у каждого
//     участника отдельное RTCPeerConnection к каждому остальному).
//  3) Демонстрация экрана теперь даёт явный выбор "весь экран" / "вкладка
//     браузера" через constraint displaySurface вместо одного общего вызова.
//
// ВАЖНОЕ ДОПУЩЕНИЕ ПРО СЕРВЕР: протокол сигнализации ниже предполагает,
// что сигнальный сервер (server.py, эндпоинт /ws/talk/{room}) — простой
// broadcast-relay: пересылает КАЖДОЕ полученное сообщение ВСЕМ остальным
// сокетам, подключённым к этой же комнате, не заглядывая внутрь. Именно
// так вело себя серверное 1-на-1 соединение раньше (иначе исходный код
// вообще не смог бы работать), поэтому groupовая версия ниже рассчитана
// на то, что сервер менять не придётся. Если это не так — например,
// сервер закрывает комнату после 2 участников, или сам разбирает
// сообщения — пришлите server.py, поправим протокол под него.
// ==========================================================================

QString PageTemplates::getTalkHtml() {
    return buildTalkHtmlPart1() + buildTalkHtmlPart2();
}

QString buildTalkHtmlPart1() {
    return u8R"HTML(
    <!DOCTYPE html>
    <html lang="ru">
    <head>
        <meta charset="UTF-8">
        <title>Storm Talk</title>
        <style>
            body {
                margin: 0; background-color: #070a12; color: #eef3ff;
                font-family: 'Segoe UI', sans-serif; display: flex; flex-direction: column; height: 100vh;
                overflow: hidden;
            }
            .header {
                padding: 12px 24px; background: rgba(255,255,255,0.03); border-bottom: 1px solid rgba(86, 211, 155, 0.2);
                display: flex; justify-content: space-between; align-items: center; box-shadow: 0 4px 15px rgba(0,0,0,0.5);
                flex-wrap: wrap; gap: 10px;
            }
            .logo { font-size: 22px; font-weight: bold; color: #56d39b; text-shadow: 0 0 10px rgba(86,211,155,0.4); }
            .controls { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }

            input[type="text"] {
                padding: 9px 14px; border-radius: 8px; border: 1px solid rgba(86, 211, 155, 0.4);
                background: #1c2128; color: white; font-size: 14px; width: 200px; outline: none; transition: 0.2s;
            }
            input[type="text"]:focus { border-color: #56d39b; box-shadow: 0 0 10px rgba(86,211,155,0.2); }

            button {
                padding: 9px 16px; border-radius: 8px; border: none; font-size: 14px; font-weight: bold;
                cursor: pointer; transition: 0.2s;
            }
            .btn-call { background: #56d39b; color: #000; }
            .btn-call:hover { background: #4ac78f; }
            .btn-call:disabled { background: #2a3a33; color: #6b7a73; cursor: not-allowed; }
            .btn-end { background: #ff5f5f; color: #fff; }
            .btn-end:hover { background: #e04a4a; }

            /* Тулбар с иконками управления звонком (микрофон/камера/экран/фон/реакции/чат) */
            .toolbar {
                display: flex; gap: 8px; padding: 10px 24px; background: rgba(255,255,255,0.02);
                border-bottom: 1px solid rgba(86, 211, 155, 0.1); flex-wrap: wrap; align-items: center;
            }
            .icon-btn {
                background: #1c2128; color: #eef3ff; width: 42px; height: 42px; border-radius: 50%;
                display: flex; align-items: center; justify-content: center; font-size: 18px; padding: 0;
                border: 1px solid rgba(255,255,255,0.08);
            }
            .icon-btn:hover { background: #262c36; }
            .icon-btn.off { background: #ff5f5f; color: #fff; }
            .icon-btn.active { background: #56d39b; color: #000; }
            .toolbar-sep { width: 1px; height: 26px; background: rgba(255,255,255,0.15); margin: 0 4px; }

            /* Панель выбора фона */
            .panel {
                position: absolute; top: 100px; left: 24px; background: #14171f; border: 1px solid rgba(86,211,155,0.3);
                border-radius: 12px; padding: 14px; z-index: 100; display: none; box-shadow: 0 10px 30px rgba(0,0,0,0.6);
                width: 220px;
            }
            .panel.open { display: block; }
            .panel h4 { margin: 0 0 10px 0; font-size: 13px; color: #8b949e; text-transform: uppercase; letter-spacing: 0.5px; }
            .bg-option, .filter-option {
                display: block; width: 100%; text-align: left; background: #1c2128; color: #eef3ff;
                margin-bottom: 6px; border: 1px solid transparent; font-weight: normal; font-size: 13px;
            }
            .bg-option.active, .filter-option.active { border-color: #56d39b; background: rgba(86,211,155,0.15); color: #56d39b; }
            #bg-unavailable-note { display: none; color: #ff5f5f; font-size: 12px; margin-top: 6px; }
            input[type="file"] { font-size: 12px; color: #8b949e; width: 100%; }

            /* Ползунок громкости собеседника */
            input[type="range"] {
                -webkit-appearance: none; appearance: none; flex: 1; height: 4px; border-radius: 2px;
                background: #262c36; outline: none; accent-color: #56d39b; cursor: pointer;
            }
            input[type="range"]::-webkit-slider-thumb {
                -webkit-appearance: none; appearance: none; width: 14px; height: 14px; border-radius: 50%;
                background: #56d39b; cursor: pointer; border: none;
            }
            input[type="range"]::-moz-range-thumb {
                width: 14px; height: 14px; border-radius: 50%; background: #56d39b; cursor: pointer; border: none;
            }
            #volume-value { font-size: 12px; color: #8b949e; width: 34px; text-align: right; }

            /* Ползунок громкости прямо в тулбаре (рядом с остальными кнопками звонка) */
            .toolbar-volume {
                display: flex; align-items: center; gap: 8px; height: 42px; padding: 0 14px 0 12px;
                background: #1c2128; border: 1px solid rgba(255,255,255,0.08); border-radius: 21px;
            }
            .toolbar-volume span:first-child { font-size: 17px; }
            .toolbar-volume input[type="range"] { width: 80px; }

            .video-container {
                flex: 1; display: flex; position: relative; padding: 16px; justify-content: center; align-items: center;
                background: radial-gradient(circle at center, #1e2638 0%, #070a12 100%); min-height: 0;
            }

            /* Сетка удалённых участников — раньше был один фиксированный .remote-video-wrap
               на одного собеседника, теперь плиток может быть сколько угодно (групповая
               комната), сетка сама подбирает размер и раскладку. */
            .remote-grid {
                width: 100%; height: 100%; max-width: 1400px;
                display: grid; gap: 14px; align-content: center; justify-content: center;
                grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            }
            .remote-tile {
                position: relative; background: #14171f; border-radius: 16px;
                overflow: hidden; border: 2px solid rgba(86, 211, 155, 0.2); box-shadow: 0 10px 40px rgba(0,0,0,0.6);
                aspect-ratio: 16/9; min-height: 160px;
            }
            .empty-room-hint {
                color: #8b949e; font-size: 15px; text-align: center; max-width: 380px; line-height: 1.5;
            }
            .local-video-wrap {
                position: absolute; bottom: 30px; right: 30px; width: 220px; aspect-ratio: 16/9;
                background: #000; border-radius: 12px; overflow: hidden; border: 2px solid #56d39b;
                box-shadow: 0 5px 20px rgba(0,0,0,0.8); z-index: 10;
            }
            video { width: 100%; height: 100%; object-fit: cover; background: #000; }
            .overlay-text { position: absolute; bottom: 10px; left: 15px; color: rgba(255,255,255,0.9); font-size: 14px; font-weight: bold; text-shadow: 0px 2px 5px rgba(0,0,0,1); z-index: 5; }

            #my-id-display { color: #56d39b; font-family: monospace; font-size: 16px; user-select: all; cursor: copy; background: rgba(86, 211, 155, 0.1); padding: 4px 8px; border-radius: 6px; }
            .status { color: #8b949e; font-size: 13px; display: flex; align-items: center; gap: 8px; }
            .pulse { width: 10px; height: 10px; border-radius: 50%; background: #ffc857; animation: pulse-anim 1.5s infinite; }

            @keyframes pulse-anim {
                0% { box-shadow: 0 0 0 0 rgba(255, 200, 87, 0.7); }
                70% { box-shadow: 0 0 0 10px rgba(255, 200, 87, 0); }
                100% { box-shadow: 0 0 0 0 rgba(255, 200, 87, 0); }
            }

            /* Летящие эмодзи-реакции */
            .floating-reaction {
                position: absolute; bottom: 60px; font-size: 34px; z-index: 50;
                animation: float-up 2s ease-out forwards; pointer-events: none;
            }
            @keyframes float-up {
                0% { transform: translateY(0) scale(0.6); opacity: 0; }
                15% { opacity: 1; transform: translateY(-20px) scale(1.1); }
                100% { transform: translateY(-220px) scale(1); opacity: 0; }
            }

            /* Панель чата */
            .chat-panel {
                position: absolute; top: 0; right: -320px; width: 300px; height: 100%;
                background: rgba(10,14,23,0.97); border-left: 1px solid rgba(86,211,155,0.25);
                display: flex; flex-direction: column; transition: right 0.25s ease; z-index: 60;
            }
            .chat-panel.open { right: 0; }
            .chat-header { padding: 14px; font-weight: bold; color: #56d39b; border-bottom: 1px solid rgba(255,255,255,0.08); }
            #chat-messages { flex: 1; overflow-y: auto; padding: 12px; display: flex; flex-direction: column; gap: 8px; }
            .chat-msg { padding: 8px 12px; border-radius: 10px; font-size: 13px; max-width: 85%; word-wrap: break-word; }
            .chat-msg.me { background: #56d39b; color: #000; align-self: flex-end; }
            .chat-msg.them { background: #1c2128; color: #eef3ff; align-self: flex-start; }
            .chat-input-row { display: flex; padding: 10px; gap: 8px; border-top: 1px solid rgba(255,255,255,0.08); }
            .chat-input-row input { flex: 1; width: auto; }

            .reaction-bar { position: absolute; top: 100px; right: 24px; background: #14171f; border: 1px solid rgba(86,211,155,0.3);
                border-radius: 30px; padding: 8px 12px; display: none; gap: 6px; z-index: 100; box-shadow: 0 10px 30px rgba(0,0,0,0.6); }
            .reaction-bar.open { display: flex; }
            .reaction-bar button { background: transparent; font-size: 22px; padding: 4px 6px; }
            .reaction-bar button:hover { background: rgba(255,255,255,0.08); }

            /* Список участников — слева, зеркально панели чата справа, чтобы обе
               можно было открыть одновременно без конфликта. */
            .participants-panel {
                position: absolute; top: 0; left: -300px; width: 280px; height: 100%;
                background: rgba(10,14,23,0.97); border-right: 1px solid rgba(86,211,155,0.25);
                display: flex; flex-direction: column; transition: left 0.25s ease; z-index: 60;
            }
            .participants-panel.open { left: 0; }
            .participants-header { padding: 14px; font-weight: bold; color: #56d39b; border-bottom: 1px solid rgba(255,255,255,0.08); }
            #participants-list { flex: 1; overflow-y: auto; padding: 10px; display: flex; flex-direction: column; gap: 8px; }
            .participant-row { background: #1c2128; border-radius: 10px; padding: 10px 12px; }
            .participant-row-top { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
            .participant-name { font-size: 13px; color: #eef3ff; font-weight: 600; display: flex; align-items: center; gap: 6px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
            .participant-icons { font-size: 12px; color: #8b949e; white-space: nowrap; flex-shrink: 0; }
            .participant-row input[type="range"] { width: 100%; margin-top: 8px; }

            /* Индикатор "говорит сейчас" — янтарная обводка плитки по уровню звука. */
            .remote-tile.speaking, .local-video-wrap.speaking {
                border-color: #ffc857 !important;
                box-shadow: 0 0 0 3px rgba(255,200,87,0.35), 0 10px 40px rgba(0,0,0,0.6);
            }

            /* Значок поднятой руки — держится, пока не снимут вручную (в отличие
               от летящих emoji-реакций, которые сами гаснут за 2 секунды). */
            .hand-badge {
                position: absolute; top: 10px; right: 10px; font-size: 22px; z-index: 6;
                display: none; filter: drop-shadow(0 2px 6px rgba(0,0,0,0.8));
                animation: hand-wave 1.4s ease-in-out infinite;
            }
            .hand-badge.show { display: block; }
            @keyframes hand-wave { 0%, 100% { transform: rotate(0deg); } 25% { transform: rotate(-14deg); } 75% { transform: rotate(14deg); } }

            /* Переключатели в панели настроек звука (шумоподавление и т.п.) */
            .audio-settings-row {
                display: flex; align-items: center; justify-content: space-between; gap: 10px;
                padding: 8px 2px; font-size: 13px; color: #eef3ff;
            }
            .toggle-switch {
                width: 38px; height: 22px; border-radius: 11px; background: #262c36; position: relative;
                cursor: pointer; border: 1px solid rgba(255,255,255,0.1); flex-shrink: 0;
            }
            .toggle-switch.on { background: #56d39b; }
            .toggle-switch::after {
                content: ''; position: absolute; top: 2px; left: 2px; width: 16px; height: 16px; border-radius: 50%;
                background: #fff; transition: left 0.15s ease;
            }
            .toggle-switch.on::after { left: 18px; }

            /* Общая доска — рисуем поверх сетки участников, закрывается крестиком. */
            .whiteboard-overlay {
                position: absolute; inset: 0; background: #eef1ee; z-index: 200; display: none;
                flex-direction: column; border-radius: 12px; overflow: hidden;
            }
            .whiteboard-overlay.open { display: flex; }
            .whiteboard-toolbar {
                display: flex; align-items: center; gap: 10px; padding: 10px 14px; background: #14171f;
                border-bottom: 1px solid rgba(86,211,155,0.2);
            }
            .wb-color { width: 22px; height: 22px; border-radius: 50%; border: 2px solid transparent; cursor: pointer; padding: 0; }
            .wb-color.active { border-color: #fff; }
            #whiteboard-canvas { flex: 1; touch-action: none; cursor: crosshair; }

            /* Свой alert()/confirm() — вместо системного диалога ОС/браузера с белой
               полосой заголовка, который не подчиняется теме страницы. */
            .modal-backdrop {
                position: fixed; inset: 0; background: rgba(3,5,10,0.72);
                display: none; align-items: center; justify-content: center; z-index: 1000;
            }
            .modal-backdrop.open { display: flex; }
            .modal-box {
                background: #14171f; border: 1px solid rgba(86,211,155,0.35); border-radius: 14px;
                padding: 22px 24px; width: 360px; max-width: 88vw; box-shadow: 0 20px 60px rgba(0,0,0,0.7);
                display: flex; flex-direction: column; gap: 16px; animation: modal-pop 0.15s ease-out;
            }
            @keyframes modal-pop { from { transform: scale(0.94); opacity: 0; } to { transform: scale(1); opacity: 1; } }
            .modal-message { color: #eef3ff; font-size: 14px; line-height: 1.55; white-space: pre-line; }
            .modal-buttons { display: flex; justify-content: flex-end; gap: 10px; }
            .modal-buttons button { min-width: 84px; }
        </style>
    </head>
    <body>
        <div class="header">
            <div class="logo">📹 Storm Talk</div>
            <div class="status" id="status-container">
                <div class="pulse" id="status-indicator"></div>
                <span id="status-text">Инициализация оборудования...</span>
                <span id="call-duration" style="display:none; margin-left:8px; color:#56d39b; font-family:monospace;"></span>
                <span id="quality-indicator" style="display:none; margin-left:8px; font-size:12px;" title="Количество участников в комнате"></span>
            </div>
            <div class="controls">
                <span style="color: #8b949e;">Комната:</span>
                <b id="my-id-display" title="Нажмите, чтобы скопировать код комнаты">загрузка...</b>
                <button class="icon-btn" id="new-room-btn" title="Создать новую комнату" style="width:32px;height:32px;font-size:14px;">🔄</button>
                <div style="width: 1px; height: 26px; background: rgba(255,255,255,0.2); margin: 0 6px;"></div>
                <input type="text" id="remote-id-input" placeholder="Код комнаты для входа...">
                <button class="btn-call" id="call-btn">📞 Войти в комнату</button>
                <button class="btn-end" id="end-btn" style="display: none;">🛑 Покинуть комнату</button>
            </div>
        </div>

        <!-- Тулбар управления звонком -->
        <div class="toolbar">
            <button class="icon-btn active" id="mic-btn" title="Микрофон вкл/выкл">🎤</button>
            <button class="icon-btn active" id="cam-btn" title="Камера вкл/выкл">📷</button>
            <div class="toolbar-sep"></div>
            <div class="toolbar-volume" title="Громкость собеседников">
                <span>🔉</span>
                <input type="range" id="volume-slider" min="0" max="100" value="50">
                <span id="volume-value">50%</span>
            </div>
            <div class="toolbar-sep"></div>
            <button class="icon-btn" id="screen-btn" title="Демонстрация экрана">🖥️</button>
            <span style="position:relative; display:inline-flex;">
                <button class="icon-btn" id="audio-settings-btn" title="Обработка звука (шумоподавление и т.п.)">🎚️</button>
                <div class="panel" id="audio-settings-panel" style="top:52px; left:0; width:250px;">
                    <h4>Обработка микрофона</h4>
                    <div class="audio-settings-row">
                        <span>Шумоподавление</span>
                        <div class="toggle-switch on" id="toggle-noise-suppression"></div>
                    </div>
                    <div class="audio-settings-row">
                        <span>Эхоподавление</span>
                        <div class="toggle-switch on" id="toggle-echo-cancellation"></div>
                    </div>
                    <div class="audio-settings-row">
                        <span>Автоусиление громкости</span>
                        <div class="toggle-switch on" id="toggle-auto-gain"></div>
                    </div>
                </div>
            </span>
            <button class="icon-btn" id="bg-btn" title="Фон">🖼️</button>
            <div class="toolbar-sep"></div>
            <button class="icon-btn" id="hand-btn" title="Поднять руку">✋</button>
            <button class="icon-btn" id="reaction-btn" title="Реакции">😊</button>
            <button class="icon-btn" id="chat-btn" title="Чат">💬</button>
            <button class="icon-btn" id="participants-btn" title="Участники">👥</button>
            <button class="icon-btn" id="whiteboard-btn" title="Доска видна только тем, у кого она открыта">🖊️</button>
            <div class="toolbar-sep"></div>
            <button class="icon-btn" id="record-btn" title="Запись звонка на диск">⏺</button>
        </div>

        <!-- Панель выбора фона -->
        <div class="panel" id="bg-panel">
            <h4>Фон</h4>
            <button class="bg-option active" id="bg-none">🚫 Без эффекта</button>
            <button class="bg-option" id="bg-blur-light">🌫️ Лёгкое размытие</button>
            <button class="bg-option" id="bg-blur-strong">🌫️ Сильное размытие</button>
            <button class="bg-option" id="bg-blur-max">🌫️ Максимальное размытие</button>
            <button class="bg-option" id="bg-solid">🎨 Однотонный фон</button>
            <input type="color" id="bg-solid-color" value="#1e2638" style="width:100%; height:30px; margin-bottom:6px; display:none; border:1px solid #30363d; border-radius:6px; background:#1c2128;">
            <input type="file" id="bg-image-input" accept="image/*">
            <div id="bg-unavailable-note">⚠️ Модель обработки фона не загрузилась (нет сети?). Доступен только режим "Без эффекта".</div>
            <h4 style="margin-top:14px;">Цветовой фильтр</h4>
            <button class="filter-option active" data-filter="none">Обычный</button>
            <button class="filter-option" data-filter="grayscale(1)">Ч/Б</button>
            <button class="filter-option" data-filter="sepia(0.6) saturate(1.3)">Тёплый (сепия)</button>
            <button class="filter-option" data-filter="contrast(1.15) saturate(1.3) brightness(1.05)">Насыщенный</button>
            <button class="filter-option" data-filter="contrast(1.05) saturate(0.9) brightness(1.05) hue-rotate(-8deg)">Холодный</button>
            <button class="filter-option" data-filter="sepia(0.35) contrast(1.1) brightness(0.95) saturate(1.2)">Винтаж</button>
            <button class="filter-option" data-filter="brightness(1.3) contrast(1.05)">Ярче (тёмная комната)</button>
            <button class="filter-option" data-filter="saturate(0.8) brightness(1.1) contrast(0.95)">Мягкий</button>
        </div>

        <!-- Панель быстрых реакций -->
        <div class="reaction-bar" id="reaction-bar">
            <button data-emoji="👍">👍</button>
            <button data-emoji="❤️">❤️</button>
            <button data-emoji="😂">😂</button>
            <button data-emoji="👏">👏</button>
            <button data-emoji="🎉">🎉</button>
        </div>

        <div class="video-container">
            <!-- Плитки удалённых участников добавляются/удаляются сюда динамически -->
            <div class="remote-grid" id="remote-grid">
                <div class="empty-room-hint" id="empty-room-hint">Ожидание участников...<br>Отправьте код комнаты собеседникам, чтобы они присоединились.</div>
            </div>
            <div class="local-video-wrap" id="local-video-wrap">
                <!-- muted обязательно, чтобы не было эхо от самого себя -->
                <video id="local-video" autoplay playsinline muted></video>
                <div class="overlay-text" id="local-name-label" title="Нажмите, чтобы изменить своё имя">Вы</div>
                <span class="hand-badge" id="local-hand-badge">✋</span>
            </div>

            <!-- Скрытые служебные элементы: сырое видео с камеры и canvas для обработки кадров -->
            <video id="raw-video" autoplay playsinline muted style="display:none;"></video>
            <canvas id="process-canvas" style="display:none;"></canvas>

            <!-- Список участников комнаты -->
            <div class="participants-panel" id="participants-panel">
                <div class="participants-header">👥 Участники</div>
                <div id="participants-list"></div>
            </div>

            <!-- Общая доска для рисования -->
            <div class="whiteboard-overlay" id="whiteboard-overlay">
                <div class="whiteboard-toolbar">
                    <button class="wb-color active" data-color="#1c2128" style="background:#1c2128;" title="Чёрный"></button>
                    <button class="wb-color" data-color="#ff5f5f" style="background:#ff5f5f;" title="Красный"></button>
                    <button class="wb-color" data-color="#56d39b" style="background:#56d39b;" title="Зелёный"></button>
                    <button class="wb-color" data-color="#58a6ff" style="background:#58a6ff;" title="Синий"></button>
                    <button class="wb-color" data-color="#ffc857" style="background:#ffc857;" title="Жёлтый"></button>
                    <input type="range" id="wb-size" min="2" max="20" value="4" style="width:100px;" title="Толщина кисти">
                    <div style="flex:1;"></div>
                    <button class="icon-btn" id="wb-clear-btn" title="Очистить доску для всех" style="width:36px;height:36px;font-size:15px;">🗑️</button>
                    <button class="icon-btn" id="wb-close-btn" title="Закрыть доску" style="width:36px;height:36px;font-size:15px;">✕</button>
                </div>
                <canvas id="whiteboard-canvas"></canvas>
            </div>

            <!-- Панель чата -->
            <div class="chat-panel" id="chat-panel">
                <div class="chat-header">💬 Чат комнаты</div>
                <div id="chat-messages"></div>
                <div class="chat-input-row">
                    <input type="text" id="chat-input" placeholder="Сообщение...">
                    <button class="btn-call" id="chat-send-btn">➤</button>
                </div>
            </div>
        </div>

        <!-- Свой alert()/confirm() в стиле приложения — заменяет системный белый
             диалог браузера/ОС (тот самый с белой полосой сверху), который выбивался
             из тёмной темы. showAlert()/showConfirm() ниже используют этот же блок. -->
        <div class="modal-backdrop" id="modal-backdrop">
            <div class="modal-box">
                <div class="modal-message" id="modal-message"></div>
                <div class="modal-buttons">
                    <button class="btn-end" id="modal-cancel-btn" style="display:none;">Отмена</button>
                    <button class="btn-call" id="modal-ok-btn">Ок</button>
                </div>
            </div>
        </div>

        <script src="qrc:///qtwebchannel/qwebchannel.js"></script>
        <script type="module">
            // Мост к нативной части (см. TalkBridge.h/.cpp) — нужен ТОЛЬКО для
            // отдельного always-on-top окошка с превью во время демонстрации
            // экрана. Если канал недоступен (не Qt-браузер, модуль QtWebChannel
            // не подключен и т.п.) — просто работаем без него, на сам звонок,
            // чат, реакции и фон это никак не влияет.
            window.talkBridge = null;
            try {
                if (window.QWebChannel && typeof qt !== 'undefined' && qt.webChannelTransport) {
                    new QWebChannel(qt.webChannelTransport, (channel) => {
                        window.talkBridge = channel.objects.talkBridge || null;
                        console.log("[TALK] QWebChannel подключен, talkBridge " + (window.talkBridge ? "доступен" : "не зарегистрирован"));
                    });
                } else {
                    console.warn("[TALK] QWebChannel недоступен на этой странице — превью поверх экрана при демонстрации работать не будет, сам звонок это не затрагивает.");
                }
            } catch (e) {
                console.warn("[TALK] Не удалось инициализировать QWebChannel: " + e);
            }

            // Импортируем сегментацию фона напрямую из CDN как ES-модуль —
            // так рекомендует сама документация @mediapipe/tasks-vision.
            // Если сеть недоступна/CDN заблокирован — просто отключаем фон-эффекты,
            // остальной звонок (звук/видео/чат) продолжает работать как обычно.
            import { FilesetResolver, ImageSegmenter } from "https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.2";

            let localStream = null;      // сырой поток с камеры/микрофона
            let outgoingStream = null;   // то, что реально уходит в звонок (аудио + обработанное видео с canvas)
            let ws = null;                // WebSocket до нашего собственного сигнального сервера

            // --- Комнаты и участники (групповой звонок) ---
            // myOwnRoomId — код "моей" комнаты, отображается в шапке, не меняется сам по
            // себе (только по кнопке "Создать новую комнату"). currentRoomId — комната,
            // к которой сейчас подключён WebSocket (это либо своя, либо чужая — куда зашли
            // по коду). myPeerId — случайный id ЭТОЙ сессии, нужен только чтобы различать
            // нескольких участников внутри одной комнаты и адресовать им offer/answer/candidate.
            let myOwnRoomId = null;
            let currentRoomId = null;
            let myPeerId = null;
            let isInOwnRoom = true;
            // peers: peerId -> { pc: RTCPeerConnection, dataChannel, videoEl, tileEl }
            const peers = new Map();

            let micEnabled = true;
            let camEnabled = true;
            let bgMode = 'none';         // 'none' | 'blur-light' | 'blur-strong' | 'blur-max' | 'solid' | 'image'
            let bgImage = null;
            let solidBgColor = '#1e2638';
            let colorFilter = 'none';
            let imageSegmenter = null;
            let segmenterReady = false;

            // --- Демонстрация экрана ---
            let screenStream = null;
            let isScreenSharing = false;

            // --- Своё имя, поднятая рука, обработка звука ---
            // Имя запоминается в localStorage этой страницы (origin http://localhost/storm-talk
            // одинаковый при каждом запуске) — не нужно вводить заново каждый звонок.
            let myDisplayName = '';
            let handRaised = false;
            let noiseSuppressionEnabled = true;
            let echoCancellationEnabled = true;
            let autoGainControlEnabled = true;

            // --- Индикатор "говорит сейчас" ---
            let audioMeterCtx = null;         // общий AudioContext на все анализаторы
            let speakingLoopTimer = null;
            let localAnalyser = null;         // { node, data } для собственного микрофона

            // --- Общая доска ---
            let wbColor = '#1c2128';
            let wbSize = 4;
            let wbDrawing = false;
            let wbLastPoint = null;
            let wbCtx = null;

            // --- Таймер звонка ---
            let callStartTime = null;
            let durationTimer = null;

            // --- Автопереподключение к сигнальному серверу ---
            let reconnectAttempt = 0;
            let reconnectTimer = null;

            // --- Локальная запись звонка ---
            let mediaRecorder = null;
            let isRecording = false;
            let recordedChunks = [];
            let recordCanvas = null, recordCtx = null;
            let recordRafId = null;
            let recordAudioCtx = null;

            // --- Ускорение фон-эффекта: маску пересчитываем не на каждом кадре, а раз в
            // MASK_SKIP_EVERY тиков (за 1-2 кадра силуэт визуально не меняется, а инференс —
            // самая дорогая часть эффекта), и накладываем её через GPU-композитинг
            // (globalCompositeOperation) вместо ручного попиксельного цикла по полному
            // разрешению кадра — раньше это и было основной причиной тормозов.
            let lastMaskCanvas = null;
            let maskFrameSkip = 0;
            const MASK_SKIP_EVERY = 2;

            // Аккуратно склеивает несколько CSS-filter функций в одну строку. 'none' и пустые
            // значения — не полноценные функции фильтра, склеивать их с остальными нельзя
            // (например, "blur(10px) none" — невалидная строка, и весь filter молча
            // отбрасывается браузером целиком). Раньше это и было причиной того, что
            // размытие фона не работало, пока не был выбран ещё и цветовой фильтр.
            function combineFilters(...parts) {
                const valid = parts.filter(p => p && p !== 'none');
                return valid.length ? valid.join(' ') : 'none';
            }

            const rawVideo = document.getElementById('raw-video');
            const processCanvas = document.getElementById('process-canvas');
            const ctx = processCanvas.getContext('2d', { willReadFrequently: true });
            const personCanvas = document.createElement('canvas');
            const personCtx = personCanvas.getContext('2d', { willReadFrequently: true });

            const localVideo = document.getElementById('local-video');
            const remoteGrid = document.getElementById('remote-grid');
            const emptyRoomHint = document.getElementById('empty-room-hint');
            const myIdDisplay = document.getElementById('my-id-display');
            const newRoomBtn = document.getElementById('new-room-btn');
            const remoteIdInput = document.getElementById('remote-id-input');
            const callBtn = document.getElementById('call-btn');
            const endBtn = document.getElementById('end-btn');
            const statusText = document.getElementById('status-text');
            const statusIndicator = document.getElementById('status-indicator');
            const micBtn = document.getElementById('mic-btn');
            const camBtn = document.getElementById('cam-btn');
            const screenBtn = document.getElementById('screen-btn');
            const bgBtn = document.getElementById('bg-btn');
            const bgPanel = document.getElementById('bg-panel');
            const reactionBtn = document.getElementById('reaction-btn');
            const reactionBar = document.getElementById('reaction-bar');
            const chatBtn = document.getElementById('chat-btn');
            const chatPanel = document.getElementById('chat-panel');
            const chatMessages = document.getElementById('chat-messages');
            const chatInput = document.getElementById('chat-input');
            const durationDisplay = document.getElementById('call-duration');
            const participantDisplay = document.getElementById('quality-indicator');
            const recordBtn = document.getElementById('record-btn');
            const volumeSlider = document.getElementById('volume-slider');
            const volumeValue = document.getElementById('volume-value');

            // Новые элементы: список участников, поднятая рука, настройки звука, доска
            const localVideoWrap = document.getElementById('local-video-wrap');
            const localNameLabel = document.getElementById('local-name-label');
            const localHandBadge = document.getElementById('local-hand-badge');
            const handBtn = document.getElementById('hand-btn');
            const participantsBtn = document.getElementById('participants-btn');
            const participantsPanel = document.getElementById('participants-panel');
            const participantsList = document.getElementById('participants-list');
            const audioSettingsBtn = document.getElementById('audio-settings-btn');
            const audioSettingsPanel = document.getElementById('audio-settings-panel');
            const toggleNoiseSuppression = document.getElementById('toggle-noise-suppression');
            const toggleEchoCancellation = document.getElementById('toggle-echo-cancellation');
            const toggleAutoGain = document.getElementById('toggle-auto-gain');
            const whiteboardBtn = document.getElementById('whiteboard-btn');
            const whiteboardOverlay = document.getElementById('whiteboard-overlay');
            const whiteboardCanvas = document.getElementById('whiteboard-canvas');
            const wbSizeSlider = document.getElementById('wb-size');
            const wbClearBtn = document.getElementById('wb-clear-btn');
            const wbCloseBtn = document.getElementById('wb-close-btn');

            function setStatus(text, color) {
                statusText.innerText = text;
                statusIndicator.style.background = color;
            }

            // ===================== СВОЙ ALERT()/CONFIRM() =====================
            // Заменяет системные alert()/confirm() браузера/ОС (белая полоса заголовка,
            // не подчиняется теме страницы) собственным модальным окном в стиле Storm Talk.
            const modalBackdrop = document.getElementById('modal-backdrop');
            const modalMessage = document.getElementById('modal-message');
            const modalOkBtn = document.getElementById('modal-ok-btn');
            const modalCancelBtn = document.getElementById('modal-cancel-btn');
            let modalResolve = null;

            function closeModal(result) {
                modalBackdrop.classList.remove('open');
                if (modalResolve) {
                    const resolve = modalResolve;
                    modalResolve = null;
                    resolve(result);
                }
            }
            modalOkBtn.addEventListener('click', () => closeModal(true));
            modalCancelBtn.addEventListener('click', () => closeModal(false));
            modalBackdrop.addEventListener('click', (e) => { if (e.target === modalBackdrop) closeModal(false); });
            document.addEventListener('keydown', (e) => {
                if (!modalBackdrop.classList.contains('open')) return;
                if (e.key === 'Escape') closeModal(false);
                if (e.key === 'Enter') closeModal(true);
            });

            // Информационное сообщение с одной кнопкой "Ок" — как alert(), но в теме
            // страницы. Не обязательно ждать (await) — вызывающий код в этом файле
            // и так сразу делает return сразу после показа, как и было с alert().
            function showAlert(message) {
                modalMessage.innerText = message;
                modalCancelBtn.style.display = 'none';
                modalOkBtn.innerText = 'Ок';
                modalBackdrop.classList.add('open');
                return new Promise((resolve) => { modalResolve = () => resolve(); });
            }

            // Вопрос с двумя кнопками — как confirm(), но возвращает Promise<boolean>,
            // поэтому в месте вызова нужен await (обычный confirm() блокирующий, этот — нет).
            function showConfirm(message, okText, cancelText) {
                modalMessage.innerText = message;
                modalCancelBtn.style.display = 'inline-block';
                modalCancelBtn.innerText = cancelText || 'Отмена';
                modalOkBtn.innerText = okText || 'Да';
                modalBackdrop.classList.add('open');
                return new Promise((resolve) => { modalResolve = (v) => resolve(!!v); });
            }

            // Копирование ID по клику
            myIdDisplay.addEventListener('click', () => {
                navigator.clipboard.writeText(myIdDisplay.innerText);
                const oldText = myIdDisplay.innerText;
                myIdDisplay.innerText = "Скопировано!";
                setTimeout(() => myIdDisplay.innerText = oldText, 1500);
            });

            // ===================== СВОЁ ИМЯ =====================
            // Клик по подписи "Вы" под своим превью превращает её в поле ввода —
            // без отдельной модалки, это быстрее для короткого текста. Имя
            // запоминается в localStorage (origin один и тот же при каждом запуске
            // страницы) и рассылается остальным участникам через data-канал —
            // см. broadcastToAllPeers({type:'name', ...}) и wireDataChannel() в part2.
            try {
                myDisplayName = localStorage.getItem('stormTalkDisplayName') || '';
            } catch (e) {
                console.warn("[TALK] localStorage недоступен, имя не будет запоминаться между сессиями: " + e);
            }
            if (myDisplayName) localNameLabel.innerText = myDisplayName;

            function startNameEdit() {
                const input = document.createElement('input');
                input.type = 'text';
                input.className = 'name-edit-input';
                input.maxLength = 24;
                input.value = myDisplayName || '';
                input.placeholder = 'Ваше имя';
                localNameLabel.replaceWith(input);
                input.focus();
                input.select();

                const finish = (save) => {
                    if (save) {
                        myDisplayName = input.value.trim().slice(0, 24);
                        try { localStorage.setItem('stormTalkDisplayName', myDisplayName); } catch (e) { /* не критично */ }
                        broadcastToAllPeers({ type: 'name', name: myDisplayName });
                        renderParticipantsList();
                    }
                    localNameLabel.innerText = myDisplayName || 'Вы';
                    input.replaceWith(localNameLabel);
                };
                input.addEventListener('blur', () => finish(true));
                input.addEventListener('keydown', (e) => {
                    if (e.key === 'Enter') input.blur();
                    if (e.key === 'Escape') finish(false);
                });
            }
            localNameLabel.addEventListener('click', startNameEdit);

            // Двойной клик — развернуть свою же плитку на весь экран (пара к такому же
            // обработчику на плитках собеседников, см. createTileFor() в part2).
            localVideoWrap.addEventListener('dblclick', () => toggleFullscreenTile(localVideoWrap));

            // ===================== ФОН / ЦВЕТОФИЛЬТРЫ =====================

            async function initSegmenter() {
                try {
                    await createSegmenter("GPU");
                    console.log("[TALK] Модель сегментации фона загружена (GPU-делегат).");
                } catch (e) {
                    console.warn("[TALK] GPU-делегат недоступен (" + e + "), пробуем CPU...");
                    try {
                        await createSegmenter("CPU");
                        console.log("[TALK] Модель сегментации фона загружена (CPU-делегат, медленнее).");
                    } catch (e2) {
                        console.error("[TALK] Не удалось загрузить модель сегментации фона: " + e2);
                        segmenterReady = false;
                        document.getElementById('bg-unavailable-note').style.display = 'block';
                        return;
                    }
                }
                segmenterReady = true;
            }

            async function createSegmenter(delegate) {
                const vision = await FilesetResolver.forVisionTasks(
                    "https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.2/wasm"
                );
                imageSegmenter = await ImageSegmenter.createFromOptions(vision, {
                    baseOptions: {
                        modelAssetPath: "https://storage.googleapis.com/mediapipe-models/image_segmenter/selfie_segmenter/float16/latest/selfie_segmenter.tflite",
                        delegate: delegate
                    },
                    runningMode: "VIDEO",
                    outputCategoryMask: false,
                    outputConfidenceMasks: true
                });
            }

            function setBgMode(mode) {
                if (mode !== 'none' && !segmenterReady) {
                    console.warn("[TALK] Попытка включить фон-эффект до готовности модели сегментации.");
                    return;
                }
                bgMode = mode;
                document.querySelectorAll('.bg-option').forEach(b => b.classList.remove('active'));
                if (mode !== 'image') {
                    const btn = document.getElementById('bg-' + mode);
                    if (btn) btn.classList.add('active');
                }
                document.getElementById('bg-solid-color').style.display = (mode === 'solid') ? 'block' : 'none';
                console.log("[TALK] Режим фона изменён на: " + mode);
            }

            document.getElementById('bg-none').addEventListener('click', () => setBgMode('none'));
            document.getElementById('bg-blur-light').addEventListener('click', () => setBgMode('blur-light'));
            document.getElementById('bg-blur-strong').addEventListener('click', () => setBgMode('blur-strong'));
            document.getElementById('bg-blur-max').addEventListener('click', () => setBgMode('blur-max'));
            document.getElementById('bg-solid').addEventListener('click', () => setBgMode('solid'));
            document.getElementById('bg-solid-color').addEventListener('input', (e) => {
                solidBgColor = e.target.value;
            });
            document.getElementById('bg-image-input').addEventListener('change', (e) => {
                const file = e.target.files[0];
                if (!file) return;
                if (!segmenterReady) {
                    console.warn("[TALK] Своё фото выбрано, но модель сегментации ещё не готова.");
                    return;
                }
                const reader = new FileReader();
                reader.onload = (ev) => {
                    const img = new Image();
                    img.onload = () => {
                        bgImage = img;
                        bgMode = 'image';
                        // БАГФИКС: этот обработчик выставлял bgMode='image' в обход setBgMode(),
                        // поэтому подсветка "активной" кнопки (например "Без эффекта") и палитра
                        // однотонного цвета не обновлялись — интерфейс продолжал показывать
                        // старый режим, хотя фон уже сменился на загруженное фото.
                        document.querySelectorAll('.bg-option').forEach(b => b.classList.remove('active'));
                        document.getElementById('bg-solid-color').style.display = 'none';
                        console.log("[TALK] Установлено собственное фоновое изображение.");
                    };
                    img.src = ev.target.result;
                };
                reader.readAsDataURL(file);
            });

            document.querySelectorAll('.filter-option').forEach(btn => {
                btn.addEventListener('click', () => {
                    colorFilter = btn.dataset.filter;
                    document.querySelectorAll('.filter-option').forEach(b => b.classList.remove('active'));
                    btn.classList.add('active');
                });
            });

            bgBtn.addEventListener('click', () => bgPanel.classList.toggle('open'));

            // ===================== ГРОМКОСТЬ СОБЕСЕДНИКОВ =====================
            // Регулирует громкость воспроизведения у себя локально (для каждой плитки
            // участника отдельно). На запись звонка не влияет — там пишется сырой
            // аудио-трек (см. startRecording в part2).
            let currentVolume = 0.5; // по умолчанию 50%, дальше пользователь крутит сам
            volumeSlider.addEventListener('input', () => {
                currentVolume = parseInt(volumeSlider.value, 10) / 100;
                volumeValue.innerText = volumeSlider.value + '%';
                peers.forEach(entry => { entry.videoEl.volume = currentVolume; });
            });

            // Основной цикл обработки видео: рисует итоговый кадр (с фоном/фильтром) в canvas.
            // Именно поток С ЭТОГО canvas реально уходит собеседникам — что видите вы в своём
            // превью, то видят и они.
            // Накладывает готовую альфа-маску (обычно в уменьшенном "родном" разрешении
            // модели maskW x maskH) на текущий кадр через аппаратно ускоренный
            // composite (destination-in) вместо ручного попиксельного цикла по полному
            // разрешению видео — это и было основным источником тормозов.
            function compositeFrame(w, h, maskCanvas, maskW, maskH) {
                personCtx.clearRect(0, 0, w, h);
                personCtx.filter = combineFilters(colorFilter);
                personCtx.drawImage(rawVideo, 0, 0, w, h);
                personCtx.globalCompositeOperation = 'destination-in';
                personCtx.filter = 'none';
                personCtx.drawImage(maskCanvas, 0, 0, maskW, maskH, 0, 0, w, h);
                personCtx.globalCompositeOperation = 'source-over';

                ctx.clearRect(0, 0, w, h);
                if (bgMode === 'image' && bgImage) {
                    ctx.filter = combineFilters(colorFilter);
                    ctx.drawImage(bgImage, 0, 0, w, h);
                } else if (bgMode === 'solid') {
                    // Однотонный фон вместо видео позади — фильтр тут ни при чём,
                    // цвет всегда один и тот же, поэтому colorFilter не применяем.
                    ctx.filter = 'none';
                    ctx.fillStyle = solidBgColor;
                    ctx.fillRect(0, 0, w, h);
                } else {
                    const blurPx = bgMode === 'blur-max' ? '35px' : (bgMode === 'blur-strong' ? '20px' : '10px');
                    ctx.filter = combineFilters('blur(' + blurPx + ')', colorFilter);
                    ctx.drawImage(rawVideo, 0, 0, w, h);
                }

                ctx.filter = 'none';
                ctx.drawImage(personCanvas, 0, 0);
            }

            function renderLoop() {
                requestAnimationFrame(renderLoop);
                if (!rawVideo.videoWidth) return;

                if (processCanvas.width !== rawVideo.videoWidth) {
                    processCanvas.width = rawVideo.videoWidth;
                    processCanvas.height = rawVideo.videoHeight;
                    personCanvas.width = rawVideo.videoWidth;
                    personCanvas.height = rawVideo.videoHeight;
                }
                const w = processCanvas.width, h = processCanvas.height;

                // Без эффектов (или пока модель не готова) — просто пробрасываем кадр с фильтром.
                if (bgMode === 'none' || !segmenterReady) {
                    ctx.filter = combineFilters(colorFilter);
                    ctx.drawImage(rawVideo, 0, 0, w, h);
                    return;
                }

                // Раз в MASK_SKIP_EVERY кадров переиспользуем маску с прошлого инференса —
                // видео при этом всё равно перерисовывается каждый кадр, обновляется только
                // силуэт человека, что незаметно на глаз, зато вдвое разгружает CPU/GPU.
                maskFrameSkip = (maskFrameSkip + 1) % MASK_SKIP_EVERY;
                if (maskFrameSkip !== 0 && lastMaskCanvas) {
                    try {
                        compositeFrame(w, h, lastMaskCanvas, lastMaskCanvas.width, lastMaskCanvas.height);
                    } catch (e) {
                        console.error("[TALK] Ошибка композитинга (кэш маски): " + e.message);
                        ctx.filter = combineFilters(colorFilter);
                        ctx.drawImage(rawVideo, 0, 0, w, h);
                    }
                    return;
                }

                const now = performance.now();
                imageSegmenter.segmentForVideo(rawVideo, now, (result) => {
                  try {
                    // ВАЖНО: модель selfie_segmenter бинарная (человек/не человек) и отдаёт
                    // ОДНУ маску — confidenceMasks[0]. Раньше тут стоял индекс [1], которого
                    // у этой модели просто нет — result.confidenceMasks[1] был undefined,
                    // .getAsFloat32Array() падал на каждом кадре, и canvas переставал
                    // перерисовываться (зависал на последнем удачном кадре).
                    const mask = result.confidenceMasks[0];
                    if (!mask) throw new Error("confidenceMasks[0] отсутствует в результате сегментации");
                    const personAlpha = mask.getAsFloat32Array();
                    const maskW = mask.width;
                    const maskH = mask.height;

                    // Пишем маску в маленький offscreen-canvas в её РОДНОМ разрешении
                    // (обычно меньше, чем видео) — цикл идёт по maskW*maskH точек, а не по
                    // полному кадру, и масштабирование на весь кадр делает GPU в compositeFrame.
                    if (!lastMaskCanvas) lastMaskCanvas = document.createElement('canvas');
                    if (lastMaskCanvas.width !== maskW || lastMaskCanvas.height !== maskH) {
                        lastMaskCanvas.width = maskW;
                        lastMaskCanvas.height = maskH;
                    }
                    const mCtx = lastMaskCanvas.getContext('2d');
                    const maskImgData = mCtx.createImageData(maskW, maskH);
                    const mpx = maskImgData.data;
                    for (let i = 0; i < maskW * maskH; i++) {
                        mpx[i * 4] = 255; mpx[i * 4 + 1] = 255; mpx[i * 4 + 2] = 255;
                        mpx[i * 4 + 3] = Math.round(personAlpha[i] * 255);
                    }
                    mCtx.putImageData(maskImgData, 0, 0);

                    compositeFrame(w, h, lastMaskCanvas, maskW, maskH);
                  } catch (e) {
                    // Не даём одной сбойной сегментации навсегда заморозить картинку —
                    // рисуем обычный кадр без эффекта и едем дальше на следующем тике.
                    console.error("[TALK] Ошибка обработки маски сегментации: " + e.message);
                    ctx.filter = combineFilters(colorFilter);
                    ctx.drawImage(rawVideo, 0, 0, w, h);
                  }
                });
            }

            // ===================== МИКРОФОН / КАМЕРА =====================

            micBtn.addEventListener('click', () => {
                if (!localStream) return;
                micEnabled = !micEnabled;
                localStream.getAudioTracks().forEach(t => t.enabled = micEnabled);
                // БАГФИКС: кнопка изначально имеет класс "active" (зелёная), и раньше он
                // никогда не снимался — при выключении микрофона просто добавлялся ещё и
                // класс "off" (красный). У .icon-btn.off и .icon-btn.active одинаковая
                // специфичность CSS, а .active объявлен в таблице стилей ПОСЛЕ .off,
                // поэтому при одновременном наличии обоих классов побеждал зелёный —
                // кнопка визуально оставалась "включённой", даже когда микрофон уже выключен.
                micBtn.classList.toggle('off', !micEnabled);
                micBtn.classList.toggle('active', micEnabled);
                micBtn.innerText = micEnabled ? "🎤" : "🔇";
                console.log("[TALK] Микрофон " + (micEnabled ? "включен" : "выключен"));
                broadcastToAllPeers({ type: 'state', mic: micEnabled, cam: camEnabled });
                renderParticipantsList();
            });
    )HTML";
}