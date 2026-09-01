#include "PasswordCaptureBridge.h"
#include "MainWindow.h"
#include "PasswordManager.h"
#include <QJsonArray>
#include <QJsonObject>

PasswordCaptureBridge::PasswordCaptureBridge(MainWindow* mw, QObject* parent)
    : QObject(parent), m_mainWindow(mw)
{
}

void PasswordCaptureBridge::reportCredentials(const QString& domain, const QString& login, const QString& password) {
    if (domain.isEmpty() || password.isEmpty()) return;
    emit credentialsCaptured(domain, login, password);
}

void PasswordCaptureBridge::requestAutofill(const QString& domain) {
    if (!m_mainWindow || domain.isEmpty()) return;

    PasswordManager* pm = m_mainWindow->getPasswordManager();
    if (!pm) return;

    // Если для домена одна запись — подставится она. Если несколько
    // (разные аккаунты на одном сайте) — подставится тот, которым
    // пользовались последним (см. PasswordManager::touchLastUsed).
    QJsonObject match = pm->getBestMatchForAutofill(domain);
    if (match.isEmpty()) return;

    emit autofillAvailable(match.value("login").toString(), match.value("password").toString());
}