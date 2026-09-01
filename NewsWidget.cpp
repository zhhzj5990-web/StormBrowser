#include "NewsWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QDesktopServices>

NewsWidget::NewsWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &NewsWidget::onRssFetched);

    QVBoxLayout* controlsLayout = new QVBoxLayout();
    controlsLayout->setSpacing(5);

    QHBoxLayout* row1 = new QHBoxLayout();
    feedCombo = new QComboBox(this);
    feedCombo->setStyleSheet("padding: 4px; border-radius: 4px;");
    connect(feedCombo, &QComboBox::currentIndexChanged, this, &NewsWidget::loadNews);

    refreshBtn = new QPushButton(u8"🔄", this);
    refreshBtn->setFixedSize(28, 28);
    connect(refreshBtn, &QPushButton::clicked, this, &NewsWidget::loadNews);

    delBtn = new QPushButton(u8"🗑️", this);
    delBtn->setFixedSize(28, 28);
    connect(delBtn, &QPushButton::clicked, this, &NewsWidget::deleteFeed);

    row1->addWidget(feedCombo);
    row1->addWidget(refreshBtn);
    row1->addWidget(delBtn);

    QHBoxLayout* row2 = new QHBoxLayout();
    newFeedInput = new QLineEdit(this);
    newFeedInput->setPlaceholderText(u8"Ссылка на RSS...");

    addFeedBtn = new QPushButton(u8"Добавить", this);
    connect(addFeedBtn, &QPushButton::clicked, this, &NewsWidget::addFeed);

    row2->addWidget(newFeedInput);
    row2->addWidget(addFeedBtn);

    controlsLayout->addLayout(row1);
    controlsLayout->addLayout(row2);
    layout->addLayout(controlsLayout);

    browser = new QTextBrowser(this);
    browser->setOpenLinks(false);
    browser->setStyleSheet("background: transparent; border: none;");
    connect(browser, &QTextBrowser::anchorClicked, this, &NewsWidget::openArticle);
    layout->addWidget(browser);

    initFeeds();
}

void NewsWidget::initFeeds() {
    QSettings settings("Shtorm Software", "Storm Browser");
    QString savedJson = settings.value("news_feeds", "").toString();

    if (!savedJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(savedJson.toUtf8());
        QJsonObject obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            feeds.insert(it.key(), it.value().toString());
        }
    }

    if (feeds.isEmpty()) {
        feeds.insert(u8"Lenta.ru (Главное)", "https://lenta.ru/rss/top7");
        feeds.insert(u8"IXBT (Гаджеты и IT)", "https://www.ixbt.com/export/news.rss");
        feeds.insert(u8"Habr (Разработка)", "https://habr.com/ru/rss/all/all/");
        feeds.insert(u8"StopGame (Игры)", "https://stopgame.ru/rss/news");
    }

    updateCombo();
}

void NewsWidget::updateCombo() {
    feedCombo->blockSignals(true);
    feedCombo->clear();
    for (auto it = feeds.begin(); it != feeds.end(); ++it) {
        feedCombo->addItem(it.key(), it.value());
    }
    feedCombo->blockSignals(false);
    loadNews();
}

