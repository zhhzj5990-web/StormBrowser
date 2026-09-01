#include "DatabaseManager.h"
#include "AdblockManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include <QStringList>

DatabaseManager::DatabaseManager(QObject* parent) : QObject(parent) {
    // БАГ-ФИКС (критично): раньше здесь БЕЗУСЛОВНО вызывался
    // QSqlDatabase::addDatabase("QSQLITE") — без явного имени соединения,
    // то есть каждый раз под одним и тем же дефолтным именем
    // "qt_sql_default_connection". Документация Qt для addDatabase() прямо
    // говорит: если соединение с таким именем уже существует, СТАРОЕ
    // соединение удаляется и заменяется новым.
    //
    // MainWindow хранит dbManager как обычный член (не singleton), и
    // setupUi() безусловно вызывает dbManager.initDatabase() для КАЖДОГО
    // окна — в том числе для откреплённых через detachTab()/перетаскивание
    // вкладки, которые создаются в самом обычном сценарии использования, а
    // не как редкий edge-case. Из-за этого второй (и любой следующий)
    // DatabaseManager при создании подменял соединение первого — уже
    // открытое окно тут же начинало ловить "connection 'qt_sql_default_
    // connection' is still in use" и терять доступ к истории/закладкам/
    // сессии/статистике Storm Shield сразу после отпочкования вкладки.
    //
    // Теперь: если соединение с этим именем уже зарегистрировано (значит,
    // какой-то другой DatabaseManager в этом же процессе уже его открыл) —
    // просто переиспользуем его, а не создаём заново.
    const QString connectionName = QSqlDatabase::database().connectionName(); // "qt_sql_default_connection"

    if (QSqlDatabase::contains(connectionName)) {
        db = QSqlDatabase::database(connectionName);
        m_ownsConnection = false;
    }
    else {
        db = QSqlDatabase::addDatabase("QSQLITE");
        m_ownsConnection = true;

        // Автоматически находим путь к папке AppData/Local/Shtorm Software/Storm Browser
        QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dbPath); // Создаем папки, если их еще не существует

        db.setDatabaseName(dbPath + "/storm_browser.db");
    }

    qDebug() << "Путь к базе данных:" << db.databaseName();
}

DatabaseManager::~DatabaseManager() {
    // БАГ-ФИКС: закрываем соединение только если МЫ его открыли. Соединение
    // теперь может быть общим на несколько окон (см. конструктор) — если бы
    // деструктор закрывал его безусловно, закрытие одного окна (например,
    // откреплённой вкладки) обрывало бы доступ к БД у всех остальных ещё
    // открытых окон.
    if (m_ownsConnection && db.isOpen()) {
        db.close();
    }
}

