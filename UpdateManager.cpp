#include "UpdateManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QPushButton>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QNetworkProxy>
#include <QProcess>
#include <QApplication>
#include <QTimer>
#include <QDir>
#include <QDebug>
#include <QSettings>
#include <QDateTime>
#include <QPointer>
#include <QLockFile>

UpdateInfoDialog::UpdateInfoDialog(QWidget* parent, const QString& currentVer, const QString& newVer, const QString& changelog)
    : QDialog(parent)
{
    setWindowTitle(u8"Обновление Storm Browser");
    resize(580, 460);
    if (parent) setStyleSheet(parent->styleSheet());

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    QLabel* title = new QLabel(QString(u8"🚀 Доступна новая версия: <b>%1</b>").arg(newVer), this);
    title->setStyleSheet("font-size: 18px; color: #56d39b;");
    layout->addWidget(title);

    layout->addWidget(new QLabel(QString(u8"Текущая версия: %1").arg(currentVer), this));
    layout->addWidget(new QLabel(u8"<b>Изменения в этой версии:</b>", this));

    QTextBrowser* changelogBox = new QTextBrowser(this);
    QString formattedChangelog = changelog;
    formattedChangelog.replace("\n", "<br>");
    changelogBox->setHtml(QString("<div style='line-height: 1.5; font-size: 14px;'>%1</div>").arg(formattedChangelog));
    layout->addWidget(changelogBox);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* cancelBtn = new QPushButton(u8"Отложить", this);
    QPushButton* updateBtn = new QPushButton(u8"📥 Скачать и установить", this);

    cancelBtn->setFixedHeight(34);
    updateBtn->setFixedHeight(34);
    updateBtn->setStyleSheet("background-color: #a371f7; color: white; font-weight: bold; border-radius: 6px; padding: 0 15px;");

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(updateBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(updateBtn);
    layout->addLayout(btnLayout);
}

UpdateManager::UpdateManager(QObject* parent)
    : QObject(parent), currentReply(nullptr), progressDialog(nullptr), tempFile(nullptr),
    sha256Hasher(nullptr), parentWindow(nullptr), isSilentMode(false), updateReadyToInstall(false),
    operationInProgress(false)
{
    netManager = new QNetworkAccessManager(this);
    netManager->setProxy(QNetworkProxy::NoProxy); 
}

bool UpdateManager::isVersionNewer(const QString& remoteVersion, const QString& localVersion) {
    QStringList remoteParts = remoteVersion.split('.');
    QStringList localParts = localVersion.split('.');
    int segments = qMax(remoteParts.size(), localParts.size());

    for (int i = 0; i < segments; ++i) {
        bool remoteOk = true;
        int r = i < remoteParts.size() ? remoteParts[i].toInt(&remoteOk) : 0;
        int l = i < localParts.size() ? localParts[i].toInt() : 0;

        if (!remoteOk) {
            
            return false;
        }
        if (r != l) return r > l;
    }
    return false; // версии полностью совпали
}

UpdateManager::~UpdateManager() {
    if (tempFile) {
        if (tempFile->isOpen()) tempFile->close();
        delete tempFile;
    }
    if (sha256Hasher) delete sha256Hasher;
    
    if (updateLockFile) {
        updateLockFile->unlock();
        delete updateLockFile;
    }
}

// 1. Ручной запуск (по кнопке в меню)
void UpdateManager::checkForUpdatesManual(QWidget* parentWidget) {
    
    if (updateReadyToInstall) {
        parentWindow = parentWidget;
        auto reply = QMessageBox::question(parentWindow, u8"Обновление готово",
            QString(u8"Версия %1 уже скачана в фоне и готова к установке.\n\nПерезапустить Storm Browser сейчас?").arg(stagedVersion),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            applyStagedUpdateAndRestart();
        }
        return;
    }

    if (operationInProgress) {
        QMessageBox::information(parentWidget, u8"Обновления",
            u8"Проверка обновлений уже выполняется, подождите немного.");
        return;
    }

    parentWindow = parentWidget;
    isSilentMode = false;
    operationInProgress = true;

    QUrl url(UPDATE_SERVER_URL + "/version");
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &UpdateManager::onVersionCheckFinished);
}

