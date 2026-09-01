#include "BookmarksWindow.h"
#include "MainWindow.h"
#include "CustomMenuPanel.h"
#include "BookmarksBridge.h"
#include "BookmarksReorderHelper.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QMenu>
#include <QAction>
#include <QPointer>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QUrl>
#include <QSize>
#include <QApplication>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>

namespace {

    // Тёмный стиль для контекстного меню строки закладки (Редактировать/
    // Удалить/Открыть) — тот же подход, что и в BookmarksBar.cpp, чтобы
    // всплывающее меню не выбивалось системным светлым видом из общей
    // тёмной темы.
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

    QString rowButtonStyle()
    {
        return QString(
            "QPushButton { text-align: left; padding: 6px 10px; border: none; border-radius: 6px; "
            "color: %1; background: transparent; font-size: 13px; outline: none; }"
            "QPushButton:hover, QPushButton:pressed { background-color: %2; }"
            "QPushButton:focus { border: none; outline: none; }"
        ).arg(CustomMenuPanel::themeText(), CustomMenuPanel::themeHover());
    }

} // namespace

BookmarksWindow::BookmarksWindow(MainWindow* mw, QWidget* parent)
    : QWidget(parent), m_mw(mw)
{
    // Обычное окно верхнего уровня (не Qt::Popup) — не закрывается при
    // потере фокуса/клике мимо, можно спокойно кликать по нескольким
    // закладкам подряд.
    setWindowFlag(Qt::Window, true);
    setWindowTitle(u8"Закладки");
    setAttribute(Qt::WA_DeleteOnClose);
    resize(360, 560);

    setStyleSheet(QString("BookmarksWindow { background-color: %1; }").arg(CustomMenuPanel::themeBg()));

    // Единая точка правды для мутаций — тот же класс, что использует и
    // страница storm://bookmarks, и BookmarksBar. Вызывается напрямую как
    // обычный C++-объект (публичные слоты можно звать как обычные методы).
    m_bridge = new BookmarksBridge(mw, this);

    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    buildHeader(outer);
    buildList(outer);
}

