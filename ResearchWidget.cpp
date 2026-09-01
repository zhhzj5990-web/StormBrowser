#include "ResearchWidget.h"
#include "ResearchManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFont>
#include <QLabel>
#include <QProgressBar>
#include <QListWidget>
#include <QAbstractItemView>
#include <QFrame>
#include <QThread>
#include <QTimer>
#include <QSettings>
#include <QFileInfo>
#include <QDateTime>
#include <QUrl>
#include <QVariant>
#include <QList>

// О литералах: как и в остальном проекте (см. Sidebar.cpp), НЕ используем
// префикс u8"..." для строк, идущих в QString — файл сохранён в UTF-8, этого
// достаточно, а u8"..." под C++20 не конвертируется в QString неявно.

namespace {
    constexpr int kMaxHistoryEntries = 20;
}

ResearchWidget::ResearchWidget(QWidget* parent) : QWidget(parent) {
    buildUi();
    setupWorkerThread();
    loadHistoryFromSettings();
}

ResearchWidget::~ResearchWidget() {
    // Стандартный Qt-паттерн остановки воркер-объекта: просим поток
    // завершиться и ждём (с разумным таймаутом — не вешаем закрытие
    // приложения намертво, если что-то пошло не так). Сам ResearchManager
    // удалится сам через connect(workerThread, finished, manager, deleteLater)
    // в setupWorkerThread() — вручную его удалять НЕ нужно (см. тот же
    // паттерн и его обоснование в комментариях Sidebar.h/.cpp про
    // QPointer/детерминированное удаление, только здесь роль "того, что
    // должно быть остановлено первым" играет QThread, а не QPropertyAnimation).
    if (workerThread) {
        // cancelResearch() безопасен, даже если исследование сейчас не идёт.
        emit requestCancelResearch();
        workerThread->quit();
        workerThread->wait(3000);
    }
}

