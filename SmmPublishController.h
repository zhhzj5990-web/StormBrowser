#pragma once
#include <QObject>
#include "SmmTypes.h"

class MainWindow;
class PostQueueManager;
class WebPageAgent;
class AiAgentTaskRunner;
class QWebEngineView;

// Соединяет PostQueueManager (сигнал publishDue — "пост созрел") с реальным
// браузером: открывает фоновую вкладку на compose-странице нужной платформы,
// дожидается загрузки и доводит пост до публикации через WebPageAgent +
// AiAgentTaskRunner (headless-версия того же агентного цикла, что использует
// AIAssistantWidget для чата — см. AiAgentTaskRunner.h).
//
// Публикация фото (post.mediaPaths) пока НЕ поддержана: у WebPageAgent нет
// действия для загрузки файла в <input type=file> (только navigate/click/
// type/scroll — см. WebPageAgent::executeAction), а перехват системного
// диалога выбора файла — отдельная, ещё не реализованная задача. Пока просто
// сообщаем об этом модели в тексте задачи, чтобы она не пыталась искать
// несуществующую кнопку и не зависала на этом шаге.
class SmmPublishController : public QObject {
    Q_OBJECT
public:
    explicit SmmPublishController(MainWindow* mainWindow, PostQueueManager* queueManager, QObject* parent = nullptr);

private slots:
    void onPublishDue(const ScheduledPost& post);

private:
    void startAgentSequence(QWebEngineView* view, const ScheduledPost& post);
    QString buildTaskDescription(const ScheduledPost& post) const;

    MainWindow* m_mainWindow;
    PostQueueManager* m_queueManager;
};
