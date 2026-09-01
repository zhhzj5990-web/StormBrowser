#include "DownloadManager.h"
#include <QPainter>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QScrollArea>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <QVector>
#include <QDir>
#include <QFileDialog>
#include <QSettings>
#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_status.hpp>


// --- Поток для скачивания через libtorrent ---
class TorrentDownloaderThread : public QThread {
    Q_OBJECT
public:
    explicit TorrentDownloaderThread(const QString& magnetLink, const QString& saveDir, QObject* parent = nullptr);
    void stop();

protected:
    void run() override;

signals:
    void progressUpdated(int percent, const QString& status, const QString& speed, const QString& peers, const QVector<bool>& pieces);
    void finished(const QString& path);
    void error(const QString& msg);

private:
    QString m_magnetLink;
    QString m_saveDir;
    bool m_isRunning;
};

// ==========================================
// --- КАСТОМНЫЙ ПРОГРЕСС-БАР ---
// ==========================================
StormProgressBar::StormProgressBar(QWidget* parent) : QWidget(parent), m_progress(0.0) {
    setFixedHeight(30);
    m_leftText = u8"Подготовка...";
    m_centerText = u8"0%";
}

void StormProgressBar::updateData(qreal progress, const QString& left, const QString& right, const QString& center) {
    m_progress = progress;
    m_leftText = left;
    m_rightText = right;
    m_centerText = center;
    update(); // Запускаем перерисовку
}

void StormProgressBar::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QRectF rect(0, 0, width(), height());
    qreal radius = 8.0;

    // Темный фон полосы
    painter.setBrush(QColor("#161b22"));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect, radius, radius);

    // Цветная заливка прогресса (Градиент как в Питоне)
    if (m_progress > 0) {
        QRectF fillRect(0, 0, width() * m_progress, height());
        QLinearGradient gradient(0, 0, width(), 0);
        gradient.setColorAt(0.0, QColor("#a371f7"));
        gradient.setColorAt(1.0, QColor("#4facfe"));
        painter.setBrush(gradient);
        painter.drawRoundedRect(fillRect, radius, radius);
    }

    // Текст поверх прогресс-бара
    painter.setPen(QColor("#ffffff"));
    QFont font("Segoe UI", 10, QFont::Bold);
    painter.setFont(font);

    painter.drawText(QRectF(12, 0, width() - 24, height()), Qt::AlignLeft | Qt::AlignVCenter, m_leftText);
    painter.drawText(rect, Qt::AlignCenter, m_centerText);
    painter.drawText(QRectF(0, 0, width() - 12, height()), Qt::AlignRight | Qt::AlignVCenter, m_rightText);
}

// ==========================================
// --- КАРТОЧКА ЗАГРУЗКИ ---
// ==========================================
DownloadItem::DownloadItem(QWebEngineDownloadRequest* download, QWidget* parent)
    : QFrame(parent), m_download(download)
{
    setStyleSheet("DownloadItem { background-color: #1c2128; border: 1px solid #30363d; border-radius: 8px; }");
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 15, 15, 15);

    m_infoLabel = new QLabel(u8"📄 <b>" + m_download->downloadFileName() + u8"</b>", this);
    m_infoLabel->setStyleSheet("font-size: 14px; color: #eef3ff;");

    m_progressBar = new StormProgressBar(this);

    QHBoxLayout* controlsLayout = new QHBoxLayout();
    m_pauseBtn = new QPushButton(u8"⏸ Пауза", this);
    m_cancelBtn = new QPushButton(u8"❌ Отмена", this);
    m_openFolderBtn = new QPushButton(u8"📂 Открыть папку", this);
    m_openFolderBtn->hide();

    QString btnStyle = "QPushButton { background-color: #30363d; color: white; border: none; border-radius: 5px; padding: 6px 12px; font-weight: bold; } "
        "QPushButton:hover { background-color: #444c56; }";

    m_pauseBtn->setStyleSheet(btnStyle);
    m_cancelBtn->setStyleSheet(btnStyle + "QPushButton:hover { background-color: #ff5f5f; }");
    m_openFolderBtn->setStyleSheet(btnStyle + "QPushButton:hover { background-color: #56d39b; }");

    controlsLayout->addStretch();
    controlsLayout->addWidget(m_pauseBtn);
    controlsLayout->addWidget(m_cancelBtn);
    controlsLayout->addWidget(m_openFolderBtn);

    layout->addWidget(m_infoLabel);
    layout->addWidget(m_progressBar);
    layout->addLayout(controlsLayout);

    m_lastTime = QDateTime::currentMSecsSinceEpoch();
    m_lastBytes = 0;

    connect(m_download, &QWebEngineDownloadRequest::receivedBytesChanged, this, &DownloadItem::updateProgress);
    connect(m_download, &QWebEngineDownloadRequest::stateChanged, this, &DownloadItem::updateState);
    connect(m_pauseBtn, &QPushButton::clicked, this, &DownloadItem::togglePause);
    connect(m_cancelBtn, &QPushButton::clicked, m_download, &QWebEngineDownloadRequest::cancel);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &DownloadItem::openFolder);
}

