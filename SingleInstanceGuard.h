#pragma once
#include <QObject>
#include <QSharedMemory>

class QLocalServer;

// Гарантирует, что в системе одновременно работает только ОДИН процесс
// Storm Browser. Раньше такой защиты не было вовсе: QLockFile в UpdateManager
// защищает только общий временный файл обновления в %TEMP%, но два
// независимых процесса всё равно означают два отдельных окна, два трея,
// два профиля WebEngine и двойной расход ресурсов — этим занимается уже
// именно этот класс.
//
// Схема стандартная для Qt: QSharedMemory используется как атомарный
// "замок" (create() может успешно выполнить только один процесс), а
// QLocalServer/QLocalSocket — как канал, чтобы второй запуск мог "разбудить"
// (поднять окно наверх) уже работающий первый экземпляр вместо того, чтобы
// просто молча закрыться.
class SingleInstanceGuard : public QObject {
    Q_OBJECT
public:
    // key должен быть уникальным для приложения (например, включать имя
    // и версию продукта), чтобы случайно не столкнуться с другим ПО.
    explicit SingleInstanceGuard(const QString& key, QObject* parent = nullptr);
    ~SingleInstanceGuard() override;

    // true — это первый (и единственный) запущенный процесс. false — где-то
    // уже работает другой процесс Storm Browser; этому экземпляру следует
    // немедленно завершиться (уже работающий экземпляр мы успели "разбудить"
    // прямо в конструкторе).
    bool isPrimaryInstance() const { return isPrimary; }

signals:
    // Испускается в ПЕРВОМ (главном) экземпляре, когда кто-то попытался
    // запустить второй процесс параллельно — сюда вешаем "поднять окно
    // наверх, снять минимизацию".
    void anotherInstanceStarted();

private slots:
    void handleIncomingConnection();

private:
    QString sharedMemKey;
    QSharedMemory sharedMemory;
    QLocalServer* localServer = nullptr;
    bool isPrimary = false;
};
