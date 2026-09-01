#include "QueueItemWidget.h"
#include <QHBoxLayout>

QueueItemWidget::QueueItemWidget(const ScheduledPost& post, QWidget* parent)
    : QWidget(parent), m_postId(post.id) {
    setObjectName("queueItemWidget");
    setStyleSheet(
        "QWidget#queueItemWidget { background: rgba(255,255,255,0.04); border-radius: 8px; }"
        "QWidget#queueItemWidget:hover { background: rgba(255,255,255,0.08); }"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    m_statusDot = new QLabel("●", this);
    layout->addWidget(m_statusDot);

    QString icons;
    for (auto plat : post.platforms) icons += platformRegistry().value(plat).icon + " ";
    if (!post.targetUrl.isEmpty()) icons += "🎯 "; // подсказка: пост идёт не на свою страницу, а на конкретную группу/канал
    m_platformsLabel = new QLabel(icons.trimmed(), this);
    m_platformsLabel->setToolTip(post.targetUrl.isEmpty() ? QString() : post.targetUrl);
    layout->addWidget(m_platformsLabel);

    m_timeLabel = new QLabel(post.scheduledTime.toString("dd.MM HH:mm"), this);
    m_timeLabel->setStyleSheet("color: #b0b0b0; font-size: 11px;");
    layout->addWidget(m_timeLabel, 1);

    m_cancelBtn = new QPushButton("✕", this);
    m_cancelBtn->setFixedSize(20, 20);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; color: #888; border-radius: 10px; }"
        "QPushButton:hover { background: #ff5f5f; color: white; }"
    );
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() { emit cancelRequested(m_postId); });
    layout->addWidget(m_cancelBtn);

    // post.lastError уже может быть непустым, если этот виджет создаётся
    // для поста, загруженного с диска в статусе Failed с прошлого запуска.
    updateStatus(post.status, post.lastError);
}

void QueueItemWidget::updateStatus(PostStatus status, const QString& errorMessage) {
    switch (status) {
    case PostStatus::Scheduled:  m_statusDot->setStyleSheet("color: #5b9bd5; font-size: 10px;"); break; // синий
    case PostStatus::Publishing: m_statusDot->setStyleSheet("color: #e8b339; font-size: 10px;"); break; // жёлтый
    case PostStatus::Published:  m_statusDot->setStyleSheet("color: #4caf50; font-size: 10px;"); break; // зелёный
    case PostStatus::Failed:     m_statusDot->setStyleSheet("color: #ff5f5f; font-size: 10px;"); break; // красный
    case PostStatus::Cancelled:  m_statusDot->setStyleSheet("color: #555555; font-size: 10px;"); break; // серый
    }
    // Подсказка при наведении — единственное место, где можно узнать причину
    // Failed, не заглядывая в лог/код. На остальных статусах подсказка не
    // нужна — они самоочевидны по цвету и надписи "Очередь".
    m_statusDot->setToolTip(status == PostStatus::Failed ? errorMessage : QString());
}