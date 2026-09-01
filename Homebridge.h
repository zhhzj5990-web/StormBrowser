#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

class MainWindow;

// Мост QWebChannel для главной страницы (storm://home), отвечает за:
//  - Быстрый доступ (до 6 ссылок, хранятся через DatabaseManager::favorites)
//  - Пользовательские RSS/Atom-ленты (URL хранятся в QSettings, парсинг —
//    напрямую через QNetworkAccessManager + QXmlStreamReader на стороне C++,
//    без сторонних прокси — обходит и CORS, и чужие лимиты запросов)
//
// Регистрируется под именем "homeBridge" ВТОРЫМ объектом в том же QWebChannel,
// где уже есть "homeAI" (HomeAIBridge) — можно смело регистрировать несколько
// объектов в одном канале.
class HomeBridge : public QObject {
    Q_OBJECT
public:
    explicit HomeBridge(MainWindow* mainWin, QObject* parent = nullptr);

    // --- Быстрый доступ ---
    Q_INVOKABLE void getFavorites();  // emits favoritesReceived(jsonArray)
    Q_INVOKABLE void addFavorite(const QString& title, const QString& url);
    Q_INVOKABLE void removeFavorite(const QString& url);

    // --- RSS-ленты ---
    Q_INVOKABLE void getFeeds();      // emits feedsReceived(jsonArray)
    Q_INVOKABLE void addFeed(const QString& feedUrl);
    Q_INVOKABLE void removeFeed(const QString& feedUrl);
    Q_INVOKABLE void refreshNews();   // emits newsReceived(jsonArray)

signals:
    void favoritesReceived(const QString& jsonArray);
    void favoriteAddBlocked(const QString& reason);
    void feedsReceived(const QString& jsonArray);
    void feedAddResult(bool success, const QString& message);
    void newsReceived(const QString& jsonArray);

private:
    void parseRssXml(const QByteArray& xmlData, const QString& sourceUrl);
    QStringList loadFeedList();
    void onNetworkReply(QNetworkReply* reply);
    MainWindow* m_mainWindow;
    QNetworkAccessManager* m_net;
    QJsonArray m_collectedNews;
    int m_activeFeedRequests = 0;
    bool m_refreshPending = false; // пришёл новый refreshNews()/addFeed() пока предыдущий цикл не закончился — перезапустим его сразу после
};
