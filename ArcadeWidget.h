#pragma once
#ifndef ARCADEWIDGET_H
#define ARCADEWIDGET_H

#include <QWidget>
#include <QVBoxLayout>

class ArcadeWidget : public QWidget {
    Q_OBJECT
public:
    explicit ArcadeWidget(QWidget* parent = nullptr);

signals:
    // Сигнал, который скажет главному окну открыть вкладку с игрой
    void openGameRequested(const QString& gameUrl);

private slots:
    void loadGames();
    void importGame();
    void openGamesFolder();

private:
    QString getGamesDir();
    void clearLayout();

    QVBoxLayout* gamesLayout;
};

#endif // ARCADEWIDGET_H