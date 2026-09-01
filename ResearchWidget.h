#pragma once
#include <QWidget>
#include <QUrl>

class QTextEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QListWidget;
class QListWidgetItem;
class QThread;
class QTimer;
class ResearchManager;

// ==========================================================================
// ResearchWidget — UI-виджет модуля "Deep Research Report" ("🧠") для
// боковой панели Storm Browser. Регистрируется в Sidebar так же, как
// ReaderWidget/AIAssistantWidget:
//
//     ResearchWidget* research = new ResearchWidget(this);
//     sidebar->addItem("🧠", "Глубокое исследование", research);
//     connect(research, &ResearchWidget::openReportRequested, this, &MainWindow::openInReader);
//     connect(research, &ResearchWidget::reportSavedToLibrary, readerWidget, &ReaderWidget::loadBooks);
//
// Сам виджет НЕ делает сеть/парсинг/рендер PDF — вся тяжёлая работа отдана
// ResearchManager, который живёт в собственном QThread. ResearchWidget
// только рисует UI и транслирует пользовательские действия в сигналы,
// которые ResearchManager слушает как слоты (и наоборот для прогресса).
// ==========================================================================
class ResearchWidget : public QWidget {
    Q_OBJECT
public:
    explicit ResearchWidget(QWidget* parent = nullptr);
    ~ResearchWidget() override;

signals:
    // Просьба открыть готовый файл в Storm Reader. Тип и семантика сигнала
    // намеренно зеркалят ReaderWidget::openBookRequested(QUrl) — MainWindow
    // может подключить один и тот же обработчик к обоим сигналам.
    void openReportRequested(const QUrl& fileUrl);

    // В папку books/ добавлен новый файл — сигнал для MainWindow, чтобы
    // попросить ReaderWidget обновить список книг (loadBooks()), иначе
    // свежий отчёт не появится в библиотеке до следующего её открытия.
    void reportSavedToLibrary(const QString& filePath);

    // --- "Внутренняя шина": эти два сигнала подключены в конструкторе
    // ТОЛЬКО к слотам ResearchManager (startResearch/cancelResearch) и
    // наружу класса не эмитятся. Формально это часть public API класса
    // (Qt не даёт ограничить emit только private-методами), но по смыслу
    // это именно команды воркеру, а не события для внешних подписчиков. ---
    void requestStartResearch(const QString& topic, const QString& depth, const QString& format);
    void requestCancelResearch();

private slots:
    void onStartOrCancelClicked();
    void onProgressChanged(const QString& stageText, int percent);
    void onResearchFinished(const QString& filePath, const QString& title);
    void onResearchFailed(const QString& errorMessage);
    void onResearchQuotaExceeded(const QString& message, int used, int limit);
    void onDotsAnimationTick();
    void onUpgradeClicked();

private:
    void buildUi();
    void setupWorkerThread();
    void setRunningState(bool running);
    QString currentDepthCode() const;  // "fast" | "deep" из depthCombo
    QString currentFormatCode() const; // "pdf" | "txt" из formatCombo

    // --- История отчётов (список внизу) ---
    void addHistoryRow(const QString& filePath, const QString& title, const QString& dateText);
    void loadHistoryFromSettings();
    void persistHistoryEntry(const QString& filePath, const QString& title);
    // Убирает запись ТОЛЬКО из списка истории (QSettings) — сам файл отчёта
    // на диске не трогает, он остаётся доступен через общую библиотеку
    // Storm Reader. Кнопка "✕" — это разгрести список, а не стереть отчёт.
    void removeHistoryEntry(const QString& filePath);

    // UI
    QTextEdit* topicInput = nullptr;
    QComboBox* depthCombo = nullptr;
    QComboBox* formatCombo = nullptr;
    QPushButton* startBtn = nullptr;
    QLabel* statusLabel = nullptr;
    QProgressBar* progressBar = nullptr;
    // Показывается ТОЛЬКО поверх ошибки "квота исчерпана" (см.
    // onResearchQuotaExceeded) — обычные ошибки (сеть, пустой ответ ИИ и
    // т.п.) остаются просто текстом в statusLabel, без этой кнопки.
    QPushButton* upgradeBtn = nullptr;
    QListWidget* historyList = nullptr;
    QTimer* dotsTimer = nullptr; // косметическая анимация "…" на statusLabel, пока идёт исследование
    int m_dotsPhase = 0;
    QString m_baseStatusText;

    // Воркер
    QThread* workerThread = nullptr;
    ResearchManager* manager = nullptr;

    bool m_running = false;
};