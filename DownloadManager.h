#pragma once
#include <QDialog>
#include <QWidget>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWebEngineDownloadRequest>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QPainter>
#include <QLineEdit>
#include <QThread>


// --- Кастомный прогресс-бар (как в Python) ---
class StormProgressBar : public QWidget {
    Q_OBJECT
public:
    explicit StormProgressBar(QWidget* parent = nullptr);
    void updateData(qreal progress, const QString& left, const QString& right, const QString& center);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    qreal m_progress;
    QString m_leftText, m_rightText, m_centerText;
};

// --- Карточка одной загрузки ---
class DownloadItem : public QFrame {
    Q_OBJECT
public:
    explicit DownloadItem(QWebEngineDownloadRequest* download, QWidget* parent = nullptr);
private slots:
    void updateProgress();
    void updateState(QWebEngineDownloadRequest::DownloadState state);
    void togglePause();
    void openFolder();
private:
    QWebEngineDownloadRequest* m_download;
    StormProgressBar* m_progressBar;
    QLabel* m_infoLabel;
    QPushButton* m_pauseBtn;
    QPushButton* m_cancelBtn;
    QPushButton* m_openFolderBtn;

    qint64 m_lastTime;
    qint64 m_lastBytes;
};

// --- Главная панель менеджера (теперь QWidget, а не QDialog) ---
class DownloadManager : public QWidget {
    Q_OBJECT
public:
    explicit DownloadManager(QWidget* parent = nullptr);
    void addDownload(QWebEngineDownloadRequest* request);
    void addTorrent(const QString& magnetLink);

    // Публичная перегрузка — вызывается из контекстного меню страницы (BrowserWebView)
    // с URL текущей вкладки, минуя поле ввода m_ytInput.
    void startVideoDownload(const QString& url);

    // Персистентная история загрузок — аналог save_history() из Python DownloadManager
    // (см. torrent_manager.py::TorrentItemWidget.on_finished). Вызывается при завершении
    // и обычных, и торрент-загрузок.
    void saveHistory(const QString& title, const QString& path, const QString& status);

    // Папка, использованная при последнем скачивании (обычном или торрент-загрузке).
    // Хранится в тех же QSettings, что и история — так следующий диалог выбора
    // файла/папки будет сразу открываться в ней, а не в системной "Загрузки".
    // static — доступна и из TorrentItem, у которого нет прямого указателя на DownloadManager.
    static QString lastDownloadDir();
    static void setLastDownloadDir(const QString& dir);

private slots:
    void clearHistory();
    void startVideoDownload();

private:
    QVBoxLayout* m_listLayout;
    QLineEdit* m_ytInput;
};

// --- Карта чанков торрента (перенос из Python) ---
class TorrentChunkMap : public QFrame {
    Q_OBJECT
public:
    explicit TorrentChunkMap(const QColor& accentColor, QWidget* parent = nullptr);
    void updateFromBitfield(const QVector<bool>& pieces);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor m_accentColor;
    QColor m_bgColor;
    QColor m_chunkColor;
    QVector<int> m_chunks; // 0 - пусто, 1 - готово, 2 - в процессе
};

class TorrentDownloaderThread;
// --- Карточка торрент-загрузки ---
class TorrentItem : public QFrame {
    Q_OBJECT
public:
    explicit TorrentItem(const QString& magnetLink, QWidget* parent = nullptr);
    void startDownload();

private slots:
    void cancelDownload();
    void openFolder();
    void updateUi(int percent, const QString& status, const QString& speed, const QString& peers, const QVector<bool>& pieces);

private:
    QString m_magnetLink;
    QLabel* m_statusLabel;
    QLabel* m_speedLabel;
    QLabel* m_peersLabel;
    TorrentChunkMap* m_chunkMap;
    QPushButton* m_cancelBtn;
    QPushButton* m_openFolderBtn;
    QString m_saveDir;

    TorrentDownloaderThread* m_thread = nullptr;
};