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
#include <QScrollArea>
#include <QStackedWidget>
#include <QPointer>


// --- Тонкий прогресс-бар без текста внутри (компактный вид — цифры теперь
// выводятся отдельными QLabel рядом с карточкой, см. DownloadItem/TorrentItem) ---
class StormProgressBar : public QWidget {
    Q_OBJECT
public:
    explicit StormProgressBar(QWidget* parent = nullptr);
    // progress: 0.0..1.0 — обычный прогресс; отрицательное значение — общий
    // размер неизвестен (сплошная заливка вместо доли).
    void setProgress(qreal progress);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    qreal m_progress;
};

// --- Карточка одной обычной загрузки (компактная: иконка+имя+процент,
// тонкий бар, строка метаданных + иконки-кнопки вместо текстовых) ---
class DownloadItem : public QFrame {
    Q_OBJECT
public:
    explicit DownloadItem(QWebEngineDownloadRequest* download, QWidget* parent = nullptr);

    // Геттеры для DownloadManager::activeDownloadsJson() — живой прогресс на
    // storm://downloads (страница опрашивает их раз в 1.5с через бридж,
    // пока карточка активна; после завершения запись уже есть в истории —
    // см. isActive()/m_isActive ниже).
    QString fileName() const { return m_download->downloadFileName(); }
    QString directory() const { return m_download->downloadDirectory(); } // для проверки дублей в addDownload()
    int percentValue() const { return m_percentValue; }
    QString metaText() const { return m_metaLabel->text(); }
    bool isActive() const { return m_isActive; }

private slots:
    void updateProgress();
    void updateState(QWebEngineDownloadRequest::DownloadState state);
    void togglePause();
    void openFolder();
private:
    QWebEngineDownloadRequest* m_download;
    StormProgressBar* m_progressBar;
    QLabel* m_nameLabel;
    QLabel* m_percentLabel;
    QLabel* m_metaLabel;
    QPushButton* m_pauseBtn;
    QPushButton* m_cancelBtn;
    QPushButton* m_openFolderBtn;

    qint64 m_lastTime;
    qint64 m_lastBytes;
    int m_percentValue = 0;
    bool m_isActive = true; // false как только updateState() увидел Completed/Cancelled/Interrupted
};

// --- Панель менеджера загрузок ---
// Раньше это был постоянно пристыкованный к рабочей области боковой блок.
// Теперь это два разных представления, как в современных браузерах:
//  1) DownloadManager сам — маленькое всплывающее окно (Qt::Popup), которое
//     появляется рядом с кнопкой на панели инструментов (см. popupBelow()) и
//     само закрывается по клику мимо — только "текущая сессия" (обычные и
//     торренты — два переключаемых режима m_regularTabBtn/m_torrentTabBtn,
//     как и раньше).
//  2) Полноценная страница storm://downloads (DownloadsPageHtml.h/.cpp +
//     DownloadsBridge, см. MainWindow_Tabs.cpp) — вся история загрузок сразу,
//     и обычные, и торрент, без разделения на вкладки. Она читает и меняет
//     ту же персистентную историю в QSettings через historyJson()/
//     removeHistoryEntry()/clearAllHistory() ниже — DownloadManager остаётся
//     единственным местом, которое знает формат хранения этой истории.
class DownloadManager : public QWidget {
    Q_OBJECT
public:
    explicit DownloadManager(QWidget* parent = nullptr);
    void addDownload(QWebEngineDownloadRequest* request);
    // resumeSaveDir: если указан — TorrentItem использует его сразу, без
    // диалога выбора папки (нужен для докачки после перезапуска, см.
    // resumeIncompleteTorrentsIfAny() ниже: важно продолжить в ТУ ЖЕ папку,
    // где уже лежат частично скачанные файлы, иначе libtorrent не найдёт их
    // при хеш-проверке и просто начнёт заново).
    void addTorrent(const QString& magnetLink, const QString& resumeSaveDir = QString());

    // Публичная перегрузка — вызывается из контекстного меню страницы
    // (BrowserWebView) и с полноценной страницы storm://downloads (поле
    // "Ссылка на видео" теперь только там, рядом с фильтром "Торренты" —
    // см. DownloadsBridge::startVideoDownload() и DownloadsPageHtml.cpp).
    // В самом попапе такого поля больше нет.
    void startVideoDownload(const QString& url);