void DownloadItem::updateProgress() {
    qint64 received = m_download->receivedBytes();
    qint64 total = m_download->totalBytes();
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 timeDiff = currentTime - m_lastTime;

    if (timeDiff >= 500 || received == total) {
        qint64 bytesDiff = received - m_lastBytes;
        double speed = (timeDiff > 0) ? (bytesDiff * 1000.0 / timeDiff) : 0;
        QString speedStr = QString::number(speed / (1024 * 1024), 'f', 1) + " MB/s";

        m_lastTime = currentTime;
        m_lastBytes = received;

        if (total > 0) {
            qreal ratio = (qreal)received / total;
            QString percentStr = QString::number(qRound(ratio * 100)) + "%";
            QString progressStr = QString::number(received / (1024 * 1024)) + " / " + QString::number(total / (1024 * 1024)) + " MB";

            m_progressBar->updateData(ratio, progressStr, speedStr, percentStr);
        }
        else {
            m_progressBar->updateData(1.0, QString::number(received / (1024 * 1024)) + " MB", speedStr, "?%");
        }
    }
}

void DownloadItem::updateState(QWebEngineDownloadRequest::DownloadState state) {
    if (state == QWebEngineDownloadRequest::DownloadCompleted) {
        m_progressBar->updateData(1.0, u8"Завершено", "", "100%");
        m_pauseBtn->hide();
        m_cancelBtn->hide();
        m_openFolderBtn->show();

        // Записываем в общую историю загрузок (см. DownloadManager::saveHistory) —
        // раньше это нигде не вызывалось для обычных загрузок, только для торрентов.
        QString fullPath = QDir(m_download->downloadDirectory()).filePath(m_download->downloadFileName());
        QWidget* parentPanel = this->parentWidget();
        while (parentPanel && !qobject_cast<DownloadManager*>(parentPanel)) {
            parentPanel = parentPanel->parentWidget();
        }
        if (DownloadManager* dm = qobject_cast<DownloadManager*>(parentPanel)) {
            dm->saveHistory(m_download->downloadFileName(), fullPath, u8"completed");
        }
    }
    else if (state == QWebEngineDownloadRequest::DownloadCancelled) {
        m_progressBar->updateData(0.0, u8"Отменено", "", u8"❌");
        m_pauseBtn->hide();
        m_cancelBtn->hide();
    }
    else if (state == QWebEngineDownloadRequest::DownloadInterrupted) {
        m_progressBar->updateData(0.0, u8"Ошибка сети", "", u8"⚠️");
        m_pauseBtn->hide();
        m_cancelBtn->hide();
    }
}

void DownloadItem::togglePause() {
    if (m_download->isPaused()) {
        m_download->resume();
        m_pauseBtn->setText(u8"⏸ Пауза");
        m_lastTime = QDateTime::currentMSecsSinceEpoch();
        m_lastBytes = m_download->receivedBytes();
    }
    else {
        m_download->pause();
        m_pauseBtn->setText(u8"▶ Возобновить");
    }
}