// 2. Тихий запуск (при старте браузера)
void UpdateManager::startSilentBackgroundCheck() {
    
    if (updateReadyToInstall || operationInProgress) {
        return;
    }

    isSilentMode = true;
    parentWindow = nullptr;
    operationInProgress = true;

    QUrl url(UPDATE_SERVER_URL + "/version");
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &UpdateManager::onVersionCheckFinished);
}

void UpdateManager::startPeriodicChecks() {
    
    QTimer::singleShot(30000, this, &UpdateManager::performScheduledCheck);

    if (!periodicCheckTimer) {
        periodicCheckTimer = new QTimer(this);
        connect(periodicCheckTimer, &QTimer::timeout, this, &UpdateManager::performScheduledCheck);
        periodicCheckTimer->start(60 * 60 * 1000); // раз в час
    }
}

void UpdateManager::performScheduledCheck() {
    // Обновление уже готово к установке или проверка/закачка уже идёт —
    // нечего делать.
    if (updateReadyToInstall || operationInProgress) return;

    QSettings settings;
    QDateTime lastCheck = settings.value("update/last_check_utc").toDateTime();
    if (lastCheck.isValid() && lastCheck.secsTo(QDateTime::currentDateTimeUtc()) < 24 * 60 * 60) {
        return; // сутки ещё не прошли — ждём следующего часового тика
    }
    startSilentBackgroundCheck();
}

void UpdateManager::onVersionCheckFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        operationInProgress = false;
        if (!isSilentMode && parentWindow) {
            QMessageBox::critical(parentWindow, u8"Ошибка сети", u8"Не удалось подключиться к серверу обновлений.");
        }
        return;
    }
    
    QSettings().setValue("update/last_check_utc", QDateTime::currentDateTimeUtc());

    QJsonDocument jsonDoc = QJsonDocument::fromJson(reply->readAll());
    if (!jsonDoc.isObject()) {
        operationInProgress = false;
        return;
    }

    QJsonObject obj = jsonDoc.object();
    QString remoteVer = obj["version"].toString().trimmed();
    QString remoteHash = obj["sha256"].toString().trimmed().toLower();
    QString changelog = obj["changelog"].toString();
    QString downloadUrl = obj["download_url"].toString();

    // Настоящее сравнение версий, а не просто "!=" — сервер не может
    // "откатить" клиента на более старую версию, представив её как обновление.
    if (remoteVer.isEmpty() || !isVersionNewer(remoteVer, BROWSER_VERSION)) {
        operationInProgress = false;
        if (!isSilentMode && parentWindow) {
            QMessageBox::information(parentWindow, u8"Обновления", QString(u8"У вас установлена последняя версия (%1)").arg(BROWSER_VERSION));
        }
        return;
    }

    if (remoteHash.isEmpty()) {
        operationInProgress = false;
        qWarning() << "[Storm Updater] Сервер обновлений не прислал sha256 — обновление отклонено из соображений безопасности.";
        if (!isSilentMode && parentWindow) {
            QMessageBox::critical(parentWindow, u8"Обновления",
                u8"Сервер обновлений не предоставил контрольную сумму файла. Обновление отменено в целях безопасности.");
        }
        return;
    }

    stagedVersion = remoteVer;

    if (isSilentMode) {
        // В фоновом режиме просто молча качаем!
        startDownload(downloadUrl, remoteHash, true);
    }
    else {
        UpdateInfoDialog dlg(parentWindow, BROWSER_VERSION, remoteVer, changelog);
        if (dlg.exec() == QDialog::Accepted) {
            startDownload(downloadUrl, remoteHash, false);
        }
        else {
            operationInProgress = false; // пользователь отказался — операция завершена
        }
    }
}

