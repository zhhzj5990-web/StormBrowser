#pragma once
#include <QObject>
#include <QString>
#include <QVariant> // QVariantList для orderedUrls (безопаснее QStringList над WebChannel)

class MainWindow;

// Мост между JS-страницей "storm://bookmarks" (см. BookmarksPageHtml.h) и
// C++ — тот же паттерн WebChannel-моста, что уже используется для
// storm://settings (SettingsBridge) и storm://cloud (StormCloudBridge):
// см. MainWindow::addNewTab().
//
// Закладки открываются теперь именно вкладкой (QWebEngineView), а не
// отдельным окном — так же, как и обычные сайты.
//
// ДОБАВЛЕНО в этой версии: папки (плоская структура, без вложенности) и
// ручная сортировка (drag&drop на странице + кнопки вверх/вниз). Оба
// набора функций работают только с таблицами bookmarks/bookmark_folders
// напрямую — на bookmarksMenu (используется в BookmarksBar/BookmarksWindow)
// они не влияют и специально его не перестраивают: сортировка и папки
// пока показываются только на странице storm://bookmarks, панель и окно
// закладок — отдельная задача.
//
// Схема БД (миграция идемпотентна, см. ensureSchema() в .cpp):
//   ALTER TABLE bookmarks ADD COLUMN folder_id INTEGER;      -- NULL = без папки
//   ALTER TABLE bookmarks ADD COLUMN sort_order INTEGER NOT NULL DEFAULT 0;
//   CREATE TABLE bookmark_folders (id INTEGER PRIMARY KEY AUTOINCREMENT,
//                                   name TEXT NOT NULL UNIQUE,
//                                   sort_order INTEGER NOT NULL DEFAULT 0);
//
// ВАЖНО: там, где в другом месте кода создаётся новая закладка (не в этом
// файле — код добавления не был под рукой), стоит проставлять
// sort_order = MAX(sort_order)+1 в той же папке, иначе новые закладки будут
// получать sort_order по умолчанию (0) и путаться в порядке с другими.
class BookmarksBridge : public QObject {
    Q_OBJECT
public:
    explicit BookmarksBridge(MainWindow* mw, QObject* parent = nullptr);

public slots:
    // JSON-массив: [{"title":"...", "url":"...", "icon":"data:image/png;base64,...",
    //                "folderId":0, "folderName":""}]
    // folderId == 0 означает "без папки". Иконка берётся из того же
    // bookmarksMenu, что и панель закладок (BookmarksBar) — единственный
    // источник правды для иконок. Порядок и принадлежность к папке — из БД.
    QString getBookmarks();

    // JSON-массив папок в порядке отображения: [{"id":1,"name":"Работа"}, ...]
    QString getFolders();

    void openBookmark(const QString& url);

    // Восстановленные функции, которые раньше были в подменю "Закладки"
    // (до переноса закладок на отдельную вкладку storm://bookmarks):
    void openImportedTabs();
    void importTabs();
    void exportTabs();
    void clearBookmarks();

    // Возвращают пустую строку при успехе, иначе — текст ошибки для показа пользователю.
    QString deleteBookmark(const QString& url);
    QString editBookmark(const QString& oldUrl, const QString& newTitle, const QString& newUrl);

    // --- Папки ---
    QString createFolder(const QString& name);
    QString renameFolder(int folderId, const QString& newName);
    // Закладки из удалённой папки НЕ удаляются — переносятся "без папки".
    QString deleteFolder(int folderId);
    // folderId <= 0 => переместить "без папки" (в корень).
    QString moveBookmarkToFolder(const QString& url, int folderId);

    // --- Ручная сортировка ---
    // orderedUrls — полный новый порядок url-ов ВНУТРИ указанной папки
    // (folderId <= 0 => корень). Присылается страницей целиком после
    // drag&drop, а не как единичная перестановка.
    QString reorderBookmarks(const QVariantList& orderedUrls, int folderId);
    // Точечная перестановка с соседом — для кнопок ⬆️/⬇️.
    QString moveBookmarkUp(const QString& url);
    QString moveBookmarkDown(const QString& url);

private:
    MainWindow* m_mw;
};
