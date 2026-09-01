#include "WeatherWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

WeatherWidget::WeatherWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignTop);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(12);

    QLabel* header = new QLabel(u8"<b>☁ Погода</b>", this);
    header->setStyleSheet("font-size: 16px; color: #7aa2ff;");
    layout->addWidget(header);

    QHBoxLayout* searchLayout = new QHBoxLayout();
    cityInput = new QLineEdit("Moscow", this);
    cityInput->setPlaceholderText(u8"Город (напр. Moscow)");
    cityInput->setStyleSheet("QLineEdit { background-color: rgba(0, 0, 0, 0.2); border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; padding: 6px 10px; color: white; }"
        "QLineEdit:focus { border: 1px solid #7aa2ff; }");
    connect(cityInput, &QLineEdit::returnPressed, this, &WeatherWidget::getWeather);

    getBtn = new QPushButton(u8"🔍", this);
    getBtn->setFixedSize(32, 32);
    getBtn->setCursor(Qt::PointingHandCursor);
    getBtn->setStyleSheet("QPushButton { background-color: rgba(255, 255, 255, 0.08); border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; font-size: 14px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.15); }");
    connect(getBtn, &QPushButton::clicked, this, &WeatherWidget::getWeather);

    searchLayout->addWidget(cityInput);
    searchLayout->addWidget(getBtn);
    layout->addLayout(searchLayout);

    // Карточка погоды
    QFrame* weatherCard = new QFrame(this);
    weatherCard->setStyleSheet("QFrame { background-color: rgba(255, 255, 255, 0.03); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 12px; } QLabel { background: transparent; }");
    QVBoxLayout* cardLayout = new QVBoxLayout(weatherCard);
    cardLayout->setContentsMargins(15, 15, 15, 15);
    cardLayout->setSpacing(15);

    QHBoxLayout* topLayout = new QHBoxLayout();
    iconLabel = new QLabel(u8"⛅", this);
    iconLabel->setStyleSheet("font-size: 50px;");
    iconLabel->setAlignment(Qt::AlignCenter);

    QVBoxLayout* tempLayout = new QVBoxLayout();
    tempLabel = new QLabel("--°C", this);
    tempLabel->setStyleSheet("font-size: 38px; font-weight: bold; color: #ffffff;");
    descLabel = new QLabel(u8"Загрузка...", this);
    descLabel->setStyleSheet("font-size: 14px; color: #7aa2ff;");
    descLabel->setWordWrap(true);

    tempLayout->addWidget(tempLabel);
    tempLayout->addWidget(descLabel);
    tempLayout->setAlignment(Qt::AlignVCenter);

    topLayout->addWidget(iconLabel);
    topLayout->addLayout(tempLayout);
    cardLayout->addLayout(topLayout);

    QFrame* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("background-color: rgba(255, 255, 255, 0.1); max-height: 1px;");
    cardLayout->addWidget(line);

    // Сетка данных
    grid = new QGridLayout();
    grid->setSpacing(10);

    feelsLike = createInfoItem(u8"🌡️ Ощущается", "--°C", 0, 0);
    windLabel = createInfoItem(u8"💨 Ветер", "-- км/ч", 0, 1);
    humidityLabel = createInfoItem(u8"💧 Влажность", "--%", 1, 0);
    pressureLabel = createInfoItem(u8"🧭 Давление", "-- мм", 1, 1);
    uvLabel = createInfoItem(u8"☀️ УФ-индекс", "--", 2, 0);
    visibilityLabel = createInfoItem(u8"👁️ Видимость", "-- км", 2, 1);

    cardLayout->addLayout(grid);
    layout->addWidget(weatherCard);
    layout->addStretch();

    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &WeatherWidget::onNetworkReply);

    getWeather(); // Автозагрузка
}

QLabel* WeatherWidget::createInfoItem(const QString& title, const QString& value, int row, int col) {
    QVBoxLayout* itemLayout = new QVBoxLayout();
    itemLayout->setSpacing(2);

    QLabel* lblTitle = new QLabel(title, this);
    lblTitle->setStyleSheet("font-size: 11px; color: #8b949e;");

    QLabel* lblVal = new QLabel(value, this);
    lblVal->setStyleSheet("font-size: 14px; color: #e6edf3; font-weight: bold;");

    itemLayout->addWidget(lblTitle);
    itemLayout->addWidget(lblVal);
    grid->addLayout(itemLayout, row, col);
    return lblVal;
}

