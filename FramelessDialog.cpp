#include "FramelessDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>

FramelessDialog::FramelessDialog(const QString& title, QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);

    // --- Кастомная перетаскиваемая шапка ---
    QWidget* header = new QWidget(this);
    header->setFixedHeight(HEADER_HEIGHT);
    header->setObjectName("framelessDialogHeader");

    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 0, 8, 0);

    QLabel* titleLabel = new QLabel(title, header);
    titleLabel->setStyleSheet("color: #eef3ff; font-weight: bold; font-size: 13px; background: transparent;");

    QPushButton* closeBtn = new QPushButton(u8"✕", header);
    closeBtn->setFixedSize(28, 28);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 6px; "
        "color: #ff5f5f; font-weight: bold; font-size: 14px; }"
        "QPushButton:hover { background-color: #ff5f5f; color: white; }"
    );
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(closeBtn);

    root->addWidget(header);

    // --- Область содержимого (сюда добавляет вызывающий код) ---
    QWidget* contentArea = new QWidget(this);
    contentArea->setObjectName("framelessDialogContent");
    m_contentLayout = new QVBoxLayout(contentArea);
    m_contentLayout->setContentsMargins(20, 16, 20, 16);
    root->addWidget(contentArea, 1);

    setStyleSheet(
        "#framelessDialogHeader { background-color: rgba(0,0,0,0.25); "
        "border-top-left-radius: 12px; border-top-right-radius: 12px; }"
        "#framelessDialogContent { background: transparent; }"
    );
}

void FramelessDialog::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    // WA_TranslucentBackground ломает QSS-фон верхнего уровня (тот же нюанс,
    // что и в CustomMenuPanel) — рисуем скруглённый фон вручную через QPainter.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(rect(), 12, 12);
    painter.fillPath(path, QColor("#12161f"));
    painter.setPen(QPen(QColor(255, 255, 255, 26), 1));
    painter.drawPath(path);
}

void FramelessDialog::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->position().y() <= HEADER_HEIGHT) {
        m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void FramelessDialog::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) && !m_dragPos.isNull()) {
        move(event->globalPosition().toPoint() - m_dragPos);
        event->accept();
    }
}