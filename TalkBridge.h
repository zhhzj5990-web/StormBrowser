#ifndef TALKBRIDGE_H
#define TALKBRIDGE_H

#include <QObject>
#include <QPixmap>
#include <QString>

class TalkPipOverlay;

// ==========================================================================
// TalkBridge — мост между страницей storm-talk (JS) и нативной частью (C++)
// через QWebChannel (см. регистрацию "talkBridge" в MainWindow_Tabs.cpp).
//
// Единственная задача моста — во время демонстрации экрана показывать
// отдельное всегда-поверх-всех-окон окошко (TalkPipOverlay) с двумя
// маленькими превью: видео собеседника и то, что реально уходит от вас
// (self-check). Обычный звонок/чат/реакции/фон продолжают работать
// полностью внутри страницы и этот мост не используют.
//
// JS-сторона (см. PageTemplates_Talk.cpp/2.cpp) раз в ~250мс присылает сюда
// два маленьких JPEG-кадра как data URL ("data:image/jpeg;base64,...") —
// низкое разрешение и частота специально, чтобы не грузить CPU почём зря
// (это просто индикатор "кто где", а не полноценное видео).
// ==========================================================================
class TalkBridge : public QObject {
    Q_OBJECT
public:
    explicit TalkBridge(QObject* parent = nullptr);
    ~TalkBridge() override;

public slots:
    // Вызываются из JS как window.talkBridge.<имя>(...) — QWebChannel сам
    // экспонирует public slots в JS-объект после регистрации.
    void screenShareStarted();
    void screenShareStopped();
    void pushPeerFrame(const QString& jpegDataUrl);
    void pushSelfFrame(const QString& jpegDataUrl);

signals:
    void peerFrameReady(const QPixmap& pixmap);
    void selfFrameReady(const QPixmap& pixmap);

private:
    void ensureOverlay();
    static QPixmap decodeDataUrl(const QString& dataUrl);

    TalkPipOverlay* m_overlay = nullptr;
};

#endif // TALKBRIDGE_H