void DownloadItem::openFolder() {
    QString path = m_download->downloadDirectory();
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// ==========================================
// --- ПРАВАЯ ПАНЕЛЬ ЗАГРУЗОК ---
// ==========================================
DownloadManager::DownloadManager(QWidget* parent) : QWidget(parent) {
    setFixedWidth(350); // Фиксированная ширина как в Питоне
    setObjectName("downloadsPanel");
    setStyleSheet("QWidget#downloadsPanel { background-color: #0d1117; border-left: 1px solid #30363d; }");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(15);

    // --- 1. ШАПКА ---
    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* title = new QLabel(u8"📥 Загрузки", this);
    title->setStyleSheet("font-size: 18px; font-weight: bold; color: white;");
    headerLayout->addWidget(title);
    headerLayout->addStretch();

    QPushButton* clearBtn = new QPushButton(u8"🧹 Очистить", this);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setStyleSheet("QPushButton { background: transparent; color: #a371f7; font-weight: bold; border: none; } QPushButton:hover { text-decoration: underline; }");
    connect(clearBtn, &QPushButton::clicked, this, &DownloadManager::clearHistory);
    headerLayout->addWidget(clearBtn);

    QPushButton* closeBtn = new QPushButton(u8"✕", this);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet("QPushButton { background: transparent; color: #ff5f5f; font-weight: bold; font-size: 16px; border: none; padding: 2px 6px; } QPushButton:hover { background: rgba(255, 95, 95, 0.2); border-radius: 4px; }");
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::hide);
    headerLayout->addWidget(closeBtn);

    mainLayout->addLayout(headerLayout);

    // --- 2. БЛОК ЗАГРУЗКИ ВИДЕО (YT/VK) ---
    QHBoxLayout* ytLayout = new QHBoxLayout();
    m_ytInput = new QLineEdit(this);
    m_ytInput->setPlaceholderText(u8"Вставь ссылку на видео (VK и др)");
    m_ytInput->setFixedHeight(32);
    m_ytInput->setStyleSheet("background: rgba(0,0,0,0.2); border: 1px solid #30363d; padding: 5px 10px; color: white; border-radius: 6px;");

    QPushButton* ytBtn = new QPushButton(u8"Скачать", this);
    ytBtn->setFixedHeight(32);
    ytBtn->setCursor(Qt::PointingHandCursor);
    ytBtn->setStyleSheet("background-color: #a371f7; color: white; border: none; border-radius: 6px; padding: 0 15px; font-weight: bold;");
    connect(ytBtn, &QPushButton::clicked, this, [this]() { startVideoDownload(); });

    ytLayout->addWidget(m_ytInput);
    ytLayout->addWidget(ytBtn);
    mainLayout->addLayout(ytLayout);

    // --- 3. СПИСОК ЗАГРУЗОК ---
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; } QWidget#scrollContent { background: transparent; }");

    QWidget* scrollContent = new QWidget(scrollArea);
    scrollContent->setObjectName("scrollContent");
    m_listLayout = new QVBoxLayout(scrollContent);
    m_listLayout->setAlignment(Qt::AlignTop);
    m_listLayout->setSpacing(10);
    m_listLayout->setContentsMargins(0, 0, 0, 0);

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea);
}

QString DownloadManager::lastDownloadDir() {
    QSettings settings("Shtorm Software", "Storm Browser");
    QString dir = settings.value("downloads/lastSaveDir").toString();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        // Пока папка ни разу не выбиралась (или её потом удалили) —
        // откатываемся на системную "Загрузки", как и раньше.
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    return dir;
}

void DownloadManager::setLastDownloadDir(const QString& dir) {
    if (dir.isEmpty()) return;
    QSettings settings("Shtorm Software", "Storm Browser");
    settings.setValue("downloads/lastSaveDir", dir);
}

void DownloadManager::addDownload(QWebEngineDownloadRequest* request) {
    // Раньше диалог сохранения открывался безусловно на каждой загрузке. Теперь это
    // поведение по умолчанию (askEachTime=true, чтобы ничего не изменилось для тех,
    // кто не трогал настройку), но в Настройки → Основные → Загрузки можно выключить
    // и сохранять сразу в папку по умолчанию без диалога (см. SettingsBridge::
    // toggleDownloadAskEachTime/chooseDownloadFolder).
    QSettings settings("Shtorm Software", "Storm Browser");
    bool askEachTime = settings.value("browser/download_ask_each_time", true).toBool();

    QString dirPath;
    QString fileName = request->downloadFileName();

    if (askEachTime) {
        QString defaultPath = QDir(DownloadManager::lastDownloadDir()).filePath(fileName);
        QString savePath = QFileDialog::getSaveFileName(this, u8"Сохранить файл", defaultPath);
        if (savePath.isEmpty()) {
            request->cancel();
            return;
        }
        dirPath = QFileInfo(savePath).absolutePath();
        fileName = QFileInfo(savePath).fileName();
    }
    else {
        dirPath = DownloadManager::lastDownloadDir();
    }

    request->setDownloadDirectory(dirPath);
    request->setDownloadFileName(fileName);

    // Запоминаем папку — следующий диалог (или следующая бездиалоговая загрузка)
    // откроется/сохранится уже здесь.
    DownloadManager::setLastDownloadDir(dirPath);

    DownloadItem* item = new DownloadItem(request, this);
    m_listLayout->insertWidget(0, item);

    request->accept();
    this->show(); // Показываем боковую панель при старте загрузки
}