// ==========================================================================
// UI
// ==========================================================================
void ResearchWidget::buildUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 20, 15, 15);
    layout->setSpacing(10);

    QLabel* title = new QLabel("<b>🧠 Deep Research Report</b>", this);
    title->setStyleSheet("font-size: 18px; color: #a371f7;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    QLabel* desc = new QLabel("Соберу и прочитаю подборку сайтов по теме, а Storm AI составит из них отчёт.", this);
    desc->setWordWrap(true);
    desc->setAlignment(Qt::AlignCenter);
    desc->setStyleSheet("color: #8b949e; font-size: 11px;");
    layout->addWidget(desc);

    // --- Блок ввода темы ---
    topicInput = new QTextEdit(this);
    topicInput->setFixedHeight(70);
    topicInput->setPlaceholderText("Что будем исследовать?");
    topicInput->setStyleSheet(
        "QTextEdit { background: rgba(0,0,0,0.6); border: 1px solid #444; border-radius: 6px; "
        "color: white; padding: 8px; font-size: 13px; }"
        "QTextEdit:focus { border: 1px solid #a371f7; }");
    layout->addWidget(topicInput);

    // --- Компактная панель настроек: глубина + формат в одну строку ---
    QHBoxLayout* settingsLayout = new QHBoxLayout();
    settingsLayout->setSpacing(8);

    QString comboStyle =
        "QComboBox { background: rgba(255,255,255,0.06); color: #adbac7; border: 1px solid rgba(255,255,255,0.12); "
        "border-radius: 6px; padding: 5px 8px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #22272e; color: #adbac7; selection-background-color: rgba(163,113,247,0.3); }";

    depthCombo = new QComboBox(this);
    depthCombo->addItem("⚡ Быстро (6 сайтов)", "fast");
    depthCombo->addItem("🔎 Глубоко (10-15 сайтов)", "deep");
    depthCombo->setCurrentIndex(1);
    depthCombo->setStyleSheet(comboStyle);
    depthCombo->setToolTip("Глубина поиска");

    formatCombo = new QComboBox(this);
    formatCombo->addItem("📕 PDF", "pdf");
    formatCombo->addItem("📄 TXT", "txt");
    formatCombo->setStyleSheet(comboStyle);
    formatCombo->setToolTip("Формат отчёта");

    settingsLayout->addWidget(depthCombo, 1);
    settingsLayout->addWidget(formatCombo, 1);
    layout->addLayout(settingsLayout);

    // --- Кнопка запуска (она же — кнопка отмены, пока идёт исследование) ---
    startBtn = new QPushButton("🧠 Начать исследование", this);
    startBtn->setCursor(Qt::PointingHandCursor);
    startBtn->setMinimumHeight(38);
    startBtn->setStyleSheet(
        "QPushButton { background-color: #a371f7; color: white; border: none; border-radius: 8px; "
        "font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background-color: #b98af9; }"
        "QPushButton:pressed { background-color: #8c5de0; }"
        "QPushButton:disabled { background-color: rgba(163,113,247,0.35); }");
    connect(startBtn, &QPushButton::clicked, this, &ResearchWidget::onStartOrCancelClicked);
    layout->addWidget(startBtn);

    // --- Прогресс ---
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(false);
    progressBar->setFixedHeight(6);
    progressBar->setStyleSheet(
        "QProgressBar { background: rgba(255,255,255,0.08); border-radius: 3px; }"
        "QProgressBar::chunk { background-color: #a371f7; border-radius: 3px; }");
    progressBar->hide();
    layout->addWidget(progressBar);

    statusLabel = new QLabel(this);
    statusLabel->setStyleSheet("color: #8b949e; font-size: 12px;");
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setWordWrap(true);
    statusLabel->hide();
    layout->addWidget(statusLabel);

    // Кнопка апгрейда — появляется ТОЛЬКО когда сервер сообщил, что лимит
    // бесплатных/оплаченных отчётов исчерпан (см. onResearchQuotaExceeded).
    // Ведёт в storm://cloud тем же путём, что и обычное открытие отчёта в
    // Storm Reader (см. onUpgradeClicked) — отдельного сигнала под это не
    // заводили, MainWindow и так открывает по URL любую переданную ссылку.
    upgradeBtn = new QPushButton("💎 Оформить подписку в Личном кабинете", this);
    upgradeBtn->setCursor(Qt::PointingHandCursor);
    upgradeBtn->setMinimumHeight(32);
    upgradeBtn->setStyleSheet(
        "QPushButton { background: rgba(255,200,87,0.12); color: #ffc857; border: 1px solid rgba(255,200,87,0.4); "
        "border-radius: 6px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: rgba(255,200,87,0.22); }");
    upgradeBtn->hide();
    connect(upgradeBtn, &QPushButton::clicked, this, &ResearchWidget::onUpgradeClicked);
    layout->addWidget(upgradeBtn);

    dotsTimer = new QTimer(this);
    dotsTimer->setInterval(450);
    connect(dotsTimer, &QTimer::timeout, this, &ResearchWidget::onDotsAnimationTick);

    // --- Разделитель перед историей ---
    QFrame* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet("color: rgba(255,255,255,0.1);");
    layout->addWidget(separator);

    QLabel* historyTitle = new QLabel("📚 Последние отчёты", this);
    historyTitle->setStyleSheet("color: #adbac7; font-size: 12px; font-weight: bold;");
    layout->addWidget(historyTitle);

    historyList = new QListWidget(this);
    historyList->setFrameShape(QFrame::NoFrame);
    historyList->setStyleSheet(
        "QListWidget { background: transparent; }"
        "QListWidget::item { border: none; margin-bottom: 4px; }"
        "QListWidget::item:selected { background: transparent; }");
    historyList->setSelectionMode(QAbstractItemView::NoSelection);
    historyList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    layout->addWidget(historyList, 1);
}

// ==========================================================================
// Воркер-поток
// ==========================================================================
void ResearchWidget::setupWorkerThread() {
    workerThread = new QThread(this); // родитель — this, поток удалится вместе с виджетом
    manager = new ResearchManager();  // БЕЗ родителя — обязательное условие для moveToThread()
    manager->moveToThread(workerThread);

    // Стандартный Qt worker-object паттерн: когда поток завершает работу
    // (после quit()+wait() в деструкторе), объект-воркер удаляется сам.
    connect(workerThread, &QThread::finished, manager, &QObject::deleteLater);

    // Команды из GUI-потока в поток воркера.
    connect(this, &ResearchWidget::requestStartResearch, manager, &ResearchManager::startResearch);
    connect(this, &ResearchWidget::requestCancelResearch, manager, &ResearchManager::cancelResearch);

    // События из потока воркера обратно в GUI-поток. Все соединения ниже —
    // между объектами из разных потоков, поэтому Qt::AutoConnection сам
    // становится Qt::QueuedConnection — вручную указывать не нужно.
    connect(manager, &ResearchManager::progressChanged, this, &ResearchWidget::onProgressChanged);
    connect(manager, &ResearchManager::researchFinished, this, &ResearchWidget::onResearchFinished);
    connect(manager, &ResearchManager::researchFailed, this, &ResearchWidget::onResearchFailed);
    connect(manager, &ResearchManager::researchQuotaExceeded, this, &ResearchWidget::onResearchQuotaExceeded);

    workerThread->start();
}

