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


#include "MainWindow_UiHelpers.h"

// ==========================================================================
// MainWindow_Bookmarks.cpp — закладки и история
// Меню закладок, добавление/удаление текущей страницы из закладок,
// диалог истории посещений и его очистка, панель закладок.
// Выделено из MainWindow.cpp.
// Использует WindowDragFilter из MainWindow_UiHelpers.h (перетаскивание
// безрамочного окна истории).
// ==========================================================================


namespace {

    QIcon fallbackBookmarkIcon(const QString& title)
    {
        const QString ch = title.trimmed().isEmpty()
            ? QStringLiteral("?")
            : title.trimmed().left(1).toUpper();

        QPixmap pix(20, 20);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(0, 0, 20, 20), 5, 5);
        p.fillPath(path, QColor(CustomMenuPanel::themeAccent()));
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPixelSize(11);
        f.setBold(true);
        p.setFont(f);
        p.drawText(pix.rect(), Qt::AlignCenter, ch);
        return QIcon(pix);
    }

}


void MainWindow::loadBookmarksIntoMenu() {
    QMenu* bmMenu = this->findChild<QMenu*>("bookmarksMenu");

    if (bmMenu) {
        qDeleteAll(bmMenu->actions());
    }

    // ЕДИНЫЙ СТАНДАРТ ПОРЯДКА: тот же ORDER BY, что и в
    // BookmarksBridge::getBookmarks() (см. BookmarksBridge.cpp) — сначала
    // закладки без папки, затем папки по их sort_order, а закладки внутри
    // каждой группы — по своему sort_order. Раньше здесь был запрос БЕЗ
    // ORDER BY вообще, поэтому панель (BookmarksBar) и окно (BookmarksWindow)
    // показывали закладки в порядке, никак не связанном с сортировкой,
    // которую можно задать на странице storm://bookmarks (drag&drop,
    // кнопки ⬆️/⬇️). Теперь все три места показывают один и тот же порядок,
    // и перестановка в любом из них видна везде после обновления.
    QSqlQuery query(
        "SELECT b.title, b.url, b.folder_id "
        "FROM bookmarks b LEFT JOIN bookmark_folders f ON f.id = b.folder_id "
        "ORDER BY (b.folder_id IS NULL) DESC, f.sort_order, b.sort_order, b.id"
    );

    QNetworkAccessManager* networkManager = this->findChild<QNetworkAccessManager*>("faviconManager");
    if (!networkManager) {
        networkManager = new QNetworkAccessManager(this);
        networkManager->setObjectName("faviconManager");
    }

    while (query.next()) {
        QString title = query.value(0).toString();
        QString url = query.value(1).toString();
        const QVariant folderId = query.value(2).isNull() ? QVariant(0) : query.value(2);

        if (title.isEmpty()) title = url;
        if (title.length() > 30) title = title.left(30) + "...";

        if (bmMenu) {
            QAction* act = bmMenu->addAction(title);
            act->setData(url);
            // folderId нужен BookmarksBar/BookmarksWindow для drag&drop и
            // "Переместить выше/ниже" — так они двигают закладку внутри её
            // РЕАЛЬНОЙ папки (через BookmarksBridge), даже когда сама панель
            // или окно закладок папки визуально не показывают (это всё ещё
            // плоский список — см. BookmarksReorderHelper.h).
            act->setProperty("folderId", folderId);
            act->setIcon(fallbackBookmarkIcon(title));

            connect(act, &QAction::triggered, this, [this, url]() {
                this->addNewTab(QUrl(url));
                });

            QUrl parsedUrl(url);
            QString faviconUrl = QString("https://www.google.com/s2/favicons?sz=32&domain=%1").arg(parsedUrl.host());

            QNetworkRequest request((QUrl(faviconUrl)));
            QNetworkReply* reply = networkManager->get(request);

            QPointer<QAction> actPtr(act);

            connect(reply, &QNetworkReply::finished, this, [actPtr, reply]() {
                if (reply->error() == QNetworkReply::NoError && actPtr) {
                    QByteArray data = reply->readAll();
                    QPixmap pixmap;
                    if (pixmap.loadFromData(data)) {
                        actPtr->setIcon(QIcon(pixmap));
                    }
                }
                reply->deleteLater();
                });
        }
    }

    if (bookmarksBar) {
        bookmarksBar->updateBookmarks();
    }
}


