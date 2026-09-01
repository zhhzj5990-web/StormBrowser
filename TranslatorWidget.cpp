#include "TranslatorWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QMap>

namespace {
    // left(2).toLower() on the display name gives wrong codes for German ("ge")
    // and Chinese ("ch") - neither is a valid MyMemory/ISO language code.
    // Use an explicit table instead.
    QString languageCode(const QString& displayName) {
        static const QMap<QString, QString> codes = {
            { "Russian", "ru" },
            { "English", "en" },
            { "German",  "de" },
            { "French",  "fr" },
            { "Chinese", "zh-CN" }
        };
        return codes.value(displayName, displayName.left(2).toLower());
    }
}

TranslatorWidget::TranslatorWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    layout->addWidget(new QLabel(u8"<b>🌐 Переводчик</b>", this));

    inputText = new QTextEdit(this);
    inputText->setPlaceholderText(u8"Введите текст для перевода...");
    inputText->setMaximumHeight(150);
    layout->addWidget(inputText);

    QHBoxLayout* langLayout = new QHBoxLayout();
    langFrom = new QComboBox(this);
    langFrom->addItems({ "Auto", "Russian", "English", "German", "French", "Chinese" });

    langTo = new QComboBox(this);
    langTo->addItems({ "English", "Russian", "German", "French", "Chinese" });

    langLayout->addWidget(new QLabel(u8"Из:", this));
    langLayout->addWidget(langFrom);
    langLayout->addWidget(new QLabel(u8"В:", this));
    langLayout->addWidget(langTo);
    layout->addLayout(langLayout);

    translateBtn = new QPushButton(u8"Перевести", this);
    translateBtn->setStyleSheet("background-color: #7aa2ff; color: #000; border-radius: 6px; padding: 8px; font-weight: bold;");
    connect(translateBtn, &QPushButton::clicked, this, &TranslatorWidget::performTranslation);
    layout->addWidget(translateBtn);

    resultText = new QTextBrowser(this);
    resultText->setPlaceholderText(u8"Здесь появится перевод...");
    layout->addWidget(resultText);

    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &TranslatorWidget::onNetworkReply);
}

void TranslatorWidget::performTranslation() {
    QString text = inputText->toPlainText().trimmed();
    if (text.isEmpty()) return;

    translateBtn->setEnabled(false);
    resultText->setText(u8"Перевожу...");

    QString sourceLang = langFrom->currentText() != "Auto" ? languageCode(langFrom->currentText()) : "autodetect";
    QString targetLang = languageCode(langTo->currentText());

    QUrl url("https://api.mymemory.translated.net/get");
    QUrlQuery query;
    query.addQueryItem("q", text);
    query.addQueryItem("langpair", sourceLang + "|" + targetLang);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(15000); // Qt 5.15+; without it a hung server would leave the button disabled forever
    networkManager->get(request);
}

void TranslatorWidget::onNetworkReply(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject obj = doc.object();
        if (obj.contains("responseData")) {
            QString translated = obj["responseData"].toObject()["translatedText"].toString();
            resultText->setText(translated);
        }
        else {
            resultText->setText(u8"Ошибка парсинга ответа.");
        }
    }
    else {
        resultText->setText(u8"Ошибка соединения: " + reply->errorString());
    }
    translateBtn->setEnabled(true);
    reply->deleteLater();
}