void DownloadManager::clearHistory() {
    if (QMessageBox::question(this, u8"Очистка", u8"Очистить список завершенных загрузок?\n(Файлы на диске удалены не будут)") == QMessageBox::Yes) {
        // Проходим по всем элементам и удаляем их с экрана
        QLayoutItem* child;
        while ((child = m_listLayout->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }

        // Чистим и персистентную историю — иначе после перезапуска браузера
        // список "воскресает" из QSettings, хотя пользователь его уже очистил.
        QSettings settings("Shtorm Software", "Storm Browser");
        settings.remove("downloads/history");
    }
}

void DownloadManager::saveHistory(const QString& title, const QString& path, const QString& status) {
    // Аналог save_history() из Python DownloadManager: копим историю загрузок
    // в QSettings (тот же профиль "Shtorm Software"/"Storm Browser", что уже
    // используется для VPN- и security-настроек), чтобы она переживала
    // перезапуск браузера.
    QSettings settings("Shtorm Software", "Storm Browser");

    int size = settings.beginReadArray("downloads/history");
    settings.endArray();

    settings.beginWriteArray("downloads/history");
    settings.setArrayIndex(size);
    settings.setValue("title", title);
    settings.setValue("path", path);
    settings.setValue("status", status);
    settings.setValue("timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));
    settings.endArray();
}

void DownloadManager::startVideoDownload() {
    QString url = m_ytInput->text().trimmed();
    if (url.isEmpty()) {
        QMessageBox::warning(this, u8"Ошибка", u8"Пожалуйста, вставьте ссылку на видео!");
        return;
    }
    m_ytInput->clear();
    startVideoDownload(url);
}

void DownloadManager::startVideoDownload(const QString& url) {
    if (url.trimmed().isEmpty()) {
        QMessageBox::warning(this, u8"Ошибка", u8"Пожалуйста, укажите ссылку на видео!");
        return;
    }

    // Ищем yt-dlp.exe в вашей папке resources
    QString ytDlpPath = QCoreApplication::applicationDirPath() + "/resources/yt-dlp.exe";

    if (!QFile::exists(ytDlpPath)) {
        QMessageBox::critical(this, u8"Компонент не найден",
            u8"Не найден файл загрузчика по пути:\n" + ytDlpPath +
            u8"\n\nПожалуйста, скачайте yt-dlp.exe и поместите его в папку resources.");
        return;
    }

    // Определяем стандартную папку "Загрузки" в системе пользователя
    QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

    // Создаем процесс для фоновой загрузки
    QProcess* process = new QProcess(this);
    process->setWorkingDirectory(downloadDir);

    // Аргументы yt-dlp: скачиваем лучшее доступное качество в формате mp4 и называем файл оригинальным именем видео
    QStringList args;
    args << "-f" << "bestvideo[ext=mp4]+bestaudio[ext=m4a]/best[ext=mp4]/best"
        << "-o" << "%(title)s.%(ext)s"
        << url;

    // Сигнал: когда загрузка завершится
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
            QMessageBox::information(this, u8"Загрузка завершена", u8"Видео успешно сохранено в папку 'Загрузки'!");
        }
        else {
            // Читаем ошибку, если что-то пошло не так
            QString errorMsg = process->readAllStandardError();
            QMessageBox::warning(this, u8"Ошибка загрузки", u8"Не удалось скачать видео. Возможно, неверная ссылка.\n\n" + errorMsg);
        }
        process->deleteLater(); // Освобождаем память
        });

    // Запускаем процесс
    process->start(ytDlpPath, args);

    QMessageBox::information(this, u8"Загрузка видео", u8"Скачивание началось в фоновом режиме.\nФайл будет сохранен в папку 'Загрузки'. Вы получите уведомление по завершении.");
}

// ==========================================
// --- КАРТА ЧАНКОВ ТОРРЕНТА ---
// ==========================================
TorrentChunkMap::TorrentChunkMap(const QColor& accentColor, QWidget* parent)
    : QFrame(parent), m_accentColor(accentColor), m_bgColor("#161b22"), m_chunkColor("#30363d") {
    setFixedHeight(24);
    m_chunks.fill(0, 100); // 100 визуальных блоков
}

