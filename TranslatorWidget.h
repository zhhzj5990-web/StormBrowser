#pragma once
#ifndef TRANSLATORWIDGET_H
#define TRANSLATORWIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QTextBrowser>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class TranslatorWidget : public QWidget {
    Q_OBJECT
public:
    explicit TranslatorWidget(QWidget* parent = nullptr);

private slots:
    void performTranslation();
    void onNetworkReply(QNetworkReply* reply);

private:
    QTextEdit* inputText;
    QComboBox* langFrom;
    QComboBox* langTo;
    QPushButton* translateBtn;
    QTextBrowser* resultText;
    QNetworkAccessManager* networkManager;
};

#endif // TRANSLATORWIDGET_H