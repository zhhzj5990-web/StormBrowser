#pragma once
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineCertificateError>
#include <QUrl>

class MainWindow;

class StormWebPage : public QWebEnginePage {
    Q_OBJECT
public:
    explicit StormWebPage(QWebEngineProfile* profile, MainWindow* mw, QObject* parent = nullptr);

protected:
    bool acceptNavigationRequest(const QUrl& url, QWebEnginePage::NavigationType type, bool isMainFrame) override;
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString& message, int lineNumber, const QString& sourceID) override;

private slots:
    void handleCertificateError(QWebEngineCertificateError error);

signals:
    void magnetLinkActivated(const QString& link);
    void settingsActionRequested(const QString& url);
    void credentialsCaptured(const QString& domain, const QString& login, const QString& password);
    // Аналог Python setup_password_save_handler(): JS обнаружил вероятную
    // форму РЕГИСТРАЦИИ (autocomplete=new-password или ≥2 полей password) —
    // не путать с credentialsCaptured выше, которая про сохранение ПОСЛЕ входа.
    void passwordSuggestionRequested(const QString& domain, const QString& login);

private:
    MainWindow* m_mainWindow;
};