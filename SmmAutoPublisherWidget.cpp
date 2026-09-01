#include "SmmAutoPublisherWidget.h"
#include "PostQueueManager.h"
#include "PlatformToggleButton.h"
#include "QueueItemWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QPixmap>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QAbstractItemView>
#include <QFrame>
#include <QButtonGroup>
#include <QUrl>
#include <QSettings>

SmmAutoPublisherWidget::SmmAutoPublisherWidget(QWidget* parent) : QWidget(parent) {
    m_queueManager = new PostQueueManager(this);
    setupUi();

    connect(m_queueManager, &PostQueueManager::postAdded, this, &SmmAutoPublisherWidget::onPostAdded);
    connect(m_queueManager, &PostQueueManager::postRemoved, this, &SmmAutoPublisherWidget::onPostRemoved);
    connect(m_queueManager, &PostQueueManager::postStatusChanged, this, &SmmAutoPublisherWidget::onPostStatusChanged);

    // Отрисовываем то, что уже было в очереди на момент запуска (загружено
    // PostQueueManager из smm_queue.json в своём конструкторе) — сигнал
    // postAdded для них уже не прилетит, т.к. это не новые посты этой сессии.
    for (const auto& post : m_queueManager->posts()) {
        onPostAdded(post);
    }
}

void SmmAutoPublisherWidget::setupUi() {
    setStyleSheet(
        "QWidget { color: #e0e0e0; }"
        "QTextEdit { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.1); border-radius: 8px; padding: 6px; }"
        "QDateTimeEdit { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.1); border-radius: 6px; padding: 4px; }"
    );

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    // --- Заголовок ---
    auto* title = new QLabel("📅 SMM Авто-постинг", this);
    title->setStyleSheet("font-size: 14px; font-weight: bold;");
    root->addWidget(title);

    // --- Конструктор поста ---
    m_textEdit = new QTextEdit(this);
    m_textEdit->setPlaceholderText("Текст поста…");
    m_textEdit->setFixedHeight(100);
    connect(m_textEdit, &QTextEdit::textChanged, this, &SmmAutoPublisherWidget::onTextChanged);
    root->addWidget(m_textEdit);

    m_charCounter = new QLabel("0", this);
    m_charCounter->setAlignment(Qt::AlignRight);
    m_charCounter->setStyleSheet("color: #777; font-size: 10px;");
    root->addWidget(m_charCounter);

    // Медиа-блок: кнопка прикрепления + миниатюра (видна только если фото добавлено)
    auto* mediaRow = new QHBoxLayout();
    m_attachBtn = new QPushButton("📸 Добавить фото", this);
    m_attachBtn->setCursor(Qt::PointingHandCursor);
    m_attachBtn->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,0.06); border: 1px dashed rgba(255,255,255,0.2); border-radius: 6px; padding: 5px 8px; text-align: left; }"
        "QPushButton:hover { background: rgba(255,255,255,0.12); }"
    );
    connect(m_attachBtn, &QPushButton::clicked, this, &SmmAutoPublisherWidget::onAttachPhotoClicked);
    mediaRow->addWidget(m_attachBtn, 1);

    m_thumbnailLabel = new QLabel(this);
    m_thumbnailLabel->setFixedSize(32, 32);
    m_thumbnailLabel->setStyleSheet("border-radius: 4px; background: rgba(255,255,255,0.05);");
    m_thumbnailLabel->setScaledContents(true);
    m_thumbnailLabel->hide();
    mediaRow->addWidget(m_thumbnailLabel);
    root->addLayout(mediaRow);

    // --- Платформы ---
    auto* platformsLabel = new QLabel("Платформы:", this);
    platformsLabel->setStyleSheet("color: #999; font-size: 11px;");
    root->addWidget(platformsLabel);

    auto* platformsRow = new QHBoxLayout();
    platformsRow->setSpacing(6);
    const auto& registry = platformRegistry();
    for (auto it = registry.constBegin(); it != registry.constEnd(); ++it) {
        auto* btn = new PlatformToggleButton(it.key(), this);
        // Снимаем красную рамку предупреждения, как только пользователь
        // отметил хоть одну платформу — не заставляем ждать следующего
        // нажатия "Запланировать".
        connect(btn, &QPushButton::toggled, this, [this]() { setFieldWarning(m_platformsFrame, false); });
        m_platformButtons.append(btn);
        platformsRow->addWidget(btn);
    }
    platformsRow->addStretch();

    // Обёртка-QFrame вокруг ряда кнопок — сам QHBoxLayout ничего не рисует,
    // а нам нужен виджет, на котором можно включить/выключить рамку
    // предупреждения (см. setFieldWarning). Прозрачная рамка по умолчанию —
    // чтобы при появлении красной рамки соседние элементы не "прыгали" на
    // 2px из-за резервируемого под border места.
    m_platformsFrame = new QFrame(this);
    m_platformsFrame->setObjectName("smmPlatformsFrame");
    m_platformsFrame->setStyleSheet("QFrame#smmPlatformsFrame { border: 1px solid transparent; border-radius: 8px; }");
    m_platformsFrame->setLayout(platformsRow);
    root->addWidget(m_platformsFrame);

    // --- Цель публикации ---
    // "Своя страница" по умолчанию — поведение не меняется для тех, кто
    // просто игнорирует этот блок. QButtonGroup гарантирует, что отмечен
    // ровно один из двух радио-баттонов (сам Qt это не делает автоматически
    // для QRadioButton без общего родителя-QGroupBox/QButtonGroup).
    auto* targetLabel = new QLabel("Куда опубликовать:", this);
    targetLabel->setStyleSheet("color: #999; font-size: 11px;");
    root->addWidget(targetLabel);

    m_targetOwnRadio = new QRadioButton("Моя страница", this);
    m_targetGroupRadio = new QRadioButton("Группа / Страница / Канал", this);
    m_targetOwnRadio->setChecked(true);
    auto* targetGroup = new QButtonGroup(this);
    targetGroup->addButton(m_targetOwnRadio);
    targetGroup->addButton(m_targetGroupRadio);

    auto* targetRadioRow = new QHBoxLayout();
    targetRadioRow->addWidget(m_targetOwnRadio);
    targetRadioRow->addWidget(m_targetGroupRadio);
    targetRadioRow->addStretch();
    root->addLayout(targetRadioRow);

    m_targetUrlEdit = new QLineEdit(this);
    m_targetUrlEdit->setPlaceholderText("Ссылка на группу/страницу/канал/сабреддит…");
    m_targetUrlEdit->setEnabled(false);
    m_targetUrlEdit->setStyleSheet(
        "QLineEdit { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.1); border-radius: 6px; padding: 5px 8px; }"
        "QLineEdit:disabled { color: #555; }"
    );
    connect(m_targetOwnRadio, &QRadioButton::toggled, this, &SmmAutoPublisherWidget::onTargetModeChanged);
    connect(m_targetOwnRadio, &QRadioButton::toggled, this, [this]() { setFieldWarning(m_targetUrlEdit, false); });
    connect(m_targetUrlEdit, &QLineEdit::textChanged, this, [this]() { setFieldWarning(m_targetUrlEdit, false); });
    root->addWidget(m_targetUrlEdit);

    // --- Планирование ---
    auto* scheduleLabel = new QLabel("Когда опубликовать:", this);
    scheduleLabel->setStyleSheet("color: #999; font-size: 11px;");
    root->addWidget(scheduleLabel);

    m_dateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600), this);
    m_dateTimeEdit->setCalendarPopup(true); // сам попап компактный и открывается по требованию — не занимает место в блоке постоянно
    m_dateTimeEdit->setDisplayFormat("dd.MM.yyyy HH:mm");
    root->addWidget(m_dateTimeEdit);

    auto* presetsRow = new QHBoxLayout();
    auto* presetToday = new QPushButton("Сегодня 18:00", this);
    auto* presetTomorrow = new QPushButton("Завтра утром", this);
    for (auto* btn : { presetToday, presetTomorrow }) {
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.1); border-radius: 6px; padding: 4px 8px; font-size: 11px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.1); }"
        );
    }
    connect(presetToday, &QPushButton::clicked, this, &SmmAutoPublisherWidget::onPresetTodayEvening);
    connect(presetTomorrow, &QPushButton::clicked, this, &SmmAutoPublisherWidget::onPresetTomorrowMorning);
    presetsRow->addWidget(presetToday);
    presetsRow->addWidget(presetTomorrow);
    root->addLayout(presetsRow);

    // --- Кнопка отправки ---
    m_submitBtn = new QPushButton("🚀 Запланировать публикацию", this);
    m_submitBtn->setCursor(Qt::PointingHandCursor);
    m_submitBtn->setStyleSheet(
        "QPushButton { background: #5b9bd5; border: none; border-radius: 8px; padding: 8px; font-weight: bold; color: white; }"
        "QPushButton:hover { background: #6badea; }"
        "QPushButton:disabled { background: #3a3a3a; color: #777; }"
    );
    connect(m_submitBtn, &QPushButton::clicked, this, &SmmAutoPublisherWidget::onSubmitClicked);
    root->addWidget(m_submitBtn);

    // --- Очередь задач (сворачиваемая) ---
    auto* queueHeaderRow = new QHBoxLayout();
    auto* queueLabel = new QLabel("Очередь", this);
    queueLabel->setStyleSheet("color: #999; font-size: 11px; font-weight: bold;");
    m_queueToggleBtn = new QPushButton("▾", this);
    m_queueToggleBtn->setFixedSize(20, 20);
    m_queueToggleBtn->setCursor(Qt::PointingHandCursor);
    m_queueToggleBtn->setCheckable(true);
    m_queueToggleBtn->setChecked(true);
    m_queueToggleBtn->setStyleSheet("QPushButton { background: transparent; border: none; color: #999; }");
    queueHeaderRow->addWidget(queueLabel);
    queueHeaderRow->addStretch();
    queueHeaderRow->addWidget(m_queueToggleBtn);
    root->addLayout(queueHeaderRow);

    m_queueContainer = new QWidget(this);
    auto* queueContainerLayout = new QVBoxLayout(m_queueContainer);
    queueContainerLayout->setContentsMargins(0, 0, 0, 0);

    m_queueList = new QListWidget(m_queueContainer);
    m_queueList->setStyleSheet(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { border: none; padding: 2px 0; }"
    );
    m_queueList->setFrameShape(QFrame::NoFrame);
    m_queueList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_queueList->setMaximumHeight(220); // компактность важнее — длинная очередь скроллится, а не растягивает всю панель
    queueContainerLayout->addWidget(m_queueList);
    root->addWidget(m_queueContainer);

    connect(m_queueToggleBtn, &QPushButton::toggled, m_queueContainer, &QWidget::setVisible);
    connect(m_queueToggleBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_queueToggleBtn->setText(checked ? "▾" : "▸");
        });

    root->addStretch();
}

void SmmAutoPublisherWidget::onTextChanged() {
    int len = m_textEdit->toPlainText().length();
    m_charCounter->setText(QString("%1 / %2").arg(len).arg(kMaxChars));
    m_charCounter->setStyleSheet(len > kMaxChars ? "color: #ff5f5f; font-size: 10px;" : "color: #777; font-size: 10px;");
    if (len > 0) setFieldWarning(m_textEdit, false);
}

void SmmAutoPublisherWidget::onAttachPhotoClicked() {
    QString path = QFileDialog::getOpenFileName(this, "Выбрать фото", QString(), "Images (*.png *.jpg *.jpeg *.webp)");
    if (path.isEmpty()) return;

    m_pendingMedia.append(path);
    m_thumbnailLabel->setPixmap(QPixmap(path));
    m_thumbnailLabel->show();
}

void SmmAutoPublisherWidget::onPresetTodayEvening() {
    QDateTime dt = QDateTime::currentDateTime();
    dt.setTime(QTime(18, 0));
    if (dt < QDateTime::currentDateTime()) dt = dt.addDays(1); // если уже позже 18:00 — переносим на завтра, а не в прошлое
    m_dateTimeEdit->setDateTime(dt);
}

void SmmAutoPublisherWidget::onPresetTomorrowMorning() {
    QDateTime dt = QDateTime::currentDateTime().addDays(1);
    dt.setTime(QTime(9, 0));
    m_dateTimeEdit->setDateTime(dt);
}

void SmmAutoPublisherWidget::onTargetModeChanged() {
    m_targetUrlEdit->setEnabled(m_targetGroupRadio->isChecked());
}

void SmmAutoPublisherWidget::setFieldWarning(QWidget* field, bool warn) {
    // Три конкретных виджета — не универсальный механизм, потому что у
    // каждого своя "нормальная" стилизация, заданная напрямую на инстансе
    // (а не через общий QWidget-стиль в setupUi()), и её нужно восстановить
    // ТОЧНО такой же, а не сбросить в пустую строку.
    if (field == m_textEdit) {
        // m_textEdit стилизуется через тип-селектор "QTextEdit{...}",
        // унаследованный от this->setStyleSheet() в setupUi() — достаточно
        // переопределить только border на инстансе, остальное (фон,
        // скругление, отступы) продолжит наследоваться как обычно.
        m_textEdit->setStyleSheet(warn ? "border: 2px solid #ff5f5f;" : "");
    }
    else if (field == m_platformsFrame) {
        m_platformsFrame->setStyleSheet(warn
            ? "QFrame#smmPlatformsFrame { border: 1px solid #ff5f5f; border-radius: 8px; }"
            : "QFrame#smmPlatformsFrame { border: 1px solid transparent; border-radius: 8px; }");
    }
    else if (field == m_targetUrlEdit) {
        m_targetUrlEdit->setStyleSheet(warn
            ? "QLineEdit { background: rgba(255,255,255,0.05); border: 2px solid #ff5f5f; border-radius: 6px; padding: 5px 8px; } QLineEdit:disabled { color: #555; }"
            : "QLineEdit { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.1); border-radius: 6px; padding: 5px 8px; } QLineEdit:disabled { color: #555; }");
    }
}

bool SmmAutoPublisherWidget::isAiConfigured(QString& warningOut) const {
    // Тот же критерий, что AIAssistantWidget::checkAiConfigured() применяет
    // к обычному чату — без него пост уходил в очередь при любых настройках
    // ИИ, а публикация мгновенно проваливалась в SmmPublishController при
    // первом же сетевом запросе AiAgentTaskRunner (см. историю переписки).
    QSettings settings;
    int aiMode = settings.value("ai/mode", 0).toInt();
    bool useCustomApi = (aiMode == 0);
    bool useGigaChat = (aiMode == 2);

    if (useCustomApi && settings.value("ai/openrouter_key", "").toString().isEmpty()) {
        warningOut = "Не задан API-ключ OpenRouter — откройте Настройки → ⚙️ → 🤖 Настройки ИИ и сохраните ключ. Без него публикация начнётся и сразу же провалится.";
        return false;
    }
    if (useGigaChat && settings.value("ai/gigachat_key", "").toString().isEmpty()) {
        warningOut = "Не задан ключ авторизации GigaChat — откройте Настройки → ⚙️ → 🤖 Настройки ИИ и сохраните ключ. Без него публикация начнётся и сразу же провалится.";
        return false;
    }
    if (!useCustomApi && !useGigaChat
        && (settings.value("sync/username", "").toString().isEmpty() || settings.value("sync/password", "").toString().isEmpty())) {
        warningOut = "Вы не вошли в Личный кабинет Storm Cloud (нужен для встроенного ИИ) — войдите в Настройки → ⚙️ → 🤖 Настройки ИИ, либо выберите «Свой API» или GigaChat.";
        return false;
    }
    return true;
}

QSet<SocialPlatform> SmmAutoPublisherWidget::selectedPlatforms() const {
    QSet<SocialPlatform> result;
    for (auto* btn : m_platformButtons) {
        if (btn->isChecked()) result.insert(btn->platform());
    }
    return result;
}

void SmmAutoPublisherWidget::onSubmitClicked() {
    // Снимаем все рамки предупреждения перед новой проверкой — иначе если
    // пользователь один раз ошибся сразу в двух полях, а потом исправил
    // только одно, вторая (уже неактуальная) рамка осталась бы висеть.
    setFieldWarning(m_textEdit, false);
    setFieldWarning(m_platformsFrame, false);
    setFieldWarning(m_targetUrlEdit, false);

    // Проверка бэкенда ИИ — первой, до всех остальных полей: если он не
    // настроен, остальная валидация неважна, публикация всё равно не
    // сможет даже начать разговор с ИИ.
    QString aiWarning;
    if (!isAiConfigured(aiWarning)) {
        QMessageBox::warning(this, "SMM Авто-постинг", aiWarning);
        return;
    }

    QString text = m_textEdit->toPlainText().trimmed();
    QSet<SocialPlatform> platforms = selectedPlatforms();

    if (text.isEmpty() && m_pendingMedia.isEmpty()) {
        setFieldWarning(m_textEdit, true);
        QMessageBox::warning(this, "SMM Авто-постинг", "Добавьте текст или фото перед планированием.");
        return;
    }
    if (platforms.isEmpty()) {
        setFieldWarning(m_platformsFrame, true);
        QMessageBox::warning(this, "SMM Авто-постинг", "Выберите хотя бы одну платформу.");
        return;
    }

    QString targetUrl;
    if (m_targetGroupRadio->isChecked()) {
        QString raw = m_targetUrlEdit->text().trimmed();
        if (raw.isEmpty()) {
            setFieldWarning(m_targetUrlEdit, true);
            QMessageBox::warning(this, "SMM Авто-постинг",
                "Укажите ссылку на группу/страницу/канал или переключитесь на «Моя страница».");
            return;
        }
        // Пользователь мог вставить ссылку без схемы ("vk.com/group") — тот
        // же приём нормализации, что и в SettingsBridge::addShieldException,
        // чтобы QUrl(...) при открытии вкладки не воспринял строку как
        // относительный путь/локальный файл.
        targetUrl = raw.contains("://") ? raw : ("https://" + raw);
    }

    m_queueManager->addPost(text, m_pendingMedia, platforms, m_dateTimeEdit->dateTime(), targetUrl);
    resetComposer();
}

void SmmAutoPublisherWidget::resetComposer() {
    m_textEdit->clear();
    m_pendingMedia.clear();
    m_thumbnailLabel->hide();
    for (auto* btn : m_platformButtons) btn->setChecked(false);
    m_targetOwnRadio->setChecked(true);
    m_targetUrlEdit->clear();
    m_dateTimeEdit->setDateTime(QDateTime::currentDateTime().addSecs(3600));
}

void SmmAutoPublisherWidget::onCancelQueueItem(const QString& postId) {
    m_queueManager->cancelPost(postId);
}

QListWidgetItem* SmmAutoPublisherWidget::findQueueItem(const QString& postId) const {
    for (int i = 0; i < m_queueList->count(); ++i) {
        QListWidgetItem* item = m_queueList->item(i);
        auto* w = qobject_cast<QueueItemWidget*>(m_queueList->itemWidget(item));
        if (w && w->postId() == postId) return item;
    }
    return nullptr;
}

void SmmAutoPublisherWidget::onPostAdded(const ScheduledPost& post) {
    auto* itemWidget = new QueueItemWidget(post, m_queueList);
    connect(itemWidget, &QueueItemWidget::cancelRequested, this, &SmmAutoPublisherWidget::onCancelQueueItem);

    auto* item = new QListWidgetItem(m_queueList);
    item->setSizeHint(QSize(0, 34));
    m_queueList->addItem(item);
    m_queueList->setItemWidget(item, itemWidget);
}

void SmmAutoPublisherWidget::onPostRemoved(const QString& id) {
    if (QListWidgetItem* item = findQueueItem(id)) {
        delete m_queueList->takeItem(m_queueList->row(item));
    }
}

void SmmAutoPublisherWidget::onPostStatusChanged(const QString& id, PostStatus status) {
    if (QListWidgetItem* item = findQueueItem(id)) {
        auto* w = qobject_cast<QueueItemWidget*>(m_queueList->itemWidget(item));
        if (w) {
            // lastError сохраняется в PostQueueManager отдельно от самого
            // сигнала postStatusChanged (сигнал несёт только id+статус) —
            // достаём его через findPost, чтобы подсказка при наведении на
            // красную точку показывала настоящую причину, а не пустоту.
            const ScheduledPost* p = m_queueManager->findPost(id);
            w->updateStatus(status, p ? p->lastError : QString());
        }
    }
}