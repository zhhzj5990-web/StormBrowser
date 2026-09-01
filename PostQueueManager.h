#pragma once
#include <QObject>
#include <QTimer>
#include <QList>
#include "SmmTypes.h"

// Локальный планировщик очереди постов. Сознательно НЕ занимается вкладками
// браузера напрямую (Single Responsibility) — только хранит очередь и по
// таймеру решает, какие посты "созрели" для публикации, оповещая об этом
// сигналом publishDue(...). Кто именно откроет вкладку и выполнит публикацию —
// решает MainWindow, точно так же, как TodoWidget не открывает вкладки сам,
// а просит об этом через requestOpenTabsForContext/requestOpenUrls.
//
// Важно про "работает независимо от открытых окон": пока жив процесс
// приложения — таймер тикает независимо от того, какие вкладки/окна сейчас
// открыты, и очередь переживает перезапуск (сохраняется на диск). Но если
// закрыть само приложение полностью, публикация не произойдёт — для этого
// приложение не должно завершаться по закрытию окна, а сворачиваться в
// трей (как уже сделано для уведомления AntiSub в TodoWidget). Настоящая
// публикация "даже если весь Storm Browser не запущен" потребовала бы
// отдельного фонового сервиса/демона — это уже совсем другая архитектура,
// сюда сознательно не включена.
class PostQueueManager : public QObject {
    Q_OBJECT
public:
    explicit PostQueueManager(QObject* parent = nullptr);

    // Добавляет пост в очередь, сразу сохраняет её на диск и возвращает id
    // созданного поста. targetUrl — см. ScheduledPost::targetUrl в SmmTypes.h
    // (пусто = своя страница/профиль).
    QString addPost(const QString& text, const QStringList& mediaPaths,
        const QSet<SocialPlatform>& platforms, const QDateTime& scheduledTime,
        const QString& targetUrl = QString());

    bool cancelPost(const QString& id);
    const QList<ScheduledPost>& posts() const { return m_posts; }
    const ScheduledPost* findPost(const QString& id) const;

    // Вызываются MainWindow-ом после реальной попытки публикации (см.
    // publishDue ниже) — двигают статус поста и убирают его из активной
    // очереди, либо помечают Failed и оставляют на виду, чтобы пользователь
    // мог разобраться или запланировать повторную попытку.
    void markPublished(const QString& id);
    void markFailed(const QString& id, const QString& reason);

signals:
    void postAdded(const ScheduledPost& post);
    void postRemoved(const QString& id);
    void postStatusChanged(const QString& id, PostStatus status);

    // Главный сигнал интеграции с браузером: очередной пост "созрел".
    // MainWindow подписывается на него и сам решает, как открыть вкладку с
    // нужной соцсетью и довести пост до публикации (например, передав
    // задачу уже существующему WebPageAgent — тому же, что использует
    // AIAssistantWidget для кликов/ввода текста на странице). Здесь этот
    // шаг сознательно не реализован: PostQueueManager ничего не знает про
    // BrowserWebView/WebPageAgent, а сама механика "набрать текст и нажать
    // опубликовать" у каждой площадки своя и меняется вместе с их вёрсткой.
    void publishDue(const ScheduledPost& post);

private slots:
    void checkDuePosts();

private:
    void loadFromDisk();
    void saveToDisk() const;
    QString storageFilePath() const;

    QList<ScheduledPost> m_posts;
    QTimer* m_checkTimer;
    // Раз в 20с достаточно для минутной точности расписания и не грузит
    // систему постоянными проверками.
    static constexpr int kCheckIntervalMs = 20000;
};