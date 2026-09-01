#include "BookmarksBridge.h"
#include "MainWindow.h"
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QPixmap>
#include <QBuffer>
#include <QByteArray>
#include <QUrl>
#include <QHash>
#include <QVariant>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

namespace {

    // Миграция идемпотентна: ALTER TABLE ... ADD COLUMN бросает ошибку,
    // если колонка уже есть — это ожидаемо и намеренно игнорируется
    // (exec() просто вернёт false, ничего не ломая). Вызывается один раз
    // из конструктора моста, так что схема гарантированно готова до
    // первого обращения со страницы.
    void ensureSchema()
    {
        QSqlQuery q;
        q.exec("ALTER TABLE bookmarks ADD COLUMN folder_id INTEGER");
        q.exec("ALTER TABLE bookmarks ADD COLUMN sort_order INTEGER NOT NULL DEFAULT 0");
        q.exec(
            "CREATE TABLE IF NOT EXISTS bookmark_folders ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name TEXT NOT NULL UNIQUE,"
            "  sort_order INTEGER NOT NULL DEFAULT 0"
            ")"
        );
    }

    // Общая логика для moveBookmarkUp/moveBookmarkDown — находит соседа
    // закладки в её же папке (по sort_order) в нужном направлении и меняет
    // их sort_order местами. Если закладка уже с краю списка — тихо ничего
    // не делает (это не ошибка, а нормальная граница).
    QString swapWithNeighbor(const QString& url, bool moveUp)
    {
        QSqlQuery current;
        current.prepare("SELECT id, folder_id, sort_order FROM bookmarks WHERE url = :url");
        current.bindValue(":url", url);
        if (!current.exec() || !current.next()) return u8"Закладка не найдена.";

        const int currentId = current.value(0).toInt();
        const QVariant folderId = current.value(1);
        const int currentOrder = current.value(2).toInt();

        QString sql = "SELECT id, sort_order FROM bookmarks WHERE ";
        sql += folderId.isNull() ? QStringLiteral("folder_id IS NULL") : QStringLiteral("folder_id = :folderId");
        sql += moveUp ? QStringLiteral(" AND sort_order < :order ORDER BY sort_order DESC LIMIT 1")
                      : QStringLiteral(" AND sort_order > :order ORDER BY sort_order ASC LIMIT 1");

        QSqlQuery neighbor;
        neighbor.prepare(sql);
        if (!folderId.isNull()) neighbor.bindValue(":folderId", folderId);
        neighbor.bindValue(":order", currentOrder);
        if (!neighbor.exec() || !neighbor.next()) return QString(); // уже с краю

        const int neighborId = neighbor.value(0).toInt();
        const int neighborOrder = neighbor.value(1).toInt();

        QSqlQuery updateCurrent;
        updateCurrent.prepare("UPDATE bookmarks SET sort_order = :order WHERE id = :id");
        updateCurrent.bindValue(":order", neighborOrder);
        updateCurrent.bindValue(":id", currentId);
        if (!updateCurrent.exec()) return u8"Не удалось изменить порядок.";

        QSqlQuery updateNeighbor;
        updateNeighbor.prepare("UPDATE bookmarks SET sort_order = :order WHERE id = :id");
        updateNeighbor.bindValue(":order", currentOrder);
        updateNeighbor.bindValue(":id", neighborId);
        if (!updateNeighbor.exec()) return u8"Не удалось изменить порядок.";

        return QString();
    }

} // namespace

BookmarksBridge::BookmarksBridge(MainWindow* mw, QObject* parent)
    : QObject(parent), m_mw(mw)
{
    ensureSchema();
}