bool DatabaseManager::initDatabase() {
    if (!db.open()) {
        qDebug() << "Критическая ошибка открытия БД:" << db.lastError().text();
        return false;
    }

    QSqlQuery query;
    bool success = true;

    // 1. Создаем таблицу истории посещений.
    // ВАЖНО: раньше здесь было "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP" (UTC,
    // единое поле). Python-часть (history_manager.py) всегда хранила date/time
    // раздельно и в ЛОКАЛЬНОМ времени, из-за чего история в C++ и Python не совпадала
    // по формату и не могла одинаково группироваться по дням. Приводим схему к тому
    // же виду, что и в Python.
    success &= query.exec("CREATE TABLE IF NOT EXISTS history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "title TEXT, "
        "url TEXT, "
        "date TEXT, "
        "time TEXT)");

    // Миграция уже существующих БД со старой схемой (столбец timestamp, без date/time):
    // добавляем новые колонки и конвертируем UTC timestamp в локальные date/time —
    // той же логикой, что использует SQLite при 'localtime', чтобы не терять
    // накопленную пользователем историю при обновлении версии.
    {
        QSqlQuery pragmaQuery("PRAGMA table_info(history)");
        bool hasDate = false, hasTime = false, hasTimestamp = false;
        while (pragmaQuery.next()) {
            const QString colName = pragmaQuery.value(1).toString();
            if (colName == "date") hasDate = true;
            else if (colName == "time") hasTime = true;
            else if (colName == "timestamp") hasTimestamp = true;
        }

        if (hasTimestamp && !(hasDate && hasTime)) {
            QSqlQuery alterQuery;
            if (!hasDate) alterQuery.exec("ALTER TABLE history ADD COLUMN date TEXT");
            if (!hasTime) alterQuery.exec("ALTER TABLE history ADD COLUMN time TEXT");
            alterQuery.exec(
                "UPDATE history SET "
                "date = date(timestamp, 'localtime'), "
                "time = strftime('%H:%M', timestamp, 'localtime') "
                "WHERE date IS NULL OR date = ''"
            );
            qDebug() << "История: старая схема (timestamp) мигрирована в date/time (localtime).";
        }
    }

    // 2. Создаем таблицу закладок (URL делаем UNIQUE, чтобы ссылки не дублировались)
    success &= query.exec("CREATE TABLE IF NOT EXISTS bookmarks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "title TEXT, "
        "url TEXT UNIQUE)");

    if (!success) {
        qDebug() << "Ошибка при создании таблиц:" << query.lastError().text();
    }

    // 3. Создаем таблицу для Избранного (на главной)
    success &= query.exec("CREATE TABLE IF NOT EXISTS favorites ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "title TEXT, "
        "url TEXT UNIQUE)");

    // 4. Таблица статистики Storm Shield. CHECK(id = 1) гарантирует, что строка всегда одна.
    success &= query.exec("CREATE TABLE IF NOT EXISTS shield_stats ("
        "id INTEGER PRIMARY KEY CHECK (id = 1), "
        "blocked_count INTEGER NOT NULL DEFAULT 0)");
    query.exec("INSERT OR IGNORE INTO shield_stats (id, blocked_count) VALUES (1, 0)");

    // 5. Таблица сессии (список открытых вкладок при закрытии окна).
    // КРИТИЧНО: раньше этой таблицы не было вообще — MainWindow::closeEvent писал
    // список вкладок только в QSettings (реестр Windows), а восстановление при
    // startupMode == 1 читало ТОЛЬКО оттуда. history_manager.py тем временем уже
    // давно хранит сессию именно в БД (таблица sessions, JSON со списком URL) —
    // сброс настроек пользователем (или их повреждение) стирал сессию безвозвратно,
    // хотя в БД для неё даже не было места. Создаём ту же таблицу, что и в Python.
    success &= query.exec("CREATE TABLE IF NOT EXISTS sessions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "session_data TEXT)");

    // 6. Подписка на реальные блокировки Storm Shield.
    // ИСПРАВЛЕНО: раньше shield_stats.blocked_count обновлялся сам по себе,
    // в отрыве от того, что реально блокирует AdblockManager::isBlocked() —
    // числа в БД не отражали действительность. Теперь каждая реальная
    // блокировка (реклама ИЛИ malware, см. AdblockManager::registerBlock())
    // сразу пишется в БД через этот сигнал.
    //
    // ВАЖНО: сначала подставляем в AdblockManager уже накопленное значение
    // из БД. Иначе счётчик AdblockManager стартует с 0 при каждом запуске
    // приложения и на первой же блокировке ПЕРЕЗАПИШЕТ shield_stats с уже
    // накопленных, скажем, 500 на 1 — то есть без этой строки подписка ниже
    // сама стирала бы накопленную статистику вместо того, чтобы её вести.
    AdblockManager::instance().setInitialBlockedCount(getBlockedThreatsCount());

    // "this" как контекст обязателен: blockedCountChanged эмитится с
    // IO-потока QtWebEngine (isBlocked() дергается на каждый сетевой запрос
    // страницы), а DatabaseManager/QSqlDatabase живут в главном потоке.
    // Qt::AutoConnection с QObject-контекстом сам определит разницу потоков
    // и поставит вызов в очередь главного потока — без этого мы бы дёргали
    // QSqlQuery из чужого потока (SQLITE_MISUSE).
    connect(&AdblockManager::instance(), &AdblockManager::blockedCountChanged,
        this, &DatabaseManager::setBlockedThreatsCount);

    return success;
}

// --- РАБОТА С ИСТОРИЕЙ ---
bool DatabaseManager::addHistoryItem(const QString& title, const QString& url) {
    if (url.isEmpty() || url == "about:blank") return false;

    QSqlQuery query;
    // date('now','localtime') / strftime('%H:%M','now','localtime') — те же самые
    // встроенные функции SQLite, что использует history_manager.py в add_visit().
    // Так обе части приложения всегда пишут дату/время в одинаковом формате и часовом поясе.
    query.prepare(
        "INSERT INTO history (title, url, date, time) VALUES "
        "(:title, :url, date('now','localtime'), strftime('%H:%M','now','localtime'))"
    );
    query.bindValue(":title", title);
    query.bindValue(":url", url);
    return query.exec();
}