void NewsWidget::saveFeeds() {
    QJsonObject obj;
    for (auto it = feeds.begin(); it != feeds.end(); ++it) {
        obj.insert(it.key(), it.value());
    }
    QSettings settings("Shtorm Software", "Storm Browser");
    settings.setValue("news_feeds", QString(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void NewsWidget::addFeed() {
    QString url = newFeedInput->text().trimmed();
    if (!url.startsWith("http")) {
        QMessageBox::warning(this, u8"Ошибка", u8"Ссылка должна начинаться с http:// или https://");
        return;
    }

    // Раньше одинаковая ссылка (или ссылки с одинаковым хостом) могла
    // молча перезаписать уже существующую ленту в QMap, т.к. ключ
    // ("Своя лента (host)") совпадал. Теперь проверяем дубликаты явно.
    if (feeds.values().contains(url)) {
        QMessageBox::information(this, u8"Информация", u8"Эта лента уже добавлена.");
        return;
    }

    QUrl qurl(url);
    QString baseName = u8"Своя лента (" + qurl.host() + ")";
    QString name = baseName;
    int suffix = 2;
    while (feeds.contains(name)) {
        name = QString(u8"%1 [%2]").arg(baseName).arg(suffix++);
    }

    feeds.insert(name, url);
    saveFeeds();
    updateCombo();
    feedCombo->setCurrentText(name);
    newFeedInput->clear();
}

void NewsWidget::deleteFeed() {
    if (feeds.size() <= 1) {
        QMessageBox::warning(this, u8"Внимание", u8"Нельзя удалить последний источник!");
        return;
    }
    QString name = feedCombo->currentText();
    if (feeds.contains(name)) {
        feeds.remove(name);
        saveFeeds();
        updateCombo();
    }
}

void NewsWidget::loadNews() {
    QString url = feedCombo->currentData().toString();
    if (url.isEmpty()) return;

    // Если предыдущий запрос ещё не завершился (например, пользователь
    // быстро переключил ленту или нажал "Обновить" несколько раз подряд),
    // отменяем его. Иначе устаревший ответ мог прийти позже и перезаписать
    // уже отображённую новую ленту чужим содержимым.
    if (currentReply) {
        currentReply->abort();
        currentReply = nullptr;
    }

    browser->setHtml(u8"<div style='text-align: center; margin-top: 50px; color: #6e8cff;'><h3>⏳ Загрузка новостей...</h3></div>");

    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) StormBrowser/1.0");
    currentReply = networkManager->get(request);
}

void NewsWidget::onRssFetched(QNetworkReply* reply) {
    if (reply != currentReply) {
        // Это ответ на уже отменённый/устаревший запрос — игнорируем его,
        // чтобы не затереть содержимое ленты, которая выбрана сейчас.
        reply->deleteLater();
        return;
    }
    currentReply = nullptr;

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QString html = parseXmlToHtml(data);
        browser->setHtml(html);
    }
    else {
        browser->setHtml(u8"<html><body><p style='color: #ff7b72; text-align: center;'>⚠️ Ошибка подключения:<br>" + reply->errorString() + u8"</p></body></html>");
    }
    reply->deleteLater();
}

QString NewsWidget::parseXmlToHtml(const QByteArray& xmlData) {
    QXmlStreamReader xml(xmlData);
    QString html = u8"<html><body style='font-family: \"Segoe UI\", Arial, sans-serif;'>";
    int count = 0;

    QString currentTitle, currentLink, currentDesc;
    bool isItem = false;

    while (!xml.atEnd() && !xml.hasError() && count < 15) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("item") || xml.name() == QLatin1String("entry")) {
                isItem = true;
                currentTitle.clear();
                currentLink.clear();
                currentDesc.clear();
            }
            else if (isItem) {
                if (xml.name() == QLatin1String("title")) {
                    currentTitle = xml.readElementText();
                }
                else if (xml.name() == QLatin1String("link")) {
                    // RSS usually has text inside <link>, Atom uses attributes
                    if (xml.attributes().hasAttribute("href")) {
                        // Atom-записи часто содержат несколько <link>
                        // (rel="self", rel="alternate" и т.д.). Раньше
                        // последний встреченный тег молча перезаписывал
                        // ссылку, из-за чего статья могла ссылаться не на
                        // саму себя, а, например, на сам фид. Отдаём
                        // приоритет rel="alternate" (или отсутствию rel).
                        QString rel = xml.attributes().value("rel").toString();
                        if (currentLink.isEmpty() || rel.isEmpty() || rel == QLatin1String("alternate")) {
                            currentLink = xml.attributes().value("href").toString();
                        }
                    }
                    else {
                        currentLink = xml.readElementText();
                    }
                }
                else if (xml.name() == QLatin1String("description") || xml.name() == QLatin1String("summary")) {
                    currentDesc = xml.readElementText();
                }
            }
        }
        else if (token == QXmlStreamReader::EndElement) {
            if (xml.name() == QLatin1String("item") || xml.name() == QLatin1String("entry")) {
                isItem = false;

                // Очистка HTML тегов из описания (простая замена)
                currentDesc.remove(QRegularExpression("<[^>]*>"));
                currentDesc = currentDesc.trimmed();
                if (currentDesc.length() > 150) {
                    currentDesc = currentDesc.left(150) + "...";
                }

                html += QString(u8"<div style='margin-bottom: 12px; padding-left: 5px;'>"
                    u8"<h4 style='margin: 0 0 6px 0; font-size: 13px;'>"
                    u8"<a href='%1' style='color: #7aa2ff; text-decoration: none;'>%2</a></h4>"
                    u8"<p style='margin: 0; font-size: 11px; color: #a0aabf; line-height: 1.3;'>%3</p></div>"
                    u8"<hr style='border: 0; background-color: rgba(255,255,255,0.05); height: 1px; margin: 10px 0;'>")
                    .arg(currentLink.toHtmlEscaped(), currentTitle.toHtmlEscaped(), currentDesc.toHtmlEscaped());
                count++;
            }
        }
    }

    if (count == 0) {
        html += u8"<p style='color: #a0aabf; text-align: center;'>Лента пуста или формат не поддерживается.</p>";
    }
    html += u8"</body></html>";
    return html;
}

void NewsWidget::openArticle(const QUrl& url) {
    // Если у вас в MainWindow есть метод add_tab, его лучше вызывать через сигналы/слоты.
    // Пока что используем QDesktopServices для открытия ссылки в браузере по умолчанию.
    QDesktopServices::openUrl(url);
}