QString BookmarksBridge::getBookmarks()
{
    // Иконки берём из bookmarksMenu (единственный источник, куда
    // MainWindow::loadBookmarksIntoMenu() кладёт favicon, в т.ч.
    // дозагрузившийся асинхронно) — как и раньше. А вот порядок и
    // принадлежность к папке теперь авторитетно хранятся в БД, поэтому
    // сам список и его сортировку строим SQL-запросом, а не по bookmarksMenu.
    QHash<QString, QIcon> iconByUrl;
    if (QMenu* bookmarksMenu = m_mw->findChild<QMenu*>("bookmarksMenu")) {
        for (QAction* a : bookmarksMenu->actions()) {
            if (a->isSeparator()) continue;
            iconByUrl.insert(a->data().toString(), a->icon());
        }
    }

    QJsonArray arr;
    QSqlQuery query;
    query.exec(
        "SELECT b.title, b.url, b.folder_id, COALESCE(f.name, ''), b.sort_order "
        "FROM bookmarks b LEFT JOIN bookmark_folders f ON f.id = b.folder_id "
        "ORDER BY (b.folder_id IS NULL) DESC, f.sort_order, b.sort_order, b.id"
    );
    while (query.next()) {
        const QString url = query.value(1).toString();

        QJsonObject obj;
        obj["title"] = query.value(0).toString();
        obj["url"] = url;
        obj["folderId"] = query.value(2).isNull() ? 0 : query.value(2).toInt();
        obj["folderName"] = query.value(3).toString();

        QString iconDataUrl;
        const QIcon icon = iconByUrl.value(url);
        if (!icon.isNull()) {
            const QPixmap pix = icon.pixmap(32, 32);
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            pix.save(&buffer, "PNG");
            iconDataUrl = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
        }
        obj["icon"] = iconDataUrl;

        arr.append(obj);
    }

    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString BookmarksBridge::getFolders()
{
    QJsonArray arr;
    QSqlQuery query;
    query.exec("SELECT id, name FROM bookmark_folders ORDER BY sort_order, name");
    while (query.next()) {
        QJsonObject obj;
        obj["id"] = query.value(0).toInt();
        obj["name"] = query.value(1).toString();
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void BookmarksBridge::openBookmark(const QString& url)
{
    if (url.isEmpty()) return;
    m_mw->addNewTab(QUrl(url));
}

void BookmarksBridge::openImportedTabs()
{
    m_mw->openImportedTabs();
}

void BookmarksBridge::importTabs()
{
    m_mw->importTabs();
}

void BookmarksBridge::exportTabs()
{
    m_mw->exportTabs();
}

void BookmarksBridge::clearBookmarks()
{
    m_mw->clearBookmarks();
}

QString BookmarksBridge::deleteBookmark(const QString& url)
{
    if (url.isEmpty()) return QString();

    QSqlQuery query;
    query.prepare("DELETE FROM bookmarks WHERE url = :url");
    query.bindValue(":url", url);
    if (!query.exec()) {
        return u8"Не удалось удалить закладку.";
    }

    m_mw->loadBookmarksIntoMenu();
    return QString(); // успех
}

QString BookmarksBridge::editBookmark(const QString& oldUrl, const QString& newTitle, const QString& newUrl)
{
    const QString trimmedUrl = newUrl.trimmed();
    if (trimmedUrl.isEmpty()) return u8"URL не может быть пустым.";

    if (trimmedUrl != oldUrl) {
        QSqlQuery dupQuery;
        dupQuery.prepare("SELECT id FROM bookmarks WHERE url = :url");
        dupQuery.bindValue(":url", trimmedUrl);
        if (dupQuery.exec() && dupQuery.next()) {
            return u8"Закладка с таким URL уже существует.";
        }
    }

    // folder_id/sort_order сюда не входят и остаются как были —
    // редактирование названия/URL не должно сбрасывать папку и позицию.
    QSqlQuery updateQuery;
    updateQuery.prepare("UPDATE bookmarks SET title = :title, url = :url WHERE url = :oldUrl");
    updateQuery.bindValue(":title", newTitle);
    updateQuery.bindValue(":url", trimmedUrl);
    updateQuery.bindValue(":oldUrl", oldUrl);
    if (!updateQuery.exec()) {
        return u8"Не удалось обновить закладку.";
    }

    m_mw->loadBookmarksIntoMenu();
    return QString(); // успех
}

QString BookmarksBridge::createFolder(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return u8"Название папки не может быть пустым.";

    QSqlQuery dup;
    dup.prepare("SELECT id FROM bookmark_folders WHERE name = :name");
    dup.bindValue(":name", trimmed);
    if (dup.exec() && dup.next()) return u8"Папка с таким названием уже есть.";

    QSqlQuery maxOrder;
    maxOrder.exec("SELECT COALESCE(MAX(sort_order), -1) FROM bookmark_folders");
    int nextOrder = 0;
    if (maxOrder.next()) nextOrder = maxOrder.value(0).toInt() + 1;

    QSqlQuery insert;
    insert.prepare("INSERT INTO bookmark_folders (name, sort_order) VALUES (:name, :sortOrder)");
    insert.bindValue(":name", trimmed);
    insert.bindValue(":sortOrder", nextOrder);
    if (!insert.exec()) return u8"Не удалось создать папку.";

    return QString();
}

QString BookmarksBridge::renameFolder(int folderId, const QString& newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) return u8"Название папки не может быть пустым.";

    QSqlQuery dup;
    dup.prepare("SELECT id FROM bookmark_folders WHERE name = :name AND id <> :id");
    dup.bindValue(":name", trimmed);
    dup.bindValue(":id", folderId);
    if (dup.exec() && dup.next()) return u8"Папка с таким названием уже есть.";

    QSqlQuery update;
    update.prepare("UPDATE bookmark_folders SET name = :name WHERE id = :id");
    update.bindValue(":name", trimmed);
    update.bindValue(":id", folderId);
    if (!update.exec()) return u8"Не удалось переименовать папку.";

    return QString();
}

QString BookmarksBridge::deleteFolder(int folderId)
{
    // Закладки из удалённой папки не удаляем — переносим "без папки",
    // чтобы случайное удаление папки не уничтожило сами закладки.
    QSqlQuery moveOut;
    moveOut.prepare("UPDATE bookmarks SET folder_id = NULL WHERE folder_id = :id");
    moveOut.bindValue(":id", folderId);
    moveOut.exec();

    QSqlQuery del;
    del.prepare("DELETE FROM bookmark_folders WHERE id = :id");
    del.bindValue(":id", folderId);
    if (!del.exec()) return u8"Не удалось удалить папку.";

    return QString();
}

QString BookmarksBridge::moveBookmarkToFolder(const QString& url, int folderId)
{
    QString maxSql = "SELECT COALESCE(MAX(sort_order), -1) FROM bookmarks WHERE ";
    maxSql += (folderId > 0) ? QStringLiteral("folder_id = :folderId") : QStringLiteral("folder_id IS NULL");

    QSqlQuery maxOrder;
    maxOrder.prepare(maxSql);
    if (folderId > 0) maxOrder.bindValue(":folderId", folderId);

    int nextOrder = 0;
    if (maxOrder.exec() && maxOrder.next()) nextOrder = maxOrder.value(0).toInt() + 1;

    QSqlQuery update;
    update.prepare("UPDATE bookmarks SET folder_id = :folderId, sort_order = :sortOrder WHERE url = :url");
    update.bindValue(":folderId", folderId > 0 ? QVariant(folderId) : QVariant());
    update.bindValue(":sortOrder", nextOrder);
    update.bindValue(":url", url);
    if (!update.exec()) return u8"Не удалось переместить закладку.";

    return QString();
}

QString BookmarksBridge::reorderBookmarks(const QVariantList& orderedUrls, int folderId)
{
    QSqlDatabase::database().transaction();

    for (int i = 0; i < orderedUrls.size(); ++i) {
        QString sql = "UPDATE bookmarks SET sort_order = :sortOrder WHERE url = :url AND ";
        sql += (folderId > 0) ? QStringLiteral("folder_id = :folderId") : QStringLiteral("folder_id IS NULL");

        QSqlQuery update;
        update.prepare(sql);
        update.bindValue(":sortOrder", i);
        update.bindValue(":url", orderedUrls.at(i).toString());
        if (folderId > 0) update.bindValue(":folderId", folderId);

        if (!update.exec()) {
            QSqlDatabase::database().rollback();
            return u8"Не удалось сохранить новый порядок закладок.";
        }
    }

    QSqlDatabase::database().commit();
    return QString();
}

QString BookmarksBridge::moveBookmarkUp(const QString& url)
{
    return swapWithNeighbor(url, true);
}

QString BookmarksBridge::moveBookmarkDown(const QString& url)
{
    return swapWithNeighbor(url, false);
}
