#pragma once
#include <QString>

// Возвращает HTML страницы "storm://bookmarks" — тот же паттерн, что и
// pageTemplates.getSettingsHtml()/getStormCloudHtml() (см.
// MainWindow::addNewTab()), только это отдельная функция, а не метод
// PageTemplates: исходников PageTemplates.h/.cpp не было под рукой, чтобы
// безопасно вписать в них новый метод вслепую. Функция сделана inline и
// целиком в заголовке, поэтому НЕ требует добавления ещё одного .cpp в
// список исходников сборки — достаточно просто #include.
//
// Если позже захотите перенести это в PageTemplates — переносится один в
// один: тело функции становится телом PageTemplates::getBookmarksHtml().
//
// ЧТО ДОБАВЛЕНО в этой версии (относительно первой):
//  - Тулбар "Импорт/Экспорт/Импортированные вкладки" свёрнут в один
//    дропдаун "🗂 Управление вкладками", чтобы не занимать всю ширину шапки.
//  - Папки (плоские, без вложенности): создание/переименование/удаление,
//    закладку можно перетащить в папку или выбрать её в форме редактирования.
//  - Ручная сортировка: drag&drop (зажать за ⠿ и потянуть вверх/вниз) и
//    кнопки ⬆️/⬇️ как альтернативный способ для тех, кому неудобно тащить
//    мышкой. Работает в пределах одной папки/корня.
//  - aria-label на все иконочные кнопки (редактировать/удалить/папка/
//    сортировка), плюс title как визуальная подсказка при наведении.
//  - Поиск остался прежним по смыслу, но при активном запросе показывается
//    плоский список результатов без папок и без ручной сортировки —
//    порядок среди отфильтрованной выборки не имеет практического смысла.
//
// ОБНОВЛЕНИЕ: "отдельная задача" из абзаца выше — сделана. Панель закладок
// (BookmarksBar) и отдельное окно (BookmarksWindow) больше не показывают
// произвольный порядок: MainWindow::loadBookmarksIntoMenu() теперь строит
// bookmarksMenu тем же ORDER BY, что и getBookmarks() ниже, и умеет
// переставлять закладки — drag&drop прямо на панели/в окне, плюс пункты
// "Переместить выше/ниже" в контекстном меню (см. BookmarksReorderHelper.h,
// BookmarksBar.cpp, BookmarksWindow.cpp). Папки они по-прежнему НЕ
// показывают визуально (это остаётся только на этой странице) — но
// закладка внутри своей папки корректно двигается относительно "соседей
// по папке" даже в плоском списке, поскольку сам порядок уже группирует их
// вместе.
inline QString getBookmarksHtml()
{
    return QStringLiteral(R"BMKHTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<title>Закладки</title>
<style>
    :root {
        --bg: #1a1f2b;
        --panel: #20263a;
        --text: #eef3ff;
        --muted: #8b98b8;
        --hover: #2d374f;
        --border: rgba(255, 255, 255, 0.10);
        --accent: #6e8cff;
    }
    * { box-sizing: border-box; }
    body {
        margin: 0;
        background: var(--bg);
        color: var(--text);
        font-family: 'Segoe UI', Arial, sans-serif;
    }
    header {
        position: sticky;
        top: 0;
        background: var(--bg);
        padding: 20px 24px 14px;
        border-bottom: 1px solid var(--border);
        z-index: 6;
    }
    h1 { margin: 0 0 12px; font-size: 20px; font-weight: 600; }
    #search {
        width: 100%;
        padding: 9px 12px;
        border-radius: 8px;
        border: 1px solid var(--border);
        background: var(--panel);
        color: var(--text);
        font-size: 13px;
        outline: none;
    }
    #search:focus { border-color: var(--accent); }

    .toolbar { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 10px; }
    .tbtn {
        background: var(--panel);
        border: 1px solid var(--border);
        color: var(--text);
        font-size: 12px;
        padding: 6px 10px;
        border-radius: 7px;
        cursor: pointer;
        white-space: nowrap;
    }
    .tbtn:hover { background: var(--hover); }
    .tbtn-danger:hover { background: rgba(255, 90, 90, 0.15); color: #ff8080; }

    /* Дропдаун "Управление вкладками" */
    .dropdown { position: relative; display: inline-block; }
    .dropdown-menu {
        position: absolute;
        top: calc(100% + 4px);
        left: 0;
        background: var(--panel);
        border: 1px solid var(--border);
        border-radius: 8px;
        padding: 4px;
        min-width: 270px;
        display: none;
        flex-direction: column;
        gap: 2px;
        z-index: 7;
        box-shadow: 0 10px 28px rgba(0, 0, 0, 0.4);
    }
    .dropdown-menu.visible { display: flex; }
    .dropdown-item {
        background: transparent;
        border: none;
        color: var(--text);
        text-align: left;
        padding: 8px 10px;
        border-radius: 6px;
        font-size: 12px;
        cursor: pointer;
        white-space: nowrap;
    }
    .dropdown-item:hover { background: var(--hover); }

    #list { padding: 8px 12px 24px; max-width: 720px; margin: 0 auto; }

    /* Папки */
    .folder-section { margin-bottom: 6px; }
    .folder-header {
        display: flex;
        align-items: center;
        gap: 8px;
        padding: 8px 12px;
        cursor: pointer;
        user-select: none;
        border-radius: 8px;
    }
    .folder-header:hover { background: var(--hover); }
    .folder-header.folder-dragover { background: rgba(110, 140, 255, 0.15); outline: 1px dashed var(--accent); }
    .folder-header .chevron { font-size: 10px; color: var(--muted); transition: transform .12s; flex: 0 0 auto; }
    .folder-header.collapsed .chevron { transform: rotate(-90deg); }
    .folder-title {
        flex: 1 1 auto;
        font-size: 12px;
        font-weight: 600;
        color: var(--muted);
        text-transform: uppercase;
        letter-spacing: .03em;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
    }
    .search-results-title { padding: 6px 12px 4px; }
    .folder-count { font-size: 11px; color: var(--muted); flex: 0 0 auto; }
    .folder-actions { display: flex; gap: 2px; opacity: 0; transition: opacity .12s; flex: 0 0 auto; }
    .folder-header:hover .folder-actions { opacity: 1; }
    .folder-body { padding-left: 2px; }
    .folder-body.collapsed { display: none; }
    .folder-empty { padding: 10px 14px; font-size: 12px; color: var(--muted); }

    .row {
        display: flex;
        align-items: center;
        gap: 10px;
        padding: 9px 12px;
        border-radius: 8px;
        cursor: pointer;
    }
    .row:hover { background: var(--hover); }
    .row.dragging { opacity: .4; }
    .row.drag-over-top { box-shadow: inset 0 2px 0 var(--accent); }
    .row.drag-over-bottom { box-shadow: inset 0 -2px 0 var(--accent); }

    .drag-handle {
        flex: 0 0 auto;
        color: var(--muted);
        font-size: 14px;
        padding: 0 2px;
        cursor: grab;
    }
    .row.dragging .drag-handle, .row:active .drag-handle { cursor: grabbing; }

    .favicon {
        width: 22px;
        height: 22px;
        border-radius: 5px;
        background-size: cover;
        background-position: center;
        flex: 0 0 auto;
        display: flex;
        align-items: center;
        justify-content: center;
        font-size: 11px;
        font-weight: 700;
        color: #fff;
    }
    .favicon-fallback { background: var(--accent); }
    .info { flex: 1 1 auto; min-width: 0; }
    .title { font-size: 13px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .url { font-size: 11px; color: var(--muted); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .actions { display: flex; gap: 4px; opacity: 0; transition: opacity .12s; flex: 0 0 auto; }
    .row:hover .actions { opacity: 1; }
    .icon-btn {
        border: none;
        background: transparent;
        color: var(--muted);
        cursor: pointer;
        font-size: 14px;
        padding: 5px 7px;
        border-radius: 6px;
    }
    .icon-btn:hover { background: rgba(255, 255, 255, 0.08); color: var(--text); }
    .reorder-btn { font-size: 12px; }
    .reorder-btn:disabled { opacity: .25; cursor: default; }
    .reorder-btn:disabled:hover { background: transparent; color: var(--muted); }
    .empty { padding: 60px 12px; text-align: center; color: var(--muted); font-size: 13px; }

    #overlay {
        position: fixed;
        inset: 0;
        background: rgba(0, 0, 0, 0.5);
        display: none;
        align-items: center;
        justify-content: center;
        z-index: 10;
    }
    #overlay.visible { display: flex; }
    #actionOverlay {
        position: fixed;
        inset: 0;
        background: rgba(0, 0, 0, 0.5);
        display: none;
        align-items: center;
        justify-content: center;
        z-index: 10;
    }
    #actionOverlay.visible { display: flex; }
    .modal {
        background: var(--panel);
        border: 1px solid var(--border);
        border-radius: 12px;
        padding: 20px;
        width: 320px;
    }
    .modal h2 { margin: 0 0 14px; font-size: 15px; }
    .modal label { display: block; font-size: 11px; color: var(--muted); margin: 10px 0 4px; }
    .modal input, .modal select {
        width: 100%;
        padding: 8px 10px;
        border-radius: 7px;
        border: 1px solid var(--border);
        background: var(--bg);
        color: var(--text);
        font-size: 13px;
        outline: none;
    }
    .modal input:focus, .modal select:focus { border-color: var(--accent); }
    .modal-actions { display: flex; justify-content: flex-end; gap: 8px; margin-top: 16px; }
    .btn { border: none; border-radius: 7px; padding: 7px 14px; font-size: 13px; cursor: pointer; }
    .btn-cancel { background: transparent; color: var(--muted); }
    .btn-cancel:hover { background: rgba(255, 255, 255, 0.06); }
    .btn-save { background: var(--accent); color: #fff; }
    .btn-save:hover { opacity: .9; }
    .btn-danger { background: #e5484d; color: #fff; }
    .btn-danger:hover { opacity: .9; }

    /* Кастомная замена нативных window.prompt()/window.confirm() — у тех
       белая системная шапка браузера, которая выбивается из тёмной темы.
       Здесь тот же .modal, что и у формы редактирования, плюс заголовок
       со своим крестиком закрытия. */
    .modal-header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 2px; }
    .modal-header h2 { margin: 0; }
    .modal-close {
        background: transparent;
        border: none;
        color: var(--muted);
        font-size: 18px;
        line-height: 1;
        cursor: pointer;
        padding: 4px 8px;
        border-radius: 6px;
        flex: 0 0 auto;
    }
    .modal-close:hover { background: rgba(255, 255, 255, 0.08); color: var(--text); }
    .action-message { margin: 10px 0 0; font-size: 13px; color: var(--text); line-height: 1.5; }
</style>
</head>
<body>

<header>
    <h1>⭐ Закладки</h1>
    <input id="search" type="text" placeholder="Поиск по названию или адресу...">
    <div class="toolbar">
        <div class="dropdown" id="tabsMenuDropdown">
            <button class="tbtn" id="btnTabsMenu" aria-haspopup="true" aria-expanded="false">🗂 Управление вкладками ▾</button>
            <div class="dropdown-menu" id="tabsMenuList" role="menu">
                <button class="dropdown-item" id="btnImportedTabs" role="menuitem">📋 Открыть импортированные вкладки</button>
                <button class="dropdown-item" id="btnImportTabs" role="menuitem">📥 Импорт вкладок (в отдельное окно)</button>
                <button class="dropdown-item" id="btnExportTabs" role="menuitem">📤 Экспорт вкладок (HTML)</button>
            </div>
        </div>
        <button class="tbtn" id="btnNewFolder">🗀 Новая папка</button>
        <button class="tbtn tbtn-danger" id="btnClearAll">🧹 Очистить все закладки</button>
    </div>
</header>

<div id="list"></div>

<div id="overlay">
    <div class="modal" id="editModal">
        <h2>Редактировать закладку</h2>
        <label>Название</label>
        <input id="editTitle" type="text">
        <label>URL</label>
        <input id="editUrl" type="text">
        <label>Папка</label>
        <select id="editFolder"></select>
        <div class="modal-actions">
            <button class="btn btn-cancel" id="editCancel">Отмена</button>
            <button class="btn btn-save" id="editSave">Сохранить</button>
        </div>
    </div>
</div>

<div id="actionOverlay">
    <div class="modal" id="actionModal">
        <div class="modal-header">
            <h2 id="actionTitle">Заголовок</h2>
            <button class="modal-close" id="actionClose" aria-label="Закрыть">✕</button>
        </div>
        <div id="actionBody"></div>
        <div class="modal-actions">
            <button class="btn btn-cancel" id="actionCancel">Отмена</button>
            <button class="btn btn-save" id="actionConfirm">ОК</button>
        </div>
    </div>
</div>

<script src="qrc:///qtwebchannel/qwebchannel.js"></script>
<script>
    var bridge = null;
    var allBookmarks = [];   // плоский список: {title,url,icon,folderId,folderName}
    var folders = [];        // [{id,name}], в порядке отображения
    var draggedUrl = null;
    var draggedFolderId = null;
    var collapsedFolders = {}; // состояние сворачивания папок держим на время сессии страницы

    // ---------- Загрузка данных ----------

    function loadAll() {
        if (!bridge) return;
        bridge.getFolders(function (json) {
            try { folders = JSON.parse(json); } catch (e) { folders = []; }
            bridge.getBookmarks(function (json2) {
                try { allBookmarks = JSON.parse(json2); } catch (e) { allBookmarks = []; }
                applyFilter();
            });
        });
    }

    function applyFilter() {
        var q = document.getElementById('search').value.trim().toLowerCase();
        if (!q) {
            renderGrouped(allBookmarks);
            return;
        }
        var filtered = allBookmarks.filter(function (bm) {
            return bm.title.toLowerCase().indexOf(q) !== -1 || bm.url.toLowerCase().indexOf(q) !== -1;
        });
        renderFlat(filtered);
    }

    // ---------- Рендер: обычный режим (папки + сортировка) ----------

    function groupByFolder(items) {
        var byFolder = {};
        items.forEach(function (bm) {
            var key = bm.folderId || 0;
            if (!byFolder[key]) byFolder[key] = [];
            byFolder[key].push(bm);
        });
        return byFolder;
    }

    function renderGrouped(items) {
        var container = document.getElementById('list');
        container.innerHTML = '';

        if (items.length === 0 && folders.length === 0) {
            var empty = document.createElement('div');
            empty.className = 'empty';
            empty.textContent = 'Пока нет закладок';
            container.appendChild(empty);
            return;
        }

        var byFolder = groupByFolder(items);

        // Закладки без папки — без отдельного заголовка, чтобы вид не
        // усложнялся, если папок вообще нет.
        container.appendChild(buildSection(0, null, byFolder[0] || []));

        // Затем сами папки, в т.ч. пустые — чтобы в них можно было
        // перетащить закладку.
        folders.forEach(function (folder) {
            container.appendChild(buildSection(folder.id, folder.name, byFolder[folder.id] || []));
        });
    }

    function buildSection(folderId, folderName, items) {
        var section = document.createElement('div');
        section.className = 'folder-section';

        var body = document.createElement('div');
        body.className = 'folder-body';

        if (folderName !== null) {
            var header = document.createElement('div');
            header.className = 'folder-header';
            if (collapsedFolders[folderId]) {
                header.classList.add('collapsed');
                body.classList.add('collapsed');
            }

            var chevron = document.createElement('span');
            chevron.className = 'chevron';
            chevron.textContent = '▾';
            header.appendChild(chevron);

            var titleEl = document.createElement('div');
            titleEl.className = 'folder-title';
            titleEl.textContent = folderName;
            header.appendChild(titleEl);

            var countEl = document.createElement('span');
            countEl.className = 'folder-count';
            countEl.textContent = items.length;
            header.appendChild(countEl);

            var actions = document.createElement('div');
            actions.className = 'folder-actions';

            var renameBtn = document.createElement('button');
            renameBtn.className = 'icon-btn';
            renameBtn.textContent = '✏️';
            renameBtn.title = 'Переименовать папку';
            renameBtn.setAttribute('aria-label', 'Переименовать папку');
            renameBtn.addEventListener('click', function (e) {
                e.stopPropagation();
                showPromptModal({
                    title: 'Переименовать папку',
                    label: 'Название папки',
                    defaultValue: folderName,
                    confirmLabel: 'Сохранить',
                    onConfirm: function (newName) {
                        bridge.renameFolder(folderId, newName, function (err) {
                            if (err) { showAlertModal(err); } else { loadAll(); }
                        });
                    }
                });
            });
            actions.appendChild(renameBtn);

            var deleteFolderBtn = document.createElement('button');
            deleteFolderBtn.className = 'icon-btn';
            deleteFolderBtn.textContent = '🗑️';
            deleteFolderBtn.title = 'Удалить папку';
            deleteFolderBtn.setAttribute('aria-label', 'Удалить папку');
            deleteFolderBtn.addEventListener('click', function (e) {
                e.stopPropagation();
                showConfirmModal({
                    title: 'Удалить папку',
                    message: 'Удалить папку «' + folderName + '»? Закладки останутся, но окажутся вне папок.',
                    confirmLabel: 'Удалить',
                    danger: true,
                    onConfirm: function () {
                        bridge.deleteFolder(folderId, function (err) {
                            if (err) { showAlertModal(err); } else { loadAll(); }
                        });
                    }
                });
            });
            actions.appendChild(deleteFolderBtn);
            header.appendChild(actions);

            header.addEventListener('click', function () {
                collapsedFolders[folderId] = !collapsedFolders[folderId];
                header.classList.toggle('collapsed');
                body.classList.toggle('collapsed');
            });

            // Заголовок папки — тоже цель для drag&drop: перетащить сюда
            // закладку из другого раздела (в т.ч. из корня).
            header.addEventListener('dragover', function (e) {
                e.preventDefault();
                header.classList.add('folder-dragover');
            });
            header.addEventListener('dragleave', function () {
                header.classList.remove('folder-dragover');
            });
            header.addEventListener('drop', function (e) {
                e.preventDefault();
                header.classList.remove('folder-dragover');
                if (!draggedUrl) return;
                bridge.moveBookmarkToFolder(draggedUrl, folderId, function (err) {
                    if (err) { showAlertModal(err); } else { loadAll(); }
                });
            });

            section.appendChild(header);
        } else {
            // Корень (без папки) — тоже принимает drop, чтобы можно было
            // вытащить закладку обратно из папки.
            body.addEventListener('dragover', function (e) { e.preventDefault(); });
            body.addEventListener('drop', function (e) {
                e.preventDefault();
                if (!draggedUrl) return;
                bridge.moveBookmarkToFolder(draggedUrl, 0, function (err) {
                    if (err) { showAlertModal(err); } else { loadAll(); }
                });
            });
        }

        if (items.length === 0) {
            var emptyEl = document.createElement('div');
            emptyEl.className = 'folder-empty';
            emptyEl.textContent = folderName !== null ? 'Перетащите сюда закладку' : 'Нет закладок вне папок';
            if (folderName === null && folders.length === 0) emptyEl.textContent = '';
            body.appendChild(emptyEl);
        } else {
            items.forEach(function (bm, index) {
                body.appendChild(buildRow(bm, items, index, folderId, true));
            });
        }

        section.appendChild(body);
        return section;
    }

    // ---------- Рендер: режим поиска (плоский список, без папок/сортировки) ----------

    function renderFlat(items) {
        var container = document.getElementById('list');
        container.innerHTML = '';

        if (items.length === 0) {
            var empty = document.createElement('div');
            empty.className = 'empty';
            empty.textContent = 'Ничего не найдено';
            container.appendChild(empty);
            return;
        }

        var searchHeader = document.createElement('div');
        searchHeader.className = 'folder-title search-results-title';
        searchHeader.textContent = 'Результаты поиска (' + items.length + ')';
        container.appendChild(searchHeader);

        items.forEach(function (bm) {
            container.appendChild(buildRow(bm, items, 0, bm.folderId || 0, false));
        });
    }

    // ---------- Общая строка закладки ----------
    // reorderEnabled=false скрывает ручку перетаскивания и кнопки ⬆️/⬇️ —
    // используется в режиме поиска, где порядок среди отфильтрованной
    // выборки не имеет практического смысла.
    function buildRow(bm, siblings, index, folderId, reorderEnabled) {
        var row = document.createElement('div');
        row.className = 'row';
        row.draggable = !!reorderEnabled;
        row.dataset.url = bm.url;

        if (reorderEnabled) {
            row.addEventListener('dragstart', function (e) {
                draggedUrl = bm.url;
                draggedFolderId = folderId;
                row.classList.add('dragging');
                e.dataTransfer.effectAllowed = 'move';
                e.dataTransfer.setData('text/plain', bm.url);
            });
            row.addEventListener('dragend', function () {
                row.classList.remove('dragging');
                draggedUrl = null;
                draggedFolderId = null;
            });
            row.addEventListener('dragover', function (e) {
                e.preventDefault();
                e.stopPropagation(); // не даём событию всплыть до заголовка папки
                var rect = row.getBoundingClientRect();
                var before = (e.clientY - rect.top) < rect.height / 2;
                row.classList.toggle('drag-over-top', before);
                row.classList.toggle('drag-over-bottom', !before);
            });
            row.addEventListener('dragleave', function () {
                row.classList.remove('drag-over-top', 'drag-over-bottom');
            });
            row.addEventListener('drop', function (e) {
                e.preventDefault();
                e.stopPropagation();
                row.classList.remove('drag-over-top', 'drag-over-bottom');
                if (!draggedUrl || draggedUrl === bm.url) return;

                if (draggedFolderId !== folderId) {
                    // Перетащили из другой папки/корня — переносим, сервер
                    // сам поставит закладку в конец списка новой папки.
                    bridge.moveBookmarkToFolder(draggedUrl, folderId, function (err) {
                        if (err) { showAlertModal(err); } else { loadAll(); }
                    });
                    return;
                }

                var order = siblings.map(function (s) { return s.url; });
                var fromIdx = order.indexOf(draggedUrl);
                order.splice(fromIdx, 1);
                var rect = row.getBoundingClientRect();
                var before = (e.clientY - rect.top) < rect.height / 2;
                var toIdx = order.indexOf(bm.url);
                order.splice(before ? toIdx : toIdx + 1, 0, draggedUrl);

                bridge.reorderBookmarks(order, folderId, function (err) {
                    if (err) { showAlertModal(err); } else { loadAll(); }
                });
            });

            var dragHandle = document.createElement('div');
            dragHandle.className = 'drag-handle';
            dragHandle.textContent = '⠿';
            dragHandle.title = 'Перетащите, чтобы изменить порядок';
            row.appendChild(dragHandle);
        }

        var iconEl = document.createElement('div');
        iconEl.className = 'favicon';
        if (bm.icon) {
            iconEl.style.backgroundImage = 'url(' + JSON.stringify(bm.icon) + ')';
        } else {
            iconEl.classList.add('favicon-fallback');
            var letter = (bm.title || '?').trim().charAt(0).toUpperCase();
            iconEl.textContent = letter || '?';
        }
        row.appendChild(iconEl);

        var info = document.createElement('div');
        info.className = 'info';
        var titleEl = document.createElement('div');
        titleEl.className = 'title';
        titleEl.textContent = bm.title;
        var urlEl = document.createElement('div');
        urlEl.className = 'url';
        urlEl.textContent = bm.url;
        info.appendChild(titleEl);
        info.appendChild(urlEl);
        row.appendChild(info);

        var actions = document.createElement('div');
        actions.className = 'actions';

        if (reorderEnabled) {
            var upBtn = document.createElement('button');
            upBtn.className = 'icon-btn reorder-btn';
            upBtn.textContent = '⬆️';
            upBtn.title = 'Переместить выше';
            upBtn.setAttribute('aria-label', 'Переместить выше');
            upBtn.disabled = index === 0;
            upBtn.addEventListener('click', function (e) {
                e.stopPropagation();
                bridge.moveBookmarkUp(bm.url, function (err) {
                    if (err) { showAlertModal(err); } else { loadAll(); }
                });
            });
            actions.appendChild(upBtn);

            var downBtn = document.createElement('button');
            downBtn.className = 'icon-btn reorder-btn';
            downBtn.textContent = '⬇️';
            downBtn.title = 'Переместить ниже';
            downBtn.setAttribute('aria-label', 'Переместить ниже');
            downBtn.disabled = index === siblings.length - 1;
            downBtn.addEventListener('click', function (e) {
                e.stopPropagation();
                bridge.moveBookmarkDown(bm.url, function (err) {
                    if (err) { showAlertModal(err); } else { loadAll(); }
                });
            });
            actions.appendChild(downBtn);
        }

        var editBtn = document.createElement('button');
        editBtn.className = 'icon-btn';
        editBtn.title = 'Редактировать';
        editBtn.setAttribute('aria-label', 'Редактировать');
        editBtn.textContent = '\u270F\uFE0F';
        editBtn.addEventListener('click', function (e) {
            e.stopPropagation();
            openEditModal(bm);
        });
        actions.appendChild(editBtn);

        var delBtn = document.createElement('button');
        delBtn.className = 'icon-btn';
        delBtn.title = 'Удалить';
        delBtn.setAttribute('aria-label', 'Удалить');
        delBtn.textContent = '\uD83D\uDDD1\uFE0F';
        delBtn.addEventListener('click', function (e) {
            e.stopPropagation();
            showConfirmModal({
                title: 'Удалить закладку',
                message: 'Удалить закладку «' + bm.title + '»?',
                confirmLabel: 'Удалить',
                danger: true,
                onConfirm: function () {
                    bridge.deleteBookmark(bm.url, function (err) {
                        if (err) { showAlertModal(err); } else { loadAll(); }
                    });
                }
            });
        });
        actions.appendChild(delBtn);

        row.appendChild(actions);

        row.addEventListener('click', function () {
            bridge.openBookmark(bm.url);
        });

        return row;
    }

    // ---------- Кастомная модалка вместо window.prompt()/window.confirm() ----------
    // У нативных диалогов — белая системная шапка браузера, которая не
    // вписывается в тёмную тему. Ниже — одно переиспользуемое модальное
    // окно (тот же .modal, что и у формы редактирования) со своим крестиком.

    var actionConfirmHandler = null;

    function closeActionModal() {
        document.getElementById('actionOverlay').classList.remove('visible');
        actionConfirmHandler = null;
    }

    // opts: { title, label, defaultValue, confirmLabel, onConfirm(value) }
    function showPromptModal(opts) {
        var body = document.getElementById('actionBody');
        body.innerHTML = '';

        var label = document.createElement('label');
        label.textContent = opts.label || '';
        body.appendChild(label);

        var input = document.createElement('input');
        input.type = 'text';
        input.value = opts.defaultValue || '';
        input.addEventListener('keydown', function (e) {
            if (e.key === 'Enter' && actionConfirmHandler) actionConfirmHandler();
        });
        body.appendChild(input);

        document.getElementById('actionTitle').textContent = opts.title || '';
        document.getElementById('actionCancel').style.display = '';
        var confirmBtn = document.getElementById('actionConfirm');
        confirmBtn.textContent = opts.confirmLabel || 'Сохранить';
        confirmBtn.className = 'btn btn-save';

        actionConfirmHandler = function () {
            var value = input.value.trim();
            if (!value) return; // тихо игнорируем пустое значение, как и раньше делал prompt()
            closeActionModal();
            opts.onConfirm(value);
        };

        document.getElementById('actionOverlay').classList.add('visible');
        // Фокус на поле ввода — Enter в нём уже обрабатывается отдельным
        // слушателем выше, поэтому саму кнопку "ОК" фокусить не нужно.
        input.focus();
        input.select();
    }

    // opts: { title, message, confirmLabel, danger, onConfirm() }
    function showConfirmModal(opts) {
        var body = document.getElementById('actionBody');
        body.innerHTML = '';

        var msg = document.createElement('p');
        msg.className = 'action-message';
        msg.textContent = opts.message || '';
        body.appendChild(msg);

        document.getElementById('actionTitle').textContent = opts.title || '';
        document.getElementById('actionCancel').style.display = '';
        var confirmBtn = document.getElementById('actionConfirm');
        confirmBtn.textContent = opts.confirmLabel || 'ОК';
        confirmBtn.className = opts.danger ? 'btn btn-danger' : 'btn btn-save';

        actionConfirmHandler = function () {
            closeActionModal();
            opts.onConfirm();
        };

        document.getElementById('actionOverlay').classList.add('visible');
        // Фокус на кнопке "ОК" — так Enter срабатывает штатным поведением
        // браузера для сфокусированной кнопки, без риска задвоить вызов
        // через отдельный глобальный обработчик клавиш.
        confirmBtn.focus();
    }

    // Замена window.alert() — только сообщение и кнопка "ОК", без "Отмена"
    // (отменять уже случившуюся ошибку не имеет смысла).
    function showAlertModal(message, title) {
        var body = document.getElementById('actionBody');
        body.innerHTML = '';

        var msg = document.createElement('p');
        msg.className = 'action-message';
        msg.textContent = message || '';
        body.appendChild(msg);

        document.getElementById('actionTitle').textContent = title || 'Ошибка';
        document.getElementById('actionCancel').style.display = 'none';
        var confirmBtn = document.getElementById('actionConfirm');
        confirmBtn.textContent = 'ОК';
        confirmBtn.className = 'btn btn-save';

        actionConfirmHandler = function () {
            closeActionModal();
        };

        document.getElementById('actionOverlay').classList.add('visible');
        confirmBtn.focus();
    }

    // ---------- Модалка редактирования ----------

    var editingOldUrl = null;

    function populateFolderSelect(selectedFolderId) {
        var select = document.getElementById('editFolder');
        select.innerHTML = '';

        var noneOpt = document.createElement('option');
        noneOpt.value = '0';
        noneOpt.textContent = '— Без папки —';
        select.appendChild(noneOpt);

        folders.forEach(function (f) {
            var opt = document.createElement('option');
            opt.value = String(f.id);
            opt.textContent = f.name;
            select.appendChild(opt);
        });

        select.value = String(selectedFolderId || 0);
    }

    function openEditModal(bm) {
        editingOldUrl = bm.url;
        document.getElementById('editTitle').value = bm.title;
        document.getElementById('editUrl').value = bm.url;
        populateFolderSelect(bm.folderId);
        document.getElementById('overlay').classList.add('visible');
        document.getElementById('editTitle').focus();
    }

    function closeEditModal() {
        document.getElementById('overlay').classList.remove('visible');
        editingOldUrl = null;
    }

    function saveEdit() {
        var newTitle = document.getElementById('editTitle').value.trim();
        var newUrl = document.getElementById('editUrl').value.trim();
        var newFolderId = parseInt(document.getElementById('editFolder').value, 10) || 0;
        if (!newTitle || !newUrl || !editingOldUrl) return;

        bridge.editBookmark(editingOldUrl, newTitle, newUrl, function (err) {
            if (err) { showAlertModal(err); return; }
            // editBookmark мог поменять сам url — папку проставляем уже по новому.
            bridge.moveBookmarkToFolder(newUrl, newFolderId, function (err2) {
                if (err2) { showAlertModal(err2); }
                closeEditModal();
                loadAll();
            });
        });
    }

    // ---------- Инициализация ----------

    document.addEventListener('DOMContentLoaded', function () {
        document.getElementById('search').addEventListener('input', applyFilter);
        document.getElementById('editSave').addEventListener('click', saveEdit);
        document.getElementById('editCancel').addEventListener('click', closeEditModal);
        document.getElementById('overlay').addEventListener('click', function (e) {
            if (e.target === document.getElementById('overlay')) closeEditModal();
        });
        document.getElementById('editUrl').addEventListener('keydown', function (e) {
            if (e.key === 'Enter') saveEdit();
        });

        document.getElementById('actionClose').addEventListener('click', closeActionModal);
        document.getElementById('actionCancel').addEventListener('click', closeActionModal);
        document.getElementById('actionConfirm').addEventListener('click', function () {
            if (actionConfirmHandler) actionConfirmHandler();
        });
        document.getElementById('actionOverlay').addEventListener('click', function (e) {
            if (e.target === document.getElementById('actionOverlay')) closeActionModal();
        });

        // Дропдаун "Управление вкладками"
        var tabsMenuBtn = document.getElementById('btnTabsMenu');
        var tabsMenuList = document.getElementById('tabsMenuList');
        tabsMenuBtn.addEventListener('click', function (e) {
            e.stopPropagation();
            var isOpen = tabsMenuList.classList.toggle('visible');
            tabsMenuBtn.setAttribute('aria-expanded', isOpen ? 'true' : 'false');
        });
        document.addEventListener('click', function () {
            tabsMenuList.classList.remove('visible');
            tabsMenuBtn.setAttribute('aria-expanded', 'false');
        });
        document.addEventListener('keydown', function (e) {
            if (e.key === 'Escape') {
                tabsMenuList.classList.remove('visible');
                tabsMenuBtn.setAttribute('aria-expanded', 'false');
                closeActionModal();
            }
        });

        document.getElementById('btnImportedTabs').addEventListener('click', function () {
            bridge.openImportedTabs();
        });
        document.getElementById('btnImportTabs').addEventListener('click', function () {
            bridge.importTabs();
        });
        document.getElementById('btnExportTabs').addEventListener('click', function () {
            bridge.exportTabs();
        });

        document.getElementById('btnNewFolder').addEventListener('click', function () {
            showPromptModal({
                title: 'Новая папка',
                label: 'Название папки',
                confirmLabel: 'Создать',
                onConfirm: function (name) {
                    bridge.createFolder(name, function (err) {
                        if (err) { showAlertModal(err); } else { loadAll(); }
                    });
                }
            });
        });

        document.getElementById('btnClearAll').addEventListener('click', function () {
            showConfirmModal({
                title: 'Очистить все закладки',
                message: 'Удалить ВСЕ закладки? Это действие необратимо.',
                confirmLabel: 'Очистить всё',
                danger: true,
                onConfirm: function () {
                    bridge.clearBookmarks(function () { loadAll(); });
                }
            });
        });

        // Готовность qt.webChannelTransport подстрахована повтором — раньше при
        // не готовом транспорте в момент DOMContentLoaded мост просто не
        // подключался вообще, без единого шанса на повтор.
        var bookmarksWebChannelStarted = false;
        function initBookmarksBridge() {
            if (bookmarksWebChannelStarted) return;
            if (typeof QWebChannel === 'undefined' || typeof qt === 'undefined' || !qt.webChannelTransport) {
                console.warn('[StormBookmarks] qt.webChannelTransport ещё не готов, жду...');
                setTimeout(initBookmarksBridge, 200);
                return;
            }
            bookmarksWebChannelStarted = true;
            console.warn('[StormBookmarks] qt.webChannelTransport найден, создаём QWebChannel...');

            new QWebChannel(qt.webChannelTransport, function (channel) {
                bridge = channel.objects.bookmarksBridge;
                if (!bridge) {
                    console.warn('[StormBookmarks] ОШИБКА: channel.objects.bookmarksBridge не найден!');
                    return;
                }
                console.warn('[StormBookmarks] bookmarksBridge подключён успешно');
                loadAll();
            });
        }
        initBookmarksBridge();
    });
</script>

</body>
</html>
)BMKHTML");
}