void TorrentChunkMap::updateFromBitfield(const QVector<bool>& pieces) {
    if (pieces.isEmpty()) return;

    int totalRealPieces = pieces.size();
    QVector<int> newChunks(100, 0);
    double piecesPerChunk = qMax(1.0, (double)totalRealPieces / 100.0);

    for (int i = 0; i < 100; ++i) {
        int startIdx = i * piecesPerChunk;
        int endIdx = (i + 1) * piecesPerChunk;
        if (endIdx > totalRealPieces) endIdx = totalRealPieces;

        int downloaded = 0;
        int totalInSlice = endIdx - startIdx;

        for (int j = startIdx; j < endIdx; ++j) {
            if (pieces[j]) downloaded++;
        }

        if (totalInSlice > 0) {
            if (downloaded == totalInSlice) newChunks[i] = 1; // Полностью скачан
            else if (downloaded > 0) newChunks[i] = 2;        // В процессе (желтый)
            else newChunks[i] = 0;                            // Пусто
        }
    }
    m_chunks = newChunks;
    update();
}

void TorrentChunkMap::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), m_bgColor);

    int cols = 50, rows = 2;
    double chunkW = (double)width() / cols;
    double chunkH = (double)height() / rows;

    for (int i = 0; i < m_chunks.size(); ++i) {
        int status = m_chunks[i];
        double x = (i % cols) * chunkW;
        double y = (i / cols) * chunkH;
        QRectF rect(x + 1, y + 1, chunkW - 2, chunkH - 2);

        if (status == 1) painter.fillRect(rect, m_accentColor);
        else if (status == 2) painter.fillRect(rect, QColor("#ffc857"));
        else painter.fillRect(rect, m_chunkColor);
    }
}

// ==========================================
// --- КАРТОЧКА ТОРРЕНТА ---
// ==========================================
TorrentItem::TorrentItem(const QString& magnetLink, QWidget* parent)
    : QFrame(parent), m_magnetLink(magnetLink) {

    setStyleSheet("TorrentItem { background-color: #1c2128; border: 1px solid #6e8cff; border-radius: 8px; }");
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 15, 15, 15);

    m_statusLabel = new QLabel(u8"Подготовка к P2P загрузке...", this);
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #eef3ff;");
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    QHBoxLayout* statsLayout = new QHBoxLayout();
    m_speedLabel = new QLabel("0.00 MB/s", this);
    m_peersLabel = new QLabel(u8"Пиры: 0", this);
    m_speedLabel->setStyleSheet("color: #4facfe;");
    m_peersLabel->setStyleSheet("color: gray;");
    statsLayout->addWidget(m_speedLabel);
    statsLayout->addStretch();
    statsLayout->addWidget(m_peersLabel);
    layout->addLayout(statsLayout);

    m_chunkMap = new TorrentChunkMap(QColor("#6e8cff"), this);
    layout->addWidget(m_chunkMap);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_cancelBtn = new QPushButton(u8"❌ Отменить", this);
    m_openFolderBtn = new QPushButton(u8"📂 Открыть папку", this);
    m_openFolderBtn->hide();

    QString btnStyle = "QPushButton { background-color: #30363d; color: white; border: none; border-radius: 5px; padding: 6px 12px; } "
        "QPushButton:hover { background-color: #444c56; }";

    m_cancelBtn->setStyleSheet(btnStyle + "QPushButton:hover { background-color: #ff5f5f; }");
    m_openFolderBtn->setStyleSheet(btnStyle);

    btnLayout->addStretch();
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_openFolderBtn);
    layout->addLayout(btnLayout);

    connect(m_cancelBtn, &QPushButton::clicked, this, &TorrentItem::cancelDownload);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &TorrentItem::openFolder);
}

void TorrentItem::updateUi(int percent, const QString& status, const QString& speed, const QString& peers, const QVector<bool>& pieces) {
    m_chunkMap->updateFromBitfield(pieces);
    m_statusLabel->setText(status);
    m_speedLabel->setText(speed);
    m_peersLabel->setText(peers);
}

void TorrentItem::cancelDownload() {
    if (m_thread && m_thread->isRunning()) {
        m_thread->stop();
        m_thread->wait(); // Ждем безопасного завершения потока
    }
    this->deleteLater();
}

void TorrentItem::openFolder() {
    if (!m_saveDir.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_saveDir));
    }
}

// ==========================================
// --- ПОТОК LIBTORRENT ---
// ==========================================
TorrentDownloaderThread::TorrentDownloaderThread(const QString& magnetLink, const QString& saveDir, QObject* parent)
    : QThread(parent), m_magnetLink(magnetLink), m_saveDir(saveDir), m_isRunning(true) {
}

