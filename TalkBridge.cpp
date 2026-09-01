#include "TalkBridge.h"
#include "TalkPipOverlay.h"

#include <QBuffer>
#include <QImage>

TalkBridge::TalkBridge(QObject* parent) : QObject(parent) {
}

TalkBridge::~TalkBridge() {
    if (m_overlay) {
        m_overlay->hide();
        m_overlay->deleteLater();
    }
}

QPixmap TalkBridge::decodeDataUrl(const QString& dataUrl) {
    // Ожидаем строку вида "data:image/jpeg;base64,/9j/4AAQ...". Если формат
    // не совпал (пусто, обрезано сетевой ошибкой и т.п.) — просто игнорируем
    // этот кадр, дожидаемся следующего через ~250мс, ничего не падает.
    const int commaIdx = dataUrl.indexOf(QLatin1Char(','));
    if (commaIdx < 0) return QPixmap();

    const QByteArray raw = QByteArray::fromBase64(dataUrl.mid(commaIdx + 1).toLatin1());
    if (raw.isEmpty()) return QPixmap();

    QImage img;
    if (!img.loadFromData(raw, "JPEG")) return QPixmap();
    return QPixmap::fromImage(img);
}

void TalkBridge::ensureOverlay() {
    if (m_overlay) return;
    m_overlay = new TalkPipOverlay();
    connect(this, &TalkBridge::peerFrameReady, m_overlay, &TalkPipOverlay::setPeerFrame);
    connect(this, &TalkBridge::selfFrameReady, m_overlay, &TalkPipOverlay::setSelfFrame);
}

void TalkBridge::screenShareStarted() {
    ensureOverlay();
    m_overlay->showOverlay();
}

void TalkBridge::screenShareStopped() {
    if (m_overlay) m_overlay->hideOverlay();
}

void TalkBridge::pushPeerFrame(const QString& jpegDataUrl) {
    const QPixmap pm = decodeDataUrl(jpegDataUrl);
    if (!pm.isNull()) emit peerFrameReady(pm);
}

void TalkBridge::pushSelfFrame(const QString& jpegDataUrl) {
    const QPixmap pm = decodeDataUrl(jpegDataUrl);
    if (!pm.isNull()) emit selfFrameReady(pm);
}
