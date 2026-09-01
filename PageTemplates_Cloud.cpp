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
// PageTemplates_Cloud.cpp — storm://cloud, часть 1 из 2
// getStormCloudHtml() читает настройки синхронизации/премиума,
// собирает шаблон из двух частей (buildStormCloudHtmlPart1() здесь +
// buildStormCloudHtmlPart2() в PageTemplates_Cloud2.cpp) и подставляет
// значения через .replace(...) — точно как раньше, до разбиения на
// части. Сам текст шаблона НЕ менялся, только физически поделён
// пополам по границе строк, чтобы файл не был на 1600+ строк.
// ==========================================================================

QString PageTemplates::getStormCloudHtml() {
    QSettings s;
    bool alreadyLoggedIn = !s.value("sync/username", "").toString().isEmpty() &&
        !s.value("sync/password", "").toString().isEmpty();
    QString savedAvatar = s.value("sync/avatar", u8"👤").toString();
    QString premBg = s.value("premium_ui_bg", u8"Стандартный").toString();
    QString premFrame = s.value("premium_ui_frame", u8"Обычный круг").toString();
    QString premAvatar = s.value("premium_ui_avatar", u8"Своя/Стандартная").toString();
    bool autoSyncOn = s.value("sync/auto_enabled", "false").toString() == "true";

    QString qwebchannelJs = "";
    QFile qwcFile(":/qtwebchannel/qwebchannel.js");
    if (qwcFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qwebchannelJs = QTextStream(&qwcFile).readAll();
        qwcFile.close();
    }
    QString html = (buildStormCloudHtmlPart1() + buildStormCloudHtmlPart2())
        .replace("%QWEBCHANNEL_JS%", qwebchannelJs)
        .replace("%SAVED_USERNAME%", s.value("sync/username", "").toString())
        .replace("%ALREADY_LOGGED_IN%", alreadyLoggedIn ? "true" : "false")
        .replace("%SAVED_AVATAR_JSON%", QString("\"%1\"").arg(QString(savedAvatar).replace("\"", "\\\"")))
        .replace("%SAVED_BG_JSON%", QString("\"%1\"").arg(premBg))
        .replace("%SAVED_FRAME_JSON%", QString("\"%1\"").arg(premFrame))
        .replace("%SAVED_AVATARFX_JSON%", QString("\"%1\"").arg(premAvatar))
        .replace("%AUTOSYNC_JSON%", autoSyncOn ? "true" : "false")
        .replace("%SAVED_USERNAME_JSON%", QString("\"%1\"").arg(
            s.value("sync/username", u8"User").toString().replace("\"", "\\\"")));

    return html;
}