    // Персистентная история загрузок — аналог save_history() из Python DownloadManager
    // (см. torrent_manager.py::TorrentItemWidget.on_finished). Вызывается при завершении,
    // отмене и ошибке — и обычных, и торрент-загрузок. type — "regular"/"torrent" (раньше
    // тип узнавался только по префиксу "Торрент: " в title; поле оставлено явным, старые
    // записи без него по-прежнему распознаются по этому префиксу — см. .cpp).
    // sourceUrl — исходная ссылка (URL файла или magnet) для "🔁 Повторить" на
    // storm://downloads; sizeBytes — итоговый размер для отображения (0, если
    // неизвестен — старые записи и промежуточные состояния у торрентов).
    void saveHistory(const QString& title, const QString& path, const QString& status, const QString& type,
        const QString& sourceUrl = QString(), qint64 sizeBytes = 0);

    // Вся персистентная история (обычные + торренты вместе) одним JSON-массивом
    // объектов {id,title,path,status,type,timestamp} — для storm://downloads
    // (см. DownloadsBridge::getDownloadsJson()).
    QString historyJson() const;
    // Все АКТИВНЫЕ (ещё не завершённые) загрузки прямо сейчас — отдельно от
    // historyJson(), т.к. попадают туда только после завершения/отмены/ошибки.
    // Страница storm://downloads опрашивает это раз в 1.5с, пока открыта
    // (см. DownloadsBridge::getActiveDownloadsJson()), и рисует поверх
    // истории живой прогресс. id синтетический (не персистентный) —
    // достаточно, чтобы React-подобный рендер на JS не дёргал DOM зря.
    QString activeDownloadsJson() const;
    // Удаляет одну запись истории по id (см. DownloadsBridge::removeEntry()).
    void removeHistoryEntry(const QString& id);
    // То же самое, но пачкой за один проход по QSettings-массиву — для
    // массового удаления на странице storm://downloads (см.
    // DownloadsBridge::removeEntries()); вызывать removeHistoryEntry() в
    // цикле было бы N перезаписей всего массива вместо одной.
    void removeHistoryEntries(const QStringList& ids);
    // Полностью очищает ВСЮ историю (оба режима сразу) — используется со
    // страницы storm://downloads ("Очистить всё"); отличается от приватного
    // clearHistory() ниже, который чистит только активную сейчас вкладку попапа.
    void clearAllHistory();

    // Незавершённые торренты переживают закрытие браузера: пока торрент
    // качается, реестр "downloads/incomplete_torrents" в QSettings хранит
    // его magnet-ссылку/папку/имя (markTorrentIncomplete()); при успехе или
    // явной отмене запись убирается (clearIncompleteTorrentMark()), при
    // ошибке — остаётся специально, чтобы предложить повтор. Настоящих
    // libtorrent fastresume-данных не храним (версии API notoriously
    // отличаются между релизами libtorrent) — вместо этого просто
    // передобавляем тот же magnet в ТУ ЖЕ папку: libtorrent хеш-проверит
    // уже скачанные куски на диске и докачает только недостающее, то есть
    // это не "с нуля", просто чуть медленнее честного fastresume.
    void markTorrentIncomplete(const QString& magnetLink, const QString& saveDir, const QString& name);
    void clearIncompleteTorrentMark(const QString& magnetLink);
    // Вызывается один раз из MainWindow::setupUi() после создания
    // downloadManager — если реестр не пуст, спрашивает пользователя и,
    // если да, передобавляет каждый торрент через addTorrent(magnet, saveDir).
    void resumeIncompleteTorrentsIfAny();

    // Папка, использованная при последнем скачивании (обычном или торрент-загрузке).
    // Хранится в тех же QSettings, что и история — так следующий диалог выбора
    // файла/папки будет сразу открываться в ней, а не в системной "Загрузки".
    // static — доступна и из TorrentItem, у которого нет прямого указателя на DownloadManager.
    static QString lastDownloadDir();
    static void setLastDownloadDir(const QString& dir);

    // Кнопка на панели инструментов, рядом с которой открывается попап —
    // запоминаем её один раз в MainWindow::setupUi(), чтобы addDownload()/
    // addTorrent() могли сами открыть попап в нужном месте при старте новой
    // загрузки, а не только по явному вызову popupBelow(anchor).
    void setAnchorWidget(QWidget* anchor) { m_anchorWidget = anchor; }
    // Показывает попап у указанного якоря (или у m_anchorWidget, если anchor
    // не передан) — позиционирует под ним и не даёт вылезти за правый край экрана.
    void popupBelow(QWidget* anchor = nullptr);
    // То же самое, но с переключением: если попап уже открыт — прячет его,
    // иначе показывает через popupBelow(anchor). Нужен для кнопки btnDownloads
    // на тулбаре (BrowserTopBar.cpp) — она checkable и по клику должна именно
    // ПЕРЕКЛЮЧАТЬ видимость попапа, а не просто открывать его повторно.
    // Заменяет собой прежнюю ручную toggle-логику в MainWindow::openDownloads().
    void togglePopup(QWidget* anchor = nullptr);

signals:
    // Клик по футеру попапа "📂 Все загрузки". Раньше DownloadManager сам
    // делал qobject_cast<MainWindow*>(parentWidget())->addNewTab(...), для
    // чего .cpp пришлось включить "MainWindow.h" — а MainWindow.h сам
    // включает DownloadManager.h, и это обратное включение спровоцировало
    // C1014 ("слишком много включаемых файлов: глубина = 1024"). Сигнал
    // решает то же самое без единого лишнего include: MainWindow сам
    // подписывается на него в setupUi() и вызывает свой addNewTab().
    void allDownloadsRequested();

