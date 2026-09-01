#ifndef TALKPIPOVERLAY_H
#define TALKPIPOVERLAY_H

#include <QWidget>
#include <QPixmap>

class QLabel;

// ==========================================================================
// TalkPipOverlay — маленькое окно "поверх всех окон" (Qt::WindowStaysOnTopHint),
// которое остаётся видно даже когда пользователь переключился в другое
// приложение, чтобы показать его через демонстрацию экрана. Это НЕЛЬЗЯ
// сделать элементом внутри веб-страницы (вкладка браузера физически не видна,
// если поверх неё открыто другое окно/приложение) — только отдельным
// нативным always-on-top виджетом, поэтому он и появился как отдельный
// класс, а не div в PageTemplates_Talk.
//
// Показывает два квадрата:
//  - "Собеседник" — видео того, с кем разговор, чтобы не терять с ним
//    визуальный контакт во время показа;
//  - "Что видят у вас" — self-check того, что реально уходит собеседникам
//    прямо сейчас (тот же кадр, что и демонстрация), чтобы вовремя заметить,
//    если случайно всплыло что-то лишнее (уведомление, личное окно и т.п.),
//    прежде чем это увидят другие участники.
// ==========================================================================
class TalkPipOverlay : public QWidget {
    Q_OBJECT
public:
    explicit TalkPipOverlay(QWidget* parent = nullptr);

public slots:
    void setPeerFrame(const QPixmap& pixmap);
    void setSelfFrame(const QPixmap& pixmap);
    void showOverlay();
    void hideOverlay();

private:
    QLabel* makeTile(const QString& caption);
    void repositionToBottomRight();

    QLabel* m_peerVideo = nullptr;
    QLabel* m_selfVideo = nullptr;
};

#endif // TALKPIPOVERLAY_H
