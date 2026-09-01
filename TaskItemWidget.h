#pragma once
#ifndef TASKITEMWIDGET_H
#define TASKITEMWIDGET_H

#include <QWidget>
#include "TodoTask.h"

class QCheckBox;
class QLabel;
class QPushButton;

// Одна карточка задачи в списке. Набор элементов зависит от TodoTaskType.
class TaskItemWidget : public QWidget {
    Q_OBJECT
public:
    explicit TaskItemWidget(const TodoTask& task, QWidget* parent = nullptr);

    // Перерисовать карточку по свежим данным задачи (без пересоздания виджета).
    void refresh(const TodoTask& task);
    QString taskId() const { return m_task.id; }

signals:
    void toggled(const QString& id, bool checked);
    void deleteRequested(const QString& id);
    void openUrlsRequested(const QStringList& urls);       // Context: "🔗 Открыть"
    void openInReaderRequested(const QString& filePath);   // Media: открыть в Storm Reader
    void forceVacancyCheckRequested(const QString& id);    // VacancyRadar: проверить сейчас

private:
    void buildUi();
    void applyTaskState();

    TodoTask m_task;
    QCheckBox* m_doneCheck = nullptr;
    QLabel* m_iconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QPushButton* m_actionBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
};

#endif // TASKITEMWIDGET_H