#include "TodoWidget.h"
#include "TaskItemWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QStackedWidget>
#include <QDateTimeEdit>
#include <QListWidget>
#include <QAbstractItemView>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QStyle>
#include <QUrl>
#include <QResizeEvent>
#include <QScrollBar>

namespace {
    // QStackedWidget по умолчанию подгоняет свой размер под САМУЮ БОЛЬШУЮ из
    // добавленных страниц (не под текущую видимую) — это известная особенность
    // Qt. Из-за этого, когда выбрана короткая страница (например "Обычная
    // задача"), под ней всё равно остаётся зарезервированное пустое место
    // размером со страницу "Радар вакансий" (самую высокую). Переопределяем
    // sizeHint/minimumSizeHint, чтобы стек считал место строго под текущую
    // страницу.
    class AutoSizeStackedWidget : public QStackedWidget {
    public:
        using QStackedWidget::QStackedWidget;
        QSize sizeHint() const override {
            return currentWidget() ? currentWidget()->sizeHint() : QStackedWidget::sizeHint();
        }
        QSize minimumSizeHint() const override {
            return currentWidget() ? currentWidget()->minimumSizeHint() : QStackedWidget::minimumSizeHint();
        }
    };
}

TodoWidget::TodoWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* titleLbl = new QLabel(u8"<b>✅ Задачи</b>", this);
    layout->addWidget(titleLbl);

    taskList = new QListWidget(this);
    taskList->setSelectionMode(QAbstractItemView::NoSelection);
    taskList->setSpacing(2);
    // Карточки сами подгоняются под ширину списка (см. refreshItemWidths()),
    // поэтому горизонтальный скролл не нужен — он только обрезает кнопки.
    taskList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(taskList, 1); // растягиваем список на всё свободное место

    buildAddPanel(layout);

    filepath = getTasksFilePath();
    loadTasks();
    refreshItemWidths(); // подогнать ширину карточек под фактическую ширину панели сразу при старте

    m_netManager = new QNetworkAccessManager(this);

    // Проверка "Дня Х" — чисто локальная, никаких внешних запросов.
    m_antiSubTimer = new QTimer(this);
    connect(m_antiSubTimer, &QTimer::timeout, this, &TodoWidget::onAntiSubCheckTimer);
    m_antiSubTimer->start(15 * 60 * 1000); // раз в 15 минут
    onAntiSubCheckTimer(); // и сразу при старте, если панель открыли уже после наступления даты

    // Фоновый парсер радара вакансий.
    m_vacancyTimer = new QTimer(this);
    connect(m_vacancyTimer, &QTimer::timeout, this, &TodoWidget::onVacancyCheckTimer);
    m_vacancyTimer->start(30 * 60 * 1000); // раз в 30 минут
}

void TodoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    refreshItemWidths();
}

void TodoWidget::refreshItemWidths() {
    if (!taskList) return;
    // -2px "про запас": без этого при появлении/исчезновении вертикального
    // скролла на границе ширины может на мгновение мигать горизонтальный.
    const int width = taskList->viewport()->width() - 2;
    if (width <= 0) return;

    for (auto it = m_widgetsById.constBegin(); it != m_widgetsById.constEnd(); ++it) {
        TaskItemWidget* widget = it.value();
        QListWidgetItem* item = m_itemsById.value(it.key(), nullptr);
        if (!widget || !item) continue;
        widget->setFixedWidth(width);
        item->setSizeHint(widget->sizeHint());
    }
}

