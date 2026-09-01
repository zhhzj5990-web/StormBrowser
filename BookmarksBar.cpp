#include "BookmarksBar.h"
#include "MainWindow.h"       // Обязательно в .cpp, чтобы компилятор видел методы MainWindow
#include "CustomMenuPanel.h"  // themeBg()/themeText()/themeHover()/themeBorder() — для темизации контекстного меню
#include "BookmarksBridge.h"
#include "BookmarksReorderHelper.h"
#include <QSqlQuery>
#include <QAction>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>        // Обязательно для qobject_cast<QToolButton*>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QUrl>
#include <QApplication>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>

namespace {

    // Единый стиль для всплывающего контекстного меню (Редактировать/Удалить/
    // Открыть), тёмная тема в цвет остального интерфейса.
    // БАГ (найден и исправлен): раньше здесь стоял menu.setStyleSheet(this->styleSheet()),
    // но stylesheet тулбара написан через селекторы "QToolBar {...}" / "QToolButton {...}" —
    // они никак не применяются к QMenu, так что вызов ничего не делал, и
    // контекстное меню всегда показывалось системным светлым стилем поверх
    // тёмного интерфейса. Теперь стиль явно описан под QMenu.
    QString contextMenuStyle()
    {
        return QString(
            "QMenu { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 4px; }"
            "QMenu::item { padding: 6px 20px; border-radius: 4px; }"
            "QMenu::item:selected { background-color: %4; }"
            "QMenu::separator { height: 1px; background: %3; margin: 4px 6px; }"
        ).arg(CustomMenuPanel::themeBg(), CustomMenuPanel::themeText(),
            CustomMenuPanel::themeBorder(), CustomMenuPanel::themeHover());
    }

} // namespace

BookmarksBar::BookmarksBar(MainWindow* mw, QWidget* parent)
    : QToolBar(parent), mainWindow(mw)
{
    setObjectName("bookmarksBar");

    
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setIconSize(QSize(16, 16));

    setFixedHeight(38);

    setStyleSheet(
        "QToolBar { "
        "   background: #1c2128; "
        "   border: none; "
        "   border-bottom: 1px solid #30363d; "
        "   spacing: 4px; "
        "   padding: 0px 5px; "
        "} "
        "QToolButton { "
        "   color: #8b949e; "
        "   background: transparent; "
        "   border-radius: 5px; "
        "   padding: -3px 4px; "
        "   font-size: 13px; "
        "   min-height: 30px; "
        "} "
        "QToolButton:hover { background: #30363d; color: white; }"
    );

    // Единая точка правды для мутаций (редактирование/удаление/перенос в
    // папку/сортировка) — тот же класс, что использует и страница
    // storm://bookmarks через QWebChannel. Здесь он вызывается напрямую,
    // синхронно, как обычный C++-объект (публичные слоты QObject можно
    // звать как обычные методы — WebChannel нужен только JS-стороне).
    m_bridge = new BookmarksBridge(mw, this);

    // Панель целиком принимает перетаскивание — Qt сам доносит drag-события
    // до ближайшего предка, который их принимает, если конкретная кнопка
    // (QToolButton) сама drop не принимает. Так позиция в dropEvent() уже
    // приходит в системе координат панели, что удобно для actionAt().
    setAcceptDrops(true);

    updateBookmarks();
}