bool DatabaseManager::clearHistory() {
    QSqlQuery query;
    return query.exec("DELETE FROM history");
}

bool DatabaseManager::removeHistoryItem(int id) {
    QSqlQuery query;
    query.prepare("DELETE FROM history WHERE id = :id");
    query.bindValue(":id", id);
    return query.exec();
}

QList<HistoryEntry> DatabaseManager::getHistoryEntries(int limit) {
    QList<HistoryEntry> entries;
    QSqlQuery query;
    query.prepare("SELECT id, title, url, date, time FROM history ORDER BY id DESC LIMIT :limit");
    query.bindValue(":limit", limit);
    query.exec();

    while (query.next()) {
        HistoryEntry entry;
        entry.id = query.value(0).toInt();
        entry.title = query.value(1).toString();
        entry.url = query.value(2).toString();
        // Те же плейсхолдеры для пустых значений, что и в history_manager.py.all_history(),
        // чтобы старые/повреждённые записи без даты не ломали группировку в UI.
        QString dateVal = query.value(3).toString();
        QString timeVal = query.value(4).toString();
        entry.date = dateVal.isEmpty() ? QString::fromUtf8(u8"Неизвестная дата") : dateVal;
        entry.time = timeVal.isEmpty() ? QStringLiteral("--:--") : timeVal;
        entries.append(entry);
    }
    return entries;
}

QJsonArray DatabaseManager::getAllHistory() {
    QJsonArray result;
    QSqlQuery query("SELECT title, url, date, time FROM history ORDER BY id DESC");
    while (query.next()) {
        QJsonObject item;
        item["title"] = query.value(0).toString();
        item["url"] = query.value(1).toString();

        // date/time хранятся в ЛОКАЛЬНОМ времени (см. addHistoryItem) — собираем их как
        // Qt::LocalTime и переводим в unix-секунды, как и раньше ожидает сервер синхронизации.
        QString dateStr = query.value(2).toString();
        QString timeStr = query.value(3).toString();
        QDateTime dt = QDateTime::fromString(dateStr + " " + timeStr, "yyyy-MM-dd HH:mm");
        dt.setTimeSpec(Qt::LocalTime);
        qint64 ts = dt.isValid() ? dt.toSecsSinceEpoch() : QDateTime::currentSecsSinceEpoch();
        item["timestamp"] = QString::number(ts);

        result.append(item);
    }
    return result;
}

bool DatabaseManager::addHistoryItemAt(const QString& title, const QString& url, qint64 unixTimestamp) {
    if (url.isEmpty() || url == "about:blank") return false;

    QDateTime dt = QDateTime::fromSecsSinceEpoch(unixTimestamp, Qt::LocalTime);
    QString dateStr = dt.toString("yyyy-MM-dd");
    QString timeStr = dt.toString("HH:mm");

    // INSERT ... SELECT ... WHERE NOT EXISTS: вставляем, только если визита с таким
    // же (url, date, time) ещё нет — иначе повторный login()/requestManualSync()
    // плодил бы дубли этого же исторического визита при каждом pull.
    QSqlQuery query;
    query.prepare(
        "INSERT INTO history (title, url, date, time) "
        "SELECT :title, :url, :date, :time "
        "WHERE NOT EXISTS (SELECT 1 FROM history WHERE url = :url2 AND date = :date2 AND time = :time2)"
    );
    query.bindValue(":title", title);
    query.bindValue(":url", url);
    query.bindValue(":date", dateStr);
    query.bindValue(":time", timeStr);
    query.bindValue(":url2", url);
    query.bindValue(":date2", dateStr);
    query.bindValue(":time2", timeStr);
    return query.exec();
}

int DatabaseManager::getHistoryCount() {
    QSqlQuery query("SELECT COUNT(*) FROM history");
    if (query.next()) return query.value(0).toInt();
    return 0;
}

// --- РАБОТА С ЗАКЛАДКАМИ ---
bool DatabaseManager::addBookmark(const QString& title, const QString& url) {
    if (url.isEmpty() || url == "about:blank") return false;

    QSqlQuery query;
    // INSERT OR REPLACE обновит заголовок, если такой URL уже был добавлен
    query.prepare("INSERT OR REPLACE INTO bookmarks (title, url) VALUES (:title, :url)");
    query.bindValue(":title", title);
    query.bindValue(":url", url);
    return query.exec();
}

bool DatabaseManager::removeBookmark(const QString& url) {
    QSqlQuery query;
    query.prepare("DELETE FROM bookmarks WHERE url = :url");
    query.bindValue(":url", url);
    return query.exec();
}

bool DatabaseManager::clearBookmarks() {
    QSqlQuery query;
    return query.exec("DELETE FROM bookmarks");
}

bool DatabaseManager::isBookmark(const QString& url) {
    QSqlQuery query;
    query.prepare("SELECT id FROM bookmarks WHERE url = :url");
    query.bindValue(":url", url);
    if (query.exec() && query.next()) {
        return true; // Ссылка найдена в закладках
    }
    return false;
}

QJsonArray DatabaseManager::getAllBookmarks() {
    QJsonArray result;
    QSqlQuery query("SELECT title, url FROM bookmarks ORDER BY id DESC");
    while (query.next()) {
        QJsonObject item;
        item["title"] = query.value(0).toString();
        item["url"] = query.value(1).toString();
        result.append(item);
    }
    return result;
}

// --- РАБОТА С ИЗБРАННЫМ (НА ГЛАВНОЙ) ---
bool DatabaseManager::addFavorite(const QString& title, const QString& url) {
    if (url.isEmpty() || url == "about:blank") return false;
    QSqlQuery query;
    query.prepare("INSERT OR REPLACE INTO favorites (title, url) VALUES (:title, :url)");
    query.bindValue(":title", title);
    query.bindValue(":url", url);
    return query.exec();
}

bool DatabaseManager::removeFavorite(const QString& url) {
    QSqlQuery query;
    query.prepare("DELETE FROM favorites WHERE url = :url");
    query.bindValue(":url", url);
    return query.exec();
}

bool DatabaseManager::clearFavorites() {
    QSqlQuery query;
    return query.exec("DELETE FROM favorites");
}

QList<QPair<QString, QString>> DatabaseManager::getFavorites() {
    QList<QPair<QString, QString>> list;
    QSqlQuery query("SELECT title, url FROM favorites LIMIT 12"); // Максимум 12 кнопок на главной
    while (query.next()) {
        list.append({ query.value(0).toString(), query.value(1).toString() });
    }
    return list;
}

// --- СЕССИЯ ---
bool DatabaseManager::saveSession(const QStringList& openTabs) {
    QJsonArray arr;
    for (const QString& url : openTabs) {
        arr.append(url);
    }
    QJsonDocument doc(arr);
    QString jsonData = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    QSqlQuery query;
    // Как и history_manager.py.save_session(): держим только ОДНУ (последнюю)
    // сессию — сначала сносим предыдущую запись, затем пишем новую.
    query.exec("DELETE FROM sessions");
    query.prepare("INSERT INTO sessions (session_data) VALUES (:data)");
    query.bindValue(":data", jsonData);
    return query.exec();
}

QStringList DatabaseManager::loadSession() {
    QStringList result;
    QSqlQuery query("SELECT session_data FROM sessions ORDER BY id DESC LIMIT 1");
    if (query.next()) {
        QString jsonData = query.value(0).toString();
        QJsonDocument doc = QJsonDocument::fromJson(jsonData.toUtf8());
        if (doc.isArray()) {
            for (const QJsonValue& val : doc.array()) {
                result.append(val.toString());
            }
        }
    }
    return result;
}

// --- STORM SHIELD: статистика заблокированных угроз ---
int DatabaseManager::getBlockedThreatsCount() {
    QSqlQuery query("SELECT blocked_count FROM shield_stats WHERE id = 1");
    if (query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

bool DatabaseManager::setBlockedThreatsCount(int count) {
    QSqlQuery query;
    query.prepare("UPDATE shield_stats SET blocked_count = :count WHERE id = 1");
    query.bindValue(":count", count);
    if (!query.exec()) {
        return false;
    }
    if (query.numRowsAffected() == 0) {
        // Строка почему-то не была создана в initDatabase() — создаём её сейчас
        QSqlQuery insertQuery;
        insertQuery.prepare("INSERT OR REPLACE INTO shield_stats (id, blocked_count) VALUES (1, :count)");
        insertQuery.bindValue(":count", count);
        return insertQuery.exec();
    }
    return true;
}