void TodoWidget::buildAddPanel(QVBoxLayout* rootLayout) {
    typeCombo = new QComboBox(this);
    typeCombo->addItem(u8"📝 Обычная задача", static_cast<int>(TodoTaskType::Simple));
    typeCombo->addItem(u8"🔗 Контекст (сохранить вкладки)", static_cast<int>(TodoTaskType::Context));
    typeCombo->addItem(u8"📖 Медиа (книга для Storm Reader)", static_cast<int>(TodoTaskType::Media));
    typeCombo->addItem(u8"⏰ Анти-подписка (День Х)", static_cast<int>(TodoTaskType::AntiSub));
    typeCombo->addItem(u8"🕵️ Радар вакансий", static_cast<int>(TodoTaskType::VacancyRadar));
    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TodoWidget::onTypeChanged);
    rootLayout->addWidget(typeCombo);

    extraStack = new AutoSizeStackedWidget(this);

    // 0: Simple — дополнительных полей не требуется.
    extraStack->addWidget(new QWidget(this));

    // 1: Context — создаётся сразу из открытых вкладок, нужна только подсказка.
    QWidget* contextPage = new QWidget(this);
    QVBoxLayout* contextLayout = new QVBoxLayout(contextPage);
    contextLayout->setContentsMargins(0, 0, 0, 0);
    contextLayout->addWidget(new QLabel(
        u8"При нажатии «Добавить» сохранятся все сейчас открытые вкладки.", contextPage));
    extraStack->addWidget(contextPage);

    // 2: Media
    QWidget* mediaPage = new QWidget(this);
    QHBoxLayout* mediaLayout = new QHBoxLayout(mediaPage);
    mediaLayout->setContentsMargins(0, 0, 0, 0);
    QPushButton* pickFileBtn = new QPushButton(u8"Выбрать файл из «Книг»...", mediaPage);
    connect(pickFileBtn, &QPushButton::clicked, this, &TodoWidget::onPickMediaFile);
    mediaFileLabel = new QLabel(u8"Файл не выбран", mediaPage);
    mediaFileLabel->setWordWrap(true);
    mediaLayout->addWidget(pickFileBtn);
    mediaLayout->addWidget(mediaFileLabel, 1);
    extraStack->addWidget(mediaPage);

    // 3: AntiSub
    QWidget* antiSubPage = new QWidget(this);
    QHBoxLayout* antiSubLayout = new QHBoxLayout(antiSubPage);
    antiSubLayout->setContentsMargins(0, 0, 0, 0);
    antiSubLayout->addWidget(new QLabel(u8"День Х:", antiSubPage));
    dayXEdit = new QDateTimeEdit(QDateTime::currentDateTime().addDays(7), antiSubPage);
    dayXEdit->setCalendarPopup(true);
    dayXEdit->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
    // По какой-то причине глобальная тёмная тема браузера не подхватывает
    // QDateTimeEdit (в отличие от соседних QComboBox/QLineEdit) — без явного
    // стиля поле рисуется с белым фоном, и текст на нём почти не виден, пока
    // его не выделишь. Задаём тёмный стиль прямо здесь же, включая всплывающий
    // календарь (у него та же беда) — цвета взяты из уже существующей палитры
    // тёмной темы (см. #stormFindBar в MainWindow_Navigation.cpp).
    dayXEdit->setStyleSheet(QStringLiteral(
        "QDateTimeEdit {"
        "  background-color: #2b2f36; color: #e8e8e8;"
        "  border: 1px solid #444b56; border-radius: 4px; padding: 3px 6px;"
        "}"
        "QDateTimeEdit::drop-down {"
        "  subcontrol-origin: padding; subcontrol-position: top right;"
        "  width: 22px; border-left: 1px solid #444b56;"
        "  background-color: #3a4048;"
        "  border-top-right-radius: 4px; border-bottom-right-radius: 4px;"
        "}"
        "QDateTimeEdit::drop-down:hover { background-color: #454c56; }"
        "QDateTimeEdit::down-arrow {"
        "  image: none; width: 0; height: 0;"
        "  border-left: 4px solid transparent; border-right: 4px solid transparent;"
        "  border-top: 5px solid #e8e8e8;"
        "}"
        "QDateTimeEdit QAbstractItemView { background-color: #2b2f36; color: #e8e8e8; }"
        "QCalendarWidget QWidget { background-color: #2b2f36; color: #e8e8e8; }"
        "QCalendarWidget QToolButton {"
        "  background-color: #2b2f36; color: #e8e8e8; icon-size: 18px; border-radius: 4px;"
        "}"
        "QCalendarWidget QToolButton:hover { background-color: rgba(255, 255, 255, 0.08); }"
        "QCalendarWidget QMenu { background-color: #2b2f36; color: #e8e8e8; }"
        "QCalendarWidget QSpinBox {"
        "  background-color: #2b2f36; color: #e8e8e8; border: 1px solid #444b56;"
        "}"
        "QCalendarWidget QAbstractItemView:enabled {"
        "  background-color: #2b2f36; color: #e8e8e8; selection-background-color: #58a6ff;"
        "  selection-color: #0d1117;"
        "}"
        "QCalendarWidget QAbstractItemView:disabled { color: #5a5f66; }"
    ));
    antiSubLayout->addWidget(dayXEdit, 1);
    extraStack->addWidget(antiSubPage);

    // 4: VacancyRadar
    QWidget* vacancyPage = new QWidget(this);
    QVBoxLayout* vacancyLayout = new QVBoxLayout(vacancyPage);
    vacancyLayout->setContentsMargins(0, 0, 0, 0);

    vacancyPromptInput = new QLineEdit(vacancyPage);
    vacancyPromptInput->setPlaceholderText(u8"Что ищем? Например: python developer remote");
    vacancyLayout->addWidget(vacancyPromptInput);

    QHBoxLayout* urlRow = new QHBoxLayout();
    vacancyUrlInput = new QLineEdit(vacancyPage);
    vacancyUrlInput->setPlaceholderText(u8"URL страницы с вакансиями");
    QPushButton* addUrlBtn = new QPushButton(u8"Добавить ссылку", vacancyPage);
    connect(addUrlBtn, &QPushButton::clicked, this, &TodoWidget::onAddVacancyUrl);
    connect(vacancyUrlInput, &QLineEdit::returnPressed, this, &TodoWidget::onAddVacancyUrl);
    urlRow->addWidget(vacancyUrlInput, 1);
    urlRow->addWidget(addUrlBtn);
    vacancyLayout->addLayout(urlRow);

    vacancyUrlsList = new QListWidget(vacancyPage);
    vacancyUrlsList->setMaximumHeight(56);
    vacancyLayout->addWidget(vacancyUrlsList);

    vacancyLimitLabel = new QLabel(vacancyPage);
    vacancyLimitLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    vacancyLayout->addWidget(vacancyLimitLabel);

    extraStack->addWidget(vacancyPage);

    rootLayout->addWidget(extraStack);
    extraStack->setCurrentIndex(0);
    updateVacancyLimitLabel();

    QHBoxLayout* inputLayout = new QHBoxLayout();
    taskInput = new QLineEdit(this);
    taskInput->setPlaceholderText(u8"Название задачи...");
    connect(taskInput, &QLineEdit::returnPressed, this, &TodoWidget::addTask);

    QPushButton* addBtn = new QPushButton(u8"Добавить", this);
    connect(addBtn, &QPushButton::clicked, this, &TodoWidget::addTask);

    inputLayout->addWidget(taskInput);
    inputLayout->addWidget(addBtn);
    rootLayout->addLayout(inputLayout);
}