QString ResearchWidget::currentDepthCode() const {
    return depthCombo->currentData().toString();
}

QString ResearchWidget::currentFormatCode() const {
    return formatCombo->currentData().toString();
}

// ==========================================================================
// Запуск / отмена
// ==========================================================================
void ResearchWidget::onStartOrCancelClicked() {
    if (m_running) {
        emit requestCancelResearch();
        statusLabel->setText("⏹ Отмена…");
        startBtn->setEnabled(false); // до прихода researchFailed("Исследование отменено.") не даём давить повторно
        return;
    }

    QString topic = topicInput->toPlainText().trimmed();
    if (topic.isEmpty()) {
        topicInput->setPlaceholderText("Сначала введите тему!");
        topicInput->setStyleSheet(
            "QTextEdit { background: rgba(0,0,0,0.6); border: 1px solid #ff5f5f; border-radius: 6px; "
            "color: white; padding: 8px; font-size: 13px; }");
        return;
    }

    setRunningState(true);
    m_baseStatusText = "Сбор ссылок…";
    statusLabel->setText(m_baseStatusText);
    statusLabel->show();
    upgradeBtn->hide(); // на случай, если предыдущая попытка закончилась квотой — новая попытка убирает старый CTA
    progressBar->show();
    progressBar->setValue(0);
    dotsTimer->start();

    emit requestStartResearch(topic, currentDepthCode(), currentFormatCode());
}

void ResearchWidget::setRunningState(bool running) {
    m_running = running;
    topicInput->setEnabled(!running);
    depthCombo->setEnabled(!running);
    formatCombo->setEnabled(!running);
    startBtn->setEnabled(true); // включаем сразу — вдруг это переключение в "остановлено" после отмены

    if (running) {
        startBtn->setText("⏹ Остановить исследование");
        startBtn->setStyleSheet(
            "QPushButton { background-color: #ff5f5f; color: white; border: none; border-radius: 8px; "
            "font-weight: bold; font-size: 13px; }"
            "QPushButton:hover { background-color: #ff7a7a; }");
    }
    else {
        startBtn->setText("🧠 Начать исследование");
        startBtn->setStyleSheet(
            "QPushButton { background-color: #a371f7; color: white; border: none; border-radius: 8px; "
            "font-weight: bold; font-size: 13px; }"
            "QPushButton:hover { background-color: #b98af9; }"
            "QPushButton:pressed { background-color: #8c5de0; }"
            "QPushButton:disabled { background-color: rgba(163,113,247,0.35); }");
        dotsTimer->stop();
    }
}

// ==========================================================================
// Прогресс / статус
// ==========================================================================
void ResearchWidget::onProgressChanged(const QString& stageText, int percent) {
    m_baseStatusText = stageText;
    m_dotsPhase = 0;
    statusLabel->setText(m_baseStatusText);
    progressBar->setValue(percent);
}

void ResearchWidget::onDotsAnimationTick() {
    // Косметическая "дышащая" анимация поверх текста этапа, который
    // прислал ResearchManager — сам по себе прогресс обновляется редко
    // (по этапам/страницам), а эта анимация просто показывает, что
    // приложение не зависло, пока идёт долгий сетевой запрос.
    m_dotsPhase = (m_dotsPhase + 1) % 4;
    statusLabel->setText(m_baseStatusText + QString(".").repeated(m_dotsPhase));
}

// ==========================================================================
// Завершение
// ==========================================================================
void ResearchWidget::onResearchFinished(const QString& filePath, const QString& title) {
    setRunningState(false);
    statusLabel->setText("✅ Отчёт готов: " + title);
    progressBar->setValue(100);
    upgradeBtn->hide();

    persistHistoryEntry(filePath, title);
    addHistoryRow(filePath, title, QDateTime::currentDateTime().toString("dd.MM HH:mm"));

    // Требование ТЗ: после сохранения — сразу открыть отчёт в Storm Reader,
    // не дожидаясь клика по истории.
    emit reportSavedToLibrary(filePath);
    emit openReportRequested(QUrl::fromLocalFile(filePath));
}

