#include "DownloadsBridge.h"
#include "MainWindow.h"
#include "DownloadManager.h"
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>

DownloadsBridge::DownloadsBridge(MainWindow* mainWindow, QObject* parent)
    : QObject(parent), m_mainWindow(mainWindow) {
}

QString DownloadsBridge::getDownloadsJson() {
    if (!m_mainWindow || !m_mainWindow->getDownloadManager()) return "[]";
    return m_mainWindow->getDownloadManager()->historyJson();
}

QString DownloadsBridge::getActiveDownloadsJson() {
    if (!m_mainWindow || !m_mainWindow->getDownloadManager()) return "[]";
    return m_mainWindow->getDownloadManager()->activeDownloadsJson();
}

void DownloadsBridge::openFolder(const QString& filePath) {
    if (filePath.isEmpty()) return;
    // filePath — это путь к самому файлу (как хранится в истории), а
    // открывать в проводнике нужно папку, в которой он лежит.
    QString dir = QFileInfo(filePath).absolutePath();
    if (!dir.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }
}

void DownloadsBridge::removeEntry(const QString& id) {
    if (m_mainWindow && m_mainWindow->getDownloadManager()) {
        m_mainWindow->getDownloadManager()->removeHistoryEntry(id);
    }
}

void DownloadsBridge::removeEntries(const QString& idsJson) {
    if (!m_mainWindow || !m_mainWindow->getDownloadManager()) return;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(idsJson.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) return;

    QStringList ids;
    for (const QJsonValue& v : doc.array()) {
        if (v.isString()) ids.append(v.toString());
    }
    m_mainWindow->getDownloadManager()->removeHistoryEntries(ids);
}

void DownloadsBridge::clearAll() {
    if (m_mainWindow && m_mainWindow->getDownloadManager()) {
        m_mainWindow->getDownloadManager()->clearAllHistory();
    }
}

void DownloadsBridge::startVideoDownload(const QString& url) {
    if (m_mainWindow && m_mainWindow->getDownloadManager()) {
        m_mainWindow->getDownloadManager()->startVideoDownload(url);
    }
}

void DownloadsBridge::retryDownload(const QString& type, const QString& sourceUrl) {
    if (sourceUrl.isEmpty() || !m_mainWindow) return;

    if (type == "torrent") {
        if (m_mainWindow->getDownloadManager()) {
            m_mainWindow->getDownloadManager()->addTorrent(sourceUrl);
        }
    }
    else {
        // Обычная загрузка — открываем исходный URL новой вкладкой; если это
        // прямая ссылка на файл, браузер сам заново поднимет
        // QWebEngineProfile::downloadRequested, как при обычном клике.
        m_mainWindow->addNewTab(QUrl(sourceUrl));
    }
}