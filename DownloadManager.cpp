#include "DownloadManager.h"
#include <QPainter>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
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
#include <QSet>
#include <QFontMetrics>
#include <QPointer>
#include <QScreen>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUuid>
#include <atomic>
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

    // Пауза/резюме. torrent_handle у libtorrent — лёгкий thread-safe хендл
    // (методы просто постят сообщение во внутренний io-поток сессии), так
    // что можно спокойно звать их прямо из UI-потока, не трогая run().
    // m_handleReady нужен, т.к. хендл появляется только после add_torrent
    // внутри run() — до этого момента пауза/резюме просто не действуют,
    // а сохраняют желаемое состояние в m_pausedRequested, которое run()
    // применит сразу после получения хендла.
    void pauseTorrent();
    void resumeTorrent();
    bool isPaused() const { return m_isPaused; }

protected:
    void run() override;

signals:
    void progressUpdated(int percent, const QString& status, const QString& speed, const QString& peers, const QVector<bool>& pieces);
    // totalBytes — последний известный status.total_wanted (то, что реально
    // качаем, без пропущенных файлов) — для "размера" в истории/на странице
    // storm://downloads (см. TorrentItem::startDownload()'s finished-лямбда).
    void finished(const QString& path, qint64 totalBytes);
    void error(const QString& msg);

private:
    QString m_magnetLink;
    QString m_saveDir;
    bool m_isRunning;

    lt::torrent_handle m_handle;
    std::atomic<bool> m_handleReady{ false };
    std::atomic<bool> m_isPaused{ false };
    std::atomic<bool> m_pausedRequested{ false };
};

// ==========================================
// --- ОБЩИЕ ПОМОЩНИКИ ДЛЯ КОМПАКТНОГО UI ---
// ==========================================
namespace {

    // Один и тот же стиль плоской иконки-кнопки (без текста, без рамки,
    // лёгкая подсветка при наведении) — используется и в DownloadItem, и в
    // TorrentItem, и в шапке DownloadManager, чтобы все действия выглядели
    // одинаково вместо разномастных текстовых кнопок, как было раньше.
    QString compactIconButtonStyle(const QString& hoverColor) {
        return QString(
            "QPushButton { background: transparent; border: none; border-radius: 6px; "
            "font-size: 13px; color: #c9d1d9; padding: 0px; } "
            "QPushButton:hover { background: rgba(255,255,255,0.08); color: %1; }"
        ).arg(hoverColor);
    }

    // Эмодзи-иконка по расширению файла — небольшой штрих "современности",
    // раньше все загрузки показывали одну и ту же 📄 иконку независимо от типа.
    QString iconForDownloadFile(const QString& fileName) {
        const QString ext = QFileInfo(fileName).suffix().toLower();
        static const QSet<QString> video = { "mp4","mkv","avi","mov","webm","flv","wmv" };
        static const QSet<QString> audio = { "mp3","wav","flac","aac","ogg","m4a" };
        static const QSet<QString> image = { "png","jpg","jpeg","gif","bmp","webp","svg" };
        static const QSet<QString> archive = { "zip","rar","7z","tar","gz" };
        static const QSet<QString> doc = { "pdf","doc","docx","xls","xlsx","ppt","pptx","txt" };
        static const QSet<QString> exec = { "exe","msi","apk","dmg" };

        if (video.contains(ext)) return u8"🎬";
        if (audio.contains(ext)) return u8"🎵";
        if (image.contains(ext)) return u8"🖼️";
        if (archive.contains(ext)) return u8"🗜️";
        if (doc.contains(ext)) return u8"📄";
        if (exec.contains(ext)) return u8"⚙️";
        return u8"📦";
    }

    // Человекочитаемое имя торрента из параметра dn= magnet-ссылки — раньше
    // карточка торрента вообще не показывала название, только статус.
    QString extractTorrentName(const QString& magnetLink) {
        QUrl url(magnetLink);
        QUrlQuery query(url);
        QString dn = query.queryItemValue("dn", QUrl::FullyDecoded);
        dn.replace('+', ' ');
        return dn.isEmpty() ? QString::fromUtf8(u8"Торрент-загрузка") : dn;
    }

    // btih-хэш из параметра xt= (urn:btih:HASH) — устойчивее к сравнению "тот
    // же торрент?", чем сырое сравнение magnet-строк целиком: два magnet на
    // один и тот же файл почти никогда не совпадают побайтово (разные
    // трекеры/порядок параметров/регистр хэша), а btih у них один и тот же.
    // Используется только для определения дублей при старте новой закачки
    // (см. DownloadManager::addTorrent()), приводим к нижнему регистру.
    QString extractTorrentHash(const QString& magnetLink) {
        QUrl url(magnetLink);
        QUrlQuery query(url);
        for (const auto& item : query.queryItems(QUrl::FullyDecoded)) {
            if (item.first == "xt" && item.second.startsWith("urn:btih:")) {
                return item.second.mid(QString("urn:btih:").length()).toLower();
            }
        }
        return QString();
    }

    // Общая заглушка "список пуст" — для вкладок "Обычные" и "Торренты".
    QWidget* buildEmptyState(const QString& emoji, const QString& text, QWidget* parent) {
        QWidget* w = new QWidget(parent);
        QVBoxLayout* l = new QVBoxLayout(w);
        l->setAlignment(Qt::AlignCenter);
        l->setSpacing(4);

        QLabel* emojiLabel = new QLabel(emoji, w);
        emojiLabel->setAlignment(Qt::AlignCenter);
        emojiLabel->setStyleSheet("font-size: 26px;");

        QLabel* textLabel = new QLabel(text, w);
        textLabel->setAlignment(Qt::AlignCenter);
        textLabel->setStyleSheet("font-size: 12px; color: #6e7681;");

        l->addWidget(emojiLabel);
        l->addWidget(textLabel);
        return w;
    }

} // namespace

// ==========================================
// --- КОМПАКТНЫЙ ПРОГРЕСС-БАР ---
// ==========================================
StormProgressBar::StormProgressBar(QWidget* parent) : QWidget(parent), m_progress(0.0) {
    setFixedHeight(6); // было 30 (с текстом внутри) — теперь тонкая полоса без текста
}

void StormProgressBar::setProgress(qreal progress) {
    m_progress = progress;
    update();
}

void StormProgressBar::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QRectF rect(0, 0, width(), height());
    qreal radius = height() / 2.0;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#161b22"));
    painter.drawRoundedRect(rect, radius, radius);

    if (m_progress < 0) {
        // Общий размер неизвестен — сплошная заливка вместо доли прогресса.
        painter.setBrush(QColor("#4facfe"));
        painter.drawRoundedRect(rect, radius, radius);
        return;
    }

    if (m_progress > 0) {
        QRectF fillRect(0, 0, width() * qBound(0.0, m_progress, 1.0), height());
        QLinearGradient gradient(0, 0, width(), 0);
        gradient.setColorAt(0.0, QColor("#a371f7"));
        gradient.setColorAt(1.0, QColor("#4facfe"));
        painter.setBrush(gradient);
        painter.drawRoundedRect(fillRect, radius, radius);
    }
}