void ResearchWidget::onResearchFailed(const QString& errorMessage) {
    setRunningState(false);
    statusLabel->setText("❌ " + errorMessage);
    progressBar->setValue(0);
}

void ResearchWidget::onResearchQuotaExceeded(const QString& message, int used, int limit) {
    setRunningState(false);
    progressBar->setValue(0);

    QString text = "💎 " + message;
    if (used >= 0 && limit >= 0) {
        text += QString(" (%1/%2)").arg(used).arg(limit);
    }
    statusLabel->setText(text);
    statusLabel->show();
    upgradeBtn->show();
}

void ResearchWidget::onUpgradeClicked() {
    // Переиспользуем openReportRequested — семантически это просто "открой
    // эту ссылку новой вкладкой", MainWindow подключает его к addNewTab()
    // безусловно (см. MainWindow.cpp), какой бы URL внутри ни был. Заводить
    // отдельный сигнал ради одной кнопки не стали.
    emit openReportRequested(QUrl("storm://cloud"));
}

// ==========================================================================
// История отчётов
// ==========================================================================
void ResearchWidget::addHistoryRow(const QString& filePath, const QString& title, const QString& dateText) {
    // Создаём item заранее (а не перед insertItem, как раньше) — кнопке "✕"
    // ниже нужен прямой указатель на него, чтобы удалить именно свою строку,
    // не разыскивая её по списку.
    QListWidgetItem* item = new QListWidgetItem(historyList);

    QWidget* row = new QWidget(historyList);
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(6, 6, 6, 6);
    rowLayout->setSpacing(6);
    row->setStyleSheet("background: rgba(255,255,255,0.04); border-radius: 6px;");

    QLabel* icon = new QLabel("📄", row);
    icon->setFixedWidth(18);

    QString displayTitle = title.size() > 30 ? title.left(30) + "…" : title;
    QLabel* label = new QLabel(displayTitle + "\n" + dateText, row);
    label->setStyleSheet("color: #adbac7; font-size: 11px;");
    label->setToolTip(title);

    // Компактная "▶" вместо прежней "▶ Читать" текстом — освобождает место
    // под кнопку "✕" рядом, чтобы список не разъезжался по ширине.
    QPushButton* readBtn = new QPushButton("▶", row);
    readBtn->setCursor(Qt::PointingHandCursor);
    readBtn->setFixedSize(26, 26);
    readBtn->setToolTip("Читать");
    // padding: 0 — без этого нативный стиль кнопки (особенно на Windows)
    // резервирует под рамку/фокус-рамку часть и так крошечных 26x26, и
    // одиночному глифу физически не остаётся места — кнопка рисуется
    // пустым квадратом. Плюс явный шрифт с широкой поддержкой символов —
    // тот же приём, что и у иконок в Sidebar.cpp (там тоже эмодзи/символы
    // на кнопках не отображались без явного setFamily).
    readBtn->setStyleSheet(
        "QPushButton { background: rgba(163,113,247,0.18); color: #a371f7; border: 1px solid rgba(163,113,247,0.35); "
        "border-radius: 5px; font-size: 13px; font-weight: bold; padding: 0; }"
        "QPushButton:hover { background: rgba(163,113,247,0.32); }");
    {
        QFont f = readBtn->font();
        f.setFamily("Segoe UI Symbol");
        f.setPointSize(11);
        readBtn->setFont(f);
    }

    connect(readBtn, &QPushButton::clicked, this, [this, filePath, readBtn]() {
        if (!QFileInfo::exists(filePath)) {
            readBtn->setEnabled(false);
            readBtn->setToolTip("Файл удалён");
            return;
        }
        emit openReportRequested(QUrl::fromLocalFile(filePath));
        });

    // "✕" — убирает запись только из ЭТОГО списка (истории отчётов модуля),
    // сам файл не трогает: он всё ещё лежит в books/ и виден в Storm Reader.
    // Специально без диалога подтверждения — это чисто "разгрести список",
    // а не удаление данных, лишний клик тут был бы просто помехой.
    QPushButton* deleteBtn = new QPushButton("✕", row);
    deleteBtn->setCursor(Qt::PointingHandCursor);
    deleteBtn->setFixedSize(22, 22);
    deleteBtn->setToolTip("Убрать из истории (файл не удаляется)");
    deleteBtn->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,0.05); color: #8b949e; border: 1px solid rgba(255,255,255,0.1); "
        "border-radius: 5px; font-size: 12px; padding: 0; }"
        "QPushButton:hover { background: rgba(255,95,95,0.18); color: #ff5f5f; border-color: rgba(255,95,95,0.35); }");
    {
        QFont f = deleteBtn->font();
        f.setFamily("Segoe UI Symbol");
        f.setPointSize(10);
        deleteBtn->setFont(f);
    }

    connect(deleteBtn, &QPushButton::clicked, this, [this, filePath, item]() {
        removeHistoryEntry(filePath);
        int idx = historyList->row(item);
        if (idx >= 0) {
            QListWidgetItem* taken = historyList->takeItem(idx);
            delete taken; // это тот же item — виджет row() уходит вместе с ним
        }
        });

    rowLayout->addWidget(icon);
    rowLayout->addWidget(label, 1);
    rowLayout->addWidget(readBtn);
    rowLayout->addWidget(deleteBtn);

    item->setSizeHint(row->sizeHint());
    // Вставляем в начало — самый свежий отчёт всегда сверху списка.
    historyList->insertItem(0, item);
    historyList->setItemWidget(item, row);

    while (historyList->count() > kMaxHistoryEntries) {
        QListWidgetItem* last = historyList->takeItem(historyList->count() - 1);
        delete last;
    }
}

