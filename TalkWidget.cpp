#include "TalkWidget.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

TalkWidget::TalkWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 30, 15, 30);
    layout->setAlignment(Qt::AlignTop);

    QLabel* icon = new QLabel(u8"📹", this);
    icon->setStyleSheet("font-size: 64px;");
    icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon);

    QLabel* title = new QLabel(u8"<b>Storm Talk</b>", this);
    title->setStyleSheet("font-size: 22px; color: #56d39b; margin-top: 10px;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    QLabel* desc = new QLabel(u8"Децентрализованная P2P видеосвязь высокой четкости с нейро-фоном и шифрованием.", this);
    desc->setWordWrap(true);
    desc->setAlignment(Qt::AlignCenter);
    desc->setStyleSheet("color: #adbac7; font-size: 14px; margin-bottom: 20px; line-height: 1.4;");
    layout->addWidget(desc);

    QPushButton* launchBtn = new QPushButton(u8"Начать звонок", this);
    launchBtn->setFixedHeight(45);
    launchBtn->setCursor(Qt::PointingHandCursor);
    launchBtn->setStyleSheet("QPushButton { background-color: #56d39b; color: #000; border-radius: 8px; font-size: 16px; font-weight: bold; }"
        // Раньше здесь был "transform: scale(1.02)" — это CSS-свойство,
        // которое Qt Style Sheets (QSS) не поддерживает вообще. Оно просто
        // молча игнорировалось и сыпало варнингом "Unknown property transform"
        // в лог (тот самый, что мы видели раньше). Заменил на реально
        // поддерживаемый QSS эффект — смену цвета рамки при наведении.
        "QPushButton:hover { background-color: #4ac78f; border: 2px solid #ffffff; }");

    // При клике эмитим сигнал на открытие вкладки
    connect(launchBtn, &QPushButton::clicked, this, &TalkWidget::launchTalkRequested);

    layout->addWidget(launchBtn);
    layout->addStretch();
}