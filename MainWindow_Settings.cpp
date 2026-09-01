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
// MainWindow_Settings.cpp — настройки, тема, Storm Shield
// Вкладка настроек, стартовая страница, поисковая система, язык
// интерфейса, фон новой вкладки, браузер по умолчанию, очистка данных,
// применение темы и переключение исключений Storm Shield для сайта.
// Выделено из MainWindow.cpp.
// ==========================================================================


void MainWindow::clearBrowserData() {
    auto btn = QMessageBox::warning(this, u8"Очистка данных",
        u8"Вы уверены, что хотите удалить кэш, файлы cookie и историю посещений?\n\nЭто действие приведет к выходу из всех аккаунтов на сайтах, и его нельзя отменить.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn == QMessageBox::Yes) {
        dbManager.clearHistory();

        QWebEngineProfile* profile = m_mainProfile;
        profile->clearHttpCache();
        profile->clearAllVisitedLinks();

        if (auto* cookieStore = profile->cookieStore()) {
            cookieStore->deleteAllCookies();
        }

        QMessageBox::information(this, u8"Успех", u8"Данные браузера (кэш, куки, история) успешно очищены!");
    }
}


void MainWindow::setAsDefaultBrowser() {
    QMessageBox::information(this, u8"Браузер по умолчанию",
        u8"Сейчас откроются системные настройки Windows.\n\nНайдите в списке пункт 'Веб-браузер' и выберите Storm Browser.");

    QDesktopServices::openUrl(QUrl("ms-settings:defaultapps"));
}


void MainWindow::setNewTabBackground() {
    QString fileName = QFileDialog::getOpenFileName(this, u8"Выберите фон", "", u8"Изображения (*.png *.jpg *.jpeg *.webp)");
    if (!fileName.isEmpty()) {
        QSettings settings;
        settings.setValue("browser/background_image", fileName);
        QMessageBox::information(this, u8"Готово", u8"Фон установлен! Откройте новую вкладку, чтобы увидеть.");
    }
}


void MainWindow::setSearchEngine(const QString& engineName) {
    QSettings settings;
    settings.setValue("browser/search_engine", engineName);
}


void MainWindow::setUiLanguage(const QString& langCode) {
    QSettings settings;
    settings.setValue("browser/language", langCode);

    QMessageBox::information(this, u8"Язык интерфейса",
        u8"Язык интерфейса изменен.\n\nПожалуйста, перезапустите Storm Browser, чтобы изменения вступили в силу.");
}


void MainWindow::applyTheme(const QString& themeName) {
    ThemeManager::instance().applyTheme(this, themeName);
    QSettings settings;
    settings.setValue("theme", themeName);
}


void MainWindow::showStartupSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(u8"Настройки запуска браузера");
    dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg.resize(450, 250);
    dlg.setStyleSheet(this->styleSheet());

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    QLabel* title = new QLabel(u8"🚀 При запуске Storm Browser:", &dlg);
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #a371f7;");
    layout->addWidget(title);

    QSettings settings;
    int currentMode = settings.value("startup/mode", 0).toInt();
    QString customUrl = settings.value("startup/custom_url", "https://duckduckgo.com").toString();

    QRadioButton* rbHome = new QRadioButton(u8"Открывать Главную страницу (Быстрый доступ)", &dlg);
    QRadioButton* rbLastSession = new QRadioButton(u8"Возобновлять ранее открытые вкладки", &dlg);
    QRadioButton* rbCustom = new QRadioButton(u8"Открывать заданную страницу:", &dlg);

    rbHome->setChecked(currentMode == 0);
    rbLastSession->setChecked(currentMode == 1);
    rbCustom->setChecked(currentMode == 2);

    QLineEdit* urlInput = new QLineEdit(&dlg);
    urlInput->setText(customUrl);
    urlInput->setPlaceholderText(u8"https://example.com");
    urlInput->setEnabled(currentMode == 2);
    urlInput->setFixedHeight(30);

    connect(rbCustom, &QRadioButton::toggled, urlInput, &QLineEdit::setEnabled);

    layout->addWidget(rbHome);
    layout->addWidget(rbLastSession);
    layout->addWidget(rbCustom);
    layout->addWidget(urlInput);

    layout->addStretch();

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnSave = new QPushButton(u8"Сохранить", &dlg);
    QPushButton* btnCancel = new QPushButton(u8"Отмена", &dlg);

    btnSave->setFixedHeight(32);
    btnCancel->setFixedHeight(32);
    btnSave->setStyleSheet("background-color: #a371f7; color: white; font-weight: bold; border-radius: 4px;");

    connect(btnSave, &QPushButton::clicked, [&]() {
        int selectedMode = 0;
        if (rbLastSession->isChecked()) selectedMode = 1;
        if (rbCustom->isChecked()) selectedMode = 2;

        settings.setValue("startup/mode", selectedMode);
        settings.setValue("startup/custom_url", urlInput->text().trimmed());
        dlg.accept();
        });
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);

    btnLayout->addStretch();
    btnLayout->addWidget(btnSave);
    btnLayout->addWidget(btnCancel);
    layout->addLayout(btnLayout);

    dlg.exec();
}


