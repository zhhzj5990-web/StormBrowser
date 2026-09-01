#pragma once
#ifndef POMODOROWIDGET_H
#define POMODOROWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTimer>
#include <QSoundEffect>

class PomodoroWidget : public QWidget {
    Q_OBJECT
public:
    explicit PomodoroWidget(QWidget* parent = nullptr);

private slots:
    void startTimer();
    void pauseTimer();
    void resetTimer();
    void updateTimer();

private:
    void updateDisplay();

    QLabel* timeLabel;
    QLabel* statusLabel;
    QPushButton* startBtn;
    QPushButton* pauseBtn;
    QPushButton* resetBtn;
    QLineEdit* workTimeInput;
    QLineEdit* breakTimeInput;

    QTimer* timer;
    QSoundEffect* sound;

    int remainingTime;
    bool isRunning;
    bool isBreak;
    int sessionsCompleted;
};

#endif // POMODOROWIDGET_H