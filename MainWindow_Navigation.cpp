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
// MainWindow_Navigation.cpp — навигация, адресная строка, поиск по странице
// Кнопки Назад/Вперёд/Обновить/Стоп, переход по URL, переход домой,
// и плавающая панель поиска по странице (Ctrl+F: показать/скрыть,
// следующее/предыдущее совпадение). Выделено из MainWindow.cpp.
// ==========================================================================

// --- Позиционирование плавающей панели поиска (Ctrl+F) ---
static void positionFindBar(QWidget* bar, QTabWidget* tabWidget) {
    if (!bar || !tabWidget) return;
    const int margin = 10;
    int tabBarHeight = (tabWidget->tabBar() && tabWidget->tabBar()->isVisible())
        ? tabWidget->tabBar()->height() : 0;
    int x = qMax(margin, tabWidget->width() - bar->width() - margin);
    int y = tabBarHeight + margin;
    bar->move(x, y);
}


class FindBarPositionFilter : public QObject {
public:
    FindBarPositionFilter(QWidget* bar, QTabWidget* tabWidget, QObject* parent = nullptr)
        : QObject(parent), m_bar(bar), m_tabWidget(tabWidget) {
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (obj == m_tabWidget && event->type() == QEvent::Resize) {
            positionFindBar(m_bar, m_tabWidget);
        }
        return QObject::eventFilter(obj, event);
    }

private:
    QWidget* m_bar;
    QTabWidget* m_tabWidget;
};


void MainWindow::navigateToUrl() {
    if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        QString urlStr = topBar->getAddressBar()->text();

        if (!urlStr.startsWith("http://") && !urlStr.startsWith("https://") && !urlStr.startsWith("storm://")) {
            if (urlStr.contains(".") && !urlStr.contains(" ")) {
                urlStr = "https://" + urlStr;
            }
            else {
                QSettings settings;
                QString engine = settings.value("browser/search_engine", "DuckDuckGo").toString();

                QString searchUrl = "https://duckduckgo.com/?q=";
                if (engine == "Yandex") searchUrl = "https://yandex.ru/search/?text=";
                else if (engine == "Google") searchUrl = "https://www.google.com/search?q=";
                else if (engine == "Bing") searchUrl = "https://www.bing.com/search?q=";

                urlStr = searchUrl + urlStr;
            }
        }
        view->setUrl(QUrl(urlStr));
    }
}


void MainWindow::goBack() {
    if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        view->back();
    }
}


void MainWindow::goForward() {
    if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        view->forward();
    }
}


void MainWindow::reloadPage() {
    if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        view->reload();
    }
}


void MainWindow::stopLoading() {
    if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        view->stop();
    }
}