QString buildStormCloudHtmlPart1() {
    return QString(u8R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<title>Storm Cloud</title>
<script>
%QWEBCHANNEL_JS%
</script>
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body { background:#0f172a; color:#e2e8f0; font-family:-apple-system,'Segoe UI',sans-serif;
         min-height:100vh; padding:20px; }

  #cloud-container { max-width: 850px; margin: 0 auto; background:#0f172a;
                      border:1px solid #334155; border-radius:20px; padding:0;
                      transition: background 0.4s; }

  /* ---------- ЭКРАН ЛОГИНА ---------- */
  #screen-login { display:flex; justify-content:center; align-items:center; min-height:640px; }
  .login-card { width:400px; padding:40px 20px; display:flex; flex-direction:column; gap:15px; }
  .login-logo { font-size:50px; text-align:center; }
  .login-title { font-size:24px; font-weight:bold; color:#e2e8f0; text-align:center; }
  .login-input { padding:12px; border-radius:8px; background:#1e293b; border:1px solid #334155;
                 color:#e2e8f0; font-size:14px; outline:none; }
  .login-input:focus { border-color:#a371f7; }
  .btn-auth { padding:12px; background:#a371f7; border:none; border-radius:8px; font-weight:bold;
              color:#fff; font-size:14px; cursor:pointer; }
  .btn-auth:hover { background:#b180ff; }
  .mode-toggle { background:none; border:none; color:#cbd5e1; font-size:12px; cursor:pointer; text-align:center; }
  .login-error { color:#ff5f5f; font-size:13px; text-align:center; display:none; }

  /* ---------- ЭКРАН ЛК ---------- */
  :root {
    --sc-bg:#0f172a; --sc-card:#1a2436; --sc-card-2:#151e2e;
    --sc-border:#283449; --sc-border-strong:#3a4a66;
    --sc-text:#e7ecf5; --sc-text-2:#a7b1c4; --sc-text-3:#6c7890;
    --sc-purple:#a371f7; --sc-purple-2:#7c5cff; --sc-purple-soft:rgba(163,113,247,0.14);
    --sc-green:#4fd6a0; --sc-green-soft:rgba(79,214,160,0.12);
    --sc-blue:#4facfe; --sc-blue-soft:rgba(79,172,254,0.12);
    --sc-pink:#f472b6; --sc-pink-soft:rgba(244,114,182,0.10);
    --sc-amber:#f5a524; --sc-amber-soft:rgba(245,165,36,0.14);
    --sc-red:#ff6b6b;
  }
  #screen-manage { display:none; padding:22px 26px 26px; }
  #screen-manage button { font-family:inherit; }

  .header-row { display:flex; align-items:center; gap:16px; margin-bottom:20px; }
  #avatar-btn { width:64px; height:64px; border-radius:20px; border:none; cursor:pointer;
                background: linear-gradient(135deg, var(--sc-purple), var(--sc-purple-2)); color:#fff; font-size:28px;
                display:flex; align-items:center; justify-content:center; overflow:hidden; flex-shrink:0;
                box-shadow:0 6px 18px -6px rgba(163,113,247,0.5); }
  #avatar-btn img { width:100%; height:100%; object-fit:cover; }

  .info-col { display:flex; flex-direction:column; gap:5px; }
  #user-name-label { font-size:19px; font-weight:600; color:var(--sc-text); line-height:1; }
  .status-row { display:flex; align-items:center; gap:6px; font-size:12.5px; color:var(--sc-text-2); }
  #status-dot { color:gray; font-size:9px; }
  #last-sync-label { font-size:11.5px; color:var(--sc-text-3); }

  .spacer { flex:1; }

  #quick-stats-box { display:flex; gap:8px; align-items:center; }
  .pill { display:flex; align-items:center; gap:6px; background:var(--sc-card); border:1px solid var(--sc-border);
          border-radius:999px; padding:7px 14px; font-size:12.5px; font-weight:600; white-space:nowrap; }
  #lbl-sc { color:var(--sc-green); font-weight:700; }
  #lbl-ai { color:var(--sc-purple); font-weight:700; }
  .plan-pill { flex-direction:column; align-items:flex-start; gap:0; padding:6px 14px; }
  #lbl-prem { color:var(--sc-text-2); font-size:12px; font-weight:600; }
  .plan-pill.is-premium { border-color:rgba(255,215,0,0.35); }
  .plan-pill.is-premium #lbl-prem { color:#ffd700; }
  #lbl-prem-date { font-size:10px; color:var(--sc-text-3); display:none; }
  #prem-renew-btn { display:none; margin-top:3px; padding:2px 8px; border:none; border-radius:999px;
                     background:linear-gradient(135deg,var(--sc-purple),var(--sc-purple-2)); color:#fff;
                     font-size:10px; font-weight:700; cursor:pointer; font-family:inherit; }
  #prem-renew-btn:hover { filter:brightness(1.1); }

  #settings-gear { width:38px; height:38px; background:var(--sc-card); border:1px solid var(--sc-border); border-radius:12px; font-size:18px;
                   color:var(--sc-text-2); cursor:pointer; flex-shrink:0; }
  #settings-gear:hover { border-color:var(--sc-border-strong); color:var(--sc-text); }

  .metrics-row { display:grid; grid-template-columns:repeat(3,1fr); gap:10px; margin-bottom:16px; }
  .stat-card { background:var(--sc-card); border:1px solid var(--sc-border); border-radius:12px; padding:12px 14px; }
  .stat-card .t { color:var(--sc-text-3); font-size:11.5px; margin-bottom:4px; }
  .stat-card .v { color:var(--sc-text); font-size:20px; font-weight:700; }

  .columns { display:grid; grid-template-columns:1fr 1fr; gap:14px; align-items:start; }
  .col { display:flex; flex-direction:column; gap:14px; }

  .card { background:var(--sc-card); border:1px solid var(--sc-border); border-radius:18px; padding:16px;
          box-shadow:0 1px 0 rgba(255,255,255,0.02) inset, 0 8px 24px -12px rgba(0,0,0,0.5); }
  .card-accent { border-color:rgba(163,113,247,0.35); }
  .card-title { display:flex; align-items:center; gap:8px; font-size:13.5px; font-weight:600; color:var(--sc-text); margin-bottom:12px; }
  .card-title .ico { width:26px; height:26px; border-radius:8px; display:flex; align-items:center; justify-content:center; font-size:14px; flex-shrink:0; }
  .card-title .sub { margin-left:auto; font-size:11px; font-weight:500; color:var(--sc-text-3); }

  .btn { display:block; width:100%; padding:10px; border:none; border-radius:6px; font-weight:bold;
         font-size:13px; cursor:pointer; margin-top:8px; text-align:center; }
  .btn:first-of-type { margin-top:0; }
  .btn-purple { background:#a371f7; color:#fff; }
  .btn-purple:hover { background:#b180ff; }
  .btn-ghost { background:rgba(255,255,255,0.05); border:1px solid #334155; color:#e2e8f0; font-size:11px; padding:6px; }

  .action-btn { border:none; border-radius:10px; font-family:inherit; font-weight:600; cursor:pointer; font-size:13px;
                padding:10px 14px; width:100%; text-align:center; transition:filter .15s ease, color .15s ease, border-color .15s ease; }
  .action-primary { background:linear-gradient(135deg,var(--sc-purple),var(--sc-purple-2)); color:#fff;
                     box-shadow:0 4px 14px -4px rgba(163,113,247,0.5); }
  .action-primary:hover { filter:brightness(1.08); }
  .action-secondary { background:transparent; border:1px solid var(--sc-border-strong); color:var(--sc-text-2); margin-top:8px; }
  .action-secondary:hover { color:var(--sc-text); border-color:var(--sc-text-2); }
  .action-text { background:none; border:none; color:var(--sc-text-3); font-weight:500; font-size:12px; padding:6px 4px; width:auto; text-align:left; }
  .action-text:hover { color:var(--sc-red); }

  .ref-card { background:var(--sc-purple-soft); border:1px dashed rgba(163,113,247,0.45); border-radius:18px; padding:16px; }
  .ref-top { margin-bottom:8px; }
  #ref-header { color:var(--sc-purple); font-size:13.5px; font-weight:600; }
  .ref-code-row { display:flex; align-items:center; gap:8px; background:var(--sc-bg); border:1px solid var(--sc-border); border-radius:9px; padding:9px 12px; }
  #ref-code { color:var(--sc-text); font-size:13px; font-weight:600; letter-spacing:.4px; flex:1; user-select:text; }

  .vpn-box { display:flex; flex-direction:column; }
  .vpn-status-row { margin-bottom:8px; }
  #vpn-status { color:var(--sc-text-2); font-size:12.5px; }
  #vpn-traffic { color:var(--sc-text-3); font-size:11.5px; margin-bottom:6px; }
  #vpn-progress-track { height:6px; background:var(--sc-card-2); border-radius:3px; overflow:hidden; margin-bottom:12px; }
  #vpn-progress-fill { height:100%; width:0%; background:linear-gradient(90deg,var(--sc-green),#3fc796); border-radius:3px; transition:width .3s; }
  .vpn-fraud-note { font-size:10px; color:var(--sc-text-3); margin-top:8px; }
  #btn-buy-vpn { background:transparent; border:1px solid var(--sc-border-strong); color:var(--sc-text-2); margin-top:auto; }
  #btn-buy-vpn:hover { color:var(--sc-text); border-color:var(--sc-text-2); }

  .account-row { display:flex; align-items:center; gap:10px; padding:9px 0; border-bottom:1px solid var(--sc-border); }
  .account-row:last-child { border-bottom:none; }
  .account-ico { width:30px; height:30px; border-radius:9px; display:flex; align-items:center; justify-content:center; font-size:14px; flex-shrink:0; }
  .account-body { flex:1; min-width:0; }
  .account-label { font-size:11.5px; color:var(--sc-text-3); }
  .account-value { font-size:12.5px; color:var(--sc-text); font-weight:500; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }

  #btn-vk { background:transparent; border:1px solid var(--sc-border-strong); color:var(--sc-text-2); width:auto; margin:0;
            padding:7px 12px; font-size:12px; font-family:inherit; font-weight:600; border-radius:8px; cursor:pointer; flex-shrink:0; }
  #btn-vk:hover { color:var(--sc-text); border-color:var(--sc-text-2); }

  .beta-row { display:flex; gap:8px; }
  #beta-btn { background:var(--sc-green-soft); color:var(--sc-green); border:1px solid rgba(79,214,160,0.35);
              flex:1; min-height:40px; margin:0; border-radius:10px; font-family:inherit; font-weight:600;
              font-size:12.5px; cursor:pointer; padding:8px 10px; text-align:center; }
  #beta-btn:hover { background:rgba(79,214,160,0.2); }
  #beta-btn:disabled { opacity:0.6; cursor:default; }
  #info-btn { width:40px; height:40px; border-radius:12px; background:var(--sc-card-2); color:var(--sc-purple);
              border:1px solid var(--sc-border); font-weight:bold; font-size:15px; margin:0; flex-shrink:0;
              font-family:inherit; cursor:pointer; }
  #info-btn:hover { color:#fff; border-color:var(--sc-purple); }

  #auto-sync-btn { display:flex; align-items:center; justify-content:space-between; padding:10px 12px; border-radius:10px;
                   border:1px solid var(--sc-border-strong); background:transparent; color:var(--sc-text-2); font-size:12.5px; font-weight:600; cursor:pointer; width:100%; text-align:left; }
  #auto-sync-btn .as-state { font-size:11px; padding:2px 8px; border-radius:999px; background:var(--sc-card-2); color:var(--sc-text-3); }
  #auto-sync-btn.active { border-color:rgba(163,113,247,0.45); color:var(--sc-purple); background:var(--sc-purple-soft); }
  #auto-sync-btn.active .as-state { background:rgba(163,113,247,0.2); color:var(--sc-purple); }
  #sync-now-btn { background:linear-gradient(135deg,var(--sc-purple),var(--sc-purple-2)); color:#fff; border-radius:10px;
                  box-shadow:0 4px 14px -4px rgba(163,113,247,0.5); }
  #sync-now-btn:hover { filter:brightness(1.08); }
  #logout-btn { color:var(--sc-text-3); margin-top:2px; }
  #logout-btn:hover { color:var(--sc-red); }

  /* Друзья */
  .friends-card { grid-row: span 2; }
  .friends-head { display:flex; align-items:center; gap:8px; margin-bottom:12px; }
  .friends-count { font-size:11px; font-weight:600; color:var(--sc-pink); background:var(--sc-pink-soft); padding:2px 8px; border-radius:999px; margin-left:6px; }
  .add-row { display:flex; gap:6px; margin-bottom:10px; }
  .add-row input { flex:1; background:var(--sc-card-2); border:1px solid var(--sc-border); color:var(--sc-text);
                    border-radius:9px; padding:9px 11px; font-size:12.5px; font-family:inherit; outline:none; }
  .add-row input:focus { border-color:var(--sc-purple); }
  .add-row button { width:38px; border-radius:9px; background:var(--sc-purple-soft); border:1px solid rgba(163,113,247,0.4);
                     color:var(--sc-purple); font-size:15px; cursor:pointer; margin:0; padding:0; }
  .search-row { position:relative; margin-bottom:10px; }
  .search-row input { width:100%; background:var(--sc-card-2); border:1px solid var(--sc-border); color:var(--sc-text);
                       border-radius:9px; padding:9px 11px 9px 32px; font-size:12.5px; font-family:inherit; outline:none; }
  .search-row input:focus { border-color:var(--sc-border-strong); }
  .search-row .ico { position:absolute; left:11px; top:50%; transform:translateY(-50%); color:var(--sc-text-3); font-size:13px; }
  .filter-pills { display:flex; gap:6px; margin-bottom:10px; flex-wrap:wrap; }
  .filter-pill { font-size:11px; font-weight:600; padding:6px 10px; border-radius:999px; border:1px solid var(--sc-border);
                 background:transparent; color:var(--sc-text-3); cursor:pointer; white-space:nowrap; }
  .filter-pill.active { background:var(--sc-pink-soft); border-color:rgba(244,114,182,0.4); color:var(--sc-pink); }
  #friends-list { max-height:230px; overflow-y:auto; display:flex; flex-direction:column; gap:2px; margin-bottom:8px; }
  #friends-list::-webkit-scrollbar { width:6px; }
  #friends-list::-webkit-scrollbar-thumb { background:var(--sc-border-strong); border-radius:3px; }
  .frow { display:flex; align-items:center; gap:10px; padding:7px 6px; border-radius:9px; }
  .frow:hover { background:var(--sc-card-2); }
  .favatar { width:30px; height:30px; border-radius:9px; display:flex; align-items:center; justify-content:center;
             font-size:12px; font-weight:700; color:#fff; flex-shrink:0; }
  .fbody { flex:1; min-width:0; }
  .fname { font-size:12.5px; color:var(--sc-text); font-weight:500; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
  .ftag { font-size:10.5px; color:var(--sc-text-3); }
  .fbtn { width:26px; height:26px; border-radius:8px; border:1px solid var(--sc-border); background:transparent;
          font-size:12px; cursor:pointer; flex-shrink:0; }
  .fbtn.remove { color:var(--sc-text-3); }
  .fbtn.remove:hover { color:var(--sc-red); border-color:rgba(255,107,107,0.4); }
  .fbtn.add { color:var(--sc-green); border-color:rgba(79,214,160,0.35); }
  .fbtn.add:hover { background:var(--sc-green-soft); }
  .fbtn.gift { color:#ffd700; border-color:rgba(255,215,0,0.35); }
  .fbtn.gift:hover { background:rgba(255,215,0,0.14); }
  .fcrown { margin-left:4px; font-size:10px; }
  .load-more { width:100%; padding:8px; border-radius:9px; border:1px dashed var(--sc-border-strong); background:transparent;
               color:var(--sc-text-2); font-size:12px; font-weight:600; cursor:pointer; }
  .load-more:hover { color:var(--sc-text); border-color:var(--sc-text-2); }
  .friends-empty { text-align:center; color:var(--sc-text-3); font-size:12px; padding:24px 10px; }

  .bottom-row { display:flex; justify-content:space-between; align-items:center; margin-top:16px; }
  #clear-cloud-btn { background:none; border:none; color:var(--sc-text-3); font-size:12px; cursor:pointer; }
  #clear-cloud-btn:hover { color:var(--sc-red); }

  /* ---------- МОДАЛЬНЫЕ ОКНА ---------- */
  .modal-overlay { display:none; position:fixed; inset:0; background:rgba(0,0,0,0.6);
                   align-items:center; justify-content:center; z-index:1000; }
  .modal-overlay.open { display:flex; }
  .modal-box { background:#0f172a; border:1px solid #334155; border-radius:14px; padding:22px;
               max-width:90vw; max-height:85vh; overflow-y:auto; }
  .modal-title { font-size:18px; font-weight:bold; margin-bottom:16px; text-align:center; }
  .modal-close-btn { display:block; width:100%; padding:10px; margin-top:16px; background:#1e293b;
                     color:#e2e8f0; border:1px solid #334155; border-radius:6px; cursor:pointer; }
  .modal-close-btn:hover { background:rgba(255,95,95,0.15); color:#ff5f5f; border-color:#ff5f5f; }

  /* Аватар-пикер */
  #avatar-modal .modal-box { width:420px; }
  .avatar-grid { display:grid; grid-template-columns:repeat(5,1fr); gap:8px; margin-bottom:14px; }
  .avatar-opt { font-size:26px; background:rgba(255,255,255,0.05); border:2px solid transparent;
                border-radius:10px; padding:10px 0; cursor:pointer; text-align:center; }
  .avatar-opt:hover { background:rgba(255,255,255,0.15); border-color:#a371f7; }

  /* Хаб настроек (шестерёнка) */
  #settings-modal .modal-box { width:500px; }
  .settings-tabs { display:flex; gap:6px; margin-bottom:14px; border-bottom:1px solid #334155; padding-bottom:10px; flex-wrap:wrap; }
  .settings-tab-btn { flex:1; min-width:100px; padding:8px 6px; border:none; border-radius:6px; background:#1e293b; color:#94a3b8;
                       font-weight:bold; font-size:11.5px; cursor:pointer; font-family:inherit; }
  .settings-tab-btn.active { background:#334155; color:#f8fafc; }
  .settings-tab-panel { max-height:400px; overflow-y:auto; }
  .settings-row-label { font-size:12px; font-weight:bold; color:#cbd5e1; margin:0 0 8px; }
  .settings-row-label:not(:first-child) { margin-top:18px; padding-top:14px; border-top:1px solid #283449; }
  .settings-hint { font-size:11px; color:#6c7890; margin-top:8px; line-height:1.5; }

  .pref-toggle { display:flex; align-items:center; justify-content:space-between; width:100%; padding:10px 12px;
                 border-radius:10px; border:1px solid #3a4a66; background:transparent; color:#a7b1c4; font-size:12.5px;
                 font-weight:600; cursor:pointer; font-family:inherit; text-align:left; margin-top:8px; }
  .pref-toggle:first-of-type { margin-top:0; }
  .pref-toggle .as-state { font-size:11px; padding:2px 8px; border-radius:999px; background:#151e2e; color:#6c7890; }
  .pref-toggle.active { border-color:rgba(163,113,247,0.45); color:#a371f7; background:rgba(163,113,247,0.14); }
  .pref-toggle.active .as-state { background:rgba(163,113,247,0.2); color:#a371f7; }

  .device-row { display:flex; align-items:center; justify-content:space-between; gap:10px; padding:9px 0; border-bottom:1px solid #283449; }
  .device-row:last-child { border-bottom:none; }
  .device-main { font-size:12.5px; color:#e7ecf5; font-weight:500; }
  .device-meta { font-size:11px; color:#6c7890; margin-top:2px; }
  .device-flag-btn { background:transparent; border:1px solid #3a4a66; color:#a7b1c4; border-radius:8px; padding:6px 10px;
                      font-size:11px; font-weight:600; cursor:pointer; font-family:inherit; flex-shrink:0; }
  .device-flag-btn:hover { color:#ff6b6b; border-color:rgba(255,107,107,0.4); }

  /* Оформление (внутри хаба, доступно только на Premium) */
  .premium-locked { text-align:center; color:#fbbf24; font-size:13px; padding:20px 10px; }
  .theme-select-row { margin-bottom:14px; }
  .theme-select-row label { display:block; font-size:12px; color:#cbd5e1; margin-bottom:5px; }
  .theme-select-row select { width:100%; padding:9px; background:#1e293b; color:#f8fafc;
                             border:1px solid #475569; border-radius:6px; }

  /* Магазин */
  #store-modal .modal-box { width:640px; }
  .store-header { display:flex; justify-content:space-between; margin-bottom:14px; }
  .store-tabs { display:flex; gap:6px; margin-bottom:14px; border-bottom:1px solid #334155; padding-bottom:10px; }
  .store-tab-btn { flex:1; padding:8px; border:none; border-radius:6px; background:#1e293b; color:#94a3b8;
                    font-weight:bold; font-size:12px; cursor:pointer; }
  .store-tab-btn.active { background:#334155; color:#f8fafc; }
  .store-grid { display:grid; grid-template-columns:1fr 1fr; gap:14px; margin-bottom:16px; }
  .product-card { position:relative; border-radius:10px; padding:12px; border:1px solid #334155; background:#1e293b; }
  .product-card b { font-size:15px; }
  .product-card p { color:#cbd5e1; font-size:12px; margin:6px 0 6px; }
  .price-per-unit { color:#64748b; font-size:11px; margin-bottom:10px; }
  .badge-best { position:absolute; top:-9px; right:10px; background:#f59e0b; color:#1e293b;
                font-size:10px; font-weight:bold; padding:2px 8px; border-radius:10px; }
  .product-card button { width:100%; padding:8px; border:none; border-radius:6px; color:#fff;
                          font-weight:bold; cursor:pointer; }
  .product-card button.cant-afford, .prem-tier-row button.cant-afford { opacity:.5; }
  .afford-note { font-size:11px; color:#f87171; margin-top:5px; display:none; text-align:center; }
  .prem-tier-row { display:flex; gap:8px; }
  .prem-tier-row button { flex:1; padding:8px; border:none; border-radius:6px; color:#fff;
                          font-weight:bold; cursor:pointer; white-space:pre-line; font-size:12px; }
  .premium-active-box { background:#1e293b; border:1px solid #fbbf2455; border-radius:10px;
                         padding:12px; margin-bottom:14px; text-align:center; }

  /* Кошелёк */
  #wallet-modal .modal-box { width:360px; text-align:center; }
  .purchase-history-list { max-height:150px; overflow-y:auto; font-size:11px; text-align:left; }

  .toast { position:fixed; bottom:24px; left:50%; transform:translateX(-50%); background:#1e293b;
           border:1px solid #334155; border-radius:8px; padding:12px 20px; font-size:13px;
           z-index:2000; display:none; max-width:400px; text-align:center; }
</style>
</head>
<body>

<div id="cloud-container">

  <!-- ================= ЭКРАН ЛОГИНА ================= -->
  <div id="screen-login">
    <div class="login-card">
      <div class="login-logo">⛈️</div>
      <div class="login-title">Storm Cloud</div>
      <input class="login-input" id="login-username" placeholder="Логин" value="%SAVED_USERNAME%">
      <input class="login-input" id="login-password" type="password" placeholder="Пароль">
      <input class="login-input" id="login-email" placeholder="Email (для восстановления пароля)" style="display:none">
      <input class="login-input" id="login-invite" placeholder="Код приглашения (Даст +200 SC)" style="display:none">
      <div class="login-error" id="login-error"></div>
      <button class="btn-auth" id="btn-auth" onclick="handleAuth()">Войти в аккаунт</button>
      <button class="mode-toggle" onclick="toggleMode()" id="mode-toggle">Нет аккаунта? Создать</button>
      <button class="mode-toggle" onclick="openForgotPassword()" id="forgot-password-link" style="margin-top:4px;">Забыли пароль?</button>

      <div id="forgot-password-box" style="display:none; margin-top:10px; border-top:1px solid #ffffff1a; padding-top:10px;">
        <input class="login-input" id="forgot-input" placeholder="Логин или email">
        <button class="btn-auth" style="margin-top:6px;" onclick="doForgotPassword()">Отправить временный пароль</button>
        <button class="mode-toggle" onclick="closeForgotPassword()">Отмена</button>
        <div id="forgot-message" style="font-size:12px; color:#94a3b8; margin-top:6px; text-align:center;"></div>
      </div>
    </div>
  </div>

  <!-- ================= ЭКРАН ЛК ================= -->
  <div id="screen-manage">

    <div class="header-row">
      <button id="avatar-btn" onclick="openModal('avatar-modal')">👤</button>
      <div class="info-col">
        <div id="user-name-label">User</div>
        <div class="status-row"><span id="status-dot">●</span><span id="status-text">Подключение...</span></div>
        <div id="last-sync-label">Синхронизация: —</div>
      </div>
      <div class="spacer"></div>
      <div id="quick-stats-box">
        <span class="pill" id="lbl-sc">0 SC</span>
        <span class="pill" id="lbl-ai">🧠 0 Токенов</span>
        <span class="pill plan-pill" id="prem-pill"><span id="lbl-prem">Стандарт</span><span id="lbl-prem-date"></span><button id="prem-renew-btn" onclick="openStoreForRenewal()" style="display:none;">Продлить</button></span>
      </div>
      <button id="settings-gear" onclick="openSettingsModal()">⚙️</button>
    </div>

    <div class="metrics-row">
      <div class="stat-card"><div class="t">📜 История</div><div class="v">0</div></div>
      <div class="stat-card"><div class="t">🔑 Пароли</div><div class="v">0</div></div>
      <div class="stat-card"><div class="t">⭐ Закладки</div><div class="v">0</div></div>
    </div>

    <div class="columns">
      <div class="col">

        <div class="card card-accent">
          <div class="card-title"><span class="ico" style="background:var(--sc-purple-soft);">🛒</span>Магазин Storm Cloud</div>
          <button class="action-btn action-primary" onclick="openStoreNormal()">🛍️ Открыть витрину</button>
          <button class="action-btn action-secondary" onclick="openModal('wallet-modal')">💳 Мой кошелёк</button>
        </div>

        <div class="ref-card">
          <div class="ref-top"><div id="ref-header">🎁 Пригласи друга (Осталось: Загрузка...)</div></div>
          <div class="ref-code-row"><div id="ref-code">Твой код: Загрузка...</div></div>
        </div>

        <div class="card vpn-box">
          <div class="card-title"><span class="ico" style="background:var(--sc-green-soft);">🛡️</span>Storm VPN <span class="sub">Beta</span></div>
          <div class="vpn-status-row"><span id="vpn-status">Статус: Загрузка...</span></div>
          <div id="vpn-traffic">Трафик: 0 МБ / 5000 МБ</div>
          <div id="vpn-progress-track"><div id="vpn-progress-fill"></div></div>
          <button class="btn" id="btn-buy-vpn" onclick="handleVpnClick()">Активировать VPN (390 SC)</button>
          <div class="vpn-fraud-note">* Обновление пула адресов происходит раз в неделю.</div>
        </div>

      </div>

      <div class="col">

        <div class="card" id="account-box">
          <div class="card-title">📢 Сообщество</div>
          <div class="account-row" style="border-bottom:none; padding-bottom:0;">
            <span class="account-ico" style="background:var(--sc-blue-soft);">📢</span>
            <div class="account-body">
              <div class="account-label">Сообщество Storm Browser</div>
              <div class="account-value">Мы ВКонтакте — новости и обновления</div>
            </div>
            <button id="btn-vk" onclick="call('openVkCommunity')">Подписаться</button>
          </div>
        </div>

        <div class="card friends-card" id="friends-box">
          <div class="friends-head">
            <div class="card-title" style="margin-bottom:0;"><span class="ico" style="background:var(--sc-pink-soft);">🤝</span>Друзья<span class="friends-count" id="friends-count-badge">0</span></div>
          </div>
          <div class="add-row">
            <input class="login-input" id="friend-input" placeholder="Логин друга"
                   onkeydown="if(event.key==='Enter')doAddFriend()">
            <button onclick="doAddFriend()" title="Добавить">➕</button>
          </div>
          <div class="search-row">
            <span class="ico">🔍</span>
            <input id="friend-search" placeholder="Найти в списке" oninput="onFriendSearchInput()">
          </div>
          <div class="filter-pills" id="filter-pills"></div>
          <div id="friends-list">Загрузка...</div>
          <button class="load-more" id="load-more-btn" onclick="onLoadMoreFriends()" style="display:none;">Показать ещё</button>
        </div>

        <div class="beta-row">
          <button id="beta-btn" onclick="requestBetaAccess()">🚀 Подать заявку на Beta-тест</button>
          <button id="info-btn" onclick="showBetaInfo()">❓</button>
        </div>

        <div class="card">
          <button id="auto-sync-btn" onclick="toggleAutoSync()"><span>🔄 Авто-синхронизация</span><span class="as-state">выкл</span></button>
          <button class="action-btn action-primary" id="sync-now-btn" onclick="runSync()" style="margin-top:8px;">🔄 Синхронизировать сейчас</button>
          <button class="action-btn action-text" id="logout-btn" onclick="doLogout()">🚪 Выйти из аккаунта</button>
        </div>

      </div>
    </div>

    <div class="bottom-row">
      <button id="clear-cloud-btn" onclick="confirmClearCloud()">🗑 Очистить облако</button>
    </div>
  </div>


</div>

<!-- ================= МОДАЛКА: ВЫБОР АВАТАРА ================= -->
<div class="modal-overlay" id="avatar-modal">
  <div class="modal-box">
    <div class="modal-title">Выберите аватар</div>
    <div class="avatar-grid" id="avatar-grid"></div>
    <input type="file" id="avatar-file-input" accept="image/*" style="display:none">
    <button class="btn btn-purple" onclick="document.getElementById('avatar-file-input').click()">🖼️ Загрузить своё фото</button>
    <button class="modal-close-btn" onclick="closeModal('avatar-modal')">Закрыть</button>
  </div>
</div>

<!-- ================= МОДАЛКА: НАСТРОЙКИ ================= -->
<div class="modal-overlay" id="settings-modal">
  <div class="modal-box">
    <div class="settings-tabs">
      <button class="settings-tab-btn active" id="settings-tab-btn-account" onclick="switchSettingsTab('account')">👤 Аккаунт</button>
      <button class="settings-tab-btn" id="settings-tab-btn-devices" onclick="switchSettingsTab('devices')">💻 Устройства</button>
      <button class="settings-tab-btn" id="settings-tab-btn-notify" onclick="switchSettingsTab('notify')">🔔 Уведомления</button>
      <button class="settings-tab-btn" id="settings-tab-btn-appearance" onclick="switchSettingsTab('appearance')">🎨 Оформление</button>
    </div>

    <div class="settings-tab-panel" id="settings-tab-panel-account">
      <div class="settings-row-label">Email аккаунта</div>
      <div class="account-row" id="email-box">
        <span class="account-ico" style="background:var(--sc-amber-soft,rgba(245,165,36,0.14));">✉️</span>
        <div class="account-body">
          <div class="account-value" id="email-status">Загрузка...</div>
        </div>
      </div>
      <div style="display:flex; gap:6px; margin-top:8px;">
        <input class="login-input" id="account-email-input" placeholder="you@example.com" style="flex:1; padding:8px 10px; font-size:12px;">
        <button class="btn" style="width:auto; margin:0; padding:8px 14px;" onclick="doSaveEmail()">💾</button>
      </div>

      <div class="settings-row-label">Смена пароля</div>
      <input class="login-input" type="password" id="settings-old-password" placeholder="Текущий пароль" style="margin-bottom:6px;">
      <input class="login-input" type="password" id="settings-new-password" placeholder="Новый пароль (мин. 6 символов)" style="margin-bottom:8px;">
      <button class="btn btn-purple" onclick="doChangePassword()">🔑 Сменить пароль</button>
      <div class="settings-hint">При смене пароля сбрасываются сохранённые в облаке пароли сайтов и настройки — на этом устройстве они останутся и снова уйдут в облако при следующей синхронизации.</div>
    </div>

    <div class="settings-tab-panel" id="settings-tab-panel-devices" style="display:none">
      <div class="settings-hint" style="margin-top:0;">Последние входы в аккаунт (до 20). Точная модель устройства не определяется — только IP и метка оборудования.</div>
      <div id="devices-list" style="margin-top:10px;">Загрузка...</div>
    </div>

    <div class="settings-tab-panel" id="settings-tab-panel-notify" style="display:none">
      <button class="pref-toggle" id="notify-new-device-btn" onclick="toggleNotifyPref('new_device')">
        <span>🔔 Новый вход в аккаунт</span><span class="as-state">...</span></button>
      <button class="pref-toggle" id="notify-purchases-btn" onclick="toggleNotifyPref('purchases')">
        <span>🧾 Подтверждение покупок</span><span class="as-state">...</span></button>
      <button class="pref-toggle" id="notify-premium-expiry-btn" onclick="toggleNotifyPref('premium_expiry')">
        <span>👑 Скоро истекает Premium</span><span class="as-state">...</span></button>
      <div class="settings-hint">Письма о покупках и скором истечении Premium подключим на следующем этапе — переключатели уже сохраняют ваш выбор.</div>
    </div>

    <div class="settings-tab-panel" id="settings-tab-panel-appearance" style="display:none">
      <div id="appearance-tab-content"></div>
    </div>

    <button class="modal-close-btn" onclick="closeModal('settings-modal')" style="margin-top:14px;">Закрыть</button>
  </div>
</div>

<!-- ================= МОДАЛКА: МАГАЗИН ================= -->
<div class="modal-overlay" id="store-modal">
  <div class="modal-box">
    <div class="store-header">
      <span>Баланс: <b id="store-balance" style="color:#56d39b">0 SC</b></span>
      <span style="color:#a371f7">Токены: <b id="store-tokens">0</b></span>
    </div>

    <div class="store-tabs">
      <button class="store-tab-btn active" id="store-tab-btn-tokens" onclick="switchStoreTab('tokens')">🧠 Токены</button>
      <button class="store-tab-btn" id="store-tab-btn-vpn" onclick="switchStoreTab('vpn')">🛡️ VPN</button>
      <button class="store-tab-btn" id="store-tab-btn-premium" onclick="switchStoreTab('premium')">👑 Premium</button>
      <button class="store-tab-btn" id="store-tab-btn-research" onclick="switchStoreTab('research')">🔬 Анализ</button>
    </div>

    <div class="store-tab-panel" id="store-tab-panel-tokens">
      <div class="store-grid">
        <div class="product-card" style="border-color:#ff79c6">
          <b style="color:#ff79c6">Пакет «Мини»</b>
          <p>20 000 AI Токенов. Оптимально для быстрого теста функций.</p>
          <div class="price-per-unit">≈ 7.5 SC / 1000 токенов</div>
          <button id="buy-btn-pack_20k" style="background:#ff79c6"
                  onclick="doPurchase('pack_20k', 'Пакет «Мини» (20 000 токенов)', 150)">Купить (150 SC)</button>
          <div class="afford-note" id="afford-note-pack_20k"></div>
        </div>
        <div class="product-card" style="border-color:#a371f7">
          <b style="color:#a371f7">Пакет «Старт»</b>
          <p>50 000 AI Токенов. Базовый набор для работы.</p>
          <div class="price-per-unit">≈ 5.98 SC / 1000 токенов</div>
          <button id="buy-btn-pack_50k" style="background:#a371f7"
                  onclick="doPurchase('pack_50k', 'Пакет «Старт» (50 000 токенов)', 299)">Купить (299 SC)</button>
          <div class="afford-note" id="afford-note-pack_50k"></div>
        </div>
        <div class="product-card" style="border-color:#4facfe">
          <span class="badge-best">🔥 Выгоднее всего</span>
          <b style="color:#4facfe">Пакет «Оптимум»</b>
          <p>100 000 AI Токенов. Максимальный объем слов.</p>
          <div class="price-per-unit">≈ 5.9 SC / 1000 токенов</div>
          <button id="buy-btn-pack_100k" style="background:#4facfe"
                  onclick="doPurchase('pack_100k', 'Пакет «Оптимум» (100 000 токенов)', 590)">Купить (590 SC)</button>
          <div class="afford-note" id="afford-note-pack_100k"></div>
        </div>
      </div>
    </div>

    <div class="store-tab-panel" id="store-tab-panel-vpn" style="display:none">
      <div class="store-grid" style="grid-template-columns:1fr;">
        <div class="product-card" style="border-color:#56d39b">
          <b style="color:#56d39b">Storm VPN</b>
          <p>Защищенный доступ. Выделенный лимит 5 ГБ трафика.</p>
          <button id="buy-btn-pack_vpn_5gb" style="background:#56d39b"
                  onclick="doPurchase('pack_vpn_5gb', 'Storm VPN (5 ГБ)', 390)">Купить (390 SC)</button>
          <div class="afford-note" id="afford-note-pack_vpn_5gb"></div>
        </div>
      </div>
    </div>

    <div class="store-tab-panel" id="store-tab-panel-research" style="display:none">
      <div id="research-quota-status" style="color:#94a3b8; font-size:11.5px; margin-bottom:10px;"></div>
      <div class="product-card" style="border-color:#a371f7; margin-bottom:10px;">
        <b style="color:#a371f7">🔬 Глубокое исследование — тариф отчётов</b>
        <p>
          Модуль «Глубокое исследование» боковой панели: по теме сам находит и читает
          источники, а ИИ собирает из них готовый отчёт (PDF/TXT) прямо в Storm Reader.
          Бесплатно — 4 отчёта за 30 дней. Нужно больше — выберите тариф ниже.
          Действует только при работе через токены Личного кабинета — при своём ключе
          (OpenRouter/GigaChat) модуль и так бесплатен без ограничений.
        </p>
      </div>
      <div class="prem-tier-row">
        <button id="buy-btn-pack_research_10" style="background:#7c6adf"
                onclick="doPurchase('pack_research_10', 'Deep Research: 10 отчётов / 30 дней', 149)">10 отчётов&#10;149 SC</button>
        <button id="buy-btn-pack_research_20" style="background:#a371f7"
                onclick="doPurchase('pack_research_20', 'Deep Research: 20 отчётов / 30 дней', 249)">20 отчётов&#10;249 SC</button>
        <button id="buy-btn-pack_research_30" style="background:#c084fc"
                onclick="doPurchase('pack_research_30', 'Deep Research: 30 отчётов / 30 дней', 349)">30 отчётов&#10;349 SC</button>
      </div>
      <div style="color:#64748b; font-size:11px; margin-top:8px; text-align:center;">
        Покупка сразу даёт полную квоту на новые 30 дней (не суммируется с уже
        потраченным по прошлому тарифу или бесплатному лимиту).
      </div>
    </div>

    <div class="store-tab-panel" id="store-tab-panel-premium" style="display:none">
      <div id="premium-manage-box"></div>
      <div id="gift-mode-banner" style="display:none; background:rgba(255,215,0,0.12); border:1px solid rgba(255,215,0,0.4);
           border-radius:8px; padding:8px 10px; margin-bottom:10px; font-size:12px; color:#ffd700;">
        🎁 Дарите Premium другу <b id="gift-target-name"></b> —
        <span style="text-decoration:underline; cursor:pointer;" onclick="cancelGiftMode()">выбрать себе</span>
      </div>
      <div style="color:#fbbf24; font-weight:bold; margin-bottom:8px;">👑 Оформить подписку Storm Premium:</div>
      <div class="prem-tier-row">
        <button id="buy-btn-pack_premium_1m" style="background:#3b82f6"
                onclick="doPurchasePremium('pack_premium_1m', 'Storm Premium (1 месяц)', 149)">1 Месяц&#10;149 SC</button>
        <button id="buy-btn-pack_premium_6m" style="background:#8b5cf6"
                onclick="doPurchasePremium('pack_premium_6m', 'Storm Premium (6 месяцев)', 790)">6 Месяцев&#10;790 SC</button>
        <button id="buy-btn-pack_premium_1y" style="background:#10b981"
                onclick="doPurchasePremium('pack_premium_1y', 'Storm Premium (1 год)', 1490)">1 Год&#10;1490 SC</button>
      </div>
      <div style="color:#64748b; font-size:11px; margin-top:8px; text-align:center;">
        Покупка продлевает текущий срок, а не сбрасывает его — если Premium уже активен, новый период добавится к оставшемуся.
        6/12 мес — бонус токенов и VPN до 15 ГБ, 1 мес — скидка 10% на токены и 20% на VPN.
      </div>
    </div>

    <button class="modal-close-btn" onclick="closeModal('store-modal')">Закрыть витрину</button>
  </div>
</div>

<!-- ================= МОДАЛКА: КОШЕЛЁК ================= -->
<div class="modal-overlay" id="wallet-modal">
  <div class="modal-box">
    <div class="modal-title" style="color:#56d39b">💳 Storm Кошелек</div>
    <h2 style="text-align:center; margin-bottom:4px;">Ваш баланс:</h2>
    <h1 id="wallet-balance" style="color:#56d39b; text-align:center; margin-bottom:6px;">0 SC</h1>
    <p style="color:gray; font-size:11px; text-align:center; margin-bottom:16px;">SC — Storm Coins (Виртуальная валюта)</p>
    <button class="btn btn-purple" onclick="alert('Шлюз оплаты находится в разработке.\n\nВ рамках закрытого Beta-тестирования администратор начисляет Storm Coins вручную.')">Пополнить счет</button>
    <div id="wallet-premium-block" style="margin-top:14px;"></div>
    <div style="text-align:left; margin-top:16px;">
      <div style="font-size:12px; color:#94a3b8; margin-bottom:6px;">История покупок:</div>
      <div class="purchase-history-list" id="purchase-history-list">—</div>
    </div>
    <button class="modal-close-btn" onclick="closeModal('wallet-modal')">Закрыть</button>
  </div>
</div>


<div class="toast" id="toast"></div>

<script>
// ==========================================================
// Мост QWebChannel
// ==========================================================
var bridge = null;
var isPremiumUser = false;
var currentPremiumTier = '';
var lastBalance = "0 SC";
var lastBalanceRaw = 0;
var lastTokens = "0";
var avatarAnimTimer = null;

var AVATAR_EMOJIS = ["👤","😎","🤖","👾","🐺","🦊","🐉","🦉","🌪️","⚡","🔥","🌊","🎮","💻","🚀"];

var BG_THEMES = {
  "Стандартный": null,
  "Штормовое Небо": "linear-gradient(180deg,#1e293b,#0f172a)",
  "Глубокий Океан": "linear-gradient(180deg,#091e3a,#020617)",
  "Изумрудный Лес": "linear-gradient(180deg,#064e3b,#022c22)",
  "Кровавый Закат": "linear-gradient(180deg,#450a0a,#000000)",
  "Космическая Пустота": "linear-gradient(180deg,#2e1065,#000000)",
  "Золотой Рассвет": "linear-gradient(180deg,#422006,#000000)",
  "Матрица": "linear-gradient(180deg,#064e3b,#000000)",
  "Неоновый Синдикат": "linear-gradient(180deg,#831843,#2e1065)"
};
var FRAME_COLORS = {
  "Обычный круг": null,
  "Неоновый Изумруд": "#10b981",
  "Золотое Свечение": "#f59e0b",
  "Пурпурный Киберпанк": "#d946ef",
  "Кровавый Рубин": "#ef4444",
  "Магический Сапфир": "#3b82f6",
  "Токсичный Ультрафиолет": "#8b5cf6",
  "Ледяной Кристалл": "#06b6d4",
  "Огненное Кольцо": "#f97316"
};
var AVATAR_FRAMES = {
  "Своя/Стандартная": null,
  "Кибер-Взломщик 💻⚡": ["👨‍💻","💻","⚡","📡","🔓"],
  "Призрачный Гонщик 💀🔥": ["💀","🔥","🏎️","💥","☄️"],
  "Космический Лорд 🌌🛸": ["🛸","🌌","🪐","✨","☄️"],
  "Ретро Геймер 🕹️👾": ["🕹️","👾","🎮","🚀","💥"],
  "Штормовой Пророк ⛈️🔮": ["🔮","⛈️","💡","🌪️","🌊"]
};

var savedAvatar   = %SAVED_AVATAR_JSON%;
var savedBg       = %SAVED_BG_JSON%;
var savedFrame    = %SAVED_FRAME_JSON%;
var savedAvatarFx = %SAVED_AVATARFX_JSON%;
var autoSyncSaved = %AUTOSYNC_JSON%;
var savedUsername = %SAVED_USERNAME_JSON%;

// ВАЖНО: один QWebChannel на один transport. Раньше initBridge() вызывался
// ДВАЖДЫ (сразу и повторно на DOMContentLoaded) — если оба вызова успевали
// застать qt.webChannelTransport готовым (обычный случай, т.к. скрипт лежит
// в конце body), создавались ДВА независимых QWebChannel поверх одного и
// того же transport. Они перебивают друг друга при обработке сообщений,
// из-за чего "мост" (bridge) мог в любой момент перестать получать
// пинги/баланс/статус синхронизации — и не мигать очевидной ошибкой, а
// просто тихо переставать отвечать. Плюс раньше не было повтора, если
// qt.webChannelTransport ещё не готов в момент вызова — теперь есть.
var webChannelStarted = false;
function initBridge() {
    if (webChannelStarted) return;
    if (typeof QWebChannel === 'undefined' || typeof qt === 'undefined' || !qt.webChannelTransport) {
        console.warn('[StormCloud] qt.webChannelTransport ещё не готов, жду...');
        setTimeout(initBridge, 200);
        return;
    }
    webChannelStarted = true;
    console.warn('[StormCloud] qt.webChannelTransport найден, создаём QWebChannel...');

    new QWebChannel(qt.webChannelTransport, function(channel) {
        bridge = channel.objects.cloudBridge;
        if (!bridge) {
            console.warn('[StormCloud] ОШИБКА: channel.objects.cloudBridge не найден!');
            return;
        }
        console.warn('[StormCloud] cloudBridge подключён успешно');
        wireBridgeSignals();
         if (%ALREADY_LOGGED_IN%) {
            document.getElementById('user-name-label').textContent = savedUsername;
            showManageScreen();
            bridge.checkPing();
            bridge.fetchBillingInfo();
            bridge.fetchLocalStats();
            bridge.fetchFriends();
            if (bridge.fetchResearchQuota) bridge.fetchResearchQuota();
        }
        setInterval(function() { if (bridge) bridge.checkPing(); }, 10000);
    });
}
initBridge();

function call(name) {
    var args = Array.prototype.slice.call(arguments, 1);
    if (!bridge) return;
    if (typeof bridge[name] === 'function') bridge[name].apply(bridge, args);
}

function showToast(msg) {
    var t = document.getElementById('toast');
    t.textContent = msg;
    t.style.display = 'block';
    clearTimeout(t._hideTimer);
    t._hideTimer = setTimeout(function() { t.style.display = 'none'; }, 3500);
}

function openModal(id) { document.getElementById(id).classList.add('open'); }
function closeModal(id) {
    document.getElementById(id).classList.remove('open');
    if (id === 'store-modal') cancelGiftMode();
}

function openStoreNormal() {
    cancelGiftMode();
    openModal('store-modal');
}

var giftTargetFriend = null;

function openGiftPremiumModal(friendName) {
    giftTargetFriend = friendName;
    document.getElementById('gift-target-name').textContent = friendName;
    document.getElementById('gift-mode-banner').style.display = 'block';
    openModal('store-modal');
    switchStoreTab('premium');
}

function cancelGiftMode() {
    giftTargetFriend = null;
    var banner = document.getElementById('gift-mode-banner');
    if (banner) banner.style.display = 'none';
}

function doPurchasePremium(packageId, itemName, price) {
    if (giftTargetFriend) {
        if (!confirm('Подарить «' + itemName + '» другу ' + giftTargetFriend + ' за ' + price + ' SC?')) return;
        call('giftPremium', giftTargetFriend, packageId);
    } else {
        if (!confirm('Купить «' + itemName + '» за ' + price + ' SC?')) return;
        call('purchase', packageId);
    }
}

// ---------------- ЛОГИН ----------------
function toggleMode() {
    var btn = document.getElementById('btn-auth');
    var toggle = document.getElementById('mode-toggle');
    var invite = document.getElementById('login-invite');
    var email = document.getElementById('login-email');
    if (btn.textContent === 'Войти в аккаунт') {
        btn.textContent = 'Зарегистрироваться';
        toggle.textContent = 'Уже есть аккаунт? Войти';
        invite.style.display = 'block';
        email.style.display = 'block';
        document.getElementById('forgot-password-link').style.display = 'none';
    } else {
        btn.textContent = 'Войти в аккаунт';
        toggle.textContent = 'Нет аккаунта? Создать';
        invite.style.display = 'none';
        invite.value = '';
        email.style.display = 'none';
        email.value = '';
        document.getElementById('forgot-password-link').style.display = 'block';
    }
}

function openForgotPassword() {
    document.getElementById('forgot-password-box').style.display = 'block';
    document.getElementById('forgot-input').value = document.getElementById('login-username').value.trim();
}
function closeForgotPassword() {
    document.getElementById('forgot-password-box').style.display = 'none';
    document.getElementById('forgot-message').textContent = '';
}
function doForgotPassword() {
    var v = document.getElementById('forgot-input').value.trim();
    if (!v) return;
    document.getElementById('forgot-message').textContent = 'Отправляем...';
    call('forgotPassword', v);
}

function handleAuth() {
    var user = document.getElementById('login-username').value.trim();
    var pwd  = document.getElementById('login-password').value.trim();
    var invite = document.getElementById('login-invite').value.trim();
    var email = document.getElementById('login-email').value.trim();
    if (!user || !pwd) return;

    var errEl = document.getElementById('login-error');
    errEl.style.display = 'none';

    var isRegister = document.getElementById('btn-auth').textContent === 'Зарегистрироваться';
    if (isRegister) {
        if (!email) {
            errEl.textContent = 'Укажите email — он нужен для восстановления пароля';
            errEl.style.display = 'block';
            return;
        }
        call('registerAccount', user, pwd, invite, email);
    }
    else call('login', user, pwd);
}

function wireBridgeSignals() {
    bridge.authFinished.connect(function(success, message, username) {
        var errEl = document.getElementById('login-error');
        if (success) {
            errEl.style.display = 'none';
            document.getElementById('user-name-label').textContent = username;
            showManageScreen();
            bridge.checkPing();
            bridge.fetchBillingInfo();
            bridge.fetchLocalStats();
            bridge.fetchFriends();
            if (bridge.fetchResearchQuota) bridge.fetchResearchQuota();
        } else {
            errEl.textContent = message;
            errEl.style.display = 'block';
        }

    });
    )HTML");
}