#pragma once
#ifndef WEATHERWIDGET_H
#define WEATHERWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGridLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class WeatherWidget : public QWidget {
    Q_OBJECT
public:
    explicit WeatherWidget(QWidget* parent = nullptr);

private slots:
    void getWeather();
    void onNetworkReply(QNetworkReply* reply);

private:
    QLabel* createInfoItem(const QString& title, const QString& value, int row, int col);
    QString getIconByCode(const QString& code);

    QLineEdit* cityInput;
    QPushButton* getBtn;
    QLabel* iconLabel;
    QLabel* tempLabel;
    QLabel* descLabel;

    QLabel* feelsLike;
    QLabel* windLabel;
    QLabel* humidityLabel;
    QLabel* pressureLabel;
    QLabel* uvLabel;
    QLabel* visibilityLabel;

    QGridLayout* grid;
    QNetworkAccessManager* networkManager;
};

#endif // WEATHERWIDGET_H