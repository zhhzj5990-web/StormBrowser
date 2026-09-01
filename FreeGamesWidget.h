#pragma once
#ifndef FREEGAMESWIDGET_H
#define FREEGAMESWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class FreeGamesWidget : public QWidget {
    Q_OBJECT
public:
    explicit FreeGamesWidget(QWidget* parent = nullptr);

signals:
    void openUrlRequested(const QString& url);

private slots:
    void fetchGiveaways();
    void onNetworkReply(QNetworkReply* reply);
    void openUrl(const QString& url);

private:
    void clearLayout();

    QPushButton* refreshBtn;
    QScrollArea* scrollArea;
    QWidget* contentWidget;
    QVBoxLayout* contentLayout;
    QNetworkAccessManager* networkManager;
    bool isLoading;
};

#endif // FREEGAMESWIDGET_H