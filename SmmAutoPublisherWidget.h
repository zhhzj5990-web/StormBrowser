#pragma once
#include <QWidget>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QDateTimeEdit>
#include <QListWidget>
#include <QRadioButton>
#include <QLineEdit>
#include <QFrame>
#include "SmmTypes.h"

class PostQueueManager;
class PlatformToggleButton;

// Плагин-виджет боковой панели (добавляется в Sidebar через addItem(...),
// так же как PomodoroWidget/NotesWidget/NewsWidget/TodoWidget): конструктор
// поста + выбор платформ + планирование + очередь.
//
// Сам виджет НЕ публикует посты и не трогает вкладки браузера напрямую —
// только собирает данные пользователя и передаёт их PostQueueManager.
// Реальная публикация (открытие фоновой вкладки, работа со страницей)
// происходит в MainWindow, подписанном на PostQueueManager::publishDue —
// тот же принцип разделения ответственности, что уже используется у
// TodoWidget (requestOpenTabsForContext/requestOpenUrls сигналы, которые
// ловит MainWindow, а не сам TodoWidget).
class SmmAutoPublisherWidget : public QWidget {
    Q_OBJECT
public:
    explicit SmmAutoPublisherWidget(QWidget* parent = nullptr);
    ~SmmAutoPublisherWidget() override = default;

    // MainWindow подписывается на queueManager()->publishDue(...), чтобы
    // довести пост до реальной публикации (см. заметку в PostQueueManager.h).
    PostQueueManager* queueManager() const { return m_queueManager; }

private slots:
    void onTextChanged();
    void onAttachPhotoClicked();
    void onPresetTodayEvening();
    void onPresetTomorrowMorning();
    void onSubmitClicked();
    void onCancelQueueItem(const QString& postId);
    // Переключение "Моя страница" / "Группа-Страница-Канал" — включает или
    // блокирует поле ссылки, чтобы нельзя было ввести адрес и забыть
    // переключиться обратно на "свою страницу" (или наоборот).
    void onTargetModeChanged();

    // Реакция на изменения в PostQueueManager — держат QListWidget очереди
    // в синхронизации с фактическим состоянием (в т.ч. когда пост попадает
    // в очередь не через эту сессию UI, а был загружен с диска при старте).
    void onPostAdded(const ScheduledPost& post);
    void onPostRemoved(const QString& id);
    void onPostStatusChanged(const QString& id, PostStatus status);

private:
    void setupUi();
    void resetComposer();
    QSet<SocialPlatform> selectedPlatforms() const;
    QListWidgetItem* findQueueItem(const QString& postId) const;
    // Подсвечивает/снимает красную рамку у конкретного обязательного поля —
    // field должен быть m_textEdit, m_platformsFrame или m_targetUrlEdit,
    // для остальных виджетов ничего не делает (см. реализацию).
    void setFieldWarning(QWidget* field, bool warn);
    // Тот же критерий, что AIAssistantWidget::checkAiConfigured() использует
    // для чата — сюда специально скопирован, а не переиспользован напрямую
    // (та же причина, что и у AiAgentTaskRunner: не тянуть зависимость от
    // AIAssistantWidget ради одной проверки). Возвращает false и заполняет
    // warningOut понятным текстом, если бэкенд ИИ не настроен — без этой
    // проверки пост уходил в очередь, а публикация мгновенно проваливалась
    // в SmmPublishController при первом же сетевом запросе.
    bool isAiConfigured(QString& warningOut) const;

    PostQueueManager* m_queueManager;

    // --- Конструктор поста ---
    QTextEdit* m_textEdit;
    QLabel* m_charCounter;
    QPushButton* m_attachBtn;
    QLabel* m_thumbnailLabel;
    QStringList m_pendingMedia; // пути к прикреплённым фото текущего (ещё не отправленного) поста

    // --- Платформы ---
    QFrame* m_platformsFrame; // обёртка вокруг ряда кнопок — нужна, чтобы было на чём рисовать рамку предупреждения
    QList<PlatformToggleButton*> m_platformButtons;

    // --- Цель публикации ---
    // Своя страница по умолчанию (m_targetOwnRadio отмечен) — так поведение
    // не меняется для тех, кто просто игнорирует этот блок, как и раньше.
    QRadioButton* m_targetOwnRadio;
    QRadioButton* m_targetGroupRadio;
    QLineEdit* m_targetUrlEdit; // активно, только если выбран m_targetGroupRadio

    // --- Планирование ---
    QDateTimeEdit* m_dateTimeEdit;

    QPushButton* m_submitBtn;

    // --- Очередь ---
    QListWidget* m_queueList;
    QPushButton* m_queueToggleBtn; // сворачивает/разворачивает список очереди
    QWidget* m_queueContainer;

    // Условный лимит на длину поста в UI (счётчик символов); конкретные
    // ограничения площадок (VK/Telegram и т.д.) при необходимости проверяются
    // отдельно на этапе публикации, не здесь.
    static constexpr int kMaxChars = 2000;
};