// ==========================================
// --- КАРТОЧКА ЗАГРУЗКИ (компактный вид) ---
// ==========================================
DownloadItem::DownloadItem(QWebEngineDownloadRequest* download, QWidget* parent)
    : QFrame(parent), m_download(download)
{
    setStyleSheet(
        "DownloadItem { background-color: #1c2128; border: 1px solid #30363d; border-radius: 10px; } "
        "DownloadItem:hover { border: 1px solid #3d444d; }"
    );

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8); // было 15,15,15,15
    layout->setSpacing(6);

    // --- Верхняя строка: иконка типа файла + имя (обрезается многоточием
    // посередине — самая полезная часть имени файла обычно в начале и в
    // расширении) + процент справа ---
    QHBoxLayout* topRow = new QHBoxLayout();
    topRow->setSpacing(6);

    QLabel* iconLabel = new QLabel(iconForDownloadFile(m_download->downloadFileName()), this);
    iconLabel->setStyleSheet("font-size: 15px;");
    iconLabel->setFixedWidth(20);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setStyleSheet("font-size: 12.5px; font-weight: 600; color: #eef3ff;");
    {
        QFontMetrics fm(m_nameLabel->font());
        m_nameLabel->setText(fm.elidedText(m_download->downloadFileName(), Qt::ElideMiddle, 170));
    }
    m_nameLabel->setToolTip(m_download->downloadFileName());

    m_percentLabel = new QLabel("0%", this);
    m_percentLabel->setStyleSheet("font-size: 11px; font-weight: 600; color: #8b949e;");

    topRow->addWidget(iconLabel);
    topRow->addWidget(m_nameLabel, 1);
    topRow->addWidget(m_percentLabel);
    layout->addLayout(topRow);

    // --- Тонкая полоса прогресса (без текста внутри) ---
    m_progressBar = new StormProgressBar(this);
    layout->addWidget(m_progressBar);

    // --- Нижняя строка: метаданные (объём/скорость) слева, иконки-кнопки справа ---
    QHBoxLayout* bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(2);

    m_metaLabel = new QLabel(u8"Подготовка...", this);
    m_metaLabel->setStyleSheet("font-size: 11px; color: #8b949e;");
    m_metaLabel->setMinimumWidth(0); // может сжиматься, не распирая карточку

    m_pauseBtn = new QPushButton(u8"⏸", this);
    m_cancelBtn = new QPushButton(u8"✕", this);
    m_openFolderBtn = new QPushButton(u8"📂", this);
    m_openFolderBtn->hide();

    for (QPushButton* btn : { m_pauseBtn, m_cancelBtn, m_openFolderBtn }) {
        btn->setFixedSize(26, 26);
        btn->setCursor(Qt::PointingHandCursor);
    }
    m_pauseBtn->setStyleSheet(compactIconButtonStyle("#a371f7"));
    m_pauseBtn->setToolTip(u8"Пауза");
    m_cancelBtn->setStyleSheet(compactIconButtonStyle("#ff6b6b"));
    m_cancelBtn->setToolTip(u8"Отменить");
    m_openFolderBtn->setStyleSheet(compactIconButtonStyle("#56d39b"));
    m_openFolderBtn->setToolTip(u8"Открыть папку");

    bottomRow->addWidget(m_metaLabel, 1);
    bottomRow->addWidget(m_pauseBtn);
    bottomRow->addWidget(m_cancelBtn);
    bottomRow->addWidget(m_openFolderBtn);
    layout->addLayout(bottomRow);

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
        QString speedStr = QString::number(speed / (1024 * 1024), 'f', 1) + u8" МБ/с";

        m_lastTime = currentTime;
        m_lastBytes = received;

        if (total > 0) {
            qreal ratio = (qreal)received / total;
            m_progressBar->setProgress(ratio);
            m_percentValue = qRound(ratio * 100);
            m_percentLabel->setText(QString::number(m_percentValue) + "%");
            m_metaLabel->setText(QString::number(received / (1024 * 1024)) + " / " +
                QString::number(total / (1024 * 1024)) + u8" МБ · " + speedStr);
        }
        else {
            m_progressBar->setProgress(-1.0);
            m_percentLabel->setText("?%");
            m_metaLabel->setText(QString::number(received / (1024 * 1024)) + u8" МБ · " + speedStr);
        }
    }
}

void DownloadItem::updateState(QWebEngineDownloadRequest::DownloadState state) {
    // "completed"/"cancelled"/"interrupted" пишутся в общую историю для всех
    // трёх исходов — раньше запись в историю вообще не вызывалась для
    // обычных загрузок (только для торрентов), и даже после того, как её
    // добавили для DownloadCompleted, отменённые/оборвавшиеся загрузки так и
    // не попадали в историю. Без этого storm://downloads показывала бы
    // только успешные обычные загрузки, а отменённые/сбойные — только если
    // это был торрент.
    QString historyStatus;

    if (state == QWebEngineDownloadRequest::DownloadCompleted) {
        m_progressBar->setProgress(1.0);
        m_percentValue = 100;
        m_percentLabel->setText("100%");
        m_metaLabel->setText(u8"✅ Завершено");
        m_pauseBtn->hide();
        m_cancelBtn->hide();
        m_openFolderBtn->show();
        historyStatus = u8"completed";
    }
    else if (state == QWebEngineDownloadRequest::DownloadCancelled) {
        m_progressBar->setProgress(0.0);
        m_percentLabel->setText(u8"❌");
        m_metaLabel->setText(u8"Отменено");
        m_pauseBtn->hide();
        m_cancelBtn->hide();
        historyStatus = u8"cancelled";
    }
    else if (state == QWebEngineDownloadRequest::DownloadInterrupted) {
        m_percentLabel->setText(u8"⚠️");
        m_metaLabel->setText(u8"Ошибка сети");
        m_pauseBtn->hide();
        m_cancelBtn->hide();
        historyStatus = u8"interrupted";
    }
    else {
        return; // DownloadInProgress и т.п. — сюда не попадаем, updateProgress() уже всё обновил
    }

    QString fullPath = QDir(m_download->downloadDirectory()).filePath(m_download->downloadFileName());
    QWidget* parentPanel = this->parentWidget();
    while (parentPanel && !qobject_cast<DownloadManager*>(parentPanel)) {
        parentPanel = parentPanel->parentWidget();
    }
    if (DownloadManager* dm = qobject_cast<DownloadManager*>(parentPanel)) {
        dm->saveHistory(m_download->downloadFileName(), fullPath, historyStatus, "regular",
            m_download->url().toString(), m_download->totalBytes());
    }
    m_isActive = false; // после этой строки карточка больше не попадает в activeDownloadsJson()
}

