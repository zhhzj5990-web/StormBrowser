#pragma once
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QUuid>
#include <QSet>
#include <QMap>

// Идентификатор платформы. Список сознательно короткий и плоский (enum, а
// не строка) — под конкретную площадку почти всегда нужен свой сценарий
// публикации (структура формы отличается), так что "просто добавить строку"
// всё равно не даст платформу заработать без кода на стороне MainWindow.
enum class SocialPlatform {
    VK,
    Telegram,
    Instagram,
    OK, // Одноклассники
    X,        // ранее Twitter
    Facebook,
    Reddit
};

struct PlatformInfo {
    SocialPlatform id;
    QString displayName;
    QString icon;        // эмодзи-иконка для кнопки-переключателя
    QString accentColor;  // hex, подсветка активной кнопки
    QString composeUrl;   // страница, которую открываем в фоновой вкладке для публикации
    // Лимит шагов агента (AiAgentTaskRunner::MAX_STEPS) именно для этой
    // площадки — единого числа на все платформы недостаточно: у X
    // публикация — одна форма в одно действие, а у Reddit — выбор
    // сообщества, тип поста, заголовок, текст, отправка, объективно больше
    // шагов на честно выполнимый сценарий. Значения ниже — оценка по тому,
    // как устроен UI каждой площадки, а не измеренные данные; когда
    // накопится статистика по логу шагов (см. SmmPublishController), стоит
    // пересмотреть по факту.
    int maxAgentSteps;
};

// Единственное место, где перечислены поддерживаемые площадки. Добавление
// новой платформы = новая запись здесь + сценарий публикации на стороне
// MainWindow (см. заметку в PostQueueManager.h про publishDue).
//
// ВАЖНО: новые платформы всегда добавлять В КОНЕЦ enum SocialPlatform выше,
// никогда не переставлять/не вставлять в середину — PostQueueManager хранит
// платформы в smm_queue.json как целые числа (static_cast<int>), и смена
// порядка тихо "переименует" уже сохранённые у пользователей запланированные
// посты в другую платформу при следующем запуске.
inline const QMap<SocialPlatform, PlatformInfo>& platformRegistry() {
    static const QMap<SocialPlatform, PlatformInfo> registry = {
        { SocialPlatform::VK,       { SocialPlatform::VK,       "ВКонтакте",    "🟦", "#4C75A3", "https://vk.com/wall", 12 } },
        { SocialPlatform::Telegram, { SocialPlatform::Telegram, "Telegram",     "✈️", "#229ED9", "https://web.telegram.org", 8 } },
        // Instagram: у веб-версии посадка поста заметно многошаговее, чем у
        // остальных — выбор типа публикации, пропуск/обрезка кадра, подпись,
        // отдельный экран "Поделиться" — отсюда лимит заметно выше среднего.
        { SocialPlatform::Instagram,{ SocialPlatform::Instagram,"Instagram",    "📷", "#C13584", "https://www.instagram.com", 18 } },
        { SocialPlatform::OK,       { SocialPlatform::OK,       "Одноклассники","🟧", "#EE8208", "https://ok.ru", 12 } },
        // X: официальный "compose"-URL без ссылки на конкретный твит/цель —
        // открывает окно создания записи для текущего залогиненного аккаунта.
        // Один простой блок ввода — самый низкий лимит из всех площадок.
        { SocialPlatform::X,        { SocialPlatform::X,        "X",            "𝕏", "#1D9BF0", "https://x.com/compose/post", 8 } },
        // Facebook: у личного профиля нет стабильного публичного deep-link'а
        // на композер поста (в отличие от X/Reddit) — ведём на саму ленту,
        // агенту нужно будет найти поле "Что у вас нового?" самому, плюс
        // возможен отдельный шаг с выбором аудитории — лимит чуть выше среднего.
        { SocialPlatform::Facebook, { SocialPlatform::Facebook, "Facebook",     "📘", "#1877F2", "https://www.facebook.com/", 14 } },
        // Reddit: /submit без указания конкретного сабреддита откроет шаг
        // "выбери сообщество" — у Reddit в принципе нет понятия "своя стена",
        // публикация ВСЕГДА идёт в конкретное сообщество. Из всех площадок
        // именно для Reddit поле "цель публикации" (когда будет добавлено)
        // не опционально, а фактически обязательно. Самый длинный честный
        // сценарий (сообщество → тип поста → заголовок → текст → отправка) —
        // отсюда и самый высокий лимит.
        { SocialPlatform::Reddit,   { SocialPlatform::Reddit,   "Reddit",       "👽", "#FF4500", "https://www.reddit.com/submit", 18 } },
    };
    return registry;
}

enum class PostStatus {
    Scheduled,   // ждёт своего времени в очереди
    Publishing,  // таймер сработал, MainWindow сейчас пытается опубликовать
    Published,
    Failed,
    Cancelled
};

struct ScheduledPost {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString text;
    QStringList mediaPaths;
    QSet<SocialPlatform> platforms;
    QDateTime scheduledTime;
    PostStatus status = PostStatus::Scheduled;
    QString lastError; // причина последнего Failed, для UI/повторной попытки

    // Цель публикации: пусто — публикуем на СВОЮ страницу/профиль (обычный
    // composeUrl из platformRegistry). Непусто — прямая ссылка на группу/
    // сообщество/Страницу/канал/сабреддит; SmmPublishController откроет
    // фоновую вкладку сразу на этой ссылке вместо общего composeUrl.
    // Один targetUrl на весь пост (а не отдельный на каждую платформу) —
    // осознанное упрощение: пока публикация всё равно идёт только на первую
    // выбранную платформу поста (см. MVP-заметку в SmmPublishController.cpp),
    // отдельная цель на каждую платформу была бы преждевременной сложностью.
    QString targetUrl;
};