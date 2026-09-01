#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QWidget>

class PasswordManager : public QObject {
    Q_OBJECT
public:
    explicit PasswordManager(QObject* parent = nullptr);

    void showManagerDialog(QWidget* parentWidget);

    // Сохраняет пароль. Если найдена запись с ТЕМ ЖЕ доменом и ТЕМ ЖЕ логином —
    // обновляет её. Если логин другой (второй аккаунт на том же сайте) —
    // создаёт отдельную новую запись, не затирая существующую.
    void savePassword(const QString& domain, const QString& login, const QString& password);

    // Удаление ОДНОЙ записи по уникальному id (не по домену — на одном домене
    // может быть несколько записей с разными логинами).
    void removePassword(const QString& id);

    // Проверка: есть ли УЖЕ ТОЧНО ТАКАЯ ЖЕ пара домен+логин+пароль?
    // Нужно, чтобы не спрашивать "Сохранить пароль?" повторно для того,
    // что уже сохранено — в этом случае просто ничего не делаем молча.
    bool hasExactMatch(const QString& domain, const QString& login, const QString& password);

    // Обновляет метку "последний раз использован" для точной записи
    // домен+логин, ничего не создавая и не спрашивая. Вызывается когда
    // hasExactMatch()==true — чтобы автозаполнение при нескольких аккаунтах
    // на одном сайте предлагало ИМЕННО тот, которым пользовались последним.
    void touchLastUsed(const QString& domain, const QString& login);

    // Для автозаполнения: если для домена ровно одна запись — вернёт её;
    // если несколько (разные аккаунты на одном сайте) — вернёт ту, что
    // использовалась последней. Пустой QJsonObject, если совпадений нет.
    QJsonObject getBestMatchForAutofill(const QString& domain);

    QString generatePassword();

    // Для облачной синхронизации: все пароли в расшифрованном виде
    // [{"site_url":.., "login":.., "password":..}, ...]
    QJsonArray getAllDecrypted();
    int getPasswordCount();

    void changeMasterPassword(QWidget* parentWidget);
    void resetVault(QWidget* parentWidget);
    void importPasswordsCsv(QWidget* parentWidget);

private:
    QString vaultPath;
    QString masterHash;

    void loadMasterHash();
    bool checkMasterPassword(QWidget* parentWidget);
    bool promptNewMasterPassword(QWidget* parentWidget);

    // loadVault() автоматически мигрирует старый формат хранилища
    // (passwords как объект, ключ = домен) в новый (passwords как массив
    // записей с id) при первой же загрузке, если обнаруживает старый формат.
    QJsonObject loadVault();
    void saveVault(const QJsonObject& vault);

    // Встроенное шифрование, чтобы не требовать тяжелую библиотеку OpenSSL
    QString encryptData(const QString& data, const QString& key);
    QString decryptData(const QString& data, const QString& key);
    QString hashPassword(const QString& password);
};