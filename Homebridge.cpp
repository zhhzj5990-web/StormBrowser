#include "HomeBridge.h"
#include "MainWindow.h"
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QXmlStreamReader>
#include <QSettings>
#include <QDateTime>
#include <QUrl>
#include <algorithm>
#include <QNetworkReply>
#include <QNetworkAccessManager>

namespace {
    const int MAX_FAVORITES = 6;
    const int MAX_FEEDS = 5;
    const int MAX_NEWS_ITEMS = 8;
    const QString DEFAULT_FEED = "https://habr.com/ru/rss/all/all/";
    const int NETWORK_TIMEOUT_MS = 15000; // Qt 5.15+; чтобы не зависать навсегда на битой/неотвечающей ленте

    QDateTime parseFeedDate(const QString& s) {
        QDateTime dt = QDateTime::fromString(s, Qt::RFC2822Date);
        if (!dt.isValid()) dt = QDateTime::fromString(s, Qt::ISODate);
        if (!dt.isValid()) dt = QDateTime::fromString(s, Qt::ISODateWithMs);
        return dt;
    }
}

HomeBridge::HomeBridge(MainWindow* mainWin, QObject* parent)
    : QObject(parent), m_mainWindow(mainWin)
{
    m_net = new QNetworkAccessManager(this);
}

// ============================== ИЗБРАННОЕ ==============================

