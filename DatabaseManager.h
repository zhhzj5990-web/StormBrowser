#pragma once
#include <QObject>
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QList>
#include <QPair>
#include <QJsonArray>

// Одна запись истории посещений. Дата и время хранятся РАЗДЕЛЬНО и в ЛОКАЛЬНОМ
// времени — специально, чтобы формат совпадал 1-в-1 с history_manager.py
// (Python-часть пишет date('now','localtime') / strftime('%H:%M','now','localtime')
// в свою SQLite-базу). Раньше здесь был единый UTC timestamp, из-за чего диалог
// истории не мог сгруппировать записи по дню так же, как это делает Python.
struct HistoryEntry {
    int id = -1;  // БАГ-ФИКС: первичный ключ строки в таблице history. Нужен,
    // чтобы удалить именно ЭТУ запись (см. removeHistoryItem) —
    // раньше запись искали по (date, time, url), а это НЕ
    // уникально: два визита на один и тот же URL в одну и ту же
    // минуту удаляли оба разом вместо одного выбранного.
    QString title;
    QString url;
    QString date; // "YYYY-MM-DD", локальное время
    QString time; // "HH:MM", локальное время
};

// ИСПРАВЛЕНО: класс переведён с обычного QObject-независимого типа на QObject.
// Раньше AdblockManager::rulesUpdated был единственным сигналом в проекте, а
// счётчик заблокированных угроз (shield_stats) обновлялся в обход реальных
// блокировок isBlocked() — где-то отдельно вызывался setBlockedThreatsCount()
// вручную. Теперь AdblockManager эмитит blockedCountChanged() на каждую
// реальную блокировку, и DatabaseManager подписывается на него сам (см.
// initDatabase() в .cpp). Это требует QObject: blockedCountChanged шлётся с
// IO-потока QtWebEngine, а SQLite-соединение живёт в потоке, где создан
// DatabaseManager (обычно главном) — без QObject-контекста Qt не смог бы
// автоматически поставить вызов в очередь нужного потока, и мы бы дёргали
// QSqlQuery из чужого потока (SQLITE_MISUSE).
class DatabaseManager : public QObject
{
    Q_OBJECT
public:
    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager();

    // Инициализация базы данных и создание таблиц
    bool initDatabase();

    // --- ИСТОРИЯ ---
    bool addHistoryItem(const QString& title, const QString& url);
    bool clearHistory();

    // Для восстановления истории из облака (StormCloudBridge::login/requestManualSync):
    // в отличие от addHistoryItem(), пишет ИМЕННО переданную дату/время визита, а не
    // "сейчас" — иначе вся история, пришедшая с сервера, выглядела бы так, будто
    // все сайты были посещены в момент логина. Идемпотентно: если визит с таким же
    // (url, date, time) уже есть локально — повторно не вставляет, поэтому безопасно
    // вызывать на каждом логине/синке без накопления дублей при повторных pull.
    bool addHistoryItemAt(const QString& title, const QString& url, qint64 unixTimestamp);

    // Точечное удаление ОДНОГО визита по его первичному ключу id.
    // БАГ-ФИКС: раньше матчилось по (date, time, url) — аналог remove_visit()
    // из history_dialog.py. Это казалось безопасным ("тот же сайт в тот же
    // день дважды в разное время не сотрёт обе записи"), но (date, time, url)
    // НЕ уникальны с точностью до МИНУТЫ: если один и тот же сайт открывали
    // дважды в течение одной минуты (двойной клик, автообновление и т.п.),
    // удаление "одной" записи в диалоге истории удаляло СРАЗУ ОБЕ. Строка в
    // таблице history и так имеет автоинкрементный id — используем его.
    bool removeHistoryItem(int id);

    // Для диалога истории в UI: последние записи (title/url/date/time), сгруппированные
    // по дате уже на стороне MainWindow. limit=300 — то же ограничение, что и в
    // history_manager.py.all_history(), чтобы UI не зависал на очень большой истории.
    QList<HistoryEntry> getHistoryEntries(int limit = 300);

    // Для облачной синхронизации: все записи истории {title, url, timestamp}.
    // timestamp здесь — unix-время, вычисленное из локальных date/time (см. .cpp).
    QJsonArray getAllHistory();
    int getHistoryCount();

    // --- ЗАКЛАДКИ ---
    bool addBookmark(const QString& title, const QString& url);
    bool removeBookmark(const QString& url);
    bool clearBookmarks();
    bool isBookmark(const QString& url);

    // Для облачной синхронизации: все закладки {title, url}. addBookmark() уже
    // делает INSERT OR REPLACE по url — им же безопасно восстанавливать закладки,
    // пришедшие с сервера при pull, без дублей.
    QJsonArray getAllBookmarks();
    // --- ИЗБРАННОЕ (НА ГЛАВНОЙ) ---
    bool addFavorite(const QString& title, const QString& url);
    bool removeFavorite(const QString& url);
    bool clearFavorites();
    QList<QPair<QString, QString>> getFavorites();

    // --- СЕССИЯ (список открытых вкладок при закрытии окна) ---
    // Аналог save_session()/load_session() из history_manager.py: храним ОДНУ
    // последнюю сессию как JSON-массив URL в таблице sessions, а не в QSettings —
    // так сессия переживает сброс/повреждение пользовательских настроек.
    bool saveSession(const QStringList& openTabs);
    QStringList loadSession();

    // --- STORM SHIELD: статистика заблокированных угроз ---
    int getBlockedThreatsCount();
    bool setBlockedThreatsCount(int count);

private:
    QSqlDatabase db;

    // БАГ-ФИКС: true только у того экземпляра DatabaseManager, который
    // реально создал SQL-соединение (см. конструктор в .cpp). Несколько
    // окон (в т.ч. открепленные через detachTab()) теперь могут ДЕЛИТЬ одно
    // и то же соединение — если бы каждый деструктор закрывал его
    // безусловно, закрытие ЛЮБОГО окна обрывало бы доступ к БД всем
    // остальным ещё открытым окнам.
    bool m_ownsConnection = false;
};