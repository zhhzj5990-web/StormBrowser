#pragma once
#include <QObject>
#include <QString>

class MainWindow;

// Мост в ИЗОЛИРОВАННОМ мире (QWebEngineScript::ApplicationWorld).
// Обычный JS сайта не имеет доступа к объектам этого канала — только
// наш собственный инжектированный скрипт, работающий в том же изолированном
// мире.
class PasswordCaptureBridge : public QObject {
    Q_OBJECT
public:
    explicit PasswordCaptureBridge(MainWindow* mw, QObject* parent = nullptr);

    // Вызывается ТОЛЬКО из инжектированного JS-скрипта (изолированный мир)
    Q_INVOKABLE void reportCredentials(const QString& domain, const QString& login, const QString& password);

    // JS запрашивает: "есть ли сохранённый пароль для этого домена?"
    // Если найдено РОВНО ОДНО совпадение — эмитится autofillAvailable().
    // При нескольких аккаунтах на сайте — молчим, чтобы не подставить не тот.
    Q_INVOKABLE void requestAutofill(const QString& domain);

signals:
    void credentialsCaptured(const QString& domain, const QString& login, const QString& password);
    void autofillAvailable(const QString& login, const QString& password);

private:
    MainWindow* m_mainWindow;
};