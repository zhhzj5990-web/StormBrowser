#include "MainWindow.h"
#include "ThemeManager.h"
#include "StormTabBar.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSettings>
#include <QDialog>
#include <QSizeGrip>
#include <QTableWidget>
#include <QTreeWidget>
#include <QMap>
#include <QFont>
#include <QLineEdit>
#include <QPushButton>
#include <QHeaderView>
#include <QScrollBar>
#include "AIAssistantWidget.h"
#include "NotesWidget.h"
#include "TodoWidget.h"
#include "PomodoroWidget.h"
#include "NewsWidget.h"
#include "GamesWidget.h"
#include "TranslatorWidget.h"
#include "VoiceChatWidget.h"
#include "WeatherWidget.h"
#include "FreeGamesWidget.h"
#include "EducationWidget.h"
#include "ArcadeWidget.h"
#include "ReaderWidget.h"
#include "TalkWidget.h"
#include <QWebEngineSettings>
#include <QWebEngineNewWindowRequest>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QByteArray>
#include <QToolButton>
#include <QWebEngineProfile>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QMenu>
#include <QCoreApplication>
#include "MenuBuilder.h"
#include "CustomMenuPanel.h"
#include "BookmarksBridge.h"
#include "BookmarksPageHtml.h"
#include <QtAlgorithms>
#include "BrowserWebView.h"
#include "TabSpinner.h"
#include "PasswordManager.h"
#include "HomeAIBridge.h"
#include "HomeBridge.h"
#include <QWebChannel>
#include <QShortcut>
#include <QKeySequence>
#include <QtMath>
#include <QFileDialog>
#include <QTextStream>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QTabBar>
#include "BookmarksBar.h"
#include <QActionGroup>
#include "ScreenshotEditor.h"
#include "DownloadManager.h"
#include "FernetCrypto.h"
#include "StormWebPage.h"
#include <QRadioButton>
#include <QDialogButtonBox>
#include <QStandardPaths>
#include "UpdateManager.h"
#include "GameModeManager.h"
#include <QTimer>
#include <QPointer>
#include <QWebEnginePage>
#include "AdblockManager.h"
#include "ShieldInterceptor.h"
#include <QStatusBar> 
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QSet>
#include <QUrlQuery>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QInputDialog>
#include "HelpPageHtml.h"
#include "Logger.h"
#include "ProxyManager.h"
#include "SettingsBridge.h"
#include "StormCloudBridge.h"
#include "PageTemplates.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QIcon>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsDropShadowEffect>
#include <QWebEngineFindTextResult>


// ==========================================================================
// MainWindow_Passwords.cpp — менеджер паролей
// Открытие менеджера паролей, смена мастер-пароля, сброс хранилища,
// импорт паролей и диалог предложения сохранить пароль после логина
// на сайте. Выделено из MainWindow.cpp.
// ==========================================================================


void MainWindow::showPasswordManager() {
    passwordManager->showManagerDialog(this);
}


void MainWindow::changeMasterPassword() {
    passwordManager->changeMasterPassword(this);
}


void MainWindow::resetPasswordVault() {
    passwordManager->resetVault(this);
}


void MainWindow::importPasswords() {
    passwordManager->importPasswordsCsv(this);
}


void MainWindow::showSavePasswordPrompt(const QString& domain, const QString& login, const QString& password) {
    qWarning() << "[MainWindow] showSavePasswordPrompt() вызван, домен:" << domain
        << ", логин пуст:" << login.isEmpty() << ", пароль пуст:" << password.isEmpty();

    if (domain.isEmpty() || password.isEmpty()) {
        qWarning() << "[MainWindow] showSavePasswordPrompt() прерван: пустой домен или пароль";
        return;
    }

    if (passwordManager->hasExactMatch(domain, login, password)) {
        qWarning() << "[MainWindow] Такой пароль для" << domain << "уже сохранён — пропускаем диалог";
        passwordManager->touchLastUsed(domain, login);
        return;
    }

    auto answer = QMessageBox::question(this, u8"Сохранить пароль?",
        QString(u8"Storm Vault заметил вход на «%1» (логин: %2).\n\nСохранить этот пароль?")
        .arg(domain, login.isEmpty() ? u8"—" : login),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    qWarning() << "[MainWindow] Пользователь ответил:" << (answer == QMessageBox::Yes ? "Да" : "Нет");

    if (answer == QMessageBox::Yes) {
        passwordManager->savePassword(domain, login, password);
        statusBar()->showMessage(u8"✅ Пароль сохранён в Storm Vault", 4000);
    }
}
