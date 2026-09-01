#pragma once
#include <QWidget>
#include <QPoint>
#include <QString>
#include <functional>

class MainWindow;
class BookmarksBridge;
class QVBoxLayout;
class QAction;
class QPushButton;
class QDropEvent;

// Окно "Закладки" — раньше это было боковое подменю (флайаут) внутри
// гамбургер-меню (CustomMenuPanel::addSubmenu), которое закрывалось при
// любом клике мимо него. Теперь это самостоятельное окно верхнего уровня
// (обычные Qt::Window флаги, а не Qt::Popup), которое остаётся открытым,
// пока пользователь сам его не закроет — так удобнее открывать несколько
// закладок подряд. Под каждой закладкой показывается иконка сайта
// (favicon), которая подгружается в MainWindow::loadBookmarksIntoMenu()
// асинхронно, поэтому иконка каждой строки обновляется "живьём", когда
// приходит с сети.
//
// Список показывается в том же порядке, что и на странице
// storm://bookmarks (см. MainWindow::loadBookmarksIntoMenu()), и строки
// можно перетаскивать друг относительно друга мышкой — тот же единый
// стандарт, что и на панели закладок (BookmarksBar) и на самой странице.
class BookmarksWindow : public QWidget {
    Q_OBJECT
public:
    explicit BookmarksWindow(MainWindow* mw, QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildHeader(QVBoxLayout* outer);
    void buildList(QVBoxLayout* outer);
    void refreshList();
    QWidget* makeBookmarkRow(QAction* action);
    QWidget* makeHeaderButton(const QString& text, std::function<void()> onClick);
    void installDragSupport(QPushButton* btn, const QString& url);
    void handleListDrop(QDropEvent* event);

    MainWindow* m_mw;
    BookmarksBridge* m_bridge = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QWidget* m_listWidget = nullptr;

    // Состояние "потенциального" перетаскивания — см. installDragSupport()/
    // eventFilter(), тот же паттерн, что в BookmarksBar.
    QPoint m_dragStartPos;
    QString m_dragCandidateUrl;
};
