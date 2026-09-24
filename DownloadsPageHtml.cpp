#include "DownloadsPageHtml.h"

QString getDownloadsHtml() {
    return QString::fromUtf8(u8R"DLHTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<title>Загрузки</title>
<style>
    * { box-sizing: border-box; }
    body {
        margin: 0;
        background: #0d1117;
        color: #eef3ff;
        font-family: -apple-system, "Segoe UI", Roboto, Arial, sans-serif;
    }
    .page {
        max-width: 760px;
        margin: 0 auto;
        padding: 32px 24px 60px 24px;
    }
    header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        flex-wrap: wrap;
        gap: 12px;
        margin-bottom: 18px;
    }
    header h1 {
        font-size: 22px;
        font-weight: 700;
        margin: 0;
    }
    .toolbar {
        display: flex;
        gap: 8px;
        align-items: center;
    }
    #search {
        background: #1c2128;
        border: 1px solid #30363d;
        border-radius: 8px;
        color: #eef3ff;
        padding: 8px 12px;
        font-size: 13px;
        width: 220px;
        outline: none;
    }
    #search:focus { border-color: #a371f7; }
    .danger-btn, .secondary-btn {
        border: none;
        border-radius: 8px;
        padding: 8px 14px;
        font-size: 13px;
        font-weight: 600;
        cursor: pointer;
    }
    .danger-btn { background: rgba(255,107,107,0.12); color: #ff6b6b; }
    .danger-btn:hover { background: rgba(255,107,107,0.22); }
    .secondary-btn { background: #21262d; color: #c9d1d9; }
    .secondary-btn:hover { background: #2a313a; }

    .filters {
        display: flex;
        align-items: center;
        gap: 6px;
        margin-bottom: 18px;
        flex-wrap: wrap;
    }
    .chip {
        background: transparent;
        color: #8b949e;
        border: 1px solid #30363d;
        border-radius: 20px;
        padding: 6px 14px;
        font-size: 12.5px;
        font-weight: 600;
        cursor: pointer;
    }
    .chip:hover { color: #c9d1d9; }
    /* Поле "Ссылка на видео" — раньше жило в мини-попапе DownloadManager,
       теперь только тут, сразу после фильтра "Торренты" в той же строке. */
    .video-download {
        display: flex;
        align-items: center;
        gap: 6px;
        margin-left: 6px;
    }
    .video-download input {
        background: #1c2128;
        border: 1px solid #30363d;
        border-radius: 8px;
        color: #eef3ff;
        padding: 6px 10px;
        font-size: 12.5px;
        width: 200px;
        outline: none;
    }
    .video-download input:focus { border-color: #a371f7; }
    .video-download button {
        width: 30px; height: 30px;
        flex-shrink: 0;
        display: flex; align-items: center; justify-content: center;
        border: none; border-radius: 8px;
        background: #a371f7; color: white;
        font-size: 14px; font-weight: bold;
        cursor: pointer;
    }
    .video-download button:hover { background: #b98cff; }
    .chip.active { background: #21262d; color: #eef3ff; border-color: #3d444d; }

    .list { display: flex; flex-direction: column; gap: 10px; }

    .card {
        display: flex;
        align-items: center;
        gap: 12px;
        background: #1c2128;
        border: 1px solid #30363d;
        border-radius: 12px;
        padding: 12px 14px;
    }
    .card:hover { border-color: #3d444d; }
    .card-icon { font-size: 22px; width: 28px; text-align: center; flex-shrink: 0; }
    .card-body { flex: 1; min-width: 0; }
    .card-title {
        font-size: 13.5px;
        font-weight: 600;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
    }
    .card-meta {
        margin-top: 4px;
        display: flex;
        align-items: center;
        gap: 8px;
        font-size: 11.5px;
        color: #8b949e;
        flex-wrap: wrap;
    }
    .badge { border-radius: 6px; padding: 2px 7px; font-weight: 600; }
    .badge.ok { background: rgba(86,211,155,0.12); color: #56d39b; }
    .badge.bad { background: rgba(255,107,107,0.12); color: #ff6b6b; }
    .badge.warn { background: rgba(255,200,87,0.14); color: #ffc857; }
    .badge.active { background: rgba(163,113,247,0.14); color: #a371f7; }
    .type-tag { background: #21262d; border-radius: 6px; padding: 2px 7px; }
    .date { color: #6e7681; }

    /* Карточки активных (ещё не завершённых) загрузок — живой прогресс,
       опрашивается отдельно от истории, см. pollActive() в script. */
    .card.is-active { border-color: #3a3d6e; }
    .progress-mini {
        margin-top: 6px;
        height: 4px;
        border-radius: 2px;
        background: #161b22;
        overflow: hidden;
    }
    .progress-mini-fill {
        height: 100%;
        border-radius: 2px;
        background: linear-gradient(90deg, #a371f7, #4facfe);
        transition: width 0.4s ease;
    }

    .card-actions { display: flex; gap: 4px; flex-shrink: 0; }
    .icon-btn {
        width: 30px; height: 30px;
        display: flex; align-items: center; justify-content: center;
        background: transparent; border: none; border-radius: 7px;
        font-size: 14px; color: #c9d1d9; cursor: pointer;
    }
    .icon-btn:hover { background: rgba(255,255,255,0.08); color: #eef3ff; }
    .icon-btn.danger:hover { color: #ff6b6b; }
    .icon-btn.retry:hover { color: #4facfe; }

    /* Чекбокс выбора карточки (массовое удаление) и заголовок группы по дате */
    .card-check {
        display: flex; align-items: center; flex-shrink: 0;
    }
    .card-check input { width: 15px; height: 15px; cursor: pointer; accent-color: #a371f7; }
    .group-header {
        font-size: 11.5px; font-weight: 700; color: #6e7681;
        text-transform: uppercase; letter-spacing: 0.03em;
        margin: 14px 0 6px 0;
    }
    .group-header:first-child { margin-top: 0; }
    .size-tag { color: #6e7681; }

    .bulk-bar {
        display: flex; align-items: center; justify-content: space-between;
        background: #1c2128; border: 1px solid #3a3d6e; border-radius: 10px;
        padding: 10px 14px; margin-bottom: 12px; font-size: 12.5px; color: #c9d1d9;
    }
    .bulk-actions { display: flex; gap: 8px; }

    .empty-state {
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        padding: 80px 20px;
        color: #6e7681;
    }
    .empty-state .emoji { font-size: 40px; margin-bottom: 10px; }
    .empty-state .text { font-size: 13px; }

    .modal-overlay {
        position: fixed; inset: 0;
        background: rgba(0,0,0,0.5);
        display: flex; align-items: center; justify-content: center;
        z-index: 100;
    }
    .modal {
        background: #1c2128;
        border: 1px solid #30363d;
        border-radius: 14px;
        padding: 22px;
        width: 320px;
    }
    .modal-title { font-size: 15px; font-weight: 700; margin-bottom: 8px; }
    .modal-text { font-size: 12.5px; color: #8b949e; margin-bottom: 18px; line-height: 1.5; }
    .modal-actions { display: flex; justify-content: flex-end; gap: 8px; }
</style>
</head>
<body>
    <div class="page">
        <header>
            <h1>📥 Загрузки</h1>
            <div class="toolbar">
                <input id="search" type="text" placeholder="Поиск по названию...">
                <button id="clearAllBtn" class="danger-btn">🧹 Очистить всё</button>
            </div>
        </header>

        <div class="filters">
            <button class="chip active" data-filter="all">Все</button>
            <button class="chip" data-filter="regular">📥 Обычные</button>
            <button class="chip" data-filter="torrent">🧲 Торренты</button>
            <div class="video-download">
                <input id="videoUrl" type="text" placeholder="Ссылка на видео (VK и др.)">
                <button id="videoDownloadBtn" title="Скачать видео">⬇</button>
            </div>
        </div>

        <div id="bulkBar" class="bulk-bar" style="display:none;">
            <span id="bulkCount">Выбрано: 0</span>
            <div class="bulk-actions">
                <button id="bulkDeleteBtn" class="danger-btn">🗑 Удалить выбранное</button>
                <button id="bulkCancelBtn" class="secondary-btn">Отмена</button>
            </div>
        </div>

        <div id="list" class="list"></div>

        <div id="emptyState" class="empty-state" style="display:none;">
            <div class="emoji">📭</div>
            <div class="text">Ничего не найдено</div>
        </div>
    </div>

    <div id="confirmModal" class="modal-overlay" style="display:none;">
        <div class="modal">
            <div class="modal-title" id="confirmTitle">Очистить всю историю?</div>
            <div class="modal-text" id="confirmText">И обычные, и торрент-записи — файлы на диске удалены не будут, очистится только список.</div>
            <div class="modal-actions">
                <button id="confirmCancelBtn" class="secondary-btn">Отмена</button>
                <button id="confirmOkBtn" class="danger-btn">Очистить</button>
            </div>
        </div>
    </div>

<script src="qrc:///qtwebchannel/qwebchannel.js"></script>
<script>
    let bridge = null;
    let allEntries = [];
    let activeEntries = [];
    let currentFilter = 'all';
    let searchQuery = '';
    let selectedIds = new Set(); // массовое удаление — id выбранных карточек истории
    let pendingConfirmAction = null; // callback, который выполнит confirmOkBtn (переиспользуем одно модальное окно и для "очистить всё", и для массового удаления)

    new QWebChannel(qt.webChannelTransport, function(channel) {
        bridge = channel.objects.downloadsBridge;
        loadDownloads();
        pollActive();
        setInterval(pollActive, 1500);
    });

    function pollActive() {
        if (!bridge) return;
        bridge.getActiveDownloadsJson(function(json) {
            let fresh = [];
            try { fresh = JSON.parse(json) || []; } catch (e) { fresh = []; }
            const wasActiveCount = activeEntries.length;
            activeEntries = fresh;
            render();
            // Активная загрузка только что пропала из списка — скорее всего,
            // завершилась (или отменилась/упала с ошибкой) — обновим и
            // историю, чтобы новая запись сразу появилась без ручного F5.
            if (fresh.length < wasActiveCount) loadDownloads();
        });
    }

    function loadDownloads() {
        if (!bridge) return;
        bridge.getDownloadsJson(function(json) {
            try { allEntries = JSON.parse(json) || []; } catch (e) { allEntries = []; }
            allEntries.sort(function (a, b) { return (b.timestamp || '').localeCompare(a.timestamp || ''); });
            // Записи, которых больше нет (удалены с другой вкладки/попапа),
            // не должны оставаться "выбранными" призраками.
            const stillPresent = new Set(allEntries.map(function (e) { return e.id; }));
            selectedIds.forEach(function (id) { if (!stillPresent.has(id)) selectedIds.delete(id); });
            updateBulkBar();
            render();
        });
    }

    function extIcon(path) {
        const ext = (path.split('.').pop() || '').toLowerCase();
        const video = ['mp4', 'mkv', 'avi', 'mov', 'webm', 'flv', 'wmv'];
        const audio = ['mp3', 'wav', 'flac', 'aac', 'ogg', 'm4a'];
        const image = ['png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp', 'svg'];
        const archive = ['zip', 'rar', '7z', 'tar', 'gz'];
        const doc = ['pdf', 'doc', 'docx', 'xls', 'xlsx', 'ppt', 'pptx', 'txt'];
        const exec = ['exe', 'msi', 'apk', 'dmg'];
        if (video.includes(ext)) return '🎬';
        if (audio.includes(ext)) return '🎵';
        if (image.includes(ext)) return '🖼️';
        if (archive.includes(ext)) return '🗜️';
        if (doc.includes(ext)) return '📄';
        if (exec.includes(ext)) return '⚙️';
        return '📦';
    }

    function statusBadge(status) {
        if (status === 'completed') return { label: '✅ Завершено', cls: 'ok' };
        if (status === 'cancelled') return { label: '❌ Отменено', cls: 'bad' };
        if (status === 'interrupted') return { label: '⚠️ Ошибка', cls: 'warn' };
        return { label: status || '—', cls: '' };
    }

    function formatDate(iso) {
        if (!iso) return '';
        const d = new Date(iso);
        if (isNaN(d.getTime())) return iso;
        const pad = n => String(n).padStart(2, '0');
        return pad(d.getDate()) + '.' + pad(d.getMonth() + 1) + '.' + d.getFullYear() + ', ' + pad(d.getHours()) + ':' + pad(d.getMinutes());
    }

    // "Сегодня" / "Вчера" / "Ранее" — группировка карточек истории по дате;
    // allEntries уже отсортирован по timestamp по убыванию, так что при
    // последовательном проходе группы сами идут в правильном порядке.
    function dateGroupLabel(iso) {
        if (!iso) return 'Ранее';
        const d = new Date(iso);
        if (isNaN(d.getTime())) return 'Ранее';
        const startOfDay = dt => new Date(dt.getFullYear(), dt.getMonth(), dt.getDate()).getTime();
        const diffDays = Math.round((startOfDay(new Date()) - startOfDay(d)) / 86400000);
        if (diffDays === 0) return 'Сегодня';
        if (diffDays === 1) return 'Вчера';
        return 'Ранее';
    }

    function formatBytes(bytes) {
        if (!bytes || bytes <= 0) return '';
        const units = ['Б', 'КБ', 'МБ', 'ГБ'];
        let v = bytes, i = 0;
        while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
        return v.toFixed(i > 0 && v < 10 ? 1 : 0) + ' ' + units[i];
    }

    function escapeHtml(s) {
        return String(s || '').replace(/[&<>"']/g, function (c) {
            return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
        });
    }

    function buildHistoryCardHtml(e) {
        const badge = statusBadge(e.status);
        const icon = e.type === 'torrent' ? '🧲' : extIcon(e.path || e.title || '');
        const title = e.type === 'torrent' ? (e.title || '').replace(/^Торрент:\s*/, '') : (e.title || '');
        const sizeLabel = formatBytes(e.size);
        const canRetry = (e.status === 'cancelled' || e.status === 'interrupted') && e.sourceUrl;
        const checked = selectedIds.has(e.id) ? ' checked' : '';

        return '' +
            '<div class="card">' +
                '<label class="card-check"><input type="checkbox" class="select-box" data-id="' + escapeHtml(e.id) + '"' + checked + '></label>' +
                '<div class="card-icon">' + icon + '</div>' +
                '<div class="card-body">' +
                    '<div class="card-title" title="' + escapeHtml(e.path) + '">' + escapeHtml(title) + '</div>' +
                    '<div class="card-meta">' +
                        '<span class="badge ' + badge.cls + '">' + badge.label + '</span>' +
                        '<span class="type-tag">' + (e.type === 'torrent' ? 'Торрент' : 'Обычная') + '</span>' +
                        (sizeLabel ? '<span class="size-tag">' + sizeLabel + '</span>' : '') +
                        '<span class="date">' + formatDate(e.timestamp) + '</span>' +
                    '</div>' +
                '</div>' +
                '<div class="card-actions">' +
                    (canRetry ? '<button class="icon-btn retry" data-action="retry" data-type="' + escapeHtml(e.type) + '" data-url="' + escapeHtml(e.sourceUrl) + '" title="Повторить">🔁</button>' : '') +
                    '<button class="icon-btn" data-action="open" data-path="' + escapeHtml(e.path) + '" title="Открыть папку">📂</button>' +
                    '<button class="icon-btn danger" data-action="remove" data-id="' + escapeHtml(e.id) + '" title="Удалить из списка">🗑</button>' +
                '</div>' +
            '</div>';
    }

    function render() {
        const list = document.getElementById('list');
        const empty = document.getElementById('emptyState');
        const q = searchQuery.trim().toLowerCase();

        function matchesFilters(e) {
            if (currentFilter !== 'all' && e.type !== currentFilter) return false;
            if (q && (e.title || '').toLowerCase().indexOf(q) === -1) return false;
            return true;
        }

        const filteredActive = activeEntries.filter(matchesFilters);
        const filteredHistory = allEntries.filter(matchesFilters);

        if (filteredActive.length === 0 && filteredHistory.length === 0) {
            list.innerHTML = '';
            empty.style.display = 'flex';
            return;
        }
        empty.style.display = 'none';

        const activeHtml = filteredActive.map(function (e) {
            const icon = e.type === 'torrent' ? '🧲' : extIcon(e.title || '');
            const title = e.type === 'torrent' ? (e.title || '').replace(/^Торрент:\s*/, '') : (e.title || '');
            const percent = Math.max(0, Math.min(100, e.percent || 0));
            return '' +
                '<div class="card is-active">' +
                    '<div class="card-icon">' + icon + '</div>' +
                    '<div class="card-body">' +
                        '<div class="card-title">' + escapeHtml(title) + '</div>' +
                        '<div class="card-meta">' +
                            '<span class="badge active">⏳ ' + percent + '%</span>' +
                            '<span class="type-tag">' + (e.type === 'torrent' ? 'Торрент' : 'Обычная') + '</span>' +
                            '<span class="date">' + escapeHtml(e.meta || '') + '</span>' +
                        '</div>' +
                        '<div class="progress-mini"><div class="progress-mini-fill" style="width:' + percent + '%"></div></div>' +
                    '</div>' +
                '</div>';
        }).join('');

        // Группировка истории по дате — заголовок вставляется при смене группы
        // (filteredHistory уже отсортирован по убыванию timestamp).
        let historyHtml = '';
        let lastGroup = null;
        filteredHistory.forEach(function (e) {
            const group = dateGroupLabel(e.timestamp);
            if (group !== lastGroup) {
                historyHtml += '<div class="group-header">' + group + '</div>';
                lastGroup = group;
            }
            historyHtml += buildHistoryCardHtml(e);
        });

        list.innerHTML = activeHtml + historyHtml;
    }

    function updateBulkBar() {
        const bar = document.getElementById('bulkBar');
        const count = document.getElementById('bulkCount');
        if (selectedIds.size === 0) {
            bar.style.display = 'none';
        } else {
            bar.style.display = 'flex';
            count.textContent = 'Выбрано: ' + selectedIds.size;
        }
    }

    function openConfirm(title, text, onConfirm) {
        document.getElementById('confirmTitle').textContent = title;
        document.getElementById('confirmText').textContent = text;
        pendingConfirmAction = onConfirm;
        document.getElementById('confirmModal').style.display = 'flex';
    }

    document.getElementById('list').addEventListener('click', function (e) {
        const btn = e.target.closest('.icon-btn');
        if (!btn || !bridge) return;
        const action = btn.dataset.action;
        if (action === 'open') {
            bridge.openFolder(btn.dataset.path);
        } else if (action === 'remove') {
            const id = btn.dataset.id;
            allEntries = allEntries.filter(function (x) { return x.id !== id; });
            selectedIds.delete(id);
            updateBulkBar();
            render();
            bridge.removeEntry(id);
        } else if (action === 'retry') {
            bridge.retryDownload(btn.dataset.type, btn.dataset.url);
        }
    });

    document.getElementById('list').addEventListener('change', function (e) {
        const box = e.target.closest('.select-box');
        if (!box) return;
        if (box.checked) selectedIds.add(box.dataset.id);
        else selectedIds.delete(box.dataset.id);
        updateBulkBar();
    });

    document.querySelectorAll('.chip').forEach(function (chip) {
        chip.addEventListener('click', function () {
            document.querySelectorAll('.chip').forEach(function (c) { c.classList.remove('active'); });
            chip.classList.add('active');
            currentFilter = chip.dataset.filter;
            render();
        });
    });

    document.getElementById('search').addEventListener('input', function (e) {
        searchQuery = e.target.value;
        render();
    });

    document.getElementById('clearAllBtn').addEventListener('click', function () {
        if (allEntries.length === 0) return;
        openConfirm(
            'Очистить всю историю?',
            'И обычные, и торрент-записи — файлы на диске удалены не будут, очистится только список.',
            function () {
                allEntries = [];
                selectedIds.clear();
                updateBulkBar();
                render();
                if (bridge) bridge.clearAll();
            }
        );
    });

    document.getElementById('bulkDeleteBtn').addEventListener('click', function () {
        const ids = Array.from(selectedIds);
        if (ids.length === 0) return;
        openConfirm(
            'Удалить выбранное?',
            'Записей: ' + ids.length + '. Файлы на диске удалены не будут, очистится только список.',
            function () {
                allEntries = allEntries.filter(function (x) { return !selectedIds.has(x.id); });
                selectedIds.clear();
                updateBulkBar();
                render();
                if (bridge) bridge.removeEntries(JSON.stringify(ids));
            }
        );
    });
    document.getElementById('bulkCancelBtn').addEventListener('click', function () {
        selectedIds.clear();
        updateBulkBar();
        render();
    });

    document.getElementById('confirmCancelBtn').addEventListener('click', function () {
        document.getElementById('confirmModal').style.display = 'none';
        pendingConfirmAction = null;
    });
    document.getElementById('confirmOkBtn').addEventListener('click', function () {
        document.getElementById('confirmModal').style.display = 'none';
        if (pendingConfirmAction) pendingConfirmAction();
        pendingConfirmAction = null;
    });

    function submitVideoUrl() {
        const input = document.getElementById('videoUrl');
        const url = input.value.trim();
        if (!url || !bridge) return;
        bridge.startVideoDownload(url);
        input.value = '';
    }
    document.getElementById('videoDownloadBtn').addEventListener('click', submitVideoUrl);
    document.getElementById('videoUrl').addEventListener('keydown', function (e) {
        if (e.key === 'Enter') submitVideoUrl();
    });
</script>
</body>
</html>
)DLHTML");
}