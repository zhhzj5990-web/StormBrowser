#pragma once
#ifndef TODOWIDGET_H
#define TODOWIDGET_H

#include <QWidget>
#include <QVector>
#include <QHash>
#include <QPointer>
#include "TodoTask.h"

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QComboBox;
class QStackedWidget;
class QDateTimeEdit;
class QLabel;
class QNetworkAccessManager;
class QSystemTrayIcon;
class QTimer;
class QResizeEvent;
class TaskItemWidget;

class TodoWidget : public QWidget {
    Q_OBJECT
public:
    explicit TodoWidget(QWidget* parent = nullptr);

public slots:
    // Владелец виджета (например, MainWindow) вызывает этот слот в ответ на
    // requestOpenTabsForContext(), передавая URL всех сейчас открытых вкладок.
    void receiveOpenTabsForContext(const QStringList& urls);

signals:
    // TodoWidget не хранит список вкладок сам — он просит его у владельца
    // и получает ответ через receiveOpenTabsForContext().
    void requestOpenTabsForContext();
    // Открыть эти URL в новых вкладках (кнопка "🔗 Открыть" на задаче-контексте).
    void requestOpenUrls(const QStringList& urls);
    // Открыть указанный файл в Storm Reader (клик по медиа-задаче).
    void requestOpenInReader(const QString& filePath);

private slots:
    void addTask();
    void onTypeChanged(int index);
    void onPickMediaFile();
    void onAddVacancyUrl();
    void onTaskToggled(const QString& id, bool checked);
    void onTaskDeleteRequested(const QString& id);
    void onVacancyCheckTimer();
    void onAntiSubCheckTimer();

private:
    void buildAddPanel(class QVBoxLayout* rootLayout);
    void loadTasks();
    void saveTasks();
    QString getTasksFilePath();
    QString getBooksFolderPath();

    void addTaskToList(const TodoTask& task);
    void removeTaskById(const QString& id);
    TodoTask* findTaskById(const QString& id);
    TaskItemWidget* findWidgetById(const QString& id);

    void createContextTask(const QString& title, const QStringList& urls);
    void checkVacancyRadar(const QString& taskId);
    bool isProAccount() const;
    int vacancyUrlLimit() const;
    void updateVacancyLimitLabel();
    void ensureTrayIcon();

    // Карточки задач переносят текст по ширине, поэтому при изменении размера
    // панели (или при добавлении новой карточки) нужно заново подогнать
    // ширину каждого виджета-карточки под реальную ширину списка — иначе
    // QListWidget посчитает "естественную" (более широкую) ширину карточки
    // и появится горизонтальный скролл с обрезанными кнопками.
    void refreshItemWidths();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QListWidget* taskList = nullptr;
    QLineEdit* taskInput = nullptr;
    QComboBox* typeCombo = nullptr;
    QStackedWidget* extraStack = nullptr;

    // Страница "Медиа"
    QLabel* mediaFileLabel = nullptr;
    QString pendingMediaFilePath;

    // Страница "Анти-подписка"
    QDateTimeEdit* dayXEdit = nullptr;

    // Страница "Радар вакансий"
    QLineEdit* vacancyPromptInput = nullptr;
    QLineEdit* vacancyUrlInput = nullptr;
    QListWidget* vacancyUrlsList = nullptr;
    QLabel* vacancyLimitLabel = nullptr;

    QVector<TodoTask> m_tasks;
    QHash<QString, QListWidgetItem*> m_itemsById;
    QHash<QString, TaskItemWidget*> m_widgetsById;

    QString filepath;
    QString pendingContextTitle;

    QNetworkAccessManager* m_netManager = nullptr;
    QTimer* m_vacancyTimer = nullptr;
    QTimer* m_antiSubTimer = nullptr;
    QPointer<QSystemTrayIcon> m_trayIcon;
};

#endif // TODOWIDGET_H