void DownloadItem::togglePause() {
    if (m_download->isPaused()) {
        m_download->resume();
        m_pauseBtn->setText(u8"⏸");
        m_pauseBtn->setToolTip(u8"Пауза");
        m_lastTime = QDateTime::currentMSecsSinceEpoch();
        m_lastBytes = m_download->receivedBytes();
    }
    else {
        m_download->pause();
        m_pauseBtn->setText(u8"▶");
        m_pauseBtn->setToolTip(u8"Возобновить");
    }
}

void DownloadItem::openFolder() {
    QString path = m_download->downloadDirectory();
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// ==========================================
// --- ПРАВАЯ ПАНЕЛЬ ЗАГРУЗОК (два режима: Обычные / Торренты) ---
// ==========================================
DownloadManager::DownloadManager(QWidget* parent) : QWidget(parent) {
    // Раньше это был постоянно пристыкованный к рабочей области боковой блок
    // (добавлялся в workLayout). Теперь это всплывающее окно, как в Chrome/
    // Firefox: не занимает место в раскладке, всплывает рядом с кнопкой на
    // панели инструментов (см. popupBelow()) и само закрывается по клику
    // мимо/потере фокуса благодаря Qt::Popup.
    //
    // ИСТОРИЯ БАГА С ПРОЗРАЧНОСТЬЮ: сначала пробовали WA_TranslucentBackground
    // (ради скруглённых углов) — окно оказывалось полностью прозрачным, т.к.
    // обычный QWidget без WA_StyledBackground вообще не красит свой QSS-фон.
    // Добавили WA_StyledBackground — прозрачность осталась (сочетание
    // Qt::Popup + WA_TranslucentBackground на Windows нестабильно рендерит
    // альфа-канал независимо от QSS). Убрали WA_TranslucentBackground и
    // border-radius совсем — простое непрозрачное прямоугольное окно
    // (WA_StyledBackground оставлен — он и красит обычный QWidget фоном
    // из QSS, без него background-color тоже не рисовался бы).
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedSize(320, 440); // было setFixedWidth(320) без ограничения высоты — теперь окно, а не панель во всю высоту
    setObjectName("downloadsPanel");
    setStyleSheet("QWidget#downloadsPanel { background-color: #0d1117; border: 1px solid #30363d; }");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 14, 14, 8);
    mainLayout->setSpacing(10);

    // --- 1. ШАПКА: заголовок + компактные иконки-кнопки ---
    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* title = new QLabel(u8"📥 Загрузки", this);
    title->setStyleSheet("font-size: 16px; font-weight: 700; color: white;");
    headerLayout->addWidget(title);
    headerLayout->addStretch();

    QPushButton* clearBtn = new QPushButton(u8"🧹", this);
    clearBtn->setFixedSize(26, 26);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setToolTip(u8"Очистить текущий список");
    clearBtn->setStyleSheet(compactIconButtonStyle("#a371f7"));
    connect(clearBtn, &QPushButton::clicked, this, &DownloadManager::clearHistory);
    headerLayout->addWidget(clearBtn);

    QPushButton* closeBtn = new QPushButton(u8"✕", this);
    closeBtn->setFixedSize(26, 26);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip(u8"Скрыть панель");
    closeBtn->setStyleSheet(compactIconButtonStyle("#ff6b6b"));
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::hide);
    headerLayout->addWidget(closeBtn);

    mainLayout->addLayout(headerLayout);

    // --- 2. ПЕРЕКЛЮЧАТЕЛЬ РЕЖИМОВ: Обычные / Торренты ---
    // Раньше обе загрузки просто валились в один список вперемешку — теперь
    // это два явных режима со своим списком и счётчиком в подписи кнопки.
    QHBoxLayout* tabsLayout = new QHBoxLayout();
    tabsLayout->setSpacing(4);

    m_regularTabBtn = new QPushButton(this);
    m_torrentTabBtn = new QPushButton(this);
    m_regularTabBtn->setCheckable(true);
    m_torrentTabBtn->setCheckable(true);
    m_regularTabBtn->setChecked(true);
    m_regularTabBtn->setCursor(Qt::PointingHandCursor);
    m_torrentTabBtn->setCursor(Qt::PointingHandCursor);

    const QString tabStyle =
        "QPushButton { background: transparent; color: #8b949e; border: none; border-radius: 7px; "
        "padding: 6px 10px; font-size: 12px; font-weight: 600; } "
        "QPushButton:checked { background: #21262d; color: #eef3ff; } "
        "QPushButton:hover:!checked { color: #c9d1d9; }";
    m_regularTabBtn->setStyleSheet(tabStyle);
    m_torrentTabBtn->setStyleSheet(tabStyle);

    connect(m_regularTabBtn, &QPushButton::clicked, this, &DownloadManager::switchToRegularTab);
    connect(m_torrentTabBtn, &QPushButton::clicked, this, &DownloadManager::switchToTorrentTab);

    tabsLayout->addWidget(m_regularTabBtn);
    tabsLayout->addWidget(m_torrentTabBtn);
    tabsLayout->addStretch();
    mainLayout->addLayout(tabsLayout);

    // --- 3. СТРАНИЦА "ОБЫЧНЫЕ": список / заглушка. Поле "Ссылка на видео"
    // раньше было здесь — теперь только на полноценной странице
    // storm://downloads, рядом с фильтром "Торренты" (см. DownloadsPageHtml.cpp/
    // DownloadsBridge::startVideoDownload()), чтобы не загромождать попап. ---
    QWidget* regularPage = new QWidget(this);
    QVBoxLayout* regularPageLayout = new QVBoxLayout(regularPage);
    regularPageLayout->setContentsMargins(0, 0, 0, 0);
    regularPageLayout->setSpacing(10);


    QScrollArea* regularScroll = new QScrollArea(regularPage);
    regularScroll->setWidgetResizable(true);
    regularScroll->setStyleSheet("QScrollArea { border: none; background: transparent; } "
        "QWidget#scrollContent { background: transparent; }");
    QWidget* regularScrollContent = new QWidget(regularScroll);
    regularScrollContent->setObjectName("scrollContent");
    m_regularListLayout = new QVBoxLayout(regularScrollContent);
    m_regularListLayout->setAlignment(Qt::AlignTop);
    m_regularListLayout->setSpacing(8);
    m_regularListLayout->setContentsMargins(0, 0, 2, 0);
    regularScroll->setWidget(regularScrollContent);
    m_regularScrollArea = regularScroll;

    m_regularEmptyState = buildEmptyState(u8"📭", u8"Пока нет загрузок", regularPage);

    regularPageLayout->addWidget(m_regularEmptyState);
    regularPageLayout->addWidget(regularScroll, 1);

    // --- 4. СТРАНИЦА "ТОРРЕНТЫ": список / заглушка. Своего поля ввода нет —
    // торренты запускаются кликом по magnet-ссылке на странице
    // (см. StormWebPage::magnetLinkActivated в MainWindow_Tabs.cpp) ---
    QWidget* torrentPage = new QWidget(this);
    QVBoxLayout* torrentPageLayout = new QVBoxLayout(torrentPage);
    torrentPageLayout->setContentsMargins(0, 0, 0, 0);
    torrentPageLayout->setSpacing(10);

    QScrollArea* torrentScroll = new QScrollArea(torrentPage);
    torrentScroll->setWidgetResizable(true);
    torrentScroll->setStyleSheet("QScrollArea { border: none; background: transparent; } "
        "QWidget#scrollContent { background: transparent; }");
    QWidget* torrentScrollContent = new QWidget(torrentScroll);
    torrentScrollContent->setObjectName("scrollContent");
    m_torrentListLayout = new QVBoxLayout(torrentScrollContent);
    m_torrentListLayout->setAlignment(Qt::AlignTop);
    m_torrentListLayout->setSpacing(8);
    m_torrentListLayout->setContentsMargins(0, 0, 2, 0);
    torrentScroll->setWidget(torrentScrollContent);
    m_torrentScrollArea = torrentScroll;

    m_torrentEmptyState = buildEmptyState(u8"🧲", u8"Нет активных торрентов", torrentPage);

    torrentPageLayout->addWidget(m_torrentEmptyState);
    torrentPageLayout->addWidget(torrentScroll, 1);

    // --- 5. СТЕК СТРАНИЦ ---
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(regularPage); // index 0
    m_stack->addWidget(torrentPage); // index 1
    mainLayout->addWidget(m_stack, 1);

    // --- 6. ФУТЕР: ссылка на полноценную страницу storm://downloads —
    // попап показывает только текущую сессию, вся история (и обычные, и
    // торренты вместе, без разделения на вкладки) — на отдельной странице. ---
    QPushButton* allDownloadsBtn = new QPushButton(u8"📂 Все загрузки", this);
    allDownloadsBtn->setCursor(Qt::PointingHandCursor);
    allDownloadsBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-top: 1px solid #21262d; "
        "color: #8b949e; font-size: 12px; font-weight: 600; padding: 8px 0px 0px 0px; } "
        "QPushButton:hover { color: #eef3ff; }"
    );
    connect(allDownloadsBtn, &QPushButton::clicked, this, [this]() {
        emit allDownloadsRequested(); // MainWindow сам решит, что с этим делать (см. setupUi())
        hide(); // попап — всплывающее окно, прячем его, раз пользователь ушёл на полную страницу
        });
    mainLayout->addWidget(allDownloadsBtn);

    updateTabLabels();
    updateEmptyStates();
}

