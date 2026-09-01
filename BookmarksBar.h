#pragma once
#include <QToolBar>
#include <QPoint>
#include <QString>

// Предварительное объявление (вместо проблемного #include "MainWindow.h")
class MainWindow;
class BookmarksBridge;
class QAction;
class QToolButton;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

class BookmarksBar : public QToolBar {
    Q_OBJECT

public:
    explicit BookmarksBar(MainWindow* mw, QWidget* parent = nullptr);
    void updateBookmarks();

protected:
    // Приём перетаскивания при сбросе закладки на панель. Запускается
    // перетаскивание не здесь, а с конкретной кнопки — см. eventFilter()
    // и installDragSupport() в .cpp; тот же паттерн, что на странице
    // storm://bookmarks (BookmarksPageHtml.h), только на стороне C++.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void installDragSupport(QToolButton* btn, QAction* action);

    MainWindow* mainWindow;
    BookmarksBridge* m_bridge = nullptr;

    // Состояние "потенциального" перетаскивания — накапливается между
    // MouseButtonPress и MouseMove на кнопке закладки (см. eventFilter()).
    QPoint m_dragStartPos;
    QString m_dragCandidateUrl;
};
