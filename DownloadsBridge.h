#pragma once
#include <QObject>

class MainWindow;

// Мост QWebChannel для storm://downloads (регистрируется как "downloadsBridge",
// см. MainWindow_Tabs.cpp — тот же паттерн, что и SettingsBridge/BookmarksBridge:
// страница вызывает эти Q_INVOKABLE методы через window.downloadsBridge.*).
//
// Сам мост не хранит данные — вся персистентная история загрузок хранится в
// DownloadManager (см. DownloadManager::historyJson()/removeHistoryEntry()/
// clearAllHistory()), этот класс только даёт странице доступ к ней и к
// действиям над файлами на диске.
class DownloadsBridge : public QObject {
    Q_OBJECT
public:
    explicit DownloadsBridge(MainWindow* mainWindow, QObject* parent = nullptr);

public:
    // Вся история загрузок (обычные + торренты вместе) одним JSON-массивом —
    // страница парсит его и сама фильтрует по типу/поиску на клиенте.
    Q_INVOKABLE QString getDownloadsJson();

    // Активные (ещё не завершённые) загрузки прямо сейчас — страница
    // опрашивает это отдельно, раз в 1.5с, чтобы показывать живой прогресс
    // поверх истории (см. DownloadManager::activeDownloadsJson()).
    Q_INVOKABLE QString getActiveDownloadsJson();

    // Открыть папку, где лежит файл, в проводнике.
    Q_INVOKABLE void openFolder(const QString& filePath);

    // Удалить одну запись истории по id (саму запись — файл на диске не трогается).
    Q_INVOKABLE void removeEntry(const QString& id);

    // То же самое, но пачкой — для массового удаления на странице
    // (чекбоксы + "Удалить выбранное"). idsJson — JSON-массив строк-id.
    Q_INVOKABLE void removeEntries(const QString& idsJson);

    // Полностью очистить всю историю (оба режима сразу) — файлы на диске не трогаются.
    Q_INVOKABLE void clearAll();

    // Запустить скачивание видео по ссылке (VK и др., через yt-dlp) — то же
    // самое, что раньше делало поле ввода прямо в попапе DownloadManager;
    // теперь это поле только здесь, на полноценной странице. Делегирует в
    // уже существующий DownloadManager::startVideoDownload(url).
    Q_INVOKABLE void startVideoDownload(const QString& url);

    // "🔁 Повторить" для отменённой/сбойной записи истории. Для торрента —
    // просто заново addTorrent() тем же magnet-линком (sourceUrl). Для
    // обычной загрузки честного "перезапуска" нет: страница просто
    // открывает исходный URL новой вкладкой, а дальше это обычный поток
    // QWebEngineProfile::downloadRequested, как при первом клике по ссылке.
    Q_INVOKABLE void retryDownload(const QString& type, const QString& sourceUrl);

private:
    MainWindow* m_mainWindow;
};