void DownloadManager::switchToRegularTab() {
    m_regularTabBtn->setChecked(true);
    m_torrentTabBtn->setChecked(false);
    m_stack->setCurrentIndex(0);
}

void DownloadManager::switchToTorrentTab() {
    m_torrentTabBtn->setChecked(true);
    m_regularTabBtn->setChecked(false);
    m_stack->setCurrentIndex(1);
}

void DownloadManager::updateTabLabels() {
    m_regularTabBtn->setText(m_regularCount > 0
        ? QString(u8"📥 Обычные · %1").arg(m_regularCount)
        : QString::fromUtf8(u8"📥 Обычные"));
    m_torrentTabBtn->setText(m_torrentCount > 0
        ? QString(u8"🧲 Торренты · %1").arg(m_torrentCount)
        : QString::fromUtf8(u8"🧲 Торренты"));
}

void DownloadManager::updateEmptyStates() {
    m_regularEmptyState->setVisible(m_regularCount == 0);
    m_regularScrollArea->setVisible(m_regularCount > 0);
    m_torrentEmptyState->setVisible(m_torrentCount == 0);
    m_torrentScrollArea->setVisible(m_torrentCount > 0);
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

    // Дубль: тот же файл уже активно качается в ту же папку.
    for (int i = 0; i < m_regularListLayout->count(); ++i) {
        QLayoutItem* li = m_regularListLayout->itemAt(i);
        if (!li || !li->widget()) continue;
        auto* existing = qobject_cast<DownloadItem*>(li->widget());
        if (!existing || !existing->isActive()) continue;
        if (existing->fileName() == fileName && existing->directory() == dirPath) {
            QString question = QString(u8"Файл «%1» уже качается в эту же папку.\n\nВсё равно начать ещё раз?").arg(fileName);
            if (QMessageBox::question(this, u8"Уже качается", question, QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
                request->cancel();
                return;
            }
            break;
        }
    }

    DownloadItem* item = new DownloadItem(request, this);
    m_regularListLayout->insertWidget(0, item);

    m_regularCount++;
    updateTabLabels();
    updateEmptyStates();
    switchToRegularTab(); // новая обычная загрузка сразу открывает свою вкладку

    request->accept();
    popupBelow(); // было this->show() — теперь всплывающее окно у кнопки на тулбаре, а не боковая панель
}

void DownloadManager::addTorrent(const QString& magnetLink, const QString& resumeSaveDir) {
    // Дубль по btih-хэшу — только для явного клика по magnet-ссылке
    // (resumeSaveDir пуст); при докачке после перезапуска дубль в принципе
    // невозможен — за этим следит сам реестр незавершённых, так что тут
    // проверку пропускаем.
    if (resumeSaveDir.isEmpty()) {
        QString newHash = extractTorrentHash(magnetLink);
        if (!newHash.isEmpty()) {
            for (int i = 0; i < m_torrentListLayout->count(); ++i) {
                QLayoutItem* li = m_torrentListLayout->itemAt(i);
                if (!li || !li->widget()) continue;
                auto* existing = qobject_cast<TorrentItem*>(li->widget());
                if (!existing || !existing->isActive()) continue;
                if (extractTorrentHash(existing->magnetLink()) != newHash) continue;

                QString question = u8"Этот торрент уже есть в списке загрузок.\n\nДобавить ещё раз?";
                if (QMessageBox::question(this, u8"Уже качается", question, QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
                    return;
                }
                break;
            }
        }
    }

    TorrentItem* item = new TorrentItem(magnetLink, this, resumeSaveDir);
    m_torrentListLayout->insertWidget(0, item);

    m_torrentCount++;
    updateTabLabels();
    updateEmptyStates();
    switchToTorrentTab(); // новый торрент сразу открывает вкладку "Торренты"

    // Единственный случай, когда карточка может пропасть из списка не через
    // "Очистить" — отмена (TorrentItem::cancelDownload() вызывает
    // deleteLater()). Слушаем destroyed(), чтобы счётчик и заглушка "пусто"
    // оставались верными. QPointer на себя — чтобы не трогать уже
    // уничтоженный DownloadManager, если сигнал долетит при закрытии приложения.
    connect(item, &QObject::destroyed, this, [self = QPointer<DownloadManager>(this)]() {
        if (!self) return;
        self->m_torrentCount = qMax(0, self->m_torrentCount - 1);
        self->updateTabLabels();
        self->updateEmptyStates();
        });

    popupBelow(); // было this->show()
    item->startDownload();
}

void DownloadManager::clearHistory() {
    // Очищаем только ту вкладку, что сейчас открыта — раньше "Очистить" всегда
    // сносило единый общий список целиком.
    const bool torrentTab = (m_stack->currentIndex() == 1);
    const QString question = torrentTab
        ? QString::fromUtf8(u8"Очистить список торрент-загрузок?\n(Файлы на диске удалены не будут)")
        : QString::fromUtf8(u8"Очистить список обычных загрузок?\n(Файлы на диске удалены не будут)");

    if (QMessageBox::question(this, u8"Очистка", question) != QMessageBox::Yes) return;

    QVBoxLayout* targetLayout = torrentTab ? m_torrentListLayout : m_regularListLayout;
    QLayoutItem* child;
    while ((child = targetLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    if (torrentTab) m_torrentCount = 0; else m_regularCount = 0;
    updateTabLabels();
    updateEmptyStates();

    // Персистентная история хранится в ОДНОМ общем массиве QSettings для
    // обоих режимов (см. saveHistory()) — отфильтровываем записи только
    // текущего режима, не трогая другой. type — новое явное поле; записи,
    // сохранённые до его появления, по-прежнему распознаются старым способом
    // (по префиксу "Торрент: " в title) — та же эвристика, что и в historyJson().
    QSettings settings("Shtorm Software", "Storm Browser");
    struct Entry { QString id, title, path, status, type, sourceUrl, timestamp; qint64 size; };
    QList<Entry> keep;

    int size = settings.beginReadArray("downloads/history");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        Entry e;
        e.id = settings.value("id").toString();
        e.title = settings.value("title").toString();
        e.path = settings.value("path").toString();
        e.status = settings.value("status").toString();
        e.type = settings.value("type").toString();
        e.sourceUrl = settings.value("sourceUrl").toString();
        e.size = settings.value("size").toLongLong();
        e.timestamp = settings.value("timestamp").toString();
        bool isTorrentEntry = (e.type == "torrent")
            || (e.type.isEmpty() && e.title.startsWith(QString::fromUtf8(u8"Торрент: ")));
        if (isTorrentEntry != torrentTab) keep.append(e);
    }
    settings.endArray();

    settings.remove("downloads/history");
    settings.beginWriteArray("downloads/history");
    for (int i = 0; i < keep.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("id", keep[i].id);
        settings.setValue("title", keep[i].title);
        settings.setValue("path", keep[i].path);
        settings.setValue("status", keep[i].status);
        settings.setValue("type", keep[i].type);
        settings.setValue("sourceUrl", keep[i].sourceUrl);
        settings.setValue("size", keep[i].size);
        settings.setValue("timestamp", keep[i].timestamp);
    }
    settings.endArray();
}

void DownloadManager::saveHistory(const QString& title, const QString& path, const QString& status, const QString& type,
    const QString& sourceUrl, qint64 sizeBytes) {
    // Аналог save_history() из Python DownloadManager: копим историю загрузок
    // в QSettings (тот же профиль "Shtorm Software"/"Storm Browser", что уже
    // используется для VPN- и security-настроек), чтобы она переживала
    // перезапуск браузера. id — новый уникальный идентификатор записи (для
    // removeHistoryEntry() со страницы storm://downloads); записи, сохранённые
    // до этого обновления, id не имеют — removeHistoryEntry() их просто не найдёт,
    // но на отображение и очистку списком это не влияет.
    QSettings settings("Shtorm Software", "Storm Browser");

    int size = settings.beginReadArray("downloads/history");
    settings.endArray();

    settings.beginWriteArray("downloads/history");
    settings.setArrayIndex(size);
    settings.setValue("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
    settings.setValue("title", title);
    settings.setValue("path", path);
    settings.setValue("status", status);
    settings.setValue("type", type);
    settings.setValue("sourceUrl", sourceUrl);
    settings.setValue("size", sizeBytes);
    settings.setValue("timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));
    settings.endArray();

    emit downloadHistoryRecorded(title, status, type);
}

QString DownloadManager::historyJson() const {
    // Вся история (оба режима вместе) одним JSON-массивом — для
    // storm://downloads (см. DownloadsBridge::getDownloadsJson()). Единственное
    // место, которое знает формат хранения "downloads/history" в QSettings —
    // страница получает уже готовые объекты и просто их рендерит.
    QSettings settings("Shtorm Software", "Storm Browser");
    QJsonArray arr;

    int size = settings.beginReadArray("downloads/history");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QJsonObject obj;
        QString id = settings.value("id").toString();
        if (id.isEmpty()) id = QString("legacy-%1").arg(i); // записи до появления явного id
        QString title = settings.value("title").toString();
        QString type = settings.value("type").toString();
        if (type.isEmpty()) {
            // Записи до появления явного поля type — старая эвристика по префиксу.
            type = title.startsWith(QString::fromUtf8(u8"Торрент: ")) ? "torrent" : "regular";
        }
        obj["id"] = id;
        obj["title"] = title;
        obj["path"] = settings.value("path").toString();
        obj["status"] = settings.value("status").toString();
        obj["type"] = type;
        obj["sourceUrl"] = settings.value("sourceUrl").toString(); // пусто у записей до этого поля — "🔁 Повторить" на странице для них просто не покажется
        obj["size"] = settings.value("size").toLongLong();
        obj["timestamp"] = settings.value("timestamp").toString();
        arr.append(obj);
    }
    settings.endArray();

    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString DownloadManager::activeDownloadsJson() const {
    // В отличие от historyJson() — ничего не читает из QSettings, просто
    // опрашивает то, что прямо сейчас лежит в m_regularListLayout/
    // m_torrentListLayout (живые виджеты, не персистентные записи).
    // id синтетический: тип + адрес виджета в памяти — стабилен, пока жива
    // сама карточка, чего достаточно для JS-рендера между опросами.
    QJsonArray arr;

    for (int i = 0; i < m_regularListLayout->count(); ++i) {
        QLayoutItem* item = m_regularListLayout->itemAt(i);
        if (!item || !item->widget()) continue;
        auto* downloadItem = qobject_cast<DownloadItem*>(item->widget());
        if (!downloadItem || !downloadItem->isActive()) continue;

        QJsonObject obj;
        obj["id"] = QString("active-regular-%1").arg(reinterpret_cast<quintptr>(downloadItem));
        obj["title"] = downloadItem->fileName();
        obj["type"] = "regular";
        obj["percent"] = downloadItem->percentValue();
        obj["meta"] = downloadItem->metaText();
        arr.append(obj);
    }

    for (int i = 0; i < m_torrentListLayout->count(); ++i) {
        QLayoutItem* item = m_torrentListLayout->itemAt(i);
        if (!item || !item->widget()) continue;
        auto* torrentItem = qobject_cast<TorrentItem*>(item->widget());
        if (!torrentItem || !torrentItem->isActive()) continue;

        QJsonObject obj;
        obj["id"] = QString("active-torrent-%1").arg(reinterpret_cast<quintptr>(torrentItem));
        obj["title"] = torrentItem->displayName();
        obj["type"] = "torrent";
        obj["percent"] = torrentItem->percentValue();
        obj["meta"] = torrentItem->metaText();
        arr.append(obj);
    }

    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void DownloadManager::removeHistoryEntry(const QString& id) {
    QSettings settings("Shtorm Software", "Storm Browser");
    struct Entry { QString id, title, path, status, type, sourceUrl, timestamp; qint64 size; };
    QList<Entry> keep;

    int size = settings.beginReadArray("downloads/history");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        Entry e;
        e.id = settings.value("id").toString();
        if (e.id.isEmpty()) e.id = QString("legacy-%1").arg(i);
        e.title = settings.value("title").toString();
        e.path = settings.value("path").toString();
        e.status = settings.value("status").toString();
        e.type = settings.value("type").toString();
        e.sourceUrl = settings.value("sourceUrl").toString();
        e.size = settings.value("size").toLongLong();
        e.timestamp = settings.value("timestamp").toString();
        if (e.id != id) keep.append(e);
    }
    settings.endArray();

    settings.remove("downloads/history");
    settings.beginWriteArray("downloads/history");
    for (int i = 0; i < keep.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("id", keep[i].id.startsWith("legacy-") ? QString() : keep[i].id);
        settings.setValue("title", keep[i].title);
        settings.setValue("path", keep[i].path);
        settings.setValue("status", keep[i].status);
        settings.setValue("type", keep[i].type);
        settings.setValue("sourceUrl", keep[i].sourceUrl);
        settings.setValue("size", keep[i].size);
        settings.setValue("timestamp", keep[i].timestamp);
    }
    settings.endArray();
}

void DownloadManager::removeHistoryEntries(const QStringList& ids) {
    if (ids.isEmpty()) return;
    const QSet<QString> idsToRemove(ids.begin(), ids.end());

    QSettings settings("Shtorm Software", "Storm Browser");
    struct Entry { QString id, title, path, status, type, sourceUrl, timestamp; qint64 size; };
    QList<Entry> keep;

    int size = settings.beginReadArray("downloads/history");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        Entry e;
        e.id = settings.value("id").toString();
        if (e.id.isEmpty()) e.id = QString("legacy-%1").arg(i);
        e.title = settings.value("title").toString();
        e.path = settings.value("path").toString();
        e.status = settings.value("status").toString();
        e.type = settings.value("type").toString();
        e.sourceUrl = settings.value("sourceUrl").toString();
        e.size = settings.value("size").toLongLong();
        e.timestamp = settings.value("timestamp").toString();
        if (!idsToRemove.contains(e.id)) keep.append(e);
    }
    settings.endArray();

    settings.remove("downloads/history");
    settings.beginWriteArray("downloads/history");
    for (int i = 0; i < keep.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("id", keep[i].id.startsWith("legacy-") ? QString() : keep[i].id);
        settings.setValue("title", keep[i].title);
        settings.setValue("path", keep[i].path);
        settings.setValue("status", keep[i].status);
        settings.setValue("type", keep[i].type);
        settings.setValue("sourceUrl", keep[i].sourceUrl);
        settings.setValue("size", keep[i].size);
        settings.setValue("timestamp", keep[i].timestamp);
    }
    settings.endArray();
}

void DownloadManager::clearAllHistory() {
    // В отличие от приватного clearHistory() (чистит только активную сейчас
    // вкладку попапа) — сносит ВСЮ персистентную историю разом, оба режима.
    // Используется кнопкой "Очистить всё" на странице storm://downloads.
    QSettings settings("Shtorm Software", "Storm Browser");
    settings.remove("downloads/history");
}

void DownloadManager::markTorrentIncomplete(const QString& magnetLink, const QString& saveDir, const QString& name) {
    QSettings settings("Shtorm Software", "Storm Browser");

    // На случай повторного вызова для того же magnet (не должно происходить
    // в обычном UI-потоке, но дешевле подстраховаться, чем плодить дубли
    // в реестре) — сначала проверяем, нет ли уже такой записи.
    int size = settings.beginReadArray("downloads/incomplete_torrents");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        if (settings.value("magnetLink").toString() == magnetLink) {
            settings.endArray();
            return;
        }
    }
    settings.endArray();

    settings.beginWriteArray("downloads/incomplete_torrents");
    settings.setArrayIndex(size);
    settings.setValue("magnetLink", magnetLink);
    settings.setValue("saveDir", saveDir);
    settings.setValue("name", name);
    settings.setValue("timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));
    settings.endArray();
}

void DownloadManager::clearIncompleteTorrentMark(const QString& magnetLink) {
    QSettings settings("Shtorm Software", "Storm Browser");
    struct Entry { QString magnetLink, saveDir, name, timestamp; };
    QList<Entry> keep;

    int size = settings.beginReadArray("downloads/incomplete_torrents");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        Entry e;
        e.magnetLink = settings.value("magnetLink").toString();
        e.saveDir = settings.value("saveDir").toString();
        e.name = settings.value("name").toString();
        e.timestamp = settings.value("timestamp").toString();
        if (e.magnetLink != magnetLink) keep.append(e);
    }
    settings.endArray();

    settings.remove("downloads/incomplete_torrents");
    settings.beginWriteArray("downloads/incomplete_torrents");
    for (int i = 0; i < keep.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("magnetLink", keep[i].magnetLink);
        settings.setValue("saveDir", keep[i].saveDir);
        settings.setValue("name", keep[i].name);
        settings.setValue("timestamp", keep[i].timestamp);
    }
    settings.endArray();
}

void DownloadManager::resumeIncompleteTorrentsIfAny() {
    QSettings settings("Shtorm Software", "Storm Browser");

    struct Pending { QString magnetLink, saveDir, name; };
    QList<Pending> pending;

    int size = settings.beginReadArray("downloads/incomplete_torrents");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        Pending p;
        p.magnetLink = settings.value("magnetLink").toString();
        p.saveDir = settings.value("saveDir").toString();
        p.name = settings.value("name").toString();
        if (!p.magnetLink.isEmpty() && !p.saveDir.isEmpty()) pending.append(p);
    }
    settings.endArray();

    if (pending.isEmpty()) return;

    QString message;
    if (pending.size() == 1) {
        QString name = pending.first().name.isEmpty() ? QString::fromUtf8(u8"без названия") : pending.first().name;
        message = QString(u8"Обнаружен незавершённый торрент из прошлой сессии:\n«%1»\n\nПродолжить закачку?").arg(name);
    }
    else {
        message = QString(u8"Обнаружено незавершённых торрентов из прошлой сессии: %1.\n\nПродолжить закачку всех?").arg(pending.size());
    }

    if (QMessageBox::question(this, u8"Незавершённые загрузки", message, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        // addTorrent() сам добавит карточку и вызовет startDownload(), которое
        // (т.к. m_resumeSaveDir не пуст) сразу пойдёт в ТУ ЖЕ папку и заново
        // отметит запись как незавершённую (markTorrentIncomplete() безопасен
        // к повтору — см. проверку на дубль в начале) — реестр остаётся
        // корректным без ручной синхронизации здесь.
        for (const Pending& p : pending) {
            addTorrent(p.magnetLink, p.saveDir);
        }
    }
    else {
        // Пользователь отказался докачивать — не спрашивать об этом же снова
        // при каждом следующем запуске; считаем эти загрузки заброшенными.
        settings.remove("downloads/incomplete_torrents");
    }
}

void DownloadManager::popupBelow(QWidget* anchor) {
    QWidget* target = anchor ? anchor : m_anchorWidget.data();
    if (!target) {
        show();
        raise();
        activateWindow();
        return;
    }

    QPoint pos = target->mapToGlobal(QPoint(0, target->height() + 6));

    // Не даём попапу вылезти за правый край экрана — кнопка загрузок обычно
    // ближе к правому краю тулбара, а попап выравнивается по левому краю
    // кнопки по умолчанию, так что это частый случай, а не редкий крайний.
    if (QScreen* screen = target->screen()) {
        int rightEdge = pos.x() + width();
        if (rightEdge > screen->availableGeometry().right()) {
            pos.setX(target->mapToGlobal(QPoint(target->width(), 0)).x() - width());
        }
    }

    move(pos);
    show();
    raise();
    activateWindow();
}

void DownloadManager::togglePopup(QWidget* anchor) {
    // isVisible() достаточно: попап либо полностью показан (show()), либо
    // полностью скрыт (hide() — и вручную из clearHistory()/футера "Все
    // загрузки", и автоматически самим Qt::Popup при клике мимо/потере
    // фокуса) — промежуточных состояний тут не бывает.
    if (isVisible()) {
        hide();
    }
    else {
        popupBelow(anchor);
    }
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
    setFixedHeight(10); // было 24 — компактнее, как и остальная карточка
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
    Q_UNUSED(event);
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
// --- КАРТОЧКА ТОРРЕНТА (компактный вид, как DownloadItem) ---
// ==========================================
TorrentItem::TorrentItem(const QString& magnetLink, QWidget* parent, const QString& resumeSaveDir)
    : QFrame(parent), m_magnetLink(magnetLink), m_resumeSaveDir(resumeSaveDir) {

    setStyleSheet(
        "TorrentItem { background-color: #1c2128; border: 1px solid #3a3d6e; border-radius: 10px; } "
        "TorrentItem:hover { border: 1px solid #57599c; }"
    );

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8); // было 15,15,15,15
    layout->setSpacing(6);

    // --- Верхняя строка: иконка + имя торрента (из dn= magnet-ссылки, раньше
    // не показывалось вообще) + процент справа ---
    QHBoxLayout* topRow = new QHBoxLayout();
    topRow->setSpacing(6);

    QLabel* iconLabel = new QLabel(u8"🧲", this);
    iconLabel->setStyleSheet("font-size: 15px;");
    iconLabel->setFixedWidth(20);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setStyleSheet("font-size: 12.5px; font-weight: 600; color: #eef3ff;");
    QString rawName = extractTorrentName(magnetLink);
    m_displayName = rawName;
    {
        QFontMetrics fm(m_nameLabel->font());
        m_nameLabel->setText(fm.elidedText(rawName, Qt::ElideMiddle, 170));
    }
    m_nameLabel->setToolTip(rawName);

    m_percentLabel = new QLabel("0%", this);
    m_percentLabel->setStyleSheet("font-size: 11px; font-weight: 600; color: #8b949e;");

    topRow->addWidget(iconLabel);
    topRow->addWidget(m_nameLabel, 1);
    topRow->addWidget(m_percentLabel);
    layout->addLayout(topRow);

    // --- Компактная карта чанков ---
    m_chunkMap = new TorrentChunkMap(QColor("#6e8cff"), this);
    layout->addWidget(m_chunkMap);

    // --- Нижняя строка: статус/скорость/пиры одной строкой + иконки-кнопки ---
    QHBoxLayout* bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(2);

    m_metaLabel = new QLabel(u8"Подготовка к P2P загрузке...", this);
    m_metaLabel->setStyleSheet("font-size: 11px; color: #8b949e;");
    m_metaLabel->setMinimumWidth(0);

    m_pauseBtn = new QPushButton(u8"⏸", this);
    m_cancelBtn = new QPushButton(u8"✕", this);
    m_openFolderBtn = new QPushButton(u8"📂", this);
    m_openFolderBtn->hide();

    for (QPushButton* btn : { m_pauseBtn, m_cancelBtn, m_openFolderBtn }) {
        btn->setFixedSize(26, 26);
        btn->setCursor(Qt::PointingHandCursor);
    }
    m_pauseBtn->setStyleSheet(compactIconButtonStyle("#a371f7"));
    m_pauseBtn->setToolTip(u8"Пауза");
    m_cancelBtn->setStyleSheet(compactIconButtonStyle("#ff6b6b"));
    m_cancelBtn->setToolTip(u8"Отменить");
    m_openFolderBtn->setStyleSheet(compactIconButtonStyle("#56d39b"));
    m_openFolderBtn->setToolTip(u8"Открыть папку");

    bottomRow->addWidget(m_metaLabel, 1);
    bottomRow->addWidget(m_pauseBtn);
    bottomRow->addWidget(m_cancelBtn);
    bottomRow->addWidget(m_openFolderBtn);
    layout->addLayout(bottomRow);

    connect(m_pauseBtn, &QPushButton::clicked, this, &TorrentItem::togglePause);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TorrentItem::cancelDownload);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &TorrentItem::openFolder);
}

void TorrentItem::togglePause() {
    if (!m_thread) return; // до startDownload() (диалог выбора папки ещё не закрыт) паузить нечего

    if (m_thread->isPaused()) {
        m_thread->resumeTorrent();
        m_pauseBtn->setText(u8"⏸");
        m_pauseBtn->setToolTip(u8"Пауза");
    }
    else {
        m_thread->pauseTorrent();
        m_pauseBtn->setText(u8"▶");
        m_pauseBtn->setToolTip(u8"Возобновить");
    }
}

void TorrentItem::updateUi(int percent, const QString& status, const QString& speed, const QString& peers, const QVector<bool>& pieces) {
    m_chunkMap->updateFromBitfield(pieces);
    m_percentValue = percent;
    m_percentLabel->setText(QString::number(percent) + "%");
    m_metaLabel->setText(status + u8" · " + speed + u8" · " + peers);
}

void TorrentItem::cancelDownload() {
    m_isActive = false;
    if (m_thread && m_thread->isRunning()) {
        m_thread->stop();
        m_thread->wait(); // Ждем безопасного завершения потока
    }

    // Как и с обычными загрузками (см. DownloadItem::updateState) — отменённый
    // торрент тоже попадает в историю, иначе storm://downloads не показывала
    // бы отменённые торренты вообще.
    QWidget* parentPanel = this->parentWidget();
    while (parentPanel && !qobject_cast<DownloadManager*>(parentPanel)) {
        parentPanel = parentPanel->parentWidget();
    }
    if (DownloadManager* dm = qobject_cast<DownloadManager*>(parentPanel)) {
        QString rawName = extractTorrentName(m_magnetLink);
        dm->saveHistory(u8"Торрент: " + rawName, m_saveDir, u8"cancelled", "torrent", m_magnetLink);
        // Отменено самим пользователем — не предлагать докачать это при
        // следующем запуске (в отличие от ошибки сети, см. лямбду error ниже).
        dm->clearIncompleteTorrentMark(m_magnetLink);
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

void TorrentDownloaderThread::pauseTorrent() {
    m_pausedRequested = true;
    if (m_handleReady && m_handle.is_valid()) {
        m_handle.pause();
    }
    m_isPaused = true;
}

void TorrentDownloaderThread::resumeTorrent() {
    m_pausedRequested = false;
    if (m_handleReady && m_handle.is_valid()) {
        m_handle.resume();
    }
    m_isPaused = false;
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

        // Хендл готов — публикуем его для pauseTorrent()/resumeTorrent(),
        // которые могли быть вызваны из UI-потока ДО этого момента (тогда
        // они только выставили m_pausedRequested и m_isPaused, саму паузу
        // применяем только сейчас, когда есть на чём её вызывать).
        m_handle = handle;
        m_handleReady = true;
        if (m_pausedRequested) {
            handle.pause();
        }

        qint64 lastTotalBytes = 0;

        while (m_isRunning && !handle.status().is_seeding) {
            lt::torrent_status status = handle.status();
            lastTotalBytes = status.total_wanted;

            int progress = status.progress * 100;
            QString speed = QString::number(status.download_rate / 1000000.0, 'f', 2) + " MB/s";
            QString peers = QString(u8"Пиры: %1").arg(status.num_peers);
            QString stateStr;
            if (m_isPaused) {
                stateStr = u8"⏸ На паузе";
                speed = "0.00 MB/s";
            }
            else if (status.state == lt::torrent_status::checking_files) stateStr = u8"Проверка файлов";
            else if (status.state == lt::torrent_status::downloading_metadata) stateStr = u8"Поиск метаданных";
            else stateStr = u8"Скачивание";

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
            lastTotalBytes = handle.status().total_wanted; // финальное значение, поточнее последнего из цикла
            QVector<bool> fullPieces(100, true);
            emit progressUpdated(100, u8"✅ Завершено", "0.00 MB/s", u8"Пиры: 0", fullPieces);
            emit finished(m_saveDir, lastTotalBytes);
        }
    }
    catch (std::exception& e) {
        emit error(QString::fromStdString(e.what()));
    }
}

void TorrentItem::startDownload() {
    if (!m_resumeSaveDir.isEmpty()) {
        // Докачка после перезапуска (resumeIncompleteTorrentsIfAny()) — папка
        // уже известна из реестра, никакого диалога: важно продолжить именно
        // в неё, иначе libtorrent не найдёт уже скачанные куски при
        // хеш-проверке и по сути начнёт заново.
        m_saveDir = m_resumeSaveDir;
    }
    else {
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
    }

    // Отмечаем как незавершённый ДО старта, а не после — если браузер
    // закроют посреди закачки, реестр уже будет знать про неё (см.
    // markTorrentIncomplete()/resumeIncompleteTorrentsIfAny()).
    {
        QWidget* parentPanel = this->parentWidget();
        while (parentPanel && !qobject_cast<DownloadManager*>(parentPanel)) {
            parentPanel = parentPanel->parentWidget();
        }
        if (DownloadManager* dm = qobject_cast<DownloadManager*>(parentPanel)) {
            dm->markTorrentIncomplete(m_magnetLink, m_saveDir, extractTorrentName(m_magnetLink));
        }
    }

    m_thread = new TorrentDownloaderThread(m_magnetLink, m_saveDir, this);
    connect(m_thread, &TorrentDownloaderThread::progressUpdated, this, &TorrentItem::updateUi);
    connect(m_thread, &TorrentDownloaderThread::finished, this, [this](const QString& path, qint64 totalBytes) {
        m_isActive = false;
        m_percentValue = 100;
        m_pauseBtn->hide();
        m_cancelBtn->hide();
        m_openFolderBtn->show();
        m_percentLabel->setText("100%");
        m_metaLabel->setText(u8"✅ Завершено");

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
            // Префикс "Торрент: " оставлен и сейчас, чтобы записи, сохранённые
            // ДО этого обновления (когда явного поля type ещё не было),
            // продолжали корректно распознаваться как торренты в historyJson()/
            // clearHistory() ниже — но теперь это уже не единственный признак:
            // type="torrent" передаётся явно.
            dm->saveHistory(u8"Торрент: " + folderName, path, u8"completed", "torrent", m_magnetLink, totalBytes);
            dm->clearIncompleteTorrentMark(m_magnetLink); // успешно докачан — из реестра "незавершённых" убираем
        }
        });
    connect(m_thread, &TorrentDownloaderThread::error, this, [this](const QString& err) {
        m_isActive = false;
        m_metaLabel->setText(u8"⚠️ Ошибка: " + err);
        m_pauseBtn->hide();
        m_cancelBtn->hide();

        QWidget* parentPanel = this->parentWidget();
        while (parentPanel && !qobject_cast<DownloadManager*>(parentPanel)) {
            parentPanel = parentPanel->parentWidget();
        }
        if (DownloadManager* dm = qobject_cast<DownloadManager*>(parentPanel)) {
            QString rawName = extractTorrentName(m_magnetLink);
            dm->saveHistory(u8"Торрент: " + rawName, m_saveDir, u8"interrupted", "torrent", m_magnetLink);
        }
        });

    m_thread->start();
}
#include "DownloadManager.moc"