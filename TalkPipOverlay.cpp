#include "TalkPipOverlay.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QGuiApplication>
#include <QScreen>

TalkPipOverlay::TalkPipOverlay(QWidget* parent) : QWidget(parent) {
    // Tool — не показывается в панели задач как отдельное приложение;
    // FramelessWindowHint — без стандартной рамки/заголовка ОС;
    // WindowStaysOnTopHint — собственно "поверх всех окон", ради чего всё это.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // Не забирает фокус при показе — не мешает работать в том окне,
    // которое сейчас демонстрируется.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet("background: transparent;");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    m_peerVideo = makeTile(u8"Собеседник");
    m_selfVideo = makeTile(u8"Что видят у вас");

    resize(220, 270);
}

QLabel* TalkPipOverlay::makeTile(const QString& caption) {
    auto* wrap = new QWidget(this);
    auto* wrapLayout = new QVBoxLayout(wrap);
    wrapLayout->setContentsMargins(0, 0, 0, 0);
    wrapLayout->setSpacing(3);

    auto* video = new QLabel(wrap);
    video->setFixedSize(200, 113); // 16:9, компактно
    video->setStyleSheet(
        "background:#14171f; border:2px solid #56d39b; border-radius:8px;");
    video->setAlignment(Qt::AlignCenter);

    auto* captionLabel = new QLabel(caption, wrap);
    captionLabel->setStyleSheet(
        "color:#eef3ff; background:rgba(7,10,18,0.8); font-size:11px; "
        "padding:2px 8px; border-radius:4px;");
    captionLabel->setAlignment(Qt::AlignCenter);

    wrapLayout->addWidget(video);
    wrapLayout->addWidget(captionLabel);

    auto* mainLayout = qobject_cast<QVBoxLayout*>(this->layout());
    if (mainLayout) mainLayout->addWidget(wrap);

    return video;
}

void TalkPipOverlay::repositionToBottomRight() {
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const QRect avail = screen->availableGeometry();
    const int margin = 24;
    move(avail.right() - width() - margin, avail.bottom() - height() - margin);
}

void TalkPipOverlay::setPeerFrame(const QPixmap& pixmap) {
    if (pixmap.isNull() || !m_peerVideo) return;
    m_peerVideo->setPixmap(pixmap.scaled(m_peerVideo->size(),
        Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
}

void TalkPipOverlay::setSelfFrame(const QPixmap& pixmap) {
    if (pixmap.isNull() || !m_selfVideo) return;
    m_selfVideo->setPixmap(pixmap.scaled(m_selfVideo->size(),
        Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
}

void TalkPipOverlay::showOverlay() {
    repositionToBottomRight();
    show();
    raise();
}

void TalkPipOverlay::hideOverlay() {
    hide();
}
