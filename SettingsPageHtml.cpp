#include "SettingsPageHtml.h"
#include "MainWindow.h"
#include "DownloadManager.h"
#include <QSettings>
#include <QFile>
#include <QTextStream>
#include <QLocale>
#include <QList>

// Путь к папке загрузок подставляется в текстовое содержимое <span>, а не внутрь
// JS-строки — экранируем HTML-спецсимволы (на случай экзотического имени папки),
// а не кавычки/бэкслеш, как для полей, уходящих в JS-литералы ниже.
static QString htmlEscape(QString str) {
    return str.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
}

QString SettingsPageHtml::build(MainWindow* mw) {
    QSettings s;

    QString curLang = s.value("browser/language", "ru").toString();
    QString curSearch = s.value("browser/search_engine", "DuckDuckGo").toString();
    QString curTheme = s.value("theme", "dark").toString();
    bool    bmBarOn = s.value("browser/show_bookmarks_bar", false).toBool();
    bool    shieldOn = s.value("shield/enabled", true).toBool();
    bool    gameModeOn = s.value("browser/game_mode", false).toBool();
    bool    minimizeToTrayOn = s.value("browser/minimize_to_tray", false).toBool();
    bool    hwAccelOn = s.value("browser/hw_accel", true).toBool();
    int     aiMode = s.value("ai/mode", 0).toInt();
    QString aiKey = s.value("ai/openrouter_key", "").toString();
    QString gigaKey = s.value("ai/gigachat_key", "").toString();
    QString gigaModel = s.value("ai/gigachat_model", "GigaChat-2").toString();
    // Модели для "последнего варианта" анализа страницы — ИИ смотрит на скриншот, когда
    // обычные способы (по тексту/элементам) не сработали (см. WebPageAgent). Необязательные:
    // пустая vision_model для OpenRouter означает, что функция просто отключена.
    QString visionModel = s.value("ai/vision_model", "").toString();
    QString gigaVisionModel = s.value("ai/gigachat_vision_model", "GigaChat-2-Max").toString();
    // Ключ Tavily — общий для двух потребителей: модуля "🔬 Глубокое исследование"
    // (см. ResearchManager, пусто → бесплатный DuckDuckGo без ключа) и action "search"
    // ИИ-ассистента "🤖 Storm AI" (см. AIAssistantWidget::performTavilySearch, пусто →
    // поиск в интернете у ассистента просто недоступен). Не относится к ai/* — это
    // отдельный сервис (поиск, а не чат-ИИ).
    QString tavilyKey = s.value("research/tavily_key", "").toString();
    // Счётчик суммарно потраченных токенов GigaChat — накапливается в
    // AIAssistantWidget::onNetworkReply() из поля "usage" ответа GigaChat и
    // относится ТОЛЬКО к этому режиму (см. комментарий там же). Форматируем
    // с разделителями разрядов через русскую локаль, чтобы большое число
    // читалось легче (например, "12 480" вместо "12480").
    qint64 gigaTokensUsed = s.value("ai/gigachat_tokens_used", 0).toLongLong();
    QString gigaTokensUsedStr = QLocale(QLocale::Russian).toString(gigaTokensUsed);
    QString zoomVal = mw->getCurrentZoomString();
    bool    downloadAskEachTime = s.value("browser/download_ask_each_time", true).toBool();
    QString downloadDir = DownloadManager::lastDownloadDir();

    // Строим список тем в C++, чтобы сразу проставить класс "active" на текущей
    struct ThemeItem { QString id; QString label; };
    QList<ThemeItem> themeList = {
        {"dark",      u8"🌙 Темная"},
        {"sakura",    u8"🌸 Сакура"},
        {"matrix",    u8"💻 Матрица"},
        {"forest",    u8"🌲 Лес"},
        {"ocean",     u8"🌊 Океан"},
        {"cyberpunk", u8"🏙️ Киберпанк"},
        {"nord",      u8"❄️ Nord"},
    };
    QString themeButtonsHtml;
    for (const auto& t : themeList) {
        QString activeCls = (t.id == curTheme) ? " active" : "";
        themeButtonsHtml += QString(
            "<button class=\"theme-btn%1\" data-theme=\"%2\" onclick=\"onThemeClick(this)\">%3</button>"
        ).arg(activeCls, t.id, t.label);
    }

    // --- ЧИТАЕМ СТАНДАРТНЫЙ QWEBCHANNEL.JS ИЗ ВНУТРЕННИХ РЕСУРСОВ QT (как в getHomePageHtml) ---
    QString qwebchannelJs = "";
    QFile qwcFile(":/qtwebchannel/qwebchannel.js");
    if (qwcFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qwebchannelJs = QTextStream(&qwcFile).readAll();
        qwcFile.close();
    }

    QString html = QString(u8R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<title>Настройки Storm Browser</title>
<script>
%QWEBCHANNEL_JS%
</script>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body { background: #11141c; color: #eef3ff; font-family: -apple-system, 'Segoe UI', sans-serif;
         display: flex; min-height: 100vh; }

  .sidebar { width: 220px; background: rgba(0,0,0,0.25);
             border-right: 1px solid rgba(255,255,255,0.07);
             padding: 24px 0; flex-shrink: 0; }
  .sidebar-title { color: #a371f7; font-size: 13px; font-weight: 700;
                   letter-spacing: .08em; padding: 0 20px 14px; text-transform: uppercase; }

  .nav-search { padding: 0 16px 16px; }
  .nav-search input { width: 100%; background: rgba(255,255,255,0.04);
             border: 1px solid rgba(255,255,255,0.08); border-radius: 7px;
             color: #eef3ff; padding: 7px 10px; font-size: 12.5px; outline: none; }
  .nav-search input:focus { border-color: #6e8cff; }
  .nav-search input::placeholder { color: rgba(255,255,255,0.32); }

  .nav-item { display: flex; align-items: center; gap: 10px;
              padding: 10px 20px; cursor: pointer;
              color: #9aa3b2; font-size: 14px; transition: all .15s;
              border-left: 3px solid transparent; }
  .nav-item:hover  { background: rgba(255,255,255,0.05); color: #eef3ff; }
  .nav-item.active { background: rgba(110,140,255,0.15); color: #fff;
                     border-left-color: #6e8cff; }

  .content { flex: 1; padding: 36px 44px; overflow-y: auto; }
  .page { display: none; }
  .page.active { display: block; animation: fadeIn .18s ease; }
  @keyframes fadeIn { from { opacity: 0; transform: translateY(5px); } to { opacity: 1; transform: translateY(0); } }

  h2 { font-size: 22px; font-weight: 700; margin-bottom: 22px; color: #eef3ff; }

  /* --- Карточки: заменили плоские "section" на визуально обособленные блоки --- */
  .card { background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.08);
          border-radius: 10px; padding: 18px 20px; margin-bottom: 14px; max-width: 620px;
          transition: border-color .15s; }
  .card:hover { border-color: rgba(255,255,255,0.15); }
  .card.danger { border-color: rgba(255,95,95,0.35); background: rgba(255,95,95,0.05); }
  .card-header { display: flex; align-items: center; gap: 9px; margin-bottom: 4px; }
  .card-icon { font-size: 17px; line-height: 1; }
  .card-title { font-size: 14px; font-weight: 600; color: #eef3ff; }
  .card-desc { font-size: 12.5px; color: #9aa3b2; line-height: 1.55; margin: 4px 0 12px; max-width: 480px; }

  /* Статус-бейдж рядом с заголовком карточки — единый вид для Shield/Game Mode/HW-ускорения */
  .pill { margin-left: auto; font-size: 11px; font-weight: 700; padding: 3px 10px;
          border-radius: 20px; letter-spacing: .02em; white-space: nowrap; }
  .pill.on  { background: rgba(86,211,155,0.15); color: #56d39b; border: 1px solid rgba(86,211,155,0.35); }
  .pill.off { background: rgba(255,255,255,0.06); color: #9aa3b2; border: 1px solid rgba(255,255,255,0.12); }

  .btn-row { display: flex; gap: 8px; flex-wrap: wrap; }

  select, input[type=text], input[type=password] {
    background: #1c212d; border: 1px solid rgba(255,255,255,0.1);
    color: #eef3ff; border-radius: 7px; padding: 9px 12px;
    font-size: 14px; width: 100%; max-width: 380px; outline: none; }
  select:focus, input:focus { border-color: #6e8cff; }

  /* Обёртка поля API-ключа с кнопкой показать/скрыть */
  .input-wrap { position: relative; max-width: 380px; }
  .input-wrap input { max-width: none; padding-right: 40px; }
  .eye-btn { position: absolute; right: 4px; top: 50%; transform: translateY(-50%);
             width: 30px; height: 30px; background: none; border: none; cursor: pointer;
             font-size: 15px; opacity: .55; border-radius: 6px; }
  .eye-btn:hover { opacity: 1; background: rgba(255,255,255,0.06); }

  .btn { display: inline-flex; align-items: center; gap: 7px;
         padding: 9px 18px; border-radius: 7px; border: 1px solid rgba(255,255,255,0.12);
         background: rgba(255,255,255,0.05); color: #eef3ff;
         font-size: 13px; cursor: pointer; transition: all .15s; margin-top: 6px; }
  .btn:hover { background: rgba(255,255,255,0.10); border-color: rgba(255,255,255,0.22); }
  .btn.primary { background: #a371f7; border-color: #a371f7; color: #fff; font-weight: 600; }
  .btn.primary:hover { background: #b48cfb; }
  .btn.danger  { color: #ff5f5f; border-color: rgba(255,95,95,0.3); }
  .btn.danger:hover  { background: rgba(255,95,95,0.1); }

  .toggle-row { display: flex; align-items: center; justify-content: space-between;
                max-width: 480px; padding: 8px 0; }
  .toggle-row span { font-size: 14px; color: #ccd6f6; }
  .toggle { position:relative; width:40px; height:22px; flex-shrink:0; }
  .toggle input { opacity:0; width:0; height:0; }
  .slider { position:absolute; inset:0; background:#2d3548; border-radius:22px;
            cursor:pointer; transition:.25s; }
  .slider:before { content:''; position:absolute; height:16px; width:16px;
                   left:3px; bottom:3px; background:#fff; border-radius:50%; transition:.25s; }
  input:checked + .slider { background:#6e8cff; }
  input:checked + .slider:before { transform:translateX(18px); }

  .theme-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px,1fr));
                gap: 10px; }
  .theme-btn { padding: 10px 14px; border-radius: 8px; border: 2px solid rgba(255,255,255,0.08);
               background: rgba(255,255,255,0.04); color: #ccd6f6;
               cursor: pointer; font-size: 13px; text-align: left; transition: all .15s; }
  .theme-btn:hover  { border-color: rgba(255,255,255,0.2); background: rgba(255,255,255,0.08); }
  .theme-btn.active { border-color: #6e8cff; background: rgba(110,140,255,0.15); color: #fff; }

  .zoom-row { display:flex; align-items:center; gap:10px; }
  .zoom-btn { width:34px; height:34px; border-radius:6px; border:none;
              font-size:18px; font-weight:700; cursor:pointer; }
  .zoom-minus { background:rgba(255,95,95,.15); color:#ff5f5f; border:1px solid #ff5f5f; }
  .zoom-plus  { background:rgba(86,211,155,.15); color:#56d39b; border:1px solid #56d39b; }
  .zoom-val   { min-width:48px; text-align:center; font-size:14px; }

  .hint { font-size: 12px; color: rgba(255,255,255,0.38); margin-top: 6px; line-height: 1.5; }
  .save-bar { display: flex; align-items: center; gap: 12px; margin-top: 4px; }

  /* Тонкий разделитель внутри карточки (например, между тумблером и списком исключений) */
  .divider-thin { height: 1px; background: rgba(255,255,255,0.07); margin: 14px 0; }

  /* Исключения Storm Shield */
  .exception-add-row { display: flex; gap: 8px; max-width: 480px; }
  .exception-add-row input { flex: 1; max-width: none; }
  .exception-list { margin-top: 10px; display: flex; flex-direction: column; gap: 6px; max-width: 480px; }
  .exception-chip { display: flex; align-items: center; justify-content: space-between;
                     background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08);
                     border-radius: 7px; padding: 6px 12px; font-size: 13px; color: #ccd6f6; }
  .exception-chip button { background: none; border: none; color: #ff5f5f; cursor: pointer;
                            font-size: 14px; opacity: .7; padding: 2px 6px; }
  .exception-chip button:hover { opacity: 1; }
  .exception-empty { font-size: 12.5px; color: rgba(255,255,255,0.35); padding: 4px 0; }

  /* Проверка соединения AI-ключа */
  .test-result { font-size: 12.5px; }
  .test-result.ok { color: #56d39b; }
  .test-result.fail { color: #ff5f5f; }
  .test-result.pending { color: #9aa3b2; }

  /* Папка загрузок по умолчанию */
  .download-dir-row { display: flex; align-items: center; justify-content: space-between;
                       gap: 12px; margin-top: 14px; max-width: 480px; }
  .download-dir-row .hint { margin: 0; overflow-wrap: anywhere; }
  .save-notice { display:none; color:#56d39b; font-size:13px; }
  .bridge-warning { display:none; background: rgba(255,95,95,0.12); border:1px solid rgba(255,95,95,0.4);
                    color:#ff9d9d; padding:10px 14px; border-radius:8px; font-size:13px; margin-bottom:18px;
                    max-width: 620px; }

  /* Узкое окно — сайдбар сворачивается в горизонтальную полосу вкладок сверху */
  @media (max-width: 720px) {
    body { flex-direction: column; }
    .sidebar { width: 100%; display: flex; align-items: center; overflow-x: auto;
               padding: 10px 10px; border-right: none; border-bottom: 1px solid rgba(255,255,255,0.07); }
    .sidebar-title, .nav-search { display: none; }
    .nav-item { border-left: none; border-bottom: 3px solid transparent; white-space: nowrap; padding: 8px 14px; }
    .nav-item.active { border-left-color: transparent; border-bottom-color: #6e8cff; }
    .content { padding: 22px 18px; }
  }
</style>
</head>
<body>

<nav class="sidebar">
  <div class="sidebar-title">Storm Browser</div>
  <div class="nav-search">
    <input type="text" id="settings-search" placeholder="🔍 Найти в настройках…" oninput="onSearchInput(this.value)">
  </div>
  <div class="nav-item active" data-page="general" onclick="showPage('general', this)">⚙️  Основные</div>
  <div class="nav-item" data-page="view" onclick="showPage('view', this)">🎨  Вид</div>
  <div class="nav-item" data-page="privacy" onclick="showPage('privacy', this)">🛡  Конфиденциальность</div>
  <div class="nav-item" data-page="passwords" onclick="showPage('passwords', this)">🔑  Пароли</div>
  <div class="nav-item" data-page="ai" onclick="showPage('ai', this)">🤖  ИИ-ассистент</div>
  <div class="nav-item" data-page="help" onclick="showPage('help', this)">ℹ️  Справка</div>
</nav>

<main class="content">
  <div id="bridge-warning" class="bridge-warning">⚠️ Соединение с браузером ещё не установлено — подождите секунду и попробуйте снова.</div>

  <div id="page-general" class="page active">
    <h2>⚙️ Основные</h2>

    <div class="card">
      <div class="card-header"><span class="card-icon">🌐</span><span class="card-title">Язык интерфейса</span></div>
      <select id="lang-select" onchange="call('setLanguage', this.value)">
        <option value="ru">Русский</option>
        <option value="en">English</option>
        <option value="es">Español</option>
      </select>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">🔍</span><span class="card-title">Поисковая система</span></div>
      <select id="search-select" onchange="call('setSearchEngine', this.value)">
        <option>Yandex</option><option>Google</option>
        <option>DuckDuckGo</option><option>Bing</option>
      </select>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">🚀</span><span class="card-title">При запуске</span></div>
      <div class="card-desc">Какая страница или набор вкладок открывается при старте браузера.</div>
      <button class="btn" onclick="call('showStartupSettings')">Настроить страницу при запуске…</button>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">🗕</span><span class="card-title">Закрытие окна</span></div>
      <div class="card-desc">Что делать при нажатии на крестик окна браузера.</div>
      <div class="toggle-row">
        <span>Сворачивать в трей вместо закрытия</span>
        <label class="toggle"><input type="checkbox" id="chk-minimize-tray" onchange="call('toggleMinimizeToTray', this.checked)"><span class="slider"></span></label>
      </div>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">🖼</span><span class="card-title">Новая вкладка</span></div>
      <button class="btn" onclick="call('setNewTabBackground')">Установить фон новой вкладки…</button>
      <div class="toggle-row" style="margin-top:14px">
        <span>🔖 Показывать панель закладок</span>
        <label class="toggle"><input type="checkbox" id="chk-bmbar" onchange="call('toggleBookmarksBar', this.checked)"><span class="slider"></span></label>
      </div>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">📥</span><span class="card-title">Загрузки</span></div>
      <div class="toggle-row">
        <span>Спрашивать папку для каждой загрузки</span>
        <label class="toggle"><input type="checkbox" id="chk-download-ask" onchange="call('toggleDownloadAskEachTime', this.checked)"><span class="slider"></span></label>
      </div>
      <div class="download-dir-row">
        <span class="hint" id="download-dir-label" style="margin-top:0">%DOWNLOAD_DIR%</span>
        <button class="btn" onclick="chooseDownloadFolder()">Изменить папку…</button>
      </div>
      <div class="card-desc" style="margin:8px 0 0">Эта же папка используется как папка по умолчанию для торрент-загрузок.</div>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">🗑️</span><span class="card-title">Данные браузера</span></div>
      <div class="btn-row">
        <button class="btn" onclick="call('clearBrowserData')">Очистить данные браузера…</button>
        <button class="btn" onclick="call('printCurrentPage')">🖨️ Печать (PDF)</button>
        <button class="btn" onclick="call('setAsDefaultBrowser')">🌍 Сделать по умолчанию</button>
      </div>
    </div>
  </div>

  <div id="page-view" class="page">
    <h2>🎨 Вид</h2>

    <div class="card">
      <div class="card-header"><span class="card-icon">🎨</span><span class="card-title">Тема оформления</span></div>
      <div class="theme-grid" id="theme-grid">%THEME_BUTTONS%</div>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">🔍</span><span class="card-title">Масштаб страницы</span></div>
      <div class="zoom-row">
        <button class="zoom-btn zoom-minus" onclick="callAndRefreshZoom('zoomOut')">−</button>
        <span class="zoom-val" id="zoom-val">%ZOOM_VAL%</span>
        <button class="zoom-btn zoom-plus"  onclick="callAndRefreshZoom('zoomIn')">+</button>
        <button class="btn" onclick="call('toggleFullScreen')" style="margin-left:10px">⤢ Полный экран (F11)</button>
      </div>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">📖</span><span class="card-title">Режим чтения</span></div>
      <div class="card-desc">Убирает рекламу и лишние блоки на странице, оставляя только текст статьи.</div>
      <button class="btn" onclick="call('toggleReaderMode')">Включить режим чтения</button>
    </div>
  </div>

  <div id="page-privacy" class="page">
    <h2>🛡 Конфиденциальность</h2>

    <div class="card">
      <div class="card-header">
        <span class="card-icon">🛡</span><span class="card-title">Storm Shield</span>
        <span class="pill" id="pill-shield"></span>
      </div>
      <div class="card-desc">Блокирует рекламу и трекеры на посещаемых сайтах.</div>
      <div class="toggle-row">
        <span>Блокировка рекламы и трекеров</span>
        <label class="toggle"><input type="checkbox" id="chk-shield" checked onchange="call('toggleShield', this.checked)"><span class="slider"></span></label>
      </div>

      <div class="divider-thin"></div>

      <div class="card-title" style="font-size:13px;margin-bottom:6px">Исключения</div>
      <div class="card-desc" style="margin-bottom:10px">Для этих сайтов Shield полностью отключён — ни блокировки рекламы, ни проверки на фишинг.</div>
      <div class="exception-add-row">
        <input type="text" id="shield-exception-input" placeholder="например: example.com" onkeydown="if(event.key==='Enter'){addShieldException();}">
        <button class="btn" onclick="addShieldException()">Добавить</button>
      </div>
      <div id="shield-exceptions-list" class="exception-list"></div>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">🌐</span><span class="card-title">Proxy / VPN</span></div>
      <button class="btn" onclick="call('openProxy')">Открыть настройки Proxy/VPN…</button>
    </div>

    <div class="card">
      <div class="card-header">
        <span class="card-icon">🚀</span><span class="card-title">Игровой режим</span>
        <span class="pill" id="pill-gamemode"></span>
      </div>
      <div class="card-desc">Освобождает ОЗУ и снижает фоновую нагрузку браузера во время игр.</div>
      <div class="toggle-row">
        <span>Включить игровой режим</span>
        <label class="toggle"><input type="checkbox" id="chk-gamemode" onchange="call('toggleGameMode', this.checked)"><span class="slider"></span></label>
      </div>
    </div>

    <div class="card">
      <div class="card-header">
        <span class="card-icon">🚀</span><span class="card-title">Аппаратное ускорение</span>
        <span class="pill" id="pill-hwaccel"></span>
      </div>
      <div class="card-desc">Использует GPU для рендеринга страниц. <span style="color:#e0b25c">Требует перезапуска браузера.</span></div>
      <div class="toggle-row">
        <span>Включить аппаратное ускорение</span>
        <label class="toggle"><input type="checkbox" id="chk-hwaccel" onchange="call('toggleHwAccel', this.checked)"><span class="slider"></span></label>
      </div>
    </div>
  </div>

  <div id="page-passwords" class="page">
    <h2>🔑 Пароли</h2>

    <div class="card">
      <div class="card-header"><span class="card-icon">🔑</span><span class="card-title">Storm Vault</span></div>
      <button class="btn primary" onclick="call('showPasswordManager')">🔑 Открыть менеджер паролей</button>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">🔐</span><span class="card-title">Безопасность</span></div>
      <div class="btn-row">
        <button class="btn" onclick="call('changeMasterPassword')">Сменить мастер-пароль</button>
        <button class="btn" onclick="call('importPasswords')">📥 Импортировать (Chrome/Edge CSV)</button>
      </div>
    </div>

    <div class="card danger">
      <div class="card-header"><span class="card-icon">⚠️</span><span class="card-title">Опасная зона</span></div>
      <div class="card-desc" style="margin-bottom:10px">Необратимо удаляет все сохранённые пароли и мастер-пароль.</div>
      <button class="btn danger" onclick="call('resetPasswordVault')">Сбросить хранилище паролей</button>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">ℹ️</span><span class="card-title">Как защищены данные?</span></div>
      <div class="card-desc" style="margin-bottom:0">
        Ваши пароли защищены локальным алгоритмом шифрования.<br>
        Мастер-ключ никуда не передаётся и хранится только в виде хэша SHA-256.
      </div>
    </div>
  </div>

  <div id="page-ai" class="page">
    <h2>🤖 ИИ-ассистент</h2>

    <div class="card">
      <div class="card-header"><span class="card-icon">⚙️</span><span class="card-title">Режим работы</span></div>
      <select id="ai-mode">
        <option value="0">🔑 Свой API-ключ OpenRouter</option>
        <option value="1">🪙 Токены (Личный Кабинет Storm)</option>
        <option value="2">🇷🇺 GigaChat (свой ключ)</option>
      </select>
    </div>

    <div class="card" id="ai-key-section">
      <div class="card-header"><span class="card-icon">🔑</span><span class="card-title">API-ключ OpenRouter</span></div>
      <div class="input-wrap">
        <input type="password" id="ai-key" placeholder="sk-or-...">
        <button type="button" class="eye-btn" onclick="toggleReveal('ai-key', this)">👁</button>
      </div>
      <p class="hint">Получить ключ: openrouter.ai/keys</p>
      <div class="btn-row" style="margin-top:8px;align-items:center">
        <button class="btn" id="test-openrouter-btn" onclick="testAiKey('openrouter')">🔌 Проверить соединение</button>
        <span class="test-result" id="test-openrouter-result"></span>
      </div>

      <div class="card-header" style="margin-top:18px"><span class="card-icon">👀</span><span class="card-title" style="font-size:13px">Vision-модель (просмотр скриншотов)</span></div>
      <input type="text" id="vision-model" placeholder="например: google/gemini-2.0-flash-001">
      <p class="hint">Необязательно. Используется как крайний способ — только когда ИИ не может разобраться в странице по тексту, он один раз посмотрит на её снимок. Укажите любую модель с поддержкой изображений из каталога openrouter.ai/models. Пустое поле — функция просто отключена.</p>
    </div>

    <div class="card" id="cabinet-section">
      <div class="card-desc" style="margin-bottom:0">
        Обратите внимание: анализ страницы и поиск нужной информации на сайте работают
        только с собственным ключом. При использовании токенов Личного
        кабинета эта возможность не активируется.
      </div>
      <div class="card-desc" style="margin-top:10px; margin-bottom:0">
        Учтите: токены Личного кабинета пока работают только для текстового чата с
        Ассистентом. Генерация картинок через них сейчас в разработке и недоступна —
        для неё нужен собственный ключ (OpenRouter или GigaChat). Подробнее — в справочнике браузера.
      </div>
    </div>

    <div class="card" id="giga-section">
      <div class="card-header"><span class="card-icon">🔑</span><span class="card-title">Ключ авторизации GigaChat</span></div>
      <div class="input-wrap">
        <input type="password" id="giga-key" placeholder="Ключ авторизации (Base64 из личного кабинета)">
        <button type="button" class="eye-btn" onclick="toggleReveal('giga-key', this)">👁</button>
      </div>
      <p class="hint">Получить ключ: developers.sber.ru/studio (раздел GigaChat API)</p>
      <div class="btn-row" style="margin-top:8px;align-items:center">
        <button class="btn" id="test-giga-btn" onclick="testAiKey('gigachat')">🔌 Проверить соединение</button>
        <span class="test-result" id="test-giga-result"></span>
      </div>

      <div class="card-header" style="margin-top:18px"><span class="card-icon">🧠</span><span class="card-title" style="font-size:13px">Модель GigaChat</span></div>
      <select id="giga-model">
        <option value="GigaChat-2">GigaChat-2 — быстрая, для простых задач</option>
        <option value="GigaChat-2-Pro">GigaChat-2-Pro — сложные инструкции, тексты</option>
        <option value="GigaChat-2-Max">GigaChat-2-Max — максимальное качество</option>
        <option value="GigaChat-3-Ultra">GigaChat-3-Ultra — новая модель (freemium)</option>
      </select>
      <p class="hint">Для работы GigaChat браузер обходит проверку сертификата Минцифры на запросах к серверам GigaChat — это особенность российского API, других провайдеров ИИ не касается.</p>

      <div class="card-header" style="margin-top:18px"><span class="card-icon">👀</span><span class="card-title" style="font-size:13px">Vision-модель (просмотр скриншотов)</span></div>
      <select id="giga-vision-model">
        <option value="GigaChat-2-Max">GigaChat-2-Max — рекомендуется</option>
        <option value="GigaChat-2-Pro">GigaChat-2-Pro</option>
      </select>
      <p class="hint">Необязательно. Как и у OpenRouter — крайний способ, когда ИИ не может разобраться в странице по тексту. Обычная модель GigaChat-2 картинки не понимает, поэтому список отдельный и короче.</p>

      <div class="card-header" style="margin-top:18px"><span class="card-icon">📊</span><span class="card-title" style="font-size:13px">Использовано токенов</span></div>
      <p class="hint" style="font-size:14px;color:#e6e6e6">
        Всего потрачено токенов GigaChat: <b id="giga-tokens-used">0</b>
      </p>
      <p class="hint" style="font-size:11.5px">
        По данным официальной документации GigaChat API (тарифы, developers.sber.ru),
        один токен — это в среднем 3–4 символа, включая пробелы и знаки препинания.
        Токены запроса и ответа считаются вместе.
      </p>
      <button class="btn" onclick="resetGigaTokens()" style="margin-top:10px">🔄 Сбросить счётчик</button>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">🔬</span><span class="card-title">Поиск источников (Tavily)</span></div>
      <div class="card-desc">
        Один ключ используется в двух местах: в модуле «🔬 Глубокое исследование» боковой панели
        и у ИИ-ассистента «🤖 Storm AI» — он теперь тоже умеет искать актуальную информацию в
        интернете (новости, курсы, факты и т.п.), а не только отвечать по памяти или работать
        с открытой страницей. Не связан с режимом ИИ-чата выше — работает при любом выбранном режиме.
      </div>
      <div class="input-wrap">
        <input type="password" id="tavily-key" placeholder="tvly-...">
        <button type="button" class="eye-btn" onclick="toggleReveal('tavily-key', this)">👁</button>
      </div>
      <p class="hint">
        Получить ключ: tavily.com — бесплатно, без банковской карты, лимит
        1000 запросов в месяц (автоматически обновляется каждый месяц).
        Поле пустое — модуль «Глубокое исследование» ищет через DuckDuckGo без ключа (работает
        всегда, но чтение страниц менее аккуратное, чем у Tavily), а поиск в интернете у
        «🤖 Storm AI» просто не будет доступен — ассистент продолжит отвечать по своим знаниям
        и по открытой странице, как раньше.
      </p>
    </div>

    <div class="save-bar">
      <button class="btn primary" onclick="saveAiSettings()">💾 Сохранить</button>
      <span class="save-notice" id="ai-saved">✓ Сохранено</span>
    </div>
  </div>

  <div id="page-help" class="page">
    <h2>ℹ️ Справка</h2>

    <div class="card">
      <div class="card-header"><span class="card-icon">ℹ️</span><span class="card-title">Справка Storm Browser</span></div>
      <button class="btn" onclick="call('showHelp')">Открыть справку</button>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">🔄</span><span class="card-title">Обновления</span></div>
      <button class="btn" onclick="call('checkUpdates')">Проверить обновления</button>
    </div>

    <div class="card">
      <div class="card-header"><span class="card-icon">📄</span><span class="card-title">Диагностика</span></div>
      <button class="btn" onclick="call('openLogs')">Открыть папку с логами</button>
    </div>
  </div>

</main>

<script>
// ==========================================================
// Мост QWebChannel
// ==========================================================
var bridge = null;

// ВАЖНО: один QWebChannel на один transport. Раньше initSettingsBridge()
// вызывался ДВАЖДЫ (сразу и повторно на DOMContentLoaded) — если оба
// вызова успевали застать qt.webChannelTransport готовым (обычный случай,
// т.к. скрипт лежит в конце body), создавались ДВА независимых QWebChannel
// поверх одного и того же transport, и они перебивали друг друга при
// обработке сообщений — из-за этого мост мог в любой момент перестать
// отвечать (та же болезнь, что была на storm://home и storm://cloud).
// Плюс раньше не было повтора, если qt.webChannelTransport ещё не готов в
// момент вызова — теперь есть.
var settingsWebChannelStarted = false;
function initSettingsBridge() {
    if (settingsWebChannelStarted) return;
    if (typeof QWebChannel === 'undefined' || typeof qt === 'undefined' || !qt.webChannelTransport) {
        console.warn('[StormSettings] qt.webChannelTransport ещё не готов, жду...');
        setTimeout(initSettingsBridge, 200);
        return;
    }
    settingsWebChannelStarted = true;
    console.warn('[StormSettings] qt.webChannelTransport найден, создаём QWebChannel...');

    new QWebChannel(qt.webChannelTransport, function(channel) {
        bridge = channel.objects.settingsBridge;
        if (!bridge) {
            console.warn('[StormSettings] ОШИБКА: channel.objects.settingsBridge не найден!');
            return;
        }
        console.warn('[StormSettings] settingsBridge подключён успешно');
        document.getElementById('bridge-warning').style.display = 'none';

        // Результат проверки AI-ключа приходит асинхронно через сигнал, а не
        // как возврат значения из testAiConnection() — подписываемся один раз.
        if (bridge.aiConnectionTested && typeof bridge.aiConnectionTested.connect === 'function') {
            bridge.aiConnectionTested.connect(showAiTestResult);
        }

        // Подтягиваем актуальное состояние сразу же, как только мост готов —
        // на случай если placeholder-значения из C++ успели устареть между
        // генерацией HTML и реальной загрузкой страницы.
        refreshFromBridge();
    });
}
initSettingsBridge();

// Универсальный вызов метода моста по имени
function call(name) {
    var args = Array.prototype.slice.call(arguments, 1);
    if (!bridge) {
        document.getElementById('bridge-warning').style.display = 'block';
        initSettingsBridge();
        return;
    }
    if (typeof bridge[name] === 'function') {
        bridge[name].apply(bridge, args);
    }
}

function callAndRefreshZoom(name) {
    call(name);
    if (!bridge) return;
    // Небольшая задержка, чтобы C++ успел применить новый zoomFactor к
    // текущей вкладке до того, как мы запросим актуальное значение обратно.
    setTimeout(function() {
        bridge.getCurrentZoom(function(result) {
            var el = document.getElementById('zoom-val');
            if (el) el.textContent = result;
        });
    }, 50);
}

function showPage(id, navEl) {
    document.querySelectorAll('.page').forEach(function(p) { p.classList.remove('active'); });
    document.querySelectorAll('.nav-item').forEach(function(n) { n.classList.remove('active'); });
    document.getElementById('page-' + id).classList.add('active');
    navEl.classList.add('active');
}

function onThemeClick(btn) {
    document.querySelectorAll('.theme-btn').forEach(function(b) { b.classList.remove('active'); });
    btn.classList.add('active');
    call('applyTheme', btn.dataset.theme);
}

function saveAiSettings() {
    var mode      = document.getElementById('ai-mode').value;
    var key       = document.getElementById('ai-key').value;
    var gigaKey   = document.getElementById('giga-key').value;
    var gigaModel = document.getElementById('giga-model').value;
    var visionModel = document.getElementById('vision-model').value;
    var gigaVisionModel = document.getElementById('giga-vision-model').value;
    var tavilyKey = document.getElementById('tavily-key').value;
    // Точечные ключи для картинок/видео убрали из интерфейса — картинки теперь
    // всегда идут через основной ключ (OpenRouter/GigaChat). Пустые строки здесь
    // сохраняют старую сигнатуру saveAI() на бэкенде без изменений.
    var imageKey  = '';
    var videoKey  = '';

    call('saveAI', mode, key, gigaKey, gigaModel, visionModel, gigaVisionModel, imageKey, videoKey, tavilyKey);
    var notice = document.getElementById('ai-saved');
    notice.style.display = 'inline';
    setTimeout(function() { notice.style.display = 'none'; }, 2000);
}

// Сброс локального счётчика потраченных токенов GigaChat. Подтверждение —
// чтобы случайный клик не обнулил накопленную статистику. Обновляем цифру
// в интерфейсе сразу же (оптимистично), не дожидаясь перезагрузки страницы —
// C++-часть (SettingsBridge::resetGigaTokenCounter) обнуляет значение в
// QSettings, откуда оно будет заново прочитано при следующем открытии
// страницы настроек.
function resetGigaTokens() {
    if (!confirm('Сбросить счётчик потраченных токенов GigaChat в 0?')) return;
    call('resetGigaTokenCounter');
    document.getElementById('giga-tokens-used').textContent = '0';
}

// Показываем нужную секцию (ключ OpenRouter / ключ и модель GigaChat) в
// зависимости от выбранного режима работы ИИ. Вызывается и при переключении
// селекта, и один раз при загрузке страницы — чтобы сразу показать то, что
// реально сохранено в настройках, а не всегда секцию OpenRouter по умолчанию.
function updateAiSections() {
    var mode = document.getElementById('ai-mode').value;
    document.getElementById('ai-key-section').style.display = (mode === '0') ? 'block' : 'none';
    document.getElementById('cabinet-section').style.display = (mode === '1') ? 'block' : 'none';
    document.getElementById('giga-section').style.display   = (mode === '2') ? 'block' : 'none';
}

document.getElementById('ai-mode').addEventListener('change', updateAiSections);

// Показать/скрыть содержимое поля пароля/API-ключа по клику на иконку глаза
function toggleReveal(inputId, btn) {
    var el = document.getElementById(inputId);
    if (el.type === 'password') { el.type = 'text'; btn.textContent = '🙈'; }
    else { el.type = 'password'; btn.textContent = '👁'; }
}

// Выставляет текст/цвет бейджа "Активен/Отключен" напрямую, без обращения к
// чекбоксу — используется и из syncPill() (на клик пользователя), и из
// applySnapshot() (когда состояние подтянули с бэкенда, а не переключили руками).
function updatePill(pillId, isOn) {
    var pill = document.getElementById(pillId);
    if (!pill) return;
    pill.textContent = isOn ? 'Активен' : 'Отключен';
    pill.className = 'pill ' + (isOn ? 'on' : 'off');
}

// Вешает бейдж на чекбокс: обновляется и по клику пользователя, и один раз
// сразу при загрузке страницы.
function syncPill(checkboxId, pillId) {
    var chk = document.getElementById(checkboxId);
    if (!chk) return;
    chk.addEventListener('change', function() { updatePill(pillId, chk.checked); });
    updatePill(pillId, chk.checked);
}

// --- Живая синхронизация состояния тумблеров с C++ (SettingsBridge::getSettingsSnapshotJson) ---
// Нужна на случай, если состояние поменялось не через эту же страницу — например,
// открыты две вкладки настроек одновременно, или тумблер переключили каким-то
// другим путём в приложении. Настроек ИИ это не касается: они сохраняются только
// по явной кнопке "Сохранить", а не сразу при изменении поля.
var PILL_BY_CHECKBOX = { 'chk-shield': 'pill-shield', 'chk-gamemode': 'pill-gamemode', 'chk-hwaccel': 'pill-hwaccel' };

// Ставит чекбоксу значение из снапшота БЕЗ вызова его onchange — иначе мы бы
// тут же дёрнули call('toggleShield', ...) и т.п. повторно с тем же значением,
// которое и так уже актуально в QSettings (бессмысленный лишний вызов, а для
// Game Mode — потенциально не идемпотентный побочный эффект).
function setCheckedFromSnapshot(checkboxId, value) {
    var el = document.getElementById(checkboxId);
    if (!el || typeof value !== 'boolean' || el.checked === value) return;
    el.checked = value;
    var pillId = PILL_BY_CHECKBOX[checkboxId];
    if (pillId) updatePill(pillId, value);
}

function applySnapshot(json) {
    var data;
    try { data = JSON.parse(json); } catch (e) { return; }
    setCheckedFromSnapshot('chk-bmbar', data.bookmarksBar);
    setCheckedFromSnapshot('chk-shield', data.shield);
    setCheckedFromSnapshot('chk-gamemode', data.gameMode);
    setCheckedFromSnapshot('chk-minimize-tray', data.minimizeToTray);
    setCheckedFromSnapshot('chk-hwaccel', data.hwAccel);
    setCheckedFromSnapshot('chk-download-ask', data.downloadAskEachTime);
    if (typeof data.downloadDir === 'string' && data.downloadDir) {
        var dirLabel = document.getElementById('download-dir-label');
        if (dirLabel) dirLabel.textContent = data.downloadDir;
    }
}

function refreshFromBridge() {
    if (!bridge) return;
    if (typeof bridge.getSettingsSnapshotJson === 'function') {
        bridge.getSettingsSnapshotJson(function(json) { applySnapshot(json); });
    }
    loadShieldExceptions();
}

// Вкладка снова стала видимой (переключились на неё) или окно получило фокус —
// хороший момент подтянуть актуальное состояние.
document.addEventListener('visibilitychange', function() {
    if (document.visibilityState === 'visible') refreshFromBridge();
});
window.addEventListener('focus', refreshFromBridge);

// --- Исключения Storm Shield ---
function renderShieldExceptions(list) {
    var container = document.getElementById('shield-exceptions-list');
    if (!container) return;
    container.innerHTML = '';
    if (!list.length) {
        var empty = document.createElement('div');
        empty.className = 'exception-empty';
        empty.textContent = 'Список пуст.';
        container.appendChild(empty);
        return;
    }
    list.forEach(function(host) {
        var chip = document.createElement('div');
        chip.className = 'exception-chip';

        var span = document.createElement('span');
        span.textContent = host;

        var btn = document.createElement('button');
        btn.type = 'button';
        btn.textContent = '✕';
        btn.title = 'Удалить из исключений';
        btn.onclick = function() { removeShieldException(host); };

        chip.appendChild(span);
        chip.appendChild(btn);
        container.appendChild(chip);
    });
}

function loadShieldExceptions() {
    if (!bridge || typeof bridge.getShieldExceptionsJson !== 'function') return;
    bridge.getShieldExceptionsJson(function(json) {
        var list = [];
        try { list = JSON.parse(json) || []; } catch (e) {}
        renderShieldExceptions(list);
    });
}

function addShieldException() {
    var input = document.getElementById('shield-exception-input');
    var host = input.value.trim();
    if (!host) return;
    call('addShieldException', host);
    input.value = '';
    // call() не ждёт ответа — небольшая пауза, чтобы C++ успел применить
    // изменение до того, как мы перечитаем список (тот же приём, что и в
    // callAndRefreshZoom для масштаба).
    setTimeout(loadShieldExceptions, 60);
}

function removeShieldException(host) {
    call('removeShieldException', host);
    setTimeout(loadShieldExceptions, 60);
}

// --- Папка загрузок по умолчанию ---
function chooseDownloadFolder() {
    if (!bridge || typeof bridge.chooseDownloadFolder !== 'function') return;
    bridge.chooseDownloadFolder(function(dir) {
        if (dir) {
            var lbl = document.getElementById('download-dir-label');
            if (lbl) lbl.textContent = dir;
        }
    });
}

// --- Проверка соединения AI-ключей ---
function testAiKey(backend) {
    var inputId  = (backend === 'gigachat') ? 'giga-key' : 'ai-key';
    var resultId = (backend === 'gigachat') ? 'test-giga-result' : 'test-openrouter-result';
    var key = document.getElementById(inputId).value.trim();
    var resultEl = document.getElementById(resultId);
    if (!key) {
        resultEl.textContent = 'Сначала введите ключ';
        resultEl.className = 'test-result fail';
        return;
    }
    resultEl.textContent = 'Проверяем…';
    resultEl.className = 'test-result pending';
    call('testAiConnection', backend, key);
}

// Вызывается из подписки на сигнал bridge.aiConnectionTested (см. initSettingsBridge)
function showAiTestResult(backend, ok, message) {
    var resultId = (backend === 'gigachat') ? 'test-giga-result' : 'test-openrouter-result';
    var resultEl = document.getElementById(resultId);
    if (!resultEl) return;
    resultEl.textContent = (ok ? '✓ ' : '✗ ') + message;
    resultEl.className = 'test-result ' + (ok ? 'ok' : 'fail');
}

// --- Поиск по настройкам: фильтрует карточки на всех страницах и, если на
// текущей открытой странице совпадений нет, сам переключает на первую
// страницу, где они есть ---
var searchIndex = null;
function buildSearchIndex() {
    searchIndex = [];
    document.querySelectorAll('.page').forEach(function(page) {
        var pageId = page.id.replace('page-', '');
        page.querySelectorAll('.card').forEach(function(card) {
            searchIndex.push({ page: pageId, el: card, text: card.textContent.toLowerCase() });
        });
    });
}
function onSearchInput(query) {
    if (!searchIndex) buildSearchIndex();
    query = query.trim().toLowerCase();

    document.querySelectorAll('.card').forEach(function(c) { c.style.display = ''; });
    if (!query) return;

    var matchedPages = {};
    searchIndex.forEach(function(item) {
        var match = item.text.indexOf(query) !== -1;
        item.el.style.display = match ? '' : 'none';
        if (match) matchedPages[item.page] = true;
    });

    var activePage = document.querySelector('.page.active');
    var activeId = activePage ? activePage.id.replace('page-', '') : '';
    if (!matchedPages[activeId]) {
        var firstMatch = Object.keys(matchedPages)[0];
        if (firstMatch) {
            var navEl = document.querySelector('.nav-item[data-page="' + firstMatch + '"]');
            if (navEl) showPage(firstMatch, navEl);
        }
    }
}

// --- Проставляем начальное состояние всех полей из C++ ---
document.getElementById('lang-select').value   = "%CUR_LANG%";
document.getElementById('search-select').value = "%CUR_SEARCH%";
document.getElementById('chk-bmbar').checked    = %BMBAR_CHECKED%;
document.getElementById('chk-shield').checked   = %SHIELD_CHECKED%;
document.getElementById('chk-gamemode').checked = %GAMEMODE_CHECKED%;
document.getElementById('chk-minimize-tray').checked = %MINIMIZE_TRAY_CHECKED%;
document.getElementById('chk-hwaccel').checked  = %HWACCEL_CHECKED%;
document.getElementById('ai-mode').value        = "%AI_MODE%";
document.getElementById('ai-key').value         = "%AI_KEY%";
document.getElementById('giga-key').value       = "%GIGA_KEY%";
document.getElementById('giga-model').value     = "%GIGA_MODEL%";
document.getElementById('vision-model').value       = "%VISION_MODEL%";
document.getElementById('giga-vision-model').value  = "%GIGA_VISION_MODEL%";
document.getElementById('tavily-key').value          = "%TAVILY_KEY%";
document.getElementById('giga-tokens-used').textContent = "%GIGA_TOKENS_USED%";
document.getElementById('chk-download-ask').checked = %DOWNLOAD_ASK_CHECKED%;
updateAiSections();

syncPill('chk-shield', 'pill-shield');
syncPill('chk-gamemode', 'pill-gamemode');
syncPill('chk-hwaccel', 'pill-hwaccel');
// Список исключений Shield подтягивается не здесь (бридж в момент выполнения этого
// блока ещё не готов — соединение асинхронное), а из refreshFromBridge() ниже, которая
// вызывается сразу как только QWebChannel подключится, и повторно при каждом фокусе
// на вкладке.
</script>
</body>
</html>
)HTML")
.replace("%QWEBCHANNEL_JS%", qwebchannelJs)
.replace("%THEME_BUTTONS%", themeButtonsHtml)
.replace("%ZOOM_VAL%", zoomVal)
.replace("%CUR_LANG%", curLang)
.replace("%CUR_SEARCH%", curSearch)
.replace("%BMBAR_CHECKED%", bmBarOn ? "true" : "false")
.replace("%SHIELD_CHECKED%", shieldOn ? "true" : "false")
.replace("%GAMEMODE_CHECKED%", gameModeOn ? "true" : "false")
.replace("%MINIMIZE_TRAY_CHECKED%", minimizeToTrayOn ? "true" : "false")
.replace("%HWACCEL_CHECKED%", hwAccelOn ? "true" : "false")
.replace("%AI_MODE%", QString::number(aiMode))
.replace("%AI_KEY%", QString(aiKey).replace("\\", "\\\\").replace("\"", "\\\""))
.replace("%GIGA_KEY%", QString(gigaKey).replace("\\", "\\\\").replace("\"", "\\\""))
.replace("%DOWNLOAD_ASK_CHECKED%", downloadAskEachTime ? "true" : "false")
.replace("%DOWNLOAD_DIR%", htmlEscape(downloadDir))
.replace("%GIGA_MODEL%", gigaModel)
.replace("%VISION_MODEL%", QString(visionModel).replace("\\", "\\\\").replace("\"", "\\\""))
.replace("%GIGA_VISION_MODEL%", gigaVisionModel)
.replace("%TAVILY_KEY%", QString(tavilyKey).replace("\\", "\\\\").replace("\"", "\\\""))
.replace("%GIGA_TOKENS_USED%", gigaTokensUsedStr)
.replace("%GIGA_TOKENS_USED%", gigaTokensUsedStr);

    return html;
}