#pragma once
// ==========================================================================
// MainWindow_UiHelpers.h
// Мелкие служебные классы/функции, которые нужны более чем одному
// MainWindow_*.cpp одновременно (используются и в setupUi(), и в
// отдельных слотах). Вынесены в общий internal-заголовок при разбиении
// MainWindow.cpp на тематические файлы, чтобы не дублировать код.
// Публичный интерфейс MainWindow не менялся.
// ==========================================================================

#include <QObject>
#include <QWidget>
#include <QMouseEvent>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QString>

// --- Хелпер для перетаскивания кастомных безрамочных окон ---
class WindowDragFilter : public QObject {
    QPoint dragPosition;
public:
    WindowDragFilter(QObject* parent = nullptr) : QObject(parent) {}
    bool eventFilter(QObject* obj, QEvent* event) override {
        QWidget* titleBar = qobject_cast<QWidget*>(obj);
        if (!titleBar) return false;

        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                dragPosition = me->globalPosition().toPoint() - titleBar->window()->frameGeometry().topLeft();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->buttons() & Qt::LeftButton) {
                titleBar->window()->move(me->globalPosition().toPoint() - dragPosition);
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
};


static void applyMediaCodecFix(QWebEngineProfile* profile) {
    if (!profile) return;

    QWebEngineScript script;
    script.setName("MediaCodecFix");
    QString js = QStringLiteral(R"JS(
        (function() {
            try {
                        const host = window.location.hostname.toLowerCase();
                        if (!(host.includes('vk.com') || host.includes('vk.ru') || host.includes('vkvideo.ru'))) return;

                        const originalCanPlayType = HTMLMediaElement.prototype.canPlayType;
                HTMLMediaElement.prototype.canPlayType = function(type) {
                    if (typeof type === 'string' && (type.toLowerCase().includes('mp4') || type.toLowerCase().includes('avc'))) return '';
                    return originalCanPlayType.apply(this, arguments);
                };

                if (window.MediaSource) {
                    const originalIsTypeSupported = window.MediaSource.isTypeSupported;
                    window.MediaSource.isTypeSupported = function(type) {
                        if (typeof type === 'string' && (type.toLowerCase().includes('mp4') || type.toLowerCase().includes('avc'))) return false;
                        return originalIsTypeSupported.call(window.MediaSource, type);
                    };
                }
            } catch(e) {}
        })();
    )JS");
    script.setSourceCode(js);
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(true);
    profile->scripts()->insert(script);
}