void MainWindow::showFindBar() {
    if (!m_findBar) {
        m_findBar = new QWidget(tabWidget);
        m_findBar->setObjectName("stormFindBar");
        m_findBar->setFixedWidth(220);
        m_findBar->setStyleSheet(
            "#stormFindBar { background-color: #2b2f36; border: 1px solid #444b56; border-radius: 8px; }"
            "#stormFindBar QLineEdit { background: transparent; border: none; color: #e8e8e8; font-size: 13px; padding: 2px 4px; }"
            "#stormFindBar QToolButton { background: transparent; border: none; color: #cfd3d8; border-radius: 4px; padding: 2px 4px; font-size: 12px; }"
            "#stormFindBar QToolButton:hover { background-color: rgba(255, 255, 255, 0.08); }"
            "#stormFindBar QToolButton:disabled { color: #5a5f66; }"
            "#stormFindBar QLabel#findCountLabel { color: #9aa0a6; font-size: 11px; padding-right: 2px; }"
        );
        auto* shadow = new QGraphicsDropShadowEffect(m_findBar);
        shadow->setBlurRadius(18);
        shadow->setOffset(0, 2);
        shadow->setColor(QColor(0, 0, 0, 140));
        m_findBar->setGraphicsEffect(shadow);

        QHBoxLayout* barLayout = new QHBoxLayout(m_findBar);
        barLayout->setContentsMargins(8, 4, 6, 4);
        barLayout->setSpacing(2);

        m_findLineEdit = new QLineEdit(m_findBar);
        m_findLineEdit->setPlaceholderText(u8"Найти на странице…");
        barLayout->addWidget(m_findLineEdit, 1);

        m_findMatchCountLabel = new QLabel(m_findBar);
        m_findMatchCountLabel->setObjectName("findCountLabel");
        barLayout->addWidget(m_findMatchCountLabel);

        m_findPrevButton = new QToolButton(m_findBar);
        m_findPrevButton->setText(QString::fromUtf8("\xE2\x96\xB2"));
        m_findPrevButton->setToolTip(u8"Предыдущее совпадение (Shift+Enter)");
        m_findPrevButton->setCursor(Qt::PointingHandCursor);
        barLayout->addWidget(m_findPrevButton);

        m_findNextButton = new QToolButton(m_findBar);
        m_findNextButton->setText(QString::fromUtf8("\xE2\x96\xBC"));
        m_findNextButton->setToolTip(u8"Следующее совпадение (Enter)");
        m_findNextButton->setCursor(Qt::PointingHandCursor);
        barLayout->addWidget(m_findNextButton);

        m_findCloseButton = new QToolButton(m_findBar);
        m_findCloseButton->setText(QString::fromUtf8("\xC3\x97"));
        m_findCloseButton->setToolTip(u8"Закрыть (Esc)");
        m_findCloseButton->setCursor(Qt::PointingHandCursor);
        barLayout->addWidget(m_findCloseButton);

        connect(m_findLineEdit, &QLineEdit::textChanged, this, [this](const QString&) {
            performFindBarSearch(false, true);
            });
        connect(m_findLineEdit, &QLineEdit::returnPressed, this, &MainWindow::findNext);
        connect(m_findNextButton, &QToolButton::clicked, this, &MainWindow::findNext);
        connect(m_findPrevButton, &QToolButton::clicked, this, &MainWindow::findPrevious);
        connect(m_findCloseButton, &QToolButton::clicked, this, &MainWindow::hideFindBar);

        QShortcut* findEscShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_findBar);
        findEscShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(findEscShortcut, &QShortcut::activated, this, &MainWindow::hideFindBar);

        tabWidget->installEventFilter(new FindBarPositionFilter(m_findBar, tabWidget, m_findBar));
    }

    positionFindBar(m_findBar, tabWidget);
    m_findBar->show();
    m_findBar->raise();
    m_findLineEdit->setFocus();
    m_findLineEdit->selectAll();

    if (!m_findLineEdit->text().isEmpty()) {
        performFindBarSearch(false, true);
    }
}


void MainWindow::hideFindBar() {
    if (!m_findBar) return;
    m_findBar->hide();
    if (auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        view->findText(QString());
        view->setFocus();
    }
}


void MainWindow::performFindBarSearch(bool backward, bool resetHighlight) {
    auto* view = qobject_cast<QWebEngineView*>(tabWidget->currentWidget());
    if (!view || !m_findLineEdit) return;

    const QString text = m_findLineEdit->text();
    if (text.isEmpty()) {
        view->findText(QString());
        if (m_findMatchCountLabel) m_findMatchCountLabel->clear();
        return;
    }

    if (resetHighlight) {
        view->findText(QString());
    }

    QWebEnginePage::FindFlags flags;
    if (backward) flags |= QWebEnginePage::FindBackward;

    QPointer<QLabel> countLabel(m_findMatchCountLabel);
    view->findText(text, flags, [countLabel](const QWebEngineFindTextResult& result) {
        if (!countLabel) return;
        if (result.numberOfMatches() == 0) {
            countLabel->setText(u8"0/0");
        }
        else {
            countLabel->setText(QString("%1/%2").arg(result.activeMatch()).arg(result.numberOfMatches()));
        }
        });
}


void MainWindow::findNext() {
    performFindBarSearch(false, false);
}


void MainWindow::findPrevious() {
    performFindBarSearch(true, false);
}


void MainWindow::goHome() {
    QSettings settings;
    int startupMode = settings.value("startup/mode", 0).toInt();

    QUrl homeUrl("storm://home");
    if (startupMode == 2) {
        QString customUrlStr = settings.value("startup/custom_url", "https://duckduckgo.com").toString();
        if (!customUrlStr.startsWith("http://") && !customUrlStr.startsWith("https://")) {
            customUrlStr = "https://" + customUrlStr;
        }
        homeUrl = QUrl(customUrlStr);
    }

    addNewTab(homeUrl);
}


void MainWindow::updateAddressBar(const QUrl& url) {
    auto* senderView = qobject_cast<QWebEngineView*>(sender());
    if (senderView && senderView != tabWidget->currentWidget()) {
        return;
    }
    topBar->getAddressBar()->setText(url.toString());

    // Эта функция и так вызывается и на urlChanged (навигация), и напрямую
    // из currentChanged (переключение вкладок) — поэтому это единая точка,
    // где синхронизируем состояние кнопки закладки и иконки Storm Shield
    // с адресом именно активной вкладки.
    topBar->setBookmarked(isCurrentPageBookmarked());
    topBar->setShieldException(isCurrentPageShieldExcepted());
}