void TodoWidget::onTypeChanged(int index) {
    Q_UNUSED(index);
    // Страницы extraStack добавлялись в том же порядке, что и пункты typeCombo.
    extraStack->setCurrentIndex(typeCombo->currentIndex());
    // AutoSizeStackedWidget меняет свой sizeHint при смене страницы — но сам
    // родительский layout об этом не узнает без явного updateGeometry(),
    // иначе высота стека не пересчитается, пока окно не тронут руками.
    extraStack->updateGeometry();
}

void TodoWidget::onPickMediaFile() {
    const QString startDir = getBooksFolderPath();
    const QString path = QFileDialog::getOpenFileName(
        this, u8"Выберите файл книги", startDir,
        u8"Книги (*.pdf *.epub *.fb2 *.txt);;Все файлы (*.*)");

    if (!path.isEmpty()) {
        pendingMediaFilePath = path;
        mediaFileLabel->setText(QFileInfo(path).fileName());
    }
}

void TodoWidget::onAddVacancyUrl() {
    const QString url = vacancyUrlInput->text().trimmed();
    if (url.isEmpty()) return;

    if (vacancyUrlsList->count() >= vacancyUrlLimit()) {
        vacancyLimitLabel->setText(QString(u8"⚠ Достигнут лимит ссылок (%1) для вашего тарифа.")
            .arg(vacancyUrlLimit()));
        return;
    }

    vacancyUrlsList->addItem(url);
    vacancyUrlInput->clear();
    updateVacancyLimitLabel();
}

