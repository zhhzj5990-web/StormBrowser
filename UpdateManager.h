#pragma once
#include <QObject>
#include <QDialog>
#include <QProgressDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QCryptographicHash>
#include <QTimer>

class QLockFile;

class UpdateInfoDialog : public QDialog {
    Q_OBJECT
public:
    UpdateInfoDialog(QWidget* parent, const QString& currentVer, const QString& newVer, const QString& changelog);
};

class UpdateManager : public QObject {
    Q_OBJECT
public:
    explicit UpdateManager(QObject* parent = nullptr);
    ~UpdateManager() override;

    void checkForUpdatesManual(QWidget* parentWidget);

    void startSilentBackgroundCheck();

    void startPeriodicChecks();

    // Есть ли готовое к установке обновление?
    bool isUpdateStaged() const { return updateReadyToInstall; }

    // Идёт ли сейчас проверка версии или загрузка (защита от повторного запуска)?
    bool isBusy() const { return operationInProgress; }

    // Запустить установщик при закрытии браузера
    void applyStagedUpdateAndRestart();

    static bool isVersionNewer(const QString& remoteVersion, const QString& localVersion);

signals:
    // Сигнал, чтобы главное окно могло зажечь зеленую иконку или показать уведомление в меню
    void updateStagedAndReady(const QString& newVersion);

private slots:
    void onVersionCheckFinished();
    void onReadyRead();
    void onDownloadFinished();

    void performScheduledCheck();

private:
    void startDownload(const QString& urlStr, const QString& expectedHash, bool silent);
    void cleanShutdownAndRunInstaller(const QString& filePath);

    void cleanupDownloadState();

    QNetworkAccessManager* netManager;
    QNetworkReply* currentReply;
    QProgressDialog* progressDialog;
    QFile* tempFile;
    QCryptographicHash* sha256Hasher;

    QWidget* parentWindow;
    bool isSilentMode;
    bool updateReadyToInstall;

    bool operationInProgress;

    QString targetHash;
    QString installerPath;
    QString stagedVersion;

    // Часовой таймер для startPeriodicChecks()/performScheduledCheck().
    QTimer* periodicCheckTimer = nullptr;

    QLockFile* updateLockFile = nullptr;

    bool diskWriteError = false;

    const QString BROWSER_VERSION = "1.2.4";

    const QString UPDATE_SERVER_URL = "https://storm-browser.online:8002";
};