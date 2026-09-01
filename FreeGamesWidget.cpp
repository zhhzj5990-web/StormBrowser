#include "FreeGamesWidget.h"
#include "MainWindow.h"
#include <QLabel>
#include <QFrame>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QDesktopServices>
#include <QTabWidget>
#include <QWebEngineView>
#include <QUrl>

FreeGamesWidget::FreeGamesWidget(QWidget* parent) : QWidget(parent), isLoading(false) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);

    QLabel* header = new QLabel(u8"<b>🎁 Халява и Раздачи</b>", this);
    header->setStyleSheet("font-size: 16px; color: #56d39b; margin-bottom: 5px;");
    layout->addWidget(header);

    refreshBtn = new QPushButton(u8"🔄 Обновить список", this);
    refreshBtn->setStyleSheet("background-color: #ab47bc; color: white; border-radius: 6px; padding: 8px; font-weight: bold;");
    connect(refreshBtn, &QPushButton::clicked, this, &FreeGamesWidget::fetchGiveaways);
    layout->addWidget(refreshBtn);

    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("background: transparent;");

    contentWidget = new QWidget(scrollArea);
    contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setSpacing(10);
    contentLayout->setAlignment(Qt::AlignTop);

    scrollArea->setWidget(contentWidget);
    layout->addWidget(scrollArea);

    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &FreeGamesWidget::onNetworkReply);

    fetchGiveaways();
}

void FreeGamesWidget::clearLayout() {
    QLayoutItem* item;
    while ((item = contentLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

void FreeGamesWidget::fetchGiveaways() {
    if (isLoading) return;
    isLoading = true;
    refreshBtn->setEnabled(false);
    clearLayout();

    QLabel* loadingLbl = new QLabel(u8"⏳ Ищем активные раздачи...", contentWidget);
    loadingLbl->setAlignment(Qt::AlignCenter);
    loadingLbl->setStyleSheet("color: #888; margin-top: 20px;");
    contentLayout->addWidget(loadingLbl);

    QNetworkRequest request(QUrl("https://www.gamerpower.com/api/giveaways?platform=pc"));
    request.setHeader(QNetworkRequest::UserAgentHeader, "StormBrowser/1.0");
    request.setTransferTimeout(15000); // чтобы список не "висел" в загрузке вечно при плохой сети/недоступном API
    networkManager->get(request);
}

void FreeGamesWidget::onNetworkReply(QNetworkReply* reply) {
    clearLayout();
    isLoading = false;
    refreshBtn->setEnabled(true);

    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());

        if (!doc.isArray()) {
            // Сервер вернул не тот формат (например, HTML-страницу ошибки) —
            // раньше это молча трактовалось как "раздач не найдено"
            QLabel* errLbl = new QLabel(u8"⚠️ Некорректный ответ сервера.", contentWidget);
            errLbl->setAlignment(Qt::AlignCenter);
            contentLayout->addWidget(errLbl);
            reply->deleteLater();
            return;
        }

        QJsonArray array = doc.array();

        if (array.isEmpty()) {
            QLabel* emptyLbl = new QLabel(u8"😔 Сейчас раздач не найдено.", contentWidget);
            emptyLbl->setAlignment(Qt::AlignCenter);
            contentLayout->addWidget(emptyLbl);
        }
        else {
            int count = 0;
            for (const QJsonValue& val : array) {
                if (count >= 15) break; // Ограничение в 15 элементов для скорости
                QJsonObject game = val.toObject();

                QFrame* card = new QFrame(contentWidget);
                card->setStyleSheet("QFrame { background-color: #1c2128; border: 1px solid #30363d; border-radius: 10px; } "
                    "QFrame:hover { border-color: #56d39b; }");
                QVBoxLayout* cLayout = new QVBoxLayout(card);

                // Экранируем текст из внешнего API, иначе '&', '<', '>' в названии
                // ломают HTML-разметку (тег <b>) и отображение карточки
                QString safeTitle = game["title"].toString().toHtmlEscaped();
                QLabel* title = new QLabel(u8"<b>" + safeTitle + u8"</b>", card);
                title->setWordWrap(true);
                title->setStyleSheet("font-size: 14px; color: #adbac7;");

                QString worth = game["worth"].toString();
                worth = (worth.isEmpty() ? QStringLiteral("Free") : worth).toHtmlEscaped();
                QLabel* info = new QLabel(u8"💰 " + worth + u8" → FREE", card);
                info->setStyleSheet("color: #56d39b; font-weight: bold;");

                QString url = game["open_giveaway_url"].toString();

                QPushButton* getBtn = new QPushButton(u8"Забрать", card);
                getBtn->setStyleSheet("QPushButton { background-color: #347d39; color: white; border-radius: 5px; padding: 5px; font-weight: bold; }"
                    "QPushButton:hover { background-color: #46954a; }");

                if (url.isEmpty()) {
                    // Раньше на пустой ссылке создавалась рабочая на вид, но бесполезная кнопка
                    getBtn->setEnabled(false);
                    getBtn->setText(u8"Ссылка недоступна");
                }
                else {
                    connect(getBtn, &QPushButton::clicked, this, [this, url]() { openUrl(url); });
                }

                cLayout->addWidget(title);
                cLayout->addWidget(info);
                cLayout->addWidget(getBtn);
                contentLayout->addWidget(card);
                count++;
            }
        }
    }
    else {
        QLabel* errLbl = new QLabel(u8"⚠️ Ошибка сети.\nПроверьте интернет.", contentWidget);
        errLbl->setAlignment(Qt::AlignCenter);
        contentLayout->addWidget(errLbl);
    }
    reply->deleteLater();
}

void FreeGamesWidget::openUrl(const QString& url) {
    if (url.isEmpty()) return;

    // Открываем раздачу прямо во внутреннем браузере (новая вкладка),
    // а не через системный браузер по умолчанию.
    if (auto* mw = qobject_cast<MainWindow*>(this->window())) {
        if (QTabWidget* tabs = mw->getTabWidget()) {
            auto* view = new QWebEngineView(tabs);
            view->setUrl(QUrl(url));

            int insertIndex = qMax(tabs->count() - 1, 0); // вставляем перед кнопкой "+"
            int newIndex = tabs->insertTab(insertIndex, view, u8"⏳ Загрузка...");
            tabs->setCurrentIndex(newIndex);

            connect(view, &QWebEngineView::titleChanged, tabs, [tabs, view](const QString& t) {
                int idx = tabs->indexOf(view);
                if (idx != -1) tabs->setTabText(idx, t);
                });
            connect(view, &QWebEngineView::iconChanged, tabs, [tabs, view](const QIcon& icon) {
                int idx = tabs->indexOf(view);
                if (idx != -1) tabs->setTabIcon(idx, icon);
                });

            emit openUrlRequested(url);
            return;
        }
    }

    // Резервный вариант — если по какой-то причине окно браузера не найдено
    QDesktopServices::openUrl(QUrl(url));
}