    // Эмитится из saveHistory() при каждой записи в историю — то есть при
    // любом завершении/отмене/ошибке и обычной, и торрент-загрузки (один
    // сигнал на все случаи, а не отдельный вызов в каждом из мест, где
    // раньше уже вызывался saveHistory() — см. DownloadItem::updateState()
    // и лямбды TorrentItem). MainWindow подписывается в setupUi() и решает,
    // показывать ли системное уведомление через showTrayNotification().
    void downloadHistoryRecorded(const QString& title, const QString& status, const QString& type);

private slots:
    // Очищает список ТОЛЬКО активной сейчас вкладки попапа (обычные/торренты) —
    // и на экране, и в персистентной истории. Для полной очистки обоих режимов
    // сразу см. публичный clearAllHistory() (используется со страницы storm://downloads).
    void clearHistory();
    void switchToRegularTab();
    void switchToTorrentTab();

private:
    void updateTabLabels();   // обновляет подписи кнопок-вкладок счётчиками ("📥 Обычные · 3")
    void updateEmptyStates(); // показывает/прячет заглушку "пока пусто" для каждого режима

    QVBoxLayout* m_regularListLayout;
    QVBoxLayout* m_torrentListLayout;

    QStackedWidget* m_stack;
    QPushButton* m_regularTabBtn;
    QPushButton* m_torrentTabBtn;
    QWidget* m_regularEmptyState;
    QWidget* m_torrentEmptyState;
    QScrollArea* m_regularScrollArea;
    QScrollArea* m_torrentScrollArea;
    QPointer<QWidget> m_anchorWidget;

    int m_regularCount = 0;
    int m_torrentCount = 0;
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
// --- Карточка торрент-загрузки (компактная, как DownloadItem) ---
class TorrentItem : public QFrame {
    Q_OBJECT
public:
    // resumeSaveDir: см. DownloadManager::addTorrent() — если не пуст,
    // startDownload() использует его напрямую, без диалога выбора папки
    // (докачка после перезапуска браузера, resumeIncompleteTorrentsIfAny()).
    explicit TorrentItem(const QString& magnetLink, QWidget* parent = nullptr, const QString& resumeSaveDir = QString());
    void startDownload();

    // Те же геттеры, что и у DownloadItem — для живого прогресса на
    // storm://downloads (см. DownloadManager::activeDownloadsJson()).
    QString displayName() const { return m_displayName; }
    QString magnetLink() const { return m_magnetLink; } // для проверки дублей в addTorrent()
    int percentValue() const { return m_percentValue; }
    QString metaText() const { return m_metaLabel->text(); }
    bool isActive() const { return m_isActive; }

private slots:
    void cancelDownload();
    void openFolder();
    void togglePause(); // как в DownloadItem — та же ⏸/▶ иконка-кнопка, тот же стиль
    void updateUi(int percent, const QString& status, const QString& speed, const QString& peers, const QVector<bool>& pieces);

private:
    QString m_magnetLink;
    QString m_resumeSaveDir; // непусто — значит это докачка после перезапуска, см. конструктор/startDownload()
    QString m_displayName;   // имя торрента (из dn= magnet-ссылки, без обрезки под ширину карточки) — для displayName()
    QLabel* m_nameLabel;   // имя торрента из параметра dn= magnet-ссылки, иначе заглушка
    QLabel* m_percentLabel;
    QLabel* m_metaLabel;   // "статус · скорость · пиры" одной строкой
    QPushButton* m_pauseBtn;
    TorrentChunkMap* m_chunkMap;
    QPushButton* m_cancelBtn;
    QPushButton* m_openFolderBtn;
    QString m_saveDir;
    int m_percentValue = 0;
    bool m_isActive = true; // false как только карточка завершена/отменена/упала с ошибкой

    TorrentDownloaderThread* m_thread = nullptr;
};