void MainWindow::openSettingsTab()
{
    for (int i = 0; i < tabWidget->count(); ++i) {
        if (tabWidget->tabText(i).contains(u8"Настройки")) {
            tabWidget->setCurrentIndex(i);
            return;
        }
    }

    addNewTab(QUrl("storm://settings"));
}


bool MainWindow::isCurrentPageShieldExcepted() {
    auto* currentView = qobject_cast<QWebEngineView*>(tabWidget->currentWidget());
    if (!currentView) return false;

    QString host = currentView->url().host();
    if (host.isEmpty()) return false;

    QSettings settings;
    QStringList exceptions = settings.value("shield/exceptions").toStringList();
    return exceptions.contains(host, Qt::CaseInsensitive);
}


void MainWindow::toggleShieldException() {
    auto* currentView = qobject_cast<QWebEngineView*>(tabWidget->currentWidget());
    if (!currentView) return;

    QString host = currentView->url().host();
    if (host.isEmpty()) return;

    // mainInterceptor — локальная переменная в setupUi(), нигде не хранится
    // как поле класса, поэтому достаём его так же, как securityStatusBtn
    // в updateShieldStatusIndicator() ниже — через findChild по objectName.
    auto* interceptor = findChild<ShieldInterceptor*>("ShieldInterceptor");
    if (!interceptor) return;

    QSettings settings;
    QStringList exceptions = settings.value("shield/exceptions").toStringList();

    bool wasExcepted = exceptions.contains(host, Qt::CaseInsensitive);
    if (wasExcepted) {
        exceptions.removeAll(host);
    }
    else {
        exceptions.append(host);
    }

    settings.setValue("shield/exceptions", exceptions);
    interceptor->setExceptions(exceptions);

    topBar->setShieldException(!wasExcepted);
}


void MainWindow::updateShieldStatusIndicator(bool enabled) {
    auto* btn = findChild<QPushButton*>("securityStatusBtn");
    if (!btn) return;

    if (enabled) {
        btn->setText(u8"🛡️ Storm Shield: Активен");
        btn->setStyleSheet(
            "QPushButton { background-color: rgba(86, 211, 155, 0.1); color: #56d39b; "
            "border: 1px solid rgba(86, 211, 155, 0.3); border-radius: 6px; "
            "padding: 4px 12px; font-size: 12px; font-weight: bold; margin-right: 15px; }"
            "QPushButton:hover { background-color: rgba(86, 211, 155, 0.2); }"
        );
    }
    else {
        btn->setText(u8"🛡️ Storm Shield: Отключен");
        btn->setStyleSheet(
            "QPushButton { background-color: rgba(255,255,255,0.05); color: #9aa3b2; "
            "border: 1px solid rgba(255,255,255,0.12); border-radius: 6px; "
            "padding: 4px 12px; font-size: 12px; font-weight: bold; margin-right: 15px; }"
            "QPushButton:hover { background-color: rgba(255,255,255,0.1); }"
        );
    }
}