void WeatherWidget::getWeather() {
    QString city = cityInput->text().trimmed();
    if (city.isEmpty()) return;

    getBtn->setEnabled(false);
    descLabel->setText(u8"Обновление...");

    // The city name was previously concatenated into the URL raw, which breaks
    // for anything with a space or non-Latin characters (e.g. "New York",
    // "Санкт-Петербург"). Percent-encode it first.
    QString encodedCity = QString::fromUtf8(QUrl::toPercentEncoding(city));
    QUrl url("https://wttr.in/" + encodedCity + "?format=j1&lang=ru");
    QNetworkRequest request(url);
    request.setTransferTimeout(15000); // Qt 5.15+; avoids the button staying disabled forever on a hung server
    networkManager->get(request);
}

void WeatherWidget::onNetworkReply(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject data = doc.object();

        // data["current_condition"].toArray()[0] previously assumed the array
        // was non-empty whenever the key existed. An empty array (e.g. an
        // unrecognized city) makes operator[] read out of bounds, which is
        // undefined behaviour / an assert crash in debug builds.
        QJsonArray conditionArray = data.value("current_condition").toArray();

        if (!conditionArray.isEmpty()) {
            QJsonObject current = conditionArray[0].toObject();

            QString weatherCode = current["weatherCode"].toString();
            iconLabel->setText(getIconByCode(weatherCode));

            tempLabel->setText(current["temp_C"].toString() + u8"°C");

            // Получаем русское описание, если доступно (те же пустые-массивы риски)
            QJsonArray descArray = current.value("weatherDesc").toArray();
            QString desc = descArray.isEmpty() ? QString() : descArray[0].toObject()["value"].toString();

            QJsonArray ruDescArray = current.value("lang_ru").toArray();
            if (!ruDescArray.isEmpty()) {
                desc = ruDescArray[0].toObject()["value"].toString();
            }
            descLabel->setText(desc);

            feelsLike->setText(current["FeelsLikeC"].toString() + u8"°C");
            windLabel->setText(current["windspeedKmph"].toString() + u8" км/ч");
            humidityLabel->setText(current["humidity"].toString() + "%");

            // Перевод давления из гПа в мм рт. ст.
            int pressureHpa = current["pressure"].toString().toInt();
            int pressureMm = static_cast<int>(pressureHpa * 0.75006);
            pressureLabel->setText(QString::number(pressureMm) + u8" мм");

            uvLabel->setText(current["uvIndex"].toString());
            visibilityLabel->setText(current["visibility"].toString() + u8" км");
        }
        else {
            // Valid HTTP response but no weather data - city not recognized
            iconLabel->setText(u8"❌");
            tempLabel->setText("--");
            descLabel->setText(u8"Город не найден");
        }
    }
    else {
        // Real network/connection failure - was previously mislabeled as
        // "Город не найден" (city not found), hiding the actual problem.
        iconLabel->setText(u8"❌");
        tempLabel->setText("--");
        descLabel->setText(u8"Ошибка соединения: " + reply->errorString());
    }
    getBtn->setEnabled(true);
    reply->deleteLater();
}

QString WeatherWidget::getIconByCode(const QString& code) {
    // Упрощенная таблица кодов wttr.in для базовых состояний
    int c = code.toInt();
    if (c == 113) return u8"☀️";
    if (c == 116) return u8"⛅";
    if (c == 119 || c == 122) return u8"☁️";
    if (c >= 143 && c <= 260) return u8"🌫️"; // Туманы
    if (c >= 263 && c <= 317) return u8"🌧️"; // Дожди
    if (c >= 320 && c <= 350) return u8"🌨️"; // Снег
    if (c >= 353 && c <= 377) return u8"🌦️"; // Ливни
    if (c >= 386) return u8"⛈️"; // Грозы
    return u8"🌤️";
}