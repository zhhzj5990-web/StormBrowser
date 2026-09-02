#pragma once
#include <QObject>
#include <QDialog>
#include <QProgressDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QCryptographicHash>
#include <QTimer>
#include <QStringList>

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

    // Запустить установщик при закрытии браузера. Возвращает true, только
    // если реально запущена процедура чистого завершения + отложенного
    // запуска установщика (см. cleanShutdownAndRunInstaller) — false, если
    // обновление на самом деле не было готово или файл установщика оказался
    // повреждён (не совпал хэш). MainWindow::closeEvent() использует этот
    // результат, чтобы не "проглатывать" закрытие окна впустую, если
    // обновление на самом деле не запустилось.
    bool applyStagedUpdateAndRestart();

    static bool isVersionNewer(const QString& remoteVersion, const QString& localVersion);

    // Забирает путь и аргументы установщика, отложенные до полного и чистого
    // завершения процесса браузера (см. cleanShutdownAndRunInstaller()).
    // main() вызывает это РОВНО ОДИН РАЗ, уже после того как MainWindow (и
    // все detach-окна) гарантированно разрушены обычным выходом из области
    // видимости — только тогда безопасно запускать установщик. Возвращает
    // true и очищает статики, если отложенный запуск действительно есть.
    static bool takePendingInstaller(QString& outPath, QStringList& outArgs);

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

    // Статики (не члены экземпляра!) — переживают уничтожение и этого
    // UpdateManager, и его владельца MainWindow, которые умирают вместе с
    // закрытием окна ДО того, как main() успевает забрать отложенный путь
    // установщика. См. cleanShutdownAndRunInstaller() и takePendingInstaller().
    static QString s_pendingInstallerPath;
    static QStringList s_pendingInstallerArgs;
};