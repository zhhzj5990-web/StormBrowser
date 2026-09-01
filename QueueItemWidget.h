#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include "SmmTypes.h"

// Один элемент "Очереди задач": цветной статус-индикатор + иконки выбранных
// платформ + время публикации слева, кнопка ❌ отмены справа. Используется
// через setItemWidget(...) внутри QListWidget в SmmAutoPublisherWidget — сам
// список хранит только пустые QListWidgetItem-заглушки нужного размера, вся
// отрисовка тут.
class QueueItemWidget : public QWidget {
    Q_OBJECT
public:
    explicit QueueItemWidget(const ScheduledPost& post, QWidget* parent = nullptr);

    QString postId() const { return m_postId; }
    // Перекрашивает статус-индикатор без пересоздания виджета — вызывается
    // при Scheduled -> Publishing -> Published/Failed. errorMessage — текст
    // причины (ScheduledPost::lastError), показывается всплывающей
    // подсказкой на самой точке при status == Failed; без этого узнать,
    // ПОЧЕМУ публикация провалилась, было неоткуда, кроме как гадать.
    void updateStatus(PostStatus status, const QString& errorMessage = QString());

signals:
    void cancelRequested(const QString& postId);

private:
    QString m_postId;
    QLabel* m_statusDot;
    QLabel* m_platformsLabel;
    QLabel* m_timeLabel;
    QPushButton* m_cancelBtn;
};