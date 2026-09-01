#ifndef TALKWIDGET_H
#define TALKWIDGET_H

#include <QWidget>

class TalkWidget : public QWidget {
    Q_OBJECT
public:
    explicit TalkWidget(QWidget* parent = nullptr);

signals:
    void launchTalkRequested();
};

#endif // TALKWIDGET_H