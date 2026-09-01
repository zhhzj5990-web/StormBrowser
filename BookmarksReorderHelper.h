#pragma once
#include <QString>
#include <QMenu>
#include <QAction>
#include <QVariantList>
#include "BookmarksBridge.h"

// Общая логика "закладку A перетащили на закладку B" — используется и
// BookmarksBar (панель), и BookmarksWindow (окно из главного меню), чтобы
// перетаскивание в обоих местах вело себя ТОЧНО так же, как drag&drop на
// странице storm://bookmarks (см. buildRow()/drop-обработчик в
// BookmarksPageHtml.h):
//   - если закладку перетащили в другую папку (или в корень / из корня) —
//     переносим её в папку цели через moveBookmarkToFolder(), сервер сам
//     ставит её в конец списка новой папки;
//   - если папка та же — пересобираем порядок закладок ВНУТРИ этой папки и
//     отправляем reorderBookmarks().
//
// Сделано inline и целиком в заголовке — по тому же принципу, что и
// BookmarksPageHtml.h: не требует правки списка исходников сборки ради
// одной небольшой функции на два вызывающих файла.
//
// bookmarksMenu — источник актуального порядка и принадлежности к папке:
// url лежит в QAction::data(), folderId — в QAction::property("folderId")
// (см. MainWindow::loadBookmarksIntoMenu()).
//
// Возвращает пустую строку при успехе (или если перетаскивание было
// "пустым" — например, отпустили на самой себе); иначе — текст ошибки от
// BookmarksBridge для показа пользователю. Само обновление UI (loadBookmarksIntoMenu()/
// refreshList()) остаётся на вызывающей стороне.
inline QString reorderDroppedBookmark(BookmarksBridge* bridge, QMenu* bookmarksMenu,
    const QString& draggedUrl, const QString& targetUrl, bool dropBefore)
{
    if (!bridge || !bookmarksMenu || draggedUrl.isEmpty() || targetUrl.isEmpty() || draggedUrl == targetUrl) {
        return QString();
    }

    QAction* draggedAction = nullptr;
    QAction* targetAction = nullptr;
    for (QAction* a : bookmarksMenu->actions()) {
        if (a->isSeparator()) continue;
        if (a->data().toString() == draggedUrl) draggedAction = a;
        if (a->data().toString() == targetUrl) targetAction = a;
    }
    if (!draggedAction || !targetAction) return QString();

    const int draggedFolderId = draggedAction->property("folderId").toInt();
    const int targetFolderId = targetAction->property("folderId").toInt();

    if (draggedFolderId != targetFolderId) {
        return bridge->moveBookmarkToFolder(draggedUrl, targetFolderId);
    }

    // Текущий порядок url-ов ВНУТРИ этой же папки (не всей панели/окна
    // целиком) — reorderBookmarks() переставляет закладки только в пределах
    // одной папки, как и на странице.
    QVariantList order;
    for (QAction* a : bookmarksMenu->actions()) {
        if (a->isSeparator()) continue;
        if (a->property("folderId").toInt() == draggedFolderId) {
            order << a->data().toString();
        }
    }

    int fromIdx = -1;
    for (int i = 0; i < order.size(); ++i) {
        if (order.at(i).toString() == draggedUrl) { fromIdx = i; break; }
    }
    if (fromIdx < 0) return QString();
    order.removeAt(fromIdx);

    int targetIdx = -1;
    for (int i = 0; i < order.size(); ++i) {
        if (order.at(i).toString() == targetUrl) { targetIdx = i; break; }
    }
    if (targetIdx < 0) return QString();
    order.insert(dropBefore ? targetIdx : targetIdx + 1, draggedUrl);

    return bridge->reorderBookmarks(order, draggedFolderId);
}

// Первая/последняя ли закладка в СВОЕЙ папке — чтобы задизейблить пункты
// контекстного меню "Переместить выше"/"Переместить ниже" на границах
// списка, как дизейблятся кнопки ⬆️/⬇️ на странице storm://bookmarks.
inline void bookmarkFolderPosition(QMenu* bookmarksMenu, const QString& url, bool* isFirst, bool* isLast)
{
    if (isFirst) *isFirst = true;
    if (isLast) *isLast = true;
    if (!bookmarksMenu) return;

    QAction* thisAction = nullptr;
    for (QAction* a : bookmarksMenu->actions()) {
        if (!a->isSeparator() && a->data().toString() == url) { thisAction = a; break; }
    }
    if (!thisAction) return;

    const int folderId = thisAction->property("folderId").toInt();
    QStringList siblingUrls;
    for (QAction* a : bookmarksMenu->actions()) {
        if (a->isSeparator()) continue;
        if (a->property("folderId").toInt() == folderId) siblingUrls << a->data().toString();
    }

    const int idx = siblingUrls.indexOf(url);
    if (isFirst) *isFirst = (idx <= 0);
    if (isLast) *isLast = (idx < 0 || idx >= siblingUrls.size() - 1);
}