void BookmarksBar::updateBookmarks() {
    clear();

    QMenu* bookmarksDataModel = mainWindow->findChild<QMenu*>("bookmarksMenu");
    if (!bookmarksDataModel) return;

    for (QAction* source : bookmarksDataModel->actions()) {
        if (source->isSeparator()) { addSeparator(); continue; }

        addAction(source);
        source->setToolTip(source->data().toString()); // URL закладки при наведении

        QToolButton* btn = qobject_cast<QToolButton*>(widgetForAction(source));
        if (btn) {
            installDragSupport(btn, source);

            btn->setContextMenuPolicy(Qt::CustomContextMenu);
            const QString bookmarkUrl = source->data().toString();
            const QString bookmarkTitle = source->text();

            connect(btn, &QToolButton::customContextMenuRequested, this,
                [this, btn, bookmarkUrl, bookmarkTitle](const QPoint& pos) {
                    QMenu menu(this);
                    menu.setStyleSheet(contextMenuStyle());

                    QAction* editAct = menu.addAction(u8"✏️ Редактировать");
                    connect(editAct, &QAction::triggered, this, [this, bookmarkUrl, bookmarkTitle]() {
                        bool okTitle = false;
                        QString newTitle = QInputDialog::getText(this, u8"Редактировать закладку",
                            u8"Название:", QLineEdit::Normal, bookmarkTitle, &okTitle);
                        if (!okTitle) return;

                        bool okUrl = false;
                        QString newUrl = QInputDialog::getText(this, u8"Редактировать закладку",
                            u8"URL:", QLineEdit::Normal, bookmarkUrl, &okUrl);
                        if (!okUrl) return;
                        newUrl = newUrl.trimmed();
                        if (newUrl.isEmpty()) return;

                        // Единая точка правды — тот же BookmarksBridge, что и
                        // у страницы storm://bookmarks, и у BookmarksWindow.
                        // Раньше здесь была отдельная копия того же SQL с
                        // отдельной же проверкой на дубликат URL — теперь
                        // один код, один источник возможных багов.
                        const QString err = m_bridge->editBookmark(bookmarkUrl, newTitle, newUrl);
                        if (!err.isEmpty()) {
                            QMessageBox::warning(this, u8"Ошибка", err);
                        }
                        // editBookmark() сам вызывает MainWindow::loadBookmarksIntoMenu() при успехе.
                        });

                    QAction* deleteAct = menu.addAction(u8"❌ Удалить закладку");
                    connect(deleteAct, &QAction::triggered, this, [this, bookmarkUrl]() {
                        const QString err = m_bridge->deleteBookmark(bookmarkUrl);
                        if (!err.isEmpty()) {
                            QMessageBox::warning(this, u8"Ошибка", err);
                        }
                        // deleteBookmark() сам вызывает MainWindow::loadBookmarksIntoMenu() при успехе.
                        });

                    menu.addSeparator();

                    // "Переместить выше/ниже" — та же перестановка соседей
                    // внутри папки, что делают кнопки ⬆️/⬇️ на странице
                    // storm://bookmarks (BookmarksBridge::moveBookmarkUp/Down).
                    // Дублирует drag&drop панели как более доступный способ
                    // для тех, кому неудобно тащить мышкой.
                    bool isFirst = true, isLast = true;
                    bookmarkFolderPosition(mainWindow->findChild<QMenu*>("bookmarksMenu"), bookmarkUrl, &isFirst, &isLast);

                    QAction* moveUpAct = menu.addAction(u8"⬆️ Переместить выше");
                    moveUpAct->setEnabled(!isFirst);
                    connect(moveUpAct, &QAction::triggered, this, [this, bookmarkUrl]() {
                        const QString err = m_bridge->moveBookmarkUp(bookmarkUrl);
                        if (!err.isEmpty()) QMessageBox::warning(this, u8"Ошибка", err);
                        mainWindow->loadBookmarksIntoMenu();
                        });

                    QAction* moveDownAct = menu.addAction(u8"⬇️ Переместить ниже");
                    moveDownAct->setEnabled(!isLast);
                    connect(moveDownAct, &QAction::triggered, this, [this, bookmarkUrl]() {
                        const QString err = m_bridge->moveBookmarkDown(bookmarkUrl);
                        if (!err.isEmpty()) QMessageBox::warning(this, u8"Ошибка", err);
                        mainWindow->loadBookmarksIntoMenu();
                        });

                    menu.addSeparator();
                    QAction* openAct = menu.addAction(u8"🔗 Открыть в новой вкладке");
                    connect(openAct, &QAction::triggered, this, [this, bookmarkUrl]() {
                        m_bridge->openBookmark(bookmarkUrl);
                        });

                    menu.exec(btn->mapToGlobal(pos));
                });
        }
    }
}

void BookmarksBar::installDragSupport(QToolButton* btn, QAction* action)
{
    // URL нужен в eventFilter(), чтобы знать, какую закладку тащим —
    // храним прямо на кнопке (dynamic property), а не в отдельной map:
    // кнопки в updateBookmarks() пересоздаются целиком при каждом
    // обновлении, так проще, чем следить за синхронизацией map.
    btn->setProperty("bookmarkUrl", action->data().toString());
    btn->installEventFilter(this);
}

bool BookmarksBar::eventFilter(QObject* watched, QEvent* event)
{
    if (QToolButton* btn = qobject_cast<QToolButton*>(watched)) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragStartPos = me->position().toPoint();
                m_dragCandidateUrl = btn->property("bookmarkUrl").toString();
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if ((me->buttons() & Qt::LeftButton) && !m_dragCandidateUrl.isEmpty()
                && (me->position().toPoint() - m_dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {

                const QString url = m_dragCandidateUrl;
                m_dragCandidateUrl.clear(); // не запускать повторно, пока кнопку не отпустят заново

                QDrag* drag = new QDrag(btn);
                QMimeData* mime = new QMimeData();
                mime->setText(url); // тот же payload, что и e.dataTransfer.setData('text/plain', ...) в JS
                drag->setMimeData(mime);
                drag->setPixmap(btn->grab());
                drag->exec(Qt::MoveAction);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            m_dragCandidateUrl.clear();
        }
        // Press/Release не съедаем — обычный клик по закладке должен и
        // дальше открывать её (QAction::triggered), а не только тащить.
        return QToolBar::eventFilter(watched, event);
    }

    return QToolBar::eventFilter(watched, event);
}

void BookmarksBar::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasText()) event->acceptProposedAction();
}

void BookmarksBar::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasText()) event->acceptProposedAction();
}

void BookmarksBar::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasText()) { return; }
    const QString draggedUrl = event->mimeData()->text();

    QAction* targetAction = actionAt(event->position().toPoint());
    if (!targetAction || targetAction->isSeparator()) { event->acceptProposedAction(); return; }

    const QString targetUrl = targetAction->data().toString();

    bool dropBefore = true;
    if (QToolButton* targetBtn = qobject_cast<QToolButton*>(widgetForAction(targetAction))) {
        const int localX = targetBtn->mapFrom(this, event->position().toPoint()).x();
        dropBefore = localX < targetBtn->width() / 2;
    }

    QMenu* bookmarksDataModel = mainWindow->findChild<QMenu*>("bookmarksMenu");
    const QString err = reorderDroppedBookmark(m_bridge, bookmarksDataModel, draggedUrl, targetUrl, dropBefore);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, u8"Ошибка", err);
    }
    mainWindow->loadBookmarksIntoMenu(); // перестроит и bookmarksMenu (уже не нужно), и саму панель

    event->acceptProposedAction();
}
