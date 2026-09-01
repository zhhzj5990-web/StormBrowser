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
// MainWindow_View.cpp — вид страницы, масштаб, избранное, боковая панель
// Избранное (favorites), зум страницы, полноэкранный режим, печать,
// скриншот, скачивания, профиль, перевод страницы, режим чтения,
// боковая панель/AI-панель, инкогнито-вкладка, AI-действия из
// контекстного меню страницы. Выделено из MainWindow.cpp.
// ==========================================================================


void MainWindow::addFavorite() {
    if (auto* currentView = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        QString title = currentView->title();
        QString url = currentView->url().toString();

        if (dbManager.addFavorite(title, url)) {
            QMessageBox::information(this, u8"Избранное", u8"Страница добавлена на главную (Быстрый доступ)!");
        }
    }
}


void MainWindow::removeFavorite() {
    if (auto* currentView = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        QString url = currentView->url().toString();

        if (dbManager.removeFavorite(url)) {
            QMessageBox::information(this, u8"Избранное", u8"Текущая страница удалена с главной.");
        }
    }
}


void MainWindow::clearFavorites() {
    if (QMessageBox::question(this, u8"Очистка", u8"Удалить ВСЕ страницы с главной вкладки?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        dbManager.clearFavorites();
        QMessageBox::information(this, u8"Очистка", u8"Список избранного на главной очищен.");
    }
}


void MainWindow::zoomIn() {
    if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        view->setZoomFactor(view->zoomFactor() + 0.1);
    }
}


void MainWindow::zoomOut() {
    if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        qreal newZoom = view->zoomFactor() - 0.1;
        if (newZoom >= 0.25) {
            view->setZoomFactor(newZoom);
        }
    }
}


QString MainWindow::getCurrentZoomString() {
    if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        int percentage = qRound(view->zoomFactor() * 100);
        return QString::number(percentage) + "%";
    }
    return "100%";
}


void MainWindow::toggleFullScreen() {
    if (isFullScreen()) {
        showNormal();
        topBar->show();
    }
    else {
        showFullScreen();
        topBar->hide();
    }
}


void MainWindow::printCurrentPage() {
    if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        QString fileName = QFileDialog::getSaveFileName(this, u8"Сохранить страницу как PDF (Печать)", "", u8"PDF Файлы (*.pdf)");
        if (!fileName.isEmpty()) {
            QWebEnginePage* page = view->page();

            QMetaObject::Connection* conn = new QMetaObject::Connection();
            *conn = connect(page, &QWebEnginePage::pdfPrintingFinished, this,
                [this, conn](const QString& filePath, bool success) {
                    if (success) {
                        QMessageBox::information(this, u8"Печать",
                            u8"Страница успешно сохранена в PDF-файл:\n" + filePath);
                    }
                    else {
                        QMessageBox::warning(this, u8"Печать",
                            u8"Не удалось сохранить страницу в PDF-файл:\n" + filePath);
                    }
                    QObject::disconnect(*conn);
                    delete conn;
                });

            page->printToPdf(fileName);
        }
    }
}


void MainWindow::translateCurrentPage(const QString& langCode, const QString& engine) {
    QWebEngineView* currentView = qobject_cast<QWebEngineView*>(tabWidget->currentWidget());
    if (!currentView) return;

    QString originalUrl = currentView->url().toString();

    if (langCode == "original") {
        if (originalUrl.contains("translate.goog") || originalUrl.contains("translate.google") || originalUrl.contains("translate.yandex")) {
            currentView->back();
        }
        return;
    }

    if (originalUrl.isEmpty() || originalUrl.startsWith("storm://") || originalUrl.startsWith("about:")) {
        QMessageBox::information(this, u8"Переводчик", u8"Эту внутреннюю страницу переводить не требуется.");
        return;
    }

    QUrl u(originalUrl);
    if (originalUrl.contains("translate.goog") || originalUrl.contains("translate.yandex.ru")) {
        QUrlQuery query(u);
        if (query.hasQueryItem("u")) originalUrl = query.queryItemValue("u");
        else if (query.hasQueryItem("url")) originalUrl = query.queryItemValue("url");
    }

    QString translatedUrl;
    if (engine == "yandex") {
        translatedUrl = QString("https://translate.yandex.ru/web?lang=auto-%1&url=%2")
            .arg(langCode, QUrl::toPercentEncoding(originalUrl));
    }
    else {
        translatedUrl = QString("https://translate.google.com/translate?sl=auto&tl=%1&u=%2")
            .arg(langCode, QUrl::toPercentEncoding(originalUrl));
    }

    currentView->setUrl(QUrl(translatedUrl));
    statusBar()->showMessage(u8"🌐 Страница переводится (" + engine.toUpper() + u8" -> " + langCode + u8")...", 4000);
}


