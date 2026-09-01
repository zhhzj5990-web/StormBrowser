#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFrame>
#include <QStackedWidget>
#include <QPropertyAnimation>
#include <QMainWindow>
#include <QEvent>
#include <QPointer>

class Sidebar : public QWidget {
    Q_OBJECT
public:
    explicit Sidebar(QMainWindow* parent = nullptr);
    // См. комментарий над реализацией: contentArea намеренно припаркован не к
    // Sidebar, а к mainWindow (чтобы панель не обрезалась 50-пиксельной
    // шириной сайдбара), поэтому Qt не гарантирует порядок его автоматического
    // удаления относительно самого Sidebar при закрытии окна/приложения.
    // Явный деструктор детерминированно останавливает анимацию, снимает
    // eventFilter с mainWindow и удаляет contentArea сам, чтобы исключить
    // краш на "чтении по адресу 0x0" (обращение к уже уничтоженному виджету
    // из eventFilter/анимации во время закрытия окна).
    ~Sidebar() override;
    void addItem(const QString& iconText, const QString& tooltip, QWidget* widget);

    // Явно указать виджет (например, область вкладок), к которому должна
    // "прилипать" выезжающая панель. Если не задано, Sidebar попробует
    // найти дочерний объект mainWindow с objectName "tabs" (аналог
    // hasattr(main_window, "tabs") в Python-версии), а если и его нет —
    // использует свой собственный правый край (старое поведение).
    void setTabsWidget(QWidget* tabs);

    // Гарантированно ОТКРЫВАЕТ панель с указанным виджетом (не переключает).
    // В отличие от togglePanel() (который вызывается по клику на иконку и поэтому
    // обязан уметь закрывать панель повторным нажатием), этот метод нужен для
    // программного открытия панели извне — например, из контекстного меню
    // ИИ-ассистента (MainWindow::processAiAction). Там всегда нужно ОТКРЫТЬ панель
    // с ответом ИИ, а не случайно закрыть её, если она уже была открыта на этом виджете.
    // Если widget не был добавлен через addItem(...), ничего не делает.
    void openItem(QWidget* widget);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void togglePanel(int index);
    void closePanel();
    void onAnimationFinished();

private:
    void updateOverlayPosition();
    void updateButtonStyle(QPushButton* btn, bool isActive);
    QWidget* resolveAnchorWidget() const;
    // Общая логика раскрытия панели на нужный index (используется и togglePanel(),
    // и публичным openItem()), чтобы не дублировать код анимации/стилей кнопок.
    void openPanel(int index);

    QMainWindow* mainWindow;
    // QPointer, а не обычный указатель: tabsWidget нам не принадлежит (его
    // передаёт снаружи setTabsWidget()) и может быть уничтожен раньше
    // Sidebar — QPointer сам обнулится в этот момент вместо того, чтобы
    // остаться висящим указателем.
    QPointer<QWidget> tabsWidget;
    QVBoxLayout* iconLayout;

    // Всплывающая панель
    // QPointer: contentArea припаркован к mainWindow, а не к Sidebar (см.
    // конструктор), так что порядок его удаления Qt'ом относительно Sidebar
    // не гарантирован — см. комментарий у деструктора в этом файле.
    QPointer<QFrame> contentArea;
    QStackedWidget* stackedWidget;
    QPropertyAnimation* animation;
    QPushButton* closeBtn;

    QList<QPushButton*> buttons;
    int activeIndex;
};