#include "PomodoroWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QUrl>
#include <QCoreApplication>
#include <QIntValidator>

PomodoroWidget::PomodoroWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(18);
    layout->setContentsMargins(15, 15, 15, 15);

    QLabel* titleLbl = new QLabel(u8"<b>🍅 Pomodoro Таймер</b>", this);
    layout->addWidget(titleLbl);

    timeLabel = new QLabel("25:00", this);
    timeLabel->setAlignment(Qt::AlignCenter);
    timeLabel->setFont(QFont("Segoe UI", 56, QFont::Bold));
    timeLabel->setMinimumHeight(100);
    timeLabel->setStyleSheet("color: #56d39b;");
    layout->addWidget(timeLabel);

    QHBoxLayout* btnLayout = new QHBoxLayout();

    startBtn = new QPushButton(u8"▶ Старт", this);
    startBtn->setStyleSheet("background-color: #56d39b; color: #000; border-radius: 6px; padding: 8px; font-weight: bold;");

    pauseBtn = new QPushButton(u8"⏸ Пауза", this);
    pauseBtn->setStyleSheet("background-color: #ffc857; color: #000; border-radius: 6px; padding: 8px; font-weight: bold;");

    resetBtn = new QPushButton(u8"⟳ Сброс", this);
    resetBtn->setStyleSheet("background-color: #ff5f5f; color: #fff; border-radius: 6px; padding: 8px; font-weight: bold;");

    connect(startBtn, &QPushButton::clicked, this, &PomodoroWidget::startTimer);
    connect(pauseBtn, &QPushButton::clicked, this, &PomodoroWidget::pauseTimer);
    connect(resetBtn, &QPushButton::clicked, this, &PomodoroWidget::resetTimer);

    btnLayout->addWidget(startBtn);
    btnLayout->addWidget(pauseBtn);
    btnLayout->addWidget(resetBtn);
    layout->addLayout(btnLayout);

    QHBoxLayout* settingsLayout = new QHBoxLayout();
    workTimeInput = new QLineEdit("25", this);
    breakTimeInput = new QLineEdit("5", this);
    workTimeInput->setFixedWidth(65);
    breakTimeInput->setFixedWidth(65);
    workTimeInput->setAlignment(Qt::AlignCenter);
    breakTimeInput->setAlignment(Qt::AlignCenter);
    // Restrict inputs to sane integer ranges so users can't type letters,
    // negative numbers or decimals that silently get discarded later.
    workTimeInput->setValidator(new QIntValidator(1, 180, workTimeInput));
    breakTimeInput->setValidator(new QIntValidator(1, 60, breakTimeInput));

    settingsLayout->addWidget(new QLabel(u8"Работа (мин):"));
    settingsLayout->addWidget(workTimeInput);
    settingsLayout->addSpacing(25);
    settingsLayout->addWidget(new QLabel(u8"Перерыв (мин):"));
    settingsLayout->addWidget(breakTimeInput);
    layout->addLayout(settingsLayout);

    statusLabel = new QLabel(u8"Готов к фокусу", this);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setWordWrap(true);
    statusLabel->setMinimumHeight(55);
    statusLabel->setStyleSheet("color: #bbbbbb; font-size: 15px;");
    layout->addWidget(statusLabel);

    sound = new QSoundEffect(this);
    // Use an absolute path next to the executable instead of a path relative
    // to the current working directory, which is not guaranteed to be the
    // app folder (e.g. when launched from a shortcut or another CWD) and
    // would silently fail to load the sound.
    sound->setSource(QUrl::fromLocalFile(QCoreApplication::applicationDirPath() + "/ding.wav"));
    sound->setVolume(0.8f);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &PomodoroWidget::updateTimer);

    remainingTime = 25 * 60;
    isRunning = false;
    isBreak = false;
    sessionsCompleted = 0;

    updateDisplay();
}

void PomodoroWidget::startTimer() {
    if (!isRunning) {
        if (remainingTime <= 0) {
            int workMin = workTimeInput->text().toInt();
            int breakMin = breakTimeInput->text().toInt();
            if (workMin <= 0) workMin = 25;
            if (breakMin <= 0) breakMin = 5;

            remainingTime = isBreak ? breakMin * 60 : workMin * 60;
        }

        isRunning = true;
        timer->start(1000);
        // Disable Start while running instead of relabeling it to
        // "Продолжить" (Continue) — that label was misleading because it
        // was shown the instant the timer started running, not only after
        // an actual pause.
        startBtn->setEnabled(false);
        statusLabel->setText(isBreak ? u8"Перерыв..." : u8"Фокус в процессе...");
    }
}

void PomodoroWidget::pauseTimer() {
    if (isRunning) {
        isRunning = false;
        timer->stop();
        startBtn->setEnabled(true);
        startBtn->setText(u8"▶ Продолжить");
    }
}

void PomodoroWidget::resetTimer() {
    pauseTimer();
    isBreak = false;
    int workMin = workTimeInput->text().toInt();
    if (workMin <= 0) workMin = 25;

    remainingTime = workMin * 60;
    sessionsCompleted = 0;
    updateDisplay();

    statusLabel->setText(u8"Готов к фокусу");
    startBtn->setEnabled(true);
    startBtn->setText(u8"▶ Старт");
}

void PomodoroWidget::updateTimer() {
    if (remainingTime > 0) {
        remainingTime--;
        updateDisplay();
    }
    else {
        timer->stop();
        isRunning = false;

        sound->play();

        if (!isBreak) {
            sessionsCompleted++;
            statusLabel->setText(QString(u8"Время работать окончено! Сессий: %1\nПора на перерыв!").arg(sessionsCompleted));
            isBreak = true;
            int breakMin = breakTimeInput->text().toInt();
            if (breakMin <= 0) breakMin = 5;
            remainingTime = breakMin * 60;
        }
        else {
            statusLabel->setText(u8"Перерыв окончен!\nВозвращайся к работе");
            isBreak = false;
            int workMin = workTimeInput->text().toInt();
            if (workMin <= 0) workMin = 25;
            remainingTime = workMin * 60;
        }

        updateDisplay();
        startBtn->setEnabled(true);
        startBtn->setText(u8"▶ Старт");
    }
}

void PomodoroWidget::updateDisplay() {
    int minutes = remainingTime / 60;
    int seconds = remainingTime % 60;
    timeLabel->setText(QString().asprintf("%02d:%02d", minutes, seconds));
}