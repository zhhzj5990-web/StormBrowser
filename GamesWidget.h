#ifndef GAMESWIDGET_H
#define GAMESWIDGET_H

#include <QWidget>
#include <QString>
#include <QTimer>
#include <QLabel>
#include <QPushButton> // <-- Добавили, чтобы компилятор знал, что такое кнопки
#include <QPaintEvent> // <-- Добавили для функции отрисовки бегущей строки
#include <QList>       // <-- Добавили для безопасной работы со списками

// Класс для бегущей строки
class MarqueeLabel : public QWidget {
    Q_OBJECT
public:
    explicit MarqueeLabel(const QString& text, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent* event) override;
private slots:
    void updateScroll();
private:
    QString m_text;
    int m_offset;
    QTimer* m_timer;
};

class GamesWidget : public QWidget {
    Q_OBJECT
public:
    explicit GamesWidget(QWidget* parent = nullptr);

signals:
    void openUrlRequested(const QString& url);

private:
    QPushButton* createGameButton(const QString& name, const QString& url, const QString& emoji);
    QPushButton* createStreamButton(const QString& name, const QString& url, const QString& color, const QString& icon);
    void openLink(const QString& url);
};

#endif // GAMESWIDGET_H