void TodoWidget::updateVacancyLimitLabel() {
    if (!vacancyLimitLabel) return;
    const int limit = vacancyUrlLimit();
    const int used = vacancyUrlsList ? vacancyUrlsList->count() : 0;
    vacancyLimitLabel->setText(QString(u8"Ссылки: %1 / %2 (%3)")
        .arg(used)
        .arg(limit)
        .arg(isProAccount() ? u8"Pro" : u8"Free"));
}

bool TodoWidget::isProAccount() const {
    // billing/is_premium кэшируется в StormCloudBridge::fetchBillingInfo() и
    // StormCloud::onBillingInfoReply() при каждом ответе /api/billing/info —
    // обновляется сразу после логина/регистрации и при открытии Storm Cloud
    // или кошелька. Если пользователь никогда не логинился (или давно не
    // обновлял статус), ключа не будет — считаем это как Free, безопасный
    // дефолт. Живой сетевой запрос отсюда намеренно не делаем: это блокировало
    // бы UI при простом вводе задачи.
    QSettings settings;
    return settings.value(QStringLiteral("billing/is_premium"), false).toBool();
}

int TodoWidget::vacancyUrlLimit() const {
    return isProAccount() ? 5 : 1;
}

void TodoWidget::addTask() {
    const TodoTaskType type = static_cast<TodoTaskType>(typeCombo->currentData().toInt());
    const QString title = taskInput->text().trimmed();

    switch (type) {
    case TodoTaskType::Context: {
        pendingContextTitle = title.isEmpty()
            ? QString(u8"Сессия от %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yyyy HH:mm")))
            : title;
        emit requestOpenTabsForContext();
        // Задача будет создана в receiveOpenTabsForContext(), когда владелец пришлёт список URL.
        break;
    }
    case TodoTaskType::Media: {
        if (pendingMediaFilePath.isEmpty()) {
            mediaFileLabel->setText(u8"⚠ Сначала выберите файл");
            return;
        }
        TodoTask task;
        task.id = TodoTask::newId();
        task.type = TodoTaskType::Media;
        task.text = title.isEmpty() ? QFileInfo(pendingMediaFilePath).fileName() : title;
        task.mediaFilePath = pendingMediaFilePath;

        addTaskToList(task);
        m_tasks.append(task);
        saveTasks();

        pendingMediaFilePath.clear();
        mediaFileLabel->setText(u8"Файл не выбран");
        taskInput->clear();
        break;
    }
    case TodoTaskType::AntiSub: {
        TodoTask task;
        task.id = TodoTask::newId();
        task.type = TodoTaskType::AntiSub;
        task.text = title.isEmpty() ? u8"Отменить подписку" : title;
        task.dayX = dayXEdit->dateTime().date();

        addTaskToList(task);
        m_tasks.append(task);
        saveTasks();
        taskInput->clear();
        break;
    }
    case TodoTaskType::VacancyRadar: {
        const QString prompt = vacancyPromptInput->text().trimmed();
        QStringList urls;
        for (int i = 0; i < vacancyUrlsList->count(); ++i) urls << vacancyUrlsList->item(i)->text();

        if (prompt.isEmpty() || urls.isEmpty()) {
            vacancyLimitLabel->setText(u8"⚠ Укажите ключевые слова и хотя бы одну ссылку");
            return;
        }

        TodoTask task;
        task.id = TodoTask::newId();
        task.type = TodoTaskType::VacancyRadar;
        task.text = title.isEmpty() ? prompt : title;
        task.vacancyPrompt = prompt;
        task.vacancyUrls = urls;

        addTaskToList(task);
        m_tasks.append(task);
        saveTasks();

        vacancyPromptInput->clear();
        vacancyUrlsList->clear();
        updateVacancyLimitLabel();
        taskInput->clear();

        checkVacancyRadar(task.id); // первая проверка сразу после добавления
        break;
    }
    case TodoTaskType::Simple:
    default: {
        if (title.isEmpty()) return;
        TodoTask task;
        task.id = TodoTask::newId();
        task.type = TodoTaskType::Simple;
        task.text = title;

        addTaskToList(task);
        m_tasks.append(task);
        saveTasks();
        taskInput->clear();
        break;
    }
    }
}

