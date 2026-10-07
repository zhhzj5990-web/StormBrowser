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
#include <QDateTime>
#include <QTabWidget>
#include <QWebEngineView>

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
        // Менеджер сессий (волна R5): снимки открытых вкладок основного окна.
        // UNIQUE на name даёт "эффект транзакции" — повторное имя не создать,
        // а INSERT просто вернёт false, что и станет текстом ошибки.
        q.exec(
            "CREATE TABLE IF NOT EXISTS saved_sessions ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name TEXT NOT NULL UNIQUE,"
            "  created_at INTEGER NOT NULL,"
            "  tabs_json TEXT NOT NULL"
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

// ============================ Менеджер сессий ============================

QString BookmarksBridge::getSessions()
{
    QJsonArray arr;
    QSqlQuery query;
    query.exec("SELECT id, name, created_at, tabs_json FROM saved_sessions ORDER BY created_at DESC, id DESC");
    while (query.next()) {
        QJsonObject obj;
        obj["id"] = query.value(0).toInt();
        obj["name"] = query.value(1).toString();
        const qint64 ts = query.value(2).toLongLong();
        obj["createdText"] = QDateTime::fromMSecsSinceEpoch(ts).toString(QStringLiteral("dd.MM.yyyy HH:mm"));
        obj["tabCount"] = QJsonDocument::fromJson(query.value(3).toByteArray()).array().size();
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString BookmarksBridge::saveCurrentSession(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return u8"Название сессии не может быть пустым.";

    QTabWidget* tabs = m_mw->getTabWidget();
    if (!tabs || tabs->count() == 0) return u8"Нет открытых вкладок для сохранения.";

    // Пустые вкладки (newtab/about:blank) в сессию не пишем — при
    // восстановлении они не несут информации. Активную вкладку помечаем
    // "active":true, чтобы restoreSession мог вернуть фокус на неё.
    const int current = tabs->currentIndex();
    bool activeSaved = false;
    QJsonArray tabsArr;
    for (int i = 0; i < tabs->count(); ++i) {
        auto* view = qobject_cast<QWebEngineView*>(tabs->widget(i));
        if (!view) continue;

        const QString url = view->url().toString();
        if (url.isEmpty() || url == QLatin1String("about:blank")
            || url == QLatin1String("storm://newtab")) {
            continue;
        }

        QJsonObject t;
        t["title"] = view->title().isEmpty() ? url : view->title();
        t["url"] = url;
        // v1.2.9: закреплённые вкладки сохраняются с флагом — при
        // восстановлении они снова встают компактными в начало полосы.
        if (view->property("tabPinned").toBool()) {
            t["pinned"] = true;
        }
        if (i == current) {
            t["active"] = true;
            activeSaved = true;
        }
        tabsArr.append(t);
    }

    if (tabsArr.isEmpty()) {
        return u8"Сохранять нечего: во всех вкладках пустые страницы.";
    }
    // Активной была пустая вкладка — пометим первую сохранённую, чтобы после
    // восстановления пользователь не остался на посторонней вкладке.
    if (!activeSaved) {
        QJsonObject first = tabsArr.at(0).toObject();
        first["active"] = true;
        tabsArr.replace(0, first);
    }

    QSqlQuery dup;
    dup.prepare("SELECT id FROM saved_sessions WHERE name = :name");
    dup.bindValue(":name", trimmed);
    if (dup.exec() && dup.next()) {
        return u8"Сессия с таким названием уже есть — придумайте другое имя.";
    }

    QSqlQuery insert;
    insert.prepare(
        "INSERT INTO saved_sessions (name, created_at, tabs_json) "
        "VALUES (:name, :createdAt, :tabs)");
    insert.bindValue(":name", trimmed);
    insert.bindValue(":createdAt", QDateTime::currentMSecsSinceEpoch());
    insert.bindValue(":tabs", QString::fromUtf8(QJsonDocument(tabsArr).toJson(QJsonDocument::Compact)));
    if (!insert.exec()) return u8"Не удалось сохранить сессию (ошибка базы данных).";

    return QString(); // успех
}

QString BookmarksBridge::restoreSession(int sessionId)
{
    QJsonObject result;
    auto fail = [&result](const QString& msg) {
        result["ok"] = false;
        result["error"] = msg;
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    };

    QTabWidget* tabs = m_mw->getTabWidget();
    if (!tabs) return fail(u8"Окно браузера недоступно.");

    QSqlQuery query;
    query.prepare("SELECT tabs_json FROM saved_sessions WHERE id = :id");
    query.bindValue(":id", sessionId);
    if (!query.exec() || !query.next()) return fail(u8"Сессия не найдена (возможно, уже удалена).");

    const QJsonArray tabsArr = QJsonDocument::fromJson(query.value(0).toByteArray()).array();
    if (tabsArr.isEmpty()) return fail(u8"В этой сессии нет вкладок.");

    // Восстановление ДОБАВЛЯЕТ вкладки к уже открытым (как «Открыть
    // импортированные вкладки»), ничего не закрывая — вдруг то, что открыто
    // сейчас, тоже нужно. Лимит 20 — чтобы не задушить ОЗУ десятком тяжёлых
    // сайтов, восстановленных одним кликом.
    const int kMaxRestore = 20;
    const int firstNewIndex = tabs->count();
    int opened = 0;
    int activeIndex = -1;
    bool limited = false;

    for (const auto& v : tabsArr) {
        if (opened >= kMaxRestore) { limited = true; break; }
        const QJsonObject t = v.toObject();
        const QString url = t.value("url").toString();
        if (url.isEmpty()) continue;

        m_mw->addNewTab(QUrl(url));
        // v1.2.9: восстановление закреплённого состояния (текст вкладки
        // гасится, ширина — компактная, см. MainWindow::setTabPinned).
        if (t.value("pinned").toBool()) {
            QTabWidget* tw = m_mw->getTabWidget();
            if (QWidget* added = tw->widget(tw->count() - 1)) {
                m_mw->setTabPinned(added, true);
            }
        }
        if (t.value("active").toBool()) activeIndex = firstNewIndex + opened;
        ++opened;
    }

    if (opened == 0) return fail(u8"В этой сессии нет вкладок.");

    // addNewTab обычно делает вкладку текущей; после цикла явно возвращаем
    // фокус на сохранённую активную (или на первую из восстановленных).
    if (activeIndex < 0) activeIndex = firstNewIndex;
    if (activeIndex >= tabs->count()) activeIndex = tabs->count() - 1;
    tabs->setCurrentIndex(activeIndex);

    result["ok"] = true;
    result["opened"] = opened;
    result["limited"] = limited;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------------
// v1.2.9: быстрые операции для горячих клавиш Ctrl+Alt+S / Ctrl+Alt+R.
// ---------------------------------------------------------------------
QString BookmarksBridge::quickSaveCurrentSession(QString* nameOut) {
    // База имени — как suggestSessionName() на странице закладок
    // («Сессия от ДД.ММ ЧЧ:ММ»); при коллизии добавляем « (2)», « (3)»...
    const QString base = QDateTime::currentDateTime().toString(u8"Сессия от dd.MM HH:mm");
    for (int attempt = 0; attempt < 50; ++attempt) {
        const QString candidate = (attempt == 0)
            ? base
            : QString(u8"%1 (%2)").arg(base).arg(attempt + 1);
        const QString err = saveCurrentSession(candidate);
        if (err.isEmpty()) {
            if (nameOut) *nameOut = candidate;
            return QString(); // успех
        }
        // «уже есть» — пробуем следующий суффикс; любую другую причину
        // (нет вкладок, ошибка БД) возвращаем сразу, там ретраи бессмысленны
        if (!err.contains(u8"уже есть")) {
            return err;
        }
    }
    return u8"Не удалось подобрать свободное имя для сессии.";
}

QString BookmarksBridge::quickRestoreLatestSession(int* openedOut, QString* nameOut) {
    QSqlQuery q;
    q.prepare("SELECT id, name FROM saved_sessions ORDER BY created_at DESC, id DESC LIMIT 1");
    if (!q.exec() || !q.next()) {
        return u8"Сохранённых сессий пока нет — сначала сохраните одну (Ctrl+Alt+S).";
    }
    const int sessionId = q.value(0).toInt();
    const QString name = q.value(1).toString();

    const QString res = restoreSession(sessionId); // JSON {"ok":..,"opened":N}
    const QJsonObject obj = QJsonDocument::fromJson(res.toUtf8()).object();
    if (!obj.value("ok").toBool()) {
        return obj.value("error").toString(u8"Не удалось восстановить сессию.");
    }
    if (openedOut) *openedOut = obj.value("opened").toInt();
    if (nameOut) *nameOut = name;
    return QString();
}

QString BookmarksBridge::deleteSession(int sessionId)
{
    QSqlQuery del;
    del.prepare("DELETE FROM saved_sessions WHERE id = :id");
    del.bindValue(":id", sessionId);
    if (!del.exec() || del.numRowsAffected() == 0) {
        return u8"Не удалось удалить сессию (возможно, её уже нет).";
    }
    return QString();
}

QString BookmarksBridge::renameSession(int sessionId, const QString& newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) return u8"Название сессии не может быть пустым.";

    QSqlQuery dup;
    dup.prepare("SELECT id FROM saved_sessions WHERE name = :name AND id <> :id");
    dup.bindValue(":name", trimmed);
    dup.bindValue(":id", sessionId);
    if (dup.exec() && dup.next()) return u8"Сессия с таким названием уже есть.";

    QSqlQuery update;
    update.prepare("UPDATE saved_sessions SET name = :name WHERE id = :id");
    update.bindValue(":name", trimmed);
    update.bindValue(":id", sessionId);
    if (!update.exec() || update.numRowsAffected() == 0) {
        return u8"Не удалось переименовать сессию (возможно, её уже нет).";
    }
    return QString();
}