void MainWindow::toggleSidebar() {
    if (sidebar) sidebar->setVisible(!sidebar->isVisible());
}


void MainWindow::toggleAIPanel() {
    if (sidebar) sidebar->setVisible(!sidebar->isVisible());
}


bool MainWindow::isSidebarVisible() const {
    // Реальное текущее состояние боковой панели — используется в
    // MenuBuilder, чтобы галочка "Боковая панель" при открытии меню
    // всегда отражала фактическую видимость, а не захардкоженное значение.
    return sidebar && sidebar->isVisible();
}


void MainWindow::processAiAction(const QString& actionType, const QString& textContext) {
    if (aiAssistantWidget) {
        aiAssistantWidget->runContextMenuAction(actionType, textContext);
    }

    statusBar()->showMessage(QString(u8"🤖 Storm AI: обрабатываю запрос (%1)...").arg(actionType), 5000);
}


void MainWindow::openIncognitoTab() {
    addNewTab(QUrl("storm://newtab"), true);
}


void MainWindow::takeScreenshot() {
    if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        QPixmap rawPixmap = view->grab();
        QPixmap safePixmap = QPixmap::fromImage(rawPixmap.toImage());
        ScreenshotEditor editor(safePixmap, this);
        editor.exec();
    }
}


void MainWindow::openDownloads() {
    if (downloadManager) {
        downloadManager->setVisible(!downloadManager->isVisible());

        if (topBar && topBar->getDownloadsButton()) {
            topBar->getDownloadsButton()->setChecked(downloadManager->isVisible());
        }
    }
}


void MainWindow::openProfile() {
    for (int i = 0; i < tabWidget->count(); ++i) {
        if (tabWidget->tabText(i).contains(u8"Storm Cloud")) {
            tabWidget->setCurrentIndex(i);
            return;
        }
    }
    addNewTab(QUrl("storm://cloud"));
}


void MainWindow::toggleReaderMode() {
    auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget());
    if (!view) return;

    QString currentUrl = view->url().toString();
    if (currentUrl.startsWith("storm://") || currentUrl.startsWith("storm-")) {
        return;
    }

    QString jsCode = u8R"JS(
        (function() {
            if (document.getElementById('storm-reader-mode')) {
                window.location.reload();
                return;
            }
            var article = document.querySelector('article') || document.querySelector('main') || document.body;
            var html = article.innerHTML;

            document.body.innerHTML = `
                <div id="storm-reader-mode" style="
                    max-width: 800px; margin: 0 auto; padding: 40px;
                    font-family: 'Georgia', serif; font-size: 20px; line-height: 1.6;
                    color: #222; background: #fdfdfd; border-radius: 8px; box-shadow: 0 4px 15px rgba(0,0,0,0.1);
                    margin-top: 20px;
                ">
                    <h1 style="text-align: center; font-family: sans-serif;">📖 Режим чтения</h1>
                    <hr style="margin-bottom: 30px; border: 0; border-top: 1px solid #ddd;">
                    ${html}
                </div>
            `;
            document.body.style.backgroundColor = '#e0e0e0';
            document.body.style.backgroundImage = 'none';
        })();
    )JS";

    view->page()->runJavaScript(jsCode);
}