void HomeBridge::getFavorites() {
    if (!m_mainWindow) { emit favoritesReceived("[]"); return; }

    auto favs = m_mainWindow->getDatabaseManager().getFavorites();
    QJsonArray arr;
    for (const auto& f : favs) {
        QJsonObject o;
        o["title"] = f.first;
        o["url"] = f.second;
        arr.append(o);
    }
    emit favoritesReceived(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void HomeBridge::addFavorite(const QString& title, const QString& url) {
    if (!m_mainWindow) return;

    auto favs = m_mainWindow->getDatabaseManager().getFavorites();
    if (favs.size() >= MAX_FAVORITES) {
        emit favoriteAddBlocked(u8"Достигнут лимит: максимум 6 ссылок");
        return;
    }

    QString cleanUrl = url.trimmed();
    if (cleanUrl.isEmpty()) return;
    if (!cleanUrl.startsWith("http://") && !cleanUrl.startsWith("https://")) {
        cleanUrl = "https://" + cleanUrl;
    }

    QString cleanTitle = title.trimmed();
    if (cleanTitle.isEmpty()) cleanTitle = QUrl(cleanUrl).host();
    if (cleanTitle.isEmpty()) cleanTitle = cleanUrl;

    m_mainWindow->getDatabaseManager().addFavorite(cleanTitle, cleanUrl);
    getFavorites();
}

void HomeBridge::removeFavorite(const QString& url) {
    if (!m_mainWindow) return;
    m_mainWindow->getDatabaseManager().removeFavorite(url);
    getFavorites();
}

// ============================== RSS-ЛЕНТЫ ==============================

QStringList HomeBridge::loadFeedList() {
    QSettings s;
    QStringList feeds = s.value("home/rss_feeds").toStringList();
    if (feeds.isEmpty()) feeds << DEFAULT_FEED;
    return feeds;
}

void HomeBridge::getFeeds() {
    QStringList feeds = loadFeedList();
    QJsonArray arr;
    for (const auto& f : feeds) arr.append(f);
    emit feedsReceived(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void HomeBridge::addFeed(const QString& feedUrl) {
    QString clean = feedUrl.trimmed();
    if (clean.isEmpty()) return;
    if (!clean.startsWith("http://") && !clean.startsWith("https://")) {
        clean = "https://" + clean;
    }

    QStringList feeds = loadFeedList();
    if (feeds.contains(clean)) {
        emit feedAddResult(false, u8"Эта лента уже добавлена");
        return;
    }
    if (feeds.size() >= MAX_FEEDS) {
        emit feedAddResult(false, u8"Максимум 5 RSS-лент одновременно");
        return;
    }

    // Проверяем ссылку РЕАЛЬНЫМ запросом перед сохранением — иначе пользователь
    // получит молчаливо пустую ленту без объяснения, если URL битый.
    QNetworkRequest req{ QUrl(clean) }; // Используем фигурные скобки!
    req.setRawHeader("User-Agent", "Mozilla/5.0 (StormBrowser RSS Reader)");
    req.setTransferTimeout(NETWORK_TIMEOUT_MS);
    QNetworkReply* reply = m_net->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, clean]() {
        bool valid = false;
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            valid = data.contains("<rss") || data.contains("<feed") || data.contains("<?xml");
        }
        reply->deleteLater();

        if (!valid) {
            emit feedAddResult(false, u8"Не удалось найти RSS/Atom по этой ссылке");
            return;
        }

        QSettings s;
        QStringList feeds = loadFeedList();
        feeds << clean;
        s.setValue("home/rss_feeds", feeds);

        emit feedAddResult(true, u8"Лента успешно добавлена");
        getFeeds();
        refreshNews();
        });
}

void HomeBridge::removeFeed(const QString& feedUrl) {
    QSettings s;
    QStringList feeds = loadFeedList();
    feeds.removeAll(feedUrl);
    s.setValue("home/rss_feeds", feeds);
    getFeeds();
    refreshNews();
}

void HomeBridge::refreshNews() {
    if (m_activeFeedRequests > 0) {
        // Цикл обновления уже идёт — не запускаем второй поверх него (иначе гонка
        // за m_collectedNews/m_activeFeedRequests: счётчик и массив общие). Просто
        // запоминаем, что нужно обновиться ещё раз, и делаем это сразу после того,
        // как текущий цикл закончится (см. конец колбэка ниже).
        m_refreshPending = true;
        return;
    }

    QStringList feeds = loadFeedList();

    m_collectedNews = QJsonArray();
    m_activeFeedRequests = feeds.size();

    if (m_activeFeedRequests == 0) {
        emit newsReceived("[]");
        return;
    }

    for (const QString& feedUrl : feeds) {
        QNetworkRequest req{ QUrl(feedUrl) }; // Фигурные скобки!
        req.setRawHeader("User-Agent", "Mozilla/5.0 (StormBrowser RSS Reader)");
        req.setTransferTimeout(NETWORK_TIMEOUT_MS);
        QNetworkReply* reply = m_net->get(req);

        connect(reply, &QNetworkReply::finished, this, [this, reply, feedUrl]() {
            if (reply->error() == QNetworkReply::NoError) {
                parseRssXml(reply->readAll(), feedUrl);
            }
            reply->deleteLater();

            m_activeFeedRequests--;
            if (m_activeFeedRequests <= 0) {
                // Сортируем объединённые новости всех лент по дате (свежие сверху)
                QList<QJsonObject> items;
                for (const auto& v : m_collectedNews) items.append(v.toObject());

                std::sort(items.begin(), items.end(), [](const QJsonObject& a, const QJsonObject& b) {
                    return a["ts"].toDouble() > b["ts"].toDouble();
                    });

                QJsonArray sorted;
                int count = 0;
                for (const auto& it : items) {
                    if (count >= MAX_NEWS_ITEMS) break;
                    sorted.append(it);
                    count++;
                }

                emit newsReceived(QString::fromUtf8(QJsonDocument(sorted).toJson(QJsonDocument::Compact)));

                if (m_refreshPending) {
                    m_refreshPending = false;
                    refreshNews();
                }
            }
            });
    }
}

void HomeBridge::parseRssXml(const QByteArray& xmlData, const QString& sourceUrl) {
    QXmlStreamReader xml(xmlData);
    QString title, link, pubDate;
    bool inItem = false;

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            QString name = xml.name().toString();

            if (name == "item" || name == "entry") {
                inItem = true;
                title.clear(); link.clear(); pubDate.clear();
            }
            else if (inItem && name == "title") {
                title = xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
            }
            else if (inItem && name == "link") {
                // RSS 2.0: <link>URL текстом</link>
                // Atom:    <link href="URL" .../> — самозакрывающийся тег с атрибутом
                QString hrefAttr = xml.attributes().value("href").toString();
                if (!hrefAttr.isEmpty()) {
                    link = hrefAttr;
                }
                else {
                    link = xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
                }
            }
            else if (inItem && (name == "pubDate" || name == "updated" || name == "published")) {
                pubDate = xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
            }
        }
        else if (token == QXmlStreamReader::EndElement) {
            QString name = xml.name().toString();
            if ((name == "item" || name == "entry") && inItem) {
                inItem = false;
                if (!title.isEmpty() && !link.isEmpty() && m_collectedNews.size() < 60) {
                    QDateTime dt = parseFeedDate(pubDate);
                    qint64 ts = dt.isValid() ? dt.toSecsSinceEpoch() : 0;

                    QJsonObject entry;
                    entry["title"] = title;
                    entry["link"] = link;
                    entry["source"] = QUrl(sourceUrl).host();
                    entry["ts"] = ts;
                    m_collectedNews.append(entry);
                }
            }
        }
    }
}