bool MainWindow::isCurrentPageBookmarked() {
    if (auto* currentView = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        return dbManager.isBookmark(currentView->url().toString());
    }
    return false;
}


void MainWindow::addCurrentBookmark() {
    if (auto* currentView = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        QString title = currentView->title();
        QString url = currentView->url().toString();

        if (dbManager.addBookmark(title, url)) {
            loadBookmarksIntoMenu();
            topBar->setBookmarked(true);
            QMessageBox::information(this, u8"Закладки", u8"Страница успешно добавлена в ваши закладки!");
        }
    }
}


void MainWindow::removeCurrentBookmark() {
    if (auto* currentView = qobject_cast<QWebEngineView*>(tabWidget->currentWidget())) {
        QString url = currentView->url().toString();

        if (dbManager.removeBookmark(url)) {
            loadBookmarksIntoMenu();
            topBar->setBookmarked(false);
            QMessageBox::information(this, u8"Закладки", u8"Страница успешно удалена из закладок!");
        }
        else {
            QMessageBox::warning(this, u8"Ошибка", u8"Не удалось удалить закладку.");
        }
    }
}


void MainWindow::clearBookmarks() {
    if (QMessageBox::question(this, u8"Очистка", u8"Удалить ВСЕ закладки?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        dbManager.clearBookmarks();
        loadBookmarksIntoMenu();
    }
}


void MainWindow::showHistory() {
    QDialog* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    dlg->resize(900, 600);
    dlg->setObjectName("historyDialog");

    QVBoxLayout* mainLayout = new QVBoxLayout(dlg);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QWidget* historyTitleBar = new QWidget(dlg);
    historyTitleBar->setFixedHeight(35);
    historyTitleBar->setObjectName("customTitleBar");

    QHBoxLayout* titleLayout = new QHBoxLayout(historyTitleBar);
    titleLayout->setContentsMargins(15, 0, 0, 0);
    titleLayout->setSpacing(0);

    QLabel* titleLabel = new QLabel(u8"🕒 История посещений", historyTitleBar);
    titleLabel->setObjectName("customTitleLabel");

    QPushButton* btnCloseTitle = new QPushButton(u8"✕", historyTitleBar);
    btnCloseTitle->setObjectName("titleCloseBtn");
    btnCloseTitle->setFixedSize(35, 35);

    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(btnCloseTitle);

    mainLayout->addWidget(historyTitleBar);

    historyTitleBar->installEventFilter(new WindowDragFilter(historyTitleBar));
    connect(btnCloseTitle, &QPushButton::clicked, dlg, &QDialog::reject);

    QWidget* contentWidget = new QWidget(dlg);
    QVBoxLayout* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(15, 15, 15, 15);
    contentLayout->setSpacing(15);

    QLineEdit* searchBox = new QLineEdit(contentWidget);
    searchBox->setPlaceholderText(u8"Поиск по заголовку или URL");
    searchBox->setFixedHeight(35);
    searchBox->setObjectName("historySearchBox");
    contentLayout->addWidget(searchBox);

    QTreeWidget* historyTree = new QTreeWidget(contentWidget);
    historyTree->setObjectName("historyTree");
    historyTree->setColumnCount(3);
    historyTree->setHeaderLabels({ u8"Время", u8"Заголовок", u8"URL" });
    historyTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    historyTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    historyTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    historyTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyTree->setAlternatingRowColors(true);
    historyTree->setUniformRowHeights(true);

    historyTree->setStyleSheet(
        "QTreeWidget {"
        "   gridline-color: rgba(255, 255, 255, 0.1);"
        "   alternate-background-color: rgba(255, 255, 255, 0.03);"
        "}"
        "QTreeWidget::item {"
        "   padding: 4px 6px;"
        "}"
        "QHeaderView::section {"
        "   text-align: left;"
        "   padding-left: 10px;"
        "}"
    );

    QMap<QString, QTreeWidgetItem*> dateGroups;
    const auto entries = dbManager.getHistoryEntries(300);
    for (const auto& entry : entries) {
        QTreeWidgetItem* groupItem = dateGroups.value(entry.date, nullptr);
        if (!groupItem) {
            groupItem = new QTreeWidgetItem(historyTree);
            groupItem->setText(0, entry.date);
            groupItem->setFirstColumnSpanned(true);
            QFont groupFont = groupItem->font(0);
            groupFont.setBold(true);
            groupItem->setFont(0, groupFont);
            groupItem->setExpanded(true);
            dateGroups.insert(entry.date, groupItem);
        }

        QTreeWidgetItem* child = new QTreeWidgetItem(groupItem);
        child->setText(0, entry.time);
        child->setText(1, entry.title);
        child->setText(2, entry.url);
        child->setData(0, Qt::UserRole, entry.id);
    }
    contentLayout->addWidget(historyTree);

    connect(searchBox, &QLineEdit::textChanged, historyTree, [historyTree](const QString& text) {
        for (int i = 0; i < historyTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* group = historyTree->topLevelItem(i);
            bool anyVisible = false;
            for (int j = 0; j < group->childCount(); ++j) {
                QTreeWidgetItem* child = group->child(j);
                bool matches = text.isEmpty()
                    || child->text(1).contains(text, Qt::CaseInsensitive)
                    || child->text(2).contains(text, Qt::CaseInsensitive);
                child->setHidden(!matches);
                anyVisible = anyVisible || matches;
            }
            group->setHidden(!anyVisible);
        }
        });

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnOpen = new QPushButton(u8"Открыть", contentWidget);
    QPushButton* btnDelete = new QPushButton(u8"Удалить запись", contentWidget);
    QPushButton* btnCloseBottom = new QPushButton(u8"Закрыть", contentWidget);

    btnOpen->setFixedHeight(32);
    btnDelete->setFixedHeight(32);
    btnCloseBottom->setFixedHeight(32);

    btnOpen->setObjectName("historyBtnOpen");
    btnDelete->setObjectName("historyBtnDelete");
    btnCloseBottom->setObjectName("historyBtnClose");

    btnLayout->addWidget(btnOpen);
    btnLayout->addWidget(btnDelete);
    btnLayout->addStretch();
    btnLayout->addWidget(btnCloseBottom);

    contentLayout->addLayout(btnLayout);
    mainLayout->addWidget(contentWidget);

    connect(btnCloseBottom, &QPushButton::clicked, dlg, &QDialog::accept);

    auto openSelected = [this, historyTree, dlg]() {
        QTreeWidgetItem* item = historyTree->currentItem();
        if (!item || !item->parent()) {
            QMessageBox::information(this, u8"История",
                u8"Пожалуйста, выберите конкретный сайт, а не саму дату.");
            return;
        }
        QString url = item->text(2);
        this->addNewTab(QUrl(url));
        dlg->accept();
        };

    connect(btnOpen, &QPushButton::clicked, openSelected);
    connect(historyTree, &QTreeWidget::itemDoubleClicked, dlg, [openSelected](QTreeWidgetItem*, int) {
        openSelected();
        });

    connect(btnDelete, &QPushButton::clicked, [this, historyTree]() {
        QTreeWidgetItem* item = historyTree->currentItem();
        if (!item || !item->parent()) {
            QMessageBox::information(this, u8"История",
                u8"Пожалуйста, выберите конкретную запись для удаления.");
            return;
        }

        int visitId = item->data(0, Qt::UserRole).toInt();
        this->dbManager.removeHistoryItem(visitId);

        QTreeWidgetItem* group = item->parent();
        group->removeChild(item);
        delete item;
        if (group->childCount() == 0) {
            int idx = historyTree->indexOfTopLevelItem(group);
            historyTree->takeTopLevelItem(idx);
            delete group;
        }
        });

    dlg->setStyleSheet(this->styleSheet());
    dlg->exec();
}


void MainWindow::clearHistory() {
    if (QMessageBox::question(this, u8"Очистка", u8"Вы уверены, что хотите полностью стереть историю посещений?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        if (dbManager.clearHistory()) {
            QMessageBox::information(this, u8"История", u8"История успешно очищена.");
        }
    }
}


void MainWindow::toggleBookmarksBar() {
    if (bookmarksBar) {
        bool isVisible = !bookmarksBar->isVisible();
        bookmarksBar->setVisible(isVisible);

        QSettings settings;
        settings.setValue("browser/show_bookmarks_bar", isVisible);
    }
}


void MainWindow::openBookmarksTab()
{
    for (int i = 0; i < tabWidget->count(); ++i) {
        if (tabWidget->tabText(i).contains(u8"Закладки")) {
            tabWidget->setCurrentIndex(i);
            return;
        }
    }

    addNewTab(QUrl("storm://bookmarks"));
}