QWidget* BookmarksWindow::makeHeaderButton(const QString& text, std::function<void()> onClick)
{
    QPushButton* btn = new QPushButton(text, this);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFlat(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setStyleSheet(rowButtonStyle());
    connect(btn, &QPushButton::clicked, this, [onClick]() {
        if (onClick) onClick();
        });
    return btn;
}

void BookmarksWindow::buildHeader(QVBoxLayout* outer)
{
    QLabel* title = new QLabel(u8"⭐ Закладки", this);
    title->setStyleSheet(QString(
        "color: %1; font-size: 15px; font-weight: 600; padding: 14px 14px 6px 14px;"
    ).arg(CustomMenuPanel::themeText()));
    outer->addWidget(title);

    outer->addWidget(makeHeaderButton(u8"⭐ Добавить текущую страницу", [this]() {
        m_mw->addCurrentBookmark();
        refreshList();
        }));
    outer->addWidget(makeHeaderButton(u8"📋 Открыть импортированные вкладки", [this]() {
        m_mw->openImportedTabs();
        }));
    outer->addWidget(makeHeaderButton(u8"📥 Импорт вкладок (в отдельное окно)", [this]() {
        m_mw->importTabs();
        }));
    outer->addWidget(makeHeaderButton(u8"📤 Экспорт вкладок (HTML)", [this]() {
        m_mw->exportTabs();
        }));
    outer->addWidget(makeHeaderButton(u8"🧹 Очистить все закладки", [this]() {
        m_mw->clearBookmarks();
        refreshList();
        }));

    QFrame* line = new QFrame(this);
    line->setFixedHeight(1);
    line->setStyleSheet(QString("background-color: %1; margin: 6px 10px;").arg(CustomMenuPanel::themeBorder()));
    outer->addWidget(line);
}

void BookmarksWindow::buildList(QVBoxLayout* outer)
{
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent; border: none;");

    QWidget* listWidget = new QWidget(scroll);
    listWidget->setStyleSheet("background: transparent;");
    m_listLayout = new QVBoxLayout(listWidget);
    m_listLayout->setContentsMargins(6, 6, 6, 6);
    m_listLayout->setSpacing(1);
    m_listLayout->setAlignment(Qt::AlignTop);

    // Список целиком принимает перетаскивание строк — так же, как панель
    // (BookmarksBar) принимает его на уровне всего тулбара, а не отдельных
    // кнопок. Перехватываем через eventFilter(), а не наследуем отдельный
    // класс виджета — ради него одного заводить новый .h/.cpp избыточно.
    listWidget->setAcceptDrops(true);
    listWidget->installEventFilter(this);
    m_listWidget = listWidget;

    scroll->setWidget(listWidget);
    outer->addWidget(scroll, 1);

    refreshList();
}

QWidget* BookmarksWindow::makeBookmarkRow(QAction* action)
{
    // MainWindow::loadBookmarksIntoMenu() теперь всегда выставляет иконку
    // синхронно (заглушка, пока не пришёл настоящий favicon) — action->icon()
    // здесь никогда не пустой, отдельная заглушка тут больше не нужна.
    QPushButton* btn = new QPushButton(action->text(), this);
    btn->setIcon(action->icon());
    btn->setIconSize(QSize(18, 18));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFlat(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setStyleSheet(rowButtonStyle());

    const QString url = action->data().toString();
    const QString titleText = action->text();
    btn->setToolTip(url);

    installDragSupport(btn, url);

    // БАГ (найден и исправлен): захват "сырого" QAction* в лямбде опасен —
    // MainWindow::loadBookmarksIntoMenu() пересоздаёт (удаляет и создаёт
    // заново) все QAction закладок при каждом обновлении. Если это окно
    // осталось открытым со старым списком, клик по устаревшей строке мог бы
    // обратиться к уже удалённому объекту (use-after-free). QPointer сам
    // обнуляется, когда QAction удаляется — клик по неактуальной строке
    // просто ничего не сделает вместо краша.
    QPointer<QAction> actionPtr(action);
    connect(btn, &QPushButton::clicked, this, [actionPtr]() {
        if (actionPtr) actionPtr->trigger();
        });

    // Favicon грузится асинхронно и может обновиться (с заглушки на
    // настоящую иконку) уже ПОСЛЕ того, как эта строка отрисована —
    // подхватываем иконку живьём через QAction::changed().
    connect(action, &QAction::changed, btn, [btn, actionPtr]() {
        if (actionPtr) btn->setIcon(actionPtr->icon());
        });

    // Правый клик — редактировать/удалить/открыть/переместить, то же
    // самое, что и на панели закладок (BookmarksBar), только доступное и
    // здесь — единый стандарт для обоих мест.
    btn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btn, &QPushButton::customContextMenuRequested, this,
        [this, btn, url, titleText](const QPoint& pos) {
            QMenu menu(this);
            menu.setStyleSheet(contextMenuStyle());

            QAction* editAct = menu.addAction(u8"✏️ Редактировать");
            connect(editAct, &QAction::triggered, this, [this, url, titleText]() {
                bool okTitle = false;
                QString newTitle = QInputDialog::getText(this, u8"Редактировать закладку",
                    u8"Название:", QLineEdit::Normal, titleText, &okTitle);
                if (!okTitle) return;

                bool okUrl = false;
                QString newUrl = QInputDialog::getText(this, u8"Редактировать закладку",
                    u8"URL:", QLineEdit::Normal, url, &okUrl);
                if (!okUrl) return;
                newUrl = newUrl.trimmed();
                if (newUrl.isEmpty()) return;

                // Единая точка правды — тот же BookmarksBridge, что и у
                // страницы storm://bookmarks, и у BookmarksBar. Раньше
                // здесь была отдельная копия того же SQL с отдельной же
                // проверкой на дубликат URL — теперь один код, один
                // источник возможных багов.
                const QString err = m_bridge->editBookmark(url, newTitle, newUrl);
                if (!err.isEmpty()) {
                    QMessageBox::warning(this, u8"Ошибка", err);
                    return;
                }
                // editBookmark() сам вызывает MainWindow::loadBookmarksIntoMenu(); окну остаётся перерисовать список.
                refreshList();
                });

            QAction* deleteAct = menu.addAction(u8"❌ Удалить закладку");
            connect(deleteAct, &QAction::triggered, this, [this, url]() {
                const QString err = m_bridge->deleteBookmark(url);
                if (!err.isEmpty()) {
                    QMessageBox::warning(this, u8"Ошибка", err);
                    return;
                }
                refreshList();
                });

            menu.addSeparator();

            // "Переместить выше/ниже" — та же перестановка соседей внутри
            // папки, что и кнопки ⬆️/⬇️ на странице storm://bookmarks, и то
            // же, что теперь есть в контекстном меню BookmarksBar.
            bool isFirst = true, isLast = true;
            bookmarkFolderPosition(m_mw->findChild<QMenu*>("bookmarksMenu"), url, &isFirst, &isLast);

            QAction* moveUpAct = menu.addAction(u8"⬆️ Переместить выше");
            moveUpAct->setEnabled(!isFirst);
            connect(moveUpAct, &QAction::triggered, this, [this, url]() {
                const QString err = m_bridge->moveBookmarkUp(url);
                if (!err.isEmpty()) QMessageBox::warning(this, u8"Ошибка", err);
                m_mw->loadBookmarksIntoMenu();
                refreshList();
                });

            QAction* moveDownAct = menu.addAction(u8"⬇️ Переместить ниже");
            moveDownAct->setEnabled(!isLast);
            connect(moveDownAct, &QAction::triggered, this, [this, url]() {
                const QString err = m_bridge->moveBookmarkDown(url);
                if (!err.isEmpty()) QMessageBox::warning(this, u8"Ошибка", err);
                m_mw->loadBookmarksIntoMenu();
                refreshList();
                });

            menu.addSeparator();
            QAction* openAct = menu.addAction(u8"🔗 Открыть в новой вкладке");
            connect(openAct, &QAction::triggered, this, [this, url]() {
                m_bridge->openBookmark(url);
                });

            menu.exec(btn->mapToGlobal(pos));
        });

    return btn;
}

void BookmarksWindow::installDragSupport(QPushButton* btn, const QString& url)
{
    // URL нужен в eventFilter(), чтобы знать, какую закладку тащим —
    // храним прямо на кнопке (dynamic property), а не в отдельной map:
    // refreshList() пересоздаёт все строки целиком при каждом обновлении.
    btn->setProperty("bookmarkUrl", url);
    btn->installEventFilter(this);
}

bool BookmarksWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (QPushButton* btn = qobject_cast<QPushButton*>(watched)) {
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
                mime->setText(url);
                drag->setMimeData(mime);
                drag->setPixmap(btn->grab());
                drag->exec(Qt::MoveAction);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            m_dragCandidateUrl.clear();
        }
        // Press/Release не съедаем — обычный клик по строке должен и
        // дальше открывать закладку.
        return QWidget::eventFilter(watched, event);
    }

    if (watched == m_listWidget) {
        if (event->type() == QEvent::DragEnter) {
            auto* e = static_cast<QDragEnterEvent*>(event);
            if (e->mimeData()->hasText()) e->acceptProposedAction();
            return true;
        }
        if (event->type() == QEvent::DragMove) {
            auto* e = static_cast<QDragMoveEvent*>(event);
            if (e->mimeData()->hasText()) e->acceptProposedAction();
            return true;
        }
        if (event->type() == QEvent::Drop) {
            handleListDrop(static_cast<QDropEvent*>(event));
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void BookmarksWindow::handleListDrop(QDropEvent* event)
{
    if (!event->mimeData()->hasText()) return;
    const QString draggedUrl = event->mimeData()->text();

    // Ищем строку, над которой отпустили — список одноколоночный, поэтому
    // достаточно сравнить позицию сброса с геометрией каждой кнопки.
    const QPoint dropPos = event->position().toPoint();
    QPushButton* targetBtn = nullptr;
    for (int i = 0; i < m_listLayout->count(); ++i) {
        QWidget* w = m_listLayout->itemAt(i)->widget();
        QPushButton* btn = qobject_cast<QPushButton*>(w);
        if (!btn) continue; // разделитель (QFrame) — пропускаем
        if (btn->geometry().contains(dropPos)) { targetBtn = btn; break; }
    }
    if (!targetBtn) { event->acceptProposedAction(); return; }

    const QString targetUrl = targetBtn->property("bookmarkUrl").toString();
    const bool dropBefore = dropPos.y() < (targetBtn->y() + targetBtn->height() / 2);

    QMenu* bookmarksDataModel = m_mw->findChild<QMenu*>("bookmarksMenu");
    const QString err = reorderDroppedBookmark(m_bridge, bookmarksDataModel, draggedUrl, targetUrl, dropBefore);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, u8"Ошибка", err);
    }
    m_mw->loadBookmarksIntoMenu();
    refreshList();

    event->acceptProposedAction();
}

void BookmarksWindow::refreshList()
{
    if (!m_listLayout) return;

    QLayoutItem* item;
    while ((item = m_listLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    QMenu* bookmarksDataModel = m_mw->findChild<QMenu*>("bookmarksMenu");
    if (!bookmarksDataModel) return;

    const QList<QAction*> actions = bookmarksDataModel->actions();
    if (actions.isEmpty()) {
        QLabel* empty = new QLabel(u8"Пока нет закладок", this);
        empty->setStyleSheet(QString("color: %1; padding: 10px;").arg(CustomMenuPanel::themeText()));
        m_listLayout->addWidget(empty);
        return;
    }

    for (QAction* a : actions) {
        if (a->isSeparator()) {
            QFrame* line = new QFrame(this);
            line->setFixedHeight(1);
            line->setStyleSheet(QString("background-color: %1; margin: 4px 6px;").arg(CustomMenuPanel::themeBorder()));
            m_listLayout->addWidget(line);
            continue;
        }
        m_listLayout->addWidget(makeBookmarkRow(a));
    }
}
