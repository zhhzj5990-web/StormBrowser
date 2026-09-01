#pragma once
#include <QObject>
#include <QString>

// Мост QWebChannel для функции "предложить надёжный пароль при регистрации".
// Аналог script_injector.py -> setup_password_save_handler(): там объект
// назывался storm_handler, а метод, вызываемый из JS, — suggest_password_signal.
// Здесь — pwSuggest / suggestPassword. Этой функции в C++ не было вообще:
// существующий pwCapture/StormPasswordCapture (см. StormWebPage.cpp) решает
// другую задачу — сохранение пароля ПОСЛЕ входа, а не подсказку при регистрации.
//
// Регистрируется в StormWebPage на ТОМ ЖЕ QWebChannel, что и pwCapture —
// один канал спокойно держит несколько именованных объектов.
class PasswordSuggestBridge : public QObject {
    Q_OBJECT
public:
    explicit PasswordSuggestBridge(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    // Вызывается из JS: channel.objects.pwSuggest.suggestPassword(domain, login)
    // domain — это hostname (window.location.hostname), а не полный URL,
    // для единообразия с PasswordManager (хранит/ищет записи по домену).
    void suggestPassword(const QString& domain, const QString& login) {
        emit passwordSuggestionRequested(domain, login);
    }

signals:
    void passwordSuggestionRequested(const QString& domain, const QString& login);
};