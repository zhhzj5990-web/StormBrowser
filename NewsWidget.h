#pragma once
#ifndef NEWSWIDGET_H
#define NEWSWIDGET_H

#include <QWidget>
#include <QMap>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class NewsWidget : public QWidget {
    Q_OBJECT
public:
    explicit NewsWidget(QWidget* parent = nullptr);

private slots:
    void loadNews();
    void addFeed();
    void deleteFeed();
    void onRssFetched(QNetworkReply* reply);
    void openArticle(const QUrl& url);

private:
    void initFeeds();
    void updateCombo();
    void saveFeeds();
    QString parseXmlToHtml(const QByteArray& xmlData);

    QComboBox* feedCombo;
    QLineEdit* newFeedInput;
    QPushButton* refreshBtn;
    QPushButton* delBtn;
    QPushButton* addFeedBtn;
    QTextBrowser* browser;

    QMap<QString, QString> feeds;
    QNetworkAccessManager* networkManager;
    // Отслеживаем текущий "живой" запрос, чтобы устаревший ответ от
    // предыдущей ленты не мог перезаписать уже показанную новую ленту.
    QNetworkReply* currentReply = nullptr;
};

#endif // NEWSWIDGET_H