void TodoWidget::receiveOpenTabsForContext(const QStringList& urls) {
    if (pendingContextTitle.isEmpty()) return; // мы этого не запрашивали
    createContextTask(pendingContextTitle, urls);
    pendingContextTitle.clear();
    taskInput->clear();
}

void TodoWidget::createContextTask(const QString& title, const QStringList& urls) {
    TodoTask task;
    task.id = TodoTask::newId();
    task.type = TodoTaskType::Context;
    task.text = title;
    task.contextUrls = urls;

    addTaskToList(task);
    m_tasks.append(task);
    saveTasks();
}

void TodoWidget::addTaskToList(const TodoTask& task) {
    QListWidgetItem* item = new QListWidgetItem(taskList);
    TaskItemWidget* widget = new TaskItemWidget(task, taskList);

    connect(widget, &TaskItemWidget::toggled, this, &TodoWidget::onTaskToggled);
    connect(widget, &TaskItemWidget::deleteRequested, this, &TodoWidget::onTaskDeleteRequested);
    connect(widget, &TaskItemWidget::openUrlsRequested, this, &TodoWidget::requestOpenUrls);
    connect(widget, &TaskItemWidget::openInReaderRequested, this, &TodoWidget::requestOpenInReader);
    connect(widget, &TaskItemWidget::forceVacancyCheckRequested, this, &TodoWidget::checkVacancyRadar);

    item->setSizeHint(widget->sizeHint());
    taskList->setItemWidget(item, widget);

    m_itemsById.insert(task.id, item);
    m_widgetsById.insert(task.id, widget);

    // Сразу подгоняем ширину новой карточки под текущую ширину списка —
    // иначе на один кадр она отрисуется с "естественной" (более широкой)
    // шириной и вызовет тот самый горизонтальный скролл с обрезанными кнопками.
    refreshItemWidths();
}

void TodoWidget::removeTaskById(const QString& id) {
    if (QListWidgetItem* item = m_itemsById.value(id, nullptr)) {
        if (TaskItemWidget* w = m_widgetsById.value(id, nullptr)) {
            taskList->removeItemWidget(item);
            w->deleteLater();
        }
        delete taskList->takeItem(taskList->row(item));
    }
    m_itemsById.remove(id);
    m_widgetsById.remove(id);

    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].id == id) {
            m_tasks.remove(i);
            break;
        }
    }
    saveTasks();
}