void ResearchWidget::persistHistoryEntry(const QString& filePath, const QString& title) {
    QSettings settings;
    int size = settings.beginReadArray("research/history");
    QList<QVariantMap> entries;
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QVariantMap e;
        e["path"] = settings.value("path");
        e["title"] = settings.value("title");
        e["date"] = settings.value("date");
        entries.append(e);
    }
    settings.endArray();

    QVariantMap newEntry;
    newEntry["path"] = filePath;
    newEntry["title"] = title;
    newEntry["date"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    entries.prepend(newEntry);
    while (entries.size() > kMaxHistoryEntries) entries.removeLast();

    settings.beginWriteArray("research/history");
    for (int i = 0; i < entries.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("path", entries[i]["path"]);
        settings.setValue("title", entries[i]["title"]);
        settings.setValue("date", entries[i]["date"]);
    }
    settings.endArray();
}

void ResearchWidget::removeHistoryEntry(const QString& filePath) {
    QSettings settings;
    int size = settings.beginReadArray("research/history");
    QList<QVariantMap> entries;
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QVariantMap e;
        e["path"] = settings.value("path");
        e["title"] = settings.value("title");
        e["date"] = settings.value("date");
        entries.append(e);
    }
    settings.endArray();

    for (int i = entries.size() - 1; i >= 0; --i) {
        if (entries[i]["path"].toString() == filePath) {
            entries.removeAt(i);
        }
    }

    // Тот же приём "перезаписать массив целиком", что уже используется выше
    // при обрезке до kMaxHistoryEntries — новый массив короче старого, лишние
    // хвостовые ключи QSettings просто перестают читаться при следующем
    // beginReadArray, переписывать/чистить их отдельно не нужно.
    settings.beginWriteArray("research/history");
    for (int i = 0; i < entries.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("path", entries[i]["path"]);
        settings.setValue("title", entries[i]["title"]);
        settings.setValue("date", entries[i]["date"]);
    }
    settings.endArray();
}

void ResearchWidget::loadHistoryFromSettings() {
    QSettings settings;
    int size = settings.beginReadArray("research/history");
    // Записи хранятся от новых к старым; читаем и добавляем как есть —
    // addHistoryRow каждый раз вставляет в начало QListWidget, поэтому
    // читать нужно от САМОЙ СТАРОЙ к самой новой, чтобы после загрузки
    // порядок в списке остался "новые сверху".
    QList<QVariantMap> entries;
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QVariantMap e;
        e["path"] = settings.value("path");
        e["title"] = settings.value("title");
        e["date"] = settings.value("date");
        entries.append(e);
    }
    settings.endArray();

    for (int i = entries.size() - 1; i >= 0; --i) {
        QString path = entries[i]["path"].toString();
        QString title = entries[i]["title"].toString();
        QDateTime date = QDateTime::fromString(entries[i]["date"].toString(), Qt::ISODate);
        addHistoryRow(path, title, date.isValid() ? date.toString("dd.MM HH:mm") : QString());
    }
}