void TorrentDownloaderThread::stop() {
    m_isRunning = false;
}

void TorrentDownloaderThread::run() {
    try {
        lt::session session;
        lt::add_torrent_params params;
        params.save_path = m_saveDir.toStdString();

        // Парсим magnet-ссылку
        if (m_magnetLink.startsWith("magnet:")) {
            params = lt::parse_magnet_uri(m_magnetLink.toStdString());
            params.save_path = m_saveDir.toStdString();
        }

        lt::torrent_handle handle = session.add_torrent(params);

        while (m_isRunning && !handle.status().is_seeding) {
            lt::torrent_status status = handle.status();

            int progress = status.progress * 100;
            QString speed = QString::number(status.download_rate / 1000000.0, 'f', 2) + " MB/s";
            QString peers = QString(u8"Пиры: %1").arg(status.num_peers);
            QString stateStr = u8"Скачивание";
            if (status.state == lt::torrent_status::checking_files) stateStr = u8"Проверка файлов";
            else if (status.state == lt::torrent_status::downloading_metadata) stateStr = u8"Поиск метаданных";

            // Собираем карту чанков
            QVector<bool> pieces;
            if (status.pieces.size() > 0) {
                for (int i = 0; i < status.pieces.size(); ++i) {
                    pieces.append(status.pieces.get_bit(lt::piece_index_t(i)));
                }
            }

            emit progressUpdated(progress, stateStr, speed, peers, pieces);
            QThread::sleep(1); // Ждем 1 секунду перед следующим обновлением
        }

        if (m_isRunning) {
            QVector<bool> fullPieces(100, true);
            emit progressUpdated(100, u8"✅ Завершено", "0.00 MB/s", u8"Пиры: 0", fullPieces);
            emit finished(m_saveDir);
        }
    }
    catch (std::exception& e) {
        emit error(QString::fromStdString(e.what()));
    }
}

void TorrentItem::startDownload() {
    // Общая с обычными загрузками "последняя папка" — тот же QSettings-ключ,
    // что и в DownloadManager::addDownload() — и тот же переключатель
    // "спрашивать папку каждый раз" (см. комментарий там же).
    QSettings settings("Shtorm Software", "Storm Browser");
    bool askEachTime = settings.value("browser/download_ask_each_time", true).toBool();

    if (askEachTime) {
        m_saveDir = QFileDialog::getExistingDirectory(this, u8"Выберите папку для сохранения торрента", DownloadManager::lastDownloadDir());
        if (m_saveDir.isEmpty()) {
            this->deleteLater(); // Если отменил - удаляем карточку
            return;
        }
        DownloadManager::setLastDownloadDir(m_saveDir);
    }
    else {
        m_saveDir = DownloadManager::lastDownloadDir();
    }

    m_thread = new TorrentDownloaderThread(m_magnetLink, m_saveDir, this);
    connect(m_thread, &TorrentDownloaderThread::progressUpdated, this, &TorrentItem::updateUi);
    connect(m_thread, &TorrentDownloaderThread::finished, this, [this](const QString& path) {
        m_cancelBtn->hide();
        m_openFolderBtn->show();
        m_statusLabel->setText(u8"✅ " + m_statusLabel->text());

        // Сохраняем в общую историю загрузок — это и есть недостающий кусок:
        // Python-версия (torrent_manager.py::on_finished) поднимается по дереву
        // родителей до первого объекта с save_history() и пишет туда запись;
        // здесь делаем то же самое через qobject_cast к DownloadManager.
        QWidget* parentPanel = this->parentWidget();
        while (parentPanel && !qobject_cast<DownloadManager*>(parentPanel)) {
            parentPanel = parentPanel->parentWidget();
        }
        if (DownloadManager* dm = qobject_cast<DownloadManager*>(parentPanel)) {
            QString folderName = QDir(path).dirName(); // аналог os.path.basename(os.path.normpath(path))
            if (folderName.isEmpty()) folderName = path;
            dm->saveHistory(u8"Торрент: " + folderName, path, u8"completed");
        }
        });
    connect(m_thread, &TorrentDownloaderThread::error, this, [this](const QString& err) {
        m_statusLabel->setText(u8"⚠️ Ошибка: " + err);
        });

    m_thread->start();
}
#include "DownloadManager.moc"

void DownloadManager::addTorrent(const QString& magnetLink) {
    TorrentItem* item = new TorrentItem(magnetLink, this);
    m_listLayout->insertWidget(0, item);
    this->show();
    item->startDownload();
}