TodoTask* TodoWidget::findTaskById(const QString& id) {
    for (auto& t : m_tasks) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

TaskItemWidget* TodoWidget::findWidgetById(const QString& id) {
    return m_widgetsById.value(id, nullptr);
}

void TodoWidget::onTaskToggled(const QString& id, bool checked) {
    if (TodoTask* task = findTaskById(id)) {
        task->done = checked;
        saveTasks();
    }
}

void TodoWidget::onTaskDeleteRequested(const QString& id) {
    removeTaskById(id);
}

void TodoWidget::checkVacancyRadar(const QString& taskId) {
    TodoTask* task = findTaskById(taskId);
    if (!task || task->vacancyUrls.isEmpty()) return;

    // QPointer, чтобы безопасно обращаться к this из асинхронных колбэков сети,
    // даже если виджет к моменту ответа уже будет уничтожен.
    QPointer<TodoWidget> self(this);
    const QString prompt = task->vacancyPrompt;

    for (const QString& url : task->vacancyUrls) {
        QNetworkRequest request{ QUrl(url) };
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("StormBrowser/1.0 (VacancyRadar)"));
        QNetworkReply* reply = m_netManager->get(request);

        connect(reply, &QNetworkReply::finished, this, [self, reply, taskId, prompt]() {
            reply->deleteLater();
            if (!self) return;
            if (reply->error() != QNetworkReply::NoError) return;

            QString pageText = QString::fromUtf8(reply->readAll());
            pageText.remove(QRegularExpression(QStringLiteral("<[^>]*>"))); // грубая очистка от HTML-тегов

            bool matched = false;
            const QStringList keywords = prompt.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QString& kw : keywords) {
                if (pageText.contains(kw, Qt::CaseInsensitive)) {
                    matched = true;
                    break;
                }
            }

            TodoTask* liveTask = self->findTaskById(taskId);
            if (!liveTask) return;

            liveTask->vacancyLastCheck = QDateTime::currentDateTime();
            if (matched) {
                liveTask->vacancyLastResult = QString(u8"🟢 Совпадение найдено (%1)")
                    .arg(liveTask->vacancyLastCheck.toString(QStringLiteral("dd.MM HH:mm")));
            }
            else if (liveTask->vacancyLastResult.isEmpty()) {
                liveTask->vacancyLastResult = QString(u8"Проверено %1, совпадений пока нет")
                    .arg(liveTask->vacancyLastCheck.toString(QStringLiteral("dd.MM HH:mm")));
            }

            if (TaskItemWidget* w = self->findWidgetById(taskId)) w->refresh(*liveTask);
            self->saveTasks();
            });
    }
}

void TodoWidget::onVacancyCheckTimer() {
    for (const TodoTask& t : m_tasks) {
        if (t.type == TodoTaskType::VacancyRadar) checkVacancyRadar(t.id);
    }
}

void TodoWidget::onAntiSubCheckTimer() {
    bool anyChanged = false;

    for (TodoTask& t : m_tasks) {
        if (t.type != TodoTaskType::AntiSub || t.notified || !t.dayX.isValid()) continue;
        if (QDate::currentDate() < t.dayX) continue;

        t.notified = true;
        anyChanged = true;

        if (TaskItemWidget* w = findWidgetById(t.id)) w->refresh(t);

        ensureTrayIcon();
        if (m_trayIcon) {
            m_trayIcon->showMessage(
                u8"Storm Browser — Анти-подписка",
                QString(u8"Наступил День Х для задачи: %1").arg(t.text),
                QSystemTrayIcon::Warning, 10000);
        }
    }

    if (anyChanged) saveTasks();
}

void TodoWidget::ensureTrayIcon() {
    if (m_trayIcon || !QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
    m_trayIcon->show();
}

QString TodoWidget::getTasksFilePath() {
    // Используем AppData, чтобы файлы не терялись.
    QString appData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QDir dir(appData + QStringLiteral("/StormBrowser/plugins/todo"));
    if (!dir.exists()) dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("tasks_save.json"));
}

QString TodoWidget::getBooksFolderPath() {
    // Примечание: путь должен совпадать с тем, что использует Storm Reader
    // для хранения книг. Поправьте при необходимости под реальное расположение.
    QString appData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QDir dir(appData + QStringLiteral("/StormBrowser/books"));
    if (!dir.exists()) dir.mkpath(QStringLiteral("."));
    return dir.absolutePath();
}

void TodoWidget::loadTasks() {
    QFile file(filepath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    const QByteArray data = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray array;

    if (doc.isObject()) {
        // Новый формат: { "version": 2, "tasks": [...] }
        array = doc.object().value(QStringLiteral("tasks")).toArray();
    }
    else if (doc.isArray()) {
        // Старый формат: плоский массив строк (или уже объектов, на всякий случай).
        array = doc.array();
    }

    for (const QJsonValue& v : array) {
        const TodoTask task = v.isString() ? TodoTask::fromLegacyString(v.toString())
            : TodoTask::fromJson(v.toObject());
        m_tasks.append(task);
        addTaskToList(task);
    }
}

void TodoWidget::saveTasks() {
    QJsonArray array;
    for (const TodoTask& t : m_tasks) array.append(t.toJson());

    QJsonObject root;
    root["version"] = 2;
    root["tasks"] = array;

    QJsonDocument doc(root);
    QFile file(filepath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(doc.toJson());
        file.close();
    }
}