void UpdateManager::startDownload(const QString& urlStr, const QString& expectedHash, bool silent) {
    targetHash = expectedHash.toLower();
    isSilentMode = silent;
    diskWriteError = false;

    // сразу отступаем.
    QString lockPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/storm_browser_update.lock";
    updateLockFile = new QLockFile(lockPath);
    updateLockFile->setStaleLockTime(10 * 60 * 1000); // на случай, если предыдущий процесс завис/был убит
    if (!updateLockFile->tryLock(0)) {
        delete updateLockFile;
        updateLockFile = nullptr;
        operationInProgress = false;
        if (!isSilentMode && parentWindow) {
            QMessageBox::information(parentWindow, u8"Обновления",
                u8"Загрузка обновления уже идёт в другом окне Storm Browser.");
        }
        return;
    }

    QString finalUrl = urlStr.isEmpty() ? (UPDATE_SERVER_URL + "/browser_client") : urlStr;
    installerPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/storm_browser_setup.exe";

    if (QFile::exists(installerPath)) QFile::remove(installerPath);

    tempFile = new QFile(installerPath);
    if (!tempFile->open(QIODevice::WriteOnly)) {
        delete tempFile; 
        tempFile = nullptr;
        if (updateLockFile) { // не оставляем блокировку висеть при неудачном старте закачки
            updateLockFile->unlock();
            delete updateLockFile;
            updateLockFile = nullptr;
        }
        operationInProgress = false; // иначе апдейтер навсегда "застрянет" в занятом состоянии
        if (!isSilentMode && parentWindow) {
            QMessageBox::critical(parentWindow, u8"Ошибка", u8"Не удалось создать временный файл для загрузки обновления.");
        }
        return;
    }

    sha256Hasher = new QCryptographicHash(QCryptographicHash::Sha256);
    sha256Hasher->reset();

    if (!isSilentMode && parentWindow) {
        progressDialog = new QProgressDialog(u8"Загрузка обновления...", u8"Отмена", 0, 100, parentWindow);
        progressDialog->setWindowTitle(u8"Storm Browser Update");
        progressDialog->setStyleSheet(parentWindow->styleSheet());
        progressDialog->setWindowModality(Qt::WindowModal);
        progressDialog->show();
    }

    currentReply = netManager->get(QNetworkRequest(finalUrl));

    if (progressDialog) {
        
        QPointer<QProgressDialog> dlgPtr(progressDialog);
        connect(currentReply, &QNetworkReply::downloadProgress, this,
            [dlgPtr](qint64 bytesReceived, qint64 bytesTotal) {
                if (!dlgPtr) return;
                if (bytesTotal > 0) {
                    dlgPtr->setValue(static_cast<int>((bytesReceived * 100) / bytesTotal));
                }
                // Если сервер не прислал Content-Length (bytesTotal <= 0),
                // показывать проценты не от чего — оставляем как есть.
            });
        connect(progressDialog, &QProgressDialog::canceled, currentReply, &QNetworkReply::abort);
    }
    connect(currentReply, &QNetworkReply::readyRead, this, &UpdateManager::onReadyRead);
    connect(currentReply, &QNetworkReply::finished, this, &UpdateManager::onDownloadFinished);
}

void UpdateManager::onReadyRead() {
    if (!(currentReply && tempFile && tempFile->isOpen())) return;

    QByteArray chunk = currentReply->readAll();
    qint64 written = tempFile->write(chunk);
    if (written != chunk.size()) {
        
        qWarning() << "[Storm Updater] Ошибка записи файла обновления на диск:" << tempFile->errorString();
        diskWriteError = true;
        currentReply->abort();
        return;
    }
    sha256Hasher->addData(chunk);
}

void UpdateManager::cleanupDownloadState() {
    if (progressDialog) {
        progressDialog->close();
        progressDialog->deleteLater();
        progressDialog = nullptr;
    }
    if (sha256Hasher) {
        delete sha256Hasher;
        sha256Hasher = nullptr;
    }
    if (updateLockFile) {
        updateLockFile->unlock();
        delete updateLockFile;
        updateLockFile = nullptr;
    }
    currentReply = nullptr;
}

void UpdateManager::onDownloadFinished() {
    if (!currentReply) {
        // Не должно происходить в норме, но на всякий случай не оставляем
        // "зависший" operationInProgress навсегда true.
        cleanupDownloadState();
        operationInProgress = false;
        return;
    }

    QNetworkReply* finishedReply = currentReply;
    finishedReply->deleteLater();

    if (tempFile) {
        tempFile->close();
        delete tempFile;
        tempFile = nullptr;
    }

    if (finishedReply->error() == QNetworkReply::OperationCanceledError) {
        if (QFile::exists(installerPath)) QFile::remove(installerPath);
        
        bool wasDiskError = diskWriteError;
        cleanupDownloadState();
        operationInProgress = false;
        if (wasDiskError && !isSilentMode && parentWindow) {
            QMessageBox::critical(parentWindow, u8"Ошибка",
                u8"Не удалось сохранить файл обновления на диск (возможно, не хватает места).");
        }
        return;
    }

    if (finishedReply->error() != QNetworkReply::NoError) {
        if (!isSilentMode && parentWindow) {
            QMessageBox::warning(parentWindow, u8"Ошибка", u8"Не удалось скачать обновление.");
        }
        cleanupDownloadState();
        operationInProgress = false;
        return;
    }

    QString receivedHash = sha256Hasher ? QString(sha256Hasher->result().toHex()).toLower() : QString();

    if (targetHash.isEmpty() || receivedHash != targetHash) {
        if (QFile::exists(installerPath)) QFile::remove(installerPath);
        if (!isSilentMode && parentWindow) {
            QMessageBox::critical(parentWindow, u8"Безопасность", u8"Хеш скачанного файла не совпадает! Загрузка отменена.");
        }
        cleanupDownloadState();
        operationInProgress = false;
        return;
    }

    // --- УСПЕХ! ---
    updateReadyToInstall = true;
    cleanupDownloadState();
    operationInProgress = false;

    if (isSilentMode) {
        
        emit updateStagedAndReady(stagedVersion);
    }
    else {
        // Если пользователь качал вручную — спрашиваем сразу
        auto reply = QMessageBox::question(parentWindow, u8"Готово к установке",
            u8"Обновление загружено. Перезапустить Storm Browser для установки?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            applyStagedUpdateAndRestart();
        }
    }
}

void UpdateManager::applyStagedUpdateAndRestart() {
    if (!updateReadyToInstall || installerPath.isEmpty()) return;

    updateReadyToInstall = false; // в любом случае — одноразовая попытка установки

    QFile file(installerPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[Storm Updater] Не удалось открыть скачанный установщик перед запуском.";
        return;
    }
    QCryptographicHash verifyHasher(QCryptographicHash::Sha256);
    bool hashed = verifyHasher.addData(&file);
    file.close();

    QString actualHash = hashed ? QString(verifyHasher.result().toHex()).toLower() : QString();
    if (targetHash.isEmpty() || actualHash != targetHash) {
        qWarning() << "[Storm Updater] Хэш установщика перед запуском не совпал — установка отменена.";
        QFile::remove(installerPath);
        return;
    }

    cleanShutdownAndRunInstaller(installerPath);
}

void UpdateManager::cleanShutdownAndRunInstaller(const QString& filePath) {
    // Получаем правильный путь для Windows
    QString nativePath = QDir::toNativeSeparators(filePath);

    // Собираем флаги для Inno Setup в список (без cmd.exe и костылей)
    QStringList args;
    args << "/VERYSILENT"
        << "/SUPPRESSMSGBOXES"
        << "/CLOSEAPPLICATIONS"
        << "/FORCECLOSEAPPLICATIONS";

    // Запускаем сам установщик напрямую (Qt 6 корректно передаст пути с пробелами)
    QProcess::startDetached(nativePath, args);

    // Даем сигнал приложению на завершение работы
    QTimer::singleShot(100, []() { qApp->quit(); });
}