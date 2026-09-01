#include "PostQueueManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

PostQueueManager::PostQueueManager(QObject* parent) : QObject(parent) {
    loadFromDisk();

    m_checkTimer = new QTimer(this);
    m_checkTimer->setInterval(kCheckIntervalMs);
    connect(m_checkTimer, &QTimer::timeout, this, &PostQueueManager::checkDuePosts);
    m_checkTimer->start();

    // Проверяем сразу при старте — вдруг приложение было закрыто дольше,
    // чем интервал таймера, и какие-то посты уже просрочены.
    checkDuePosts();
}

QString PostQueueManager::addPost(const QString& text, const QStringList& mediaPaths,
    const QSet<SocialPlatform>& platforms, const QDateTime& scheduledTime,
    const QString& targetUrl) {
    ScheduledPost post;
    post.text = text;
    post.mediaPaths = mediaPaths;
    post.platforms = platforms;
    post.scheduledTime = scheduledTime;
    post.status = PostStatus::Scheduled;
    post.targetUrl = targetUrl;

    m_posts.append(post);
    saveToDisk();
    emit postAdded(post);
    return post.id;
}

bool PostQueueManager::cancelPost(const QString& id) {
    for (int i = 0; i < m_posts.size(); ++i) {
        if (m_posts[i].id == id) {
            m_posts.removeAt(i);
            saveToDisk();
            emit postRemoved(id);
            return true;
        }
    }
    return false;
}

const ScheduledPost* PostQueueManager::findPost(const QString& id) const {
    for (const auto& p : m_posts) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

void PostQueueManager::markPublished(const QString& id) {
    for (auto& p : m_posts) {
        if (p.id == id) {
            p.status = PostStatus::Published;
            saveToDisk();
            emit postStatusChanged(id, PostStatus::Published);
            return;
        }
    }
}

void PostQueueManager::markFailed(const QString& id, const QString& reason) {
    for (auto& p : m_posts) {
        if (p.id == id) {
            p.status = PostStatus::Failed;
            p.lastError = reason;
            saveToDisk();
            emit postStatusChanged(id, PostStatus::Failed);
            return;
        }
    }
}

void PostQueueManager::checkDuePosts() {
    const QDateTime now = QDateTime::currentDateTime();
    bool changed = false;

    for (auto& p : m_posts) {
        if (p.status == PostStatus::Scheduled && p.scheduledTime <= now) {
            p.status = PostStatus::Publishing;
            changed = true;
            emit postStatusChanged(p.id, PostStatus::Publishing);
            // Дальше статус переведёт MainWindow через markPublished()/
            // markFailed() после реальной попытки публикации.
            emit publishDue(p);
        }
    }

    if (changed) saveToDisk();
}

QString PostQueueManager::storageFilePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/smm_queue.json";
}

void PostQueueManager::saveToDisk() const {
    QJsonArray arr;
    for (const auto& p : m_posts) {
        QJsonObject o;
        o["id"] = p.id;
        o["text"] = p.text;
        o["media"] = QJsonArray::fromStringList(p.mediaPaths);

        QJsonArray plats;
        for (auto plat : p.platforms) plats.append(static_cast<int>(plat));
        o["platforms"] = plats;

        o["time"] = p.scheduledTime.toString(Qt::ISODate);
        o["status"] = static_cast<int>(p.status);
        o["lastError"] = p.lastError;
        o["target"] = p.targetUrl;
        arr.append(o);
    }

    QFile f(storageFilePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson());
    }
}

void PostQueueManager::loadFromDisk() {
    QFile f(storageFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const auto& v : arr) {
        QJsonObject o = v.toObject();
        ScheduledPost p;
        p.id = o["id"].toString();
        p.text = o["text"].toString();
        for (const auto& m : o["media"].toArray()) p.mediaPaths << m.toString();
        for (const auto& pl : o["platforms"].toArray()) p.platforms.insert(static_cast<SocialPlatform>(pl.toInt()));
        p.scheduledTime = QDateTime::fromString(o["time"].toString(), Qt::ISODate);
        p.status = static_cast<PostStatus>(o["status"].toInt());
        p.lastError = o["lastError"].toString();
        // Отсутствует в файлах, сохранённых до появления поля цели —
        // QJsonValue() по умолчанию превращается в toString() == "" ровно
        // так же, как и "своя страница", так что старые посты без миграции
        // просто продолжают публиковаться на свою страницу, как и раньше.
        p.targetUrl = o["target"].toString();

        // Пост, который должен был опубликоваться, пока приложение было
        // закрыто (статус "завис" в Publishing с прошлого запуска) —
        // возвращаем в Scheduled, чтобы checkDuePosts() подхватил его
        // заново, а не оставлял навсегда висеть в "Publishing".
        if (p.status == PostStatus::Publishing) p.status = PostStatus::Scheduled;

        m_posts.append(p);
    }
}