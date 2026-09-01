#include "TaskItemWidget.h"
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSizePolicy>
#include <QFont>

namespace {
    QString iconForType(TodoTaskType type) {
        switch (type) {
        case TodoTaskType::Context:      return u8"🔗";
        case TodoTaskType::Media:        return u8"📖";
        case TodoTaskType::AntiSub:      return u8"⏰";
        case TodoTaskType::VacancyRadar: return u8"🕵️";
        case TodoTaskType::Simple:
        default:                         return u8"📝";
        }
    }
}

TaskItemWidget::TaskItemWidget(const TodoTask& task, QWidget* parent)
    : QWidget(parent), m_task(task) {
    buildUi();
    applyTaskState();
}

void TaskItemWidget::buildUi() {
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 6);
    root->setSpacing(2);

    // --- Верхняя строка: чекбокс + иконка + заголовок (тянется на всю ширину, переносится) ---
    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setSpacing(6);

    m_doneCheck = new QCheckBox(this);
    connect(m_doneCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_task.done = checked;
        applyTaskState();
        emit toggled(m_task.id, checked);
        });
    headerRow->addWidget(m_doneCheck);

    m_iconLabel = new QLabel(iconForType(m_task.type), this);
    headerRow->addWidget(m_iconLabel);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerRow->addWidget(m_titleLabel, 1);

    root->addLayout(headerRow);

    // --- Строка метаданных (путь к файлу, счётчик вкладок, дата и т.д.) ---
    m_metaLabel = new QLabel(this);
    m_metaLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    m_metaLabel->setWordWrap(true);
    m_metaLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    root->addWidget(m_metaLabel);

    // --- Нижняя строка: компактные кнопки действий, прижаты вправо ---
    const QString compactBtnStyle = QStringLiteral(
        "QPushButton { padding: 2px 10px; font-size: 11px; border-radius: 4px; }");

    QHBoxLayout* actionsRow = new QHBoxLayout();
    actionsRow->setSpacing(4);
    actionsRow->addStretch(1);

    // Кнопка действия зависит от типа задачи.
    switch (m_task.type) {
    case TodoTaskType::Context:
        m_actionBtn = new QPushButton(u8"🔗 Открыть", this);
        connect(m_actionBtn, &QPushButton::clicked, this, [this]() {
            emit openUrlsRequested(m_task.contextUrls);
            });
        break;
    case TodoTaskType::Media:
        m_actionBtn = new QPushButton(u8"📖 Открыть", this);
        connect(m_actionBtn, &QPushButton::clicked, this, [this]() {
            emit openInReaderRequested(m_task.mediaFilePath);
            });
        break;
    case TodoTaskType::VacancyRadar:
        m_actionBtn = new QPushButton(u8"🔄 Проверить", this);
        connect(m_actionBtn, &QPushButton::clicked, this, [this]() {
            emit forceVacancyCheckRequested(m_task.id);
            });
        break;
    case TodoTaskType::AntiSub:
    case TodoTaskType::Simple:
    default:
        break;
    }
    if (m_actionBtn) {
        m_actionBtn->setStyleSheet(compactBtnStyle);
        actionsRow->addWidget(m_actionBtn);
    }

    m_deleteBtn = new QPushButton(u8"🗑", this);
    m_deleteBtn->setFlat(true);
    m_deleteBtn->setFixedWidth(26);
    m_deleteBtn->setStyleSheet(QStringLiteral("QPushButton { padding: 2px; font-size: 12px; }"));
    m_deleteBtn->setToolTip(u8"Удалить задачу");
    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
        emit deleteRequested(m_task.id);
        });
    actionsRow->addWidget(m_deleteBtn);

    root->addLayout(actionsRow);
}

void TaskItemWidget::refresh(const TodoTask& task) {
    m_task = task;
    applyTaskState();
}

void TaskItemWidget::applyTaskState() {
    m_doneCheck->blockSignals(true);
    m_doneCheck->setChecked(m_task.done);
    m_doneCheck->blockSignals(false);

    QFont f = m_titleLabel->font();
    f.setStrikeOut(m_task.done);
    m_titleLabel->setFont(f);
    m_titleLabel->setText(m_task.text.isEmpty() ? u8"(без названия)" : m_task.text);

    QString meta;
    QString highlightStyle;

    switch (m_task.type) {
    case TodoTaskType::Context:
        meta = QString(u8"Вкладок сохранено: %1").arg(m_task.contextUrls.size());
        break;
    case TodoTaskType::Media:
        meta = m_task.mediaFilePath.isEmpty() ? u8"Файл не выбран" : m_task.mediaFilePath;
        break;
    case TodoTaskType::AntiSub:
        if (m_task.dayX.isValid()) {
            const qint64 daysLeft = QDate::currentDate().daysTo(m_task.dayX);
            if (daysLeft > 0) {
                meta = QString(u8"День Х: %1 (осталось %2 дн.)")
                    .arg(m_task.dayX.toString(QStringLiteral("dd.MM.yyyy")))
                    .arg(daysLeft);
            }
            else {
                meta = QString(u8"День Х наступил: %1")
                    .arg(m_task.dayX.toString(QStringLiteral("dd.MM.yyyy")));
                highlightStyle = QStringLiteral("background-color: #4d1f1f;");
            }
        }
        else {
            meta = u8"Дата не задана";
        }
        break;
    case TodoTaskType::VacancyRadar:
        meta = m_task.vacancyLastResult.isEmpty()
            ? QString(u8"Ожидание проверки… (%1 ссылок)").arg(m_task.vacancyUrls.size())
            : m_task.vacancyLastResult;
        break;
    case TodoTaskType::Simple:
    default:
        break;
    }

    m_metaLabel->setText(meta);
    setStyleSheet(highlightStyle.isEmpty()
        ? QString()
        : QString(QStringLiteral("TaskItemWidget { %1 border-radius: 4px; }")).arg(highlightStyle));
}