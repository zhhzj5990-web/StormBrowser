#include "Sidebar.h"
#include <QPoint>

Sidebar::Sidebar(QMainWindow* parent) : QWidget(parent), mainWindow(parent), activeIndex(-1) {
    setFixedWidth(50);
    setObjectName("sidebarManager");

    iconLayout = new QVBoxLayout(this);
    iconLayout->setContentsMargins(5, 15, 5, 15);
    iconLayout->setSpacing(6);
    iconLayout->setAlignment(Qt::AlignTop);

    // ВЫЕЗЖАЮЩАЯ ПАНЕЛЬ (привязываем к mainWindow, чтобы плавала поверх вкладок).
    // Если mainWindow не задан (nullptr), не отдаём QFrame без родителя —
    // иначе Qt превратит его в отдельное окно верхнего уровня.
    contentArea = new QFrame(mainWindow ? static_cast<QWidget*>(mainWindow) : this);
    contentArea->resize(0, 0);
    contentArea->hide();
    contentArea->setObjectName("sidebarContentArea");
    contentArea->setStyleSheet(
        "QFrame#sidebarContentArea {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  border-right: 1px solid rgba(255, 255, 255, 0.1);"
        "  border-bottom: 1px solid rgba(255, 255, 255, 0.1);"
        "  border-bottom-right-radius: 12px;"
        "}"
    );

    QVBoxLayout* contentLayout = new QVBoxLayout(contentArea);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    // Шапка панели с кнопкой закрытия
    QWidget* headerWidget = new QWidget(contentArea);
    headerWidget->setStyleSheet("background: transparent;");
    QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(10, 10, 10, 5);

    // ВАЖНО: не используем префикс u8"..." вместе с QString/QPushButton —
    // начиная с C++20 u8"..." имеет тип const char8_t*, а не const char*,
    // и не сможет неявно преобразоваться в QString (ошибка компиляции).
    // Файл сохранён в UTF-8, поэтому обычный строковый литерал уже
    // корректно интерпретируется Qt как UTF-8 через QString(const char*).
    closeBtn = new QPushButton("✕", headerWidget);
    closeBtn->setFixedSize(28, 28);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setObjectName("sidebarCloseBtn");
    closeBtn->setToolTip("Закрыть панель");
    closeBtn->setStyleSheet(
        "QPushButton#sidebarCloseBtn {"
        "  background: rgba(255, 255, 255, 0.1);"
        "  color: #ffffff;"
        "  border-radius: 14px;"
        "  font-size: 15px;"
        "  font-weight: bold;"
        "}"
        "QPushButton#sidebarCloseBtn:hover { background: #ff5f5f; color: white; }"
    );
    connect(closeBtn, &QPushButton::clicked, this, &Sidebar::closePanel);

    headerLayout->addStretch();
    headerLayout->addWidget(closeBtn);
    contentLayout->addWidget(headerWidget);

    stackedWidget = new QStackedWidget(contentArea);
    contentLayout->addWidget(stackedWidget);

    // Анимация ширины панели
    animation = new QPropertyAnimation(contentArea, "geometry", this);
    animation->setDuration(250);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QPropertyAnimation::finished, this, &Sidebar::onAnimationFinished);

    if (mainWindow) {
        mainWindow->installEventFilter(this);
    }
}

Sidebar::~Sidebar() {
    // Останавливаем анимацию первым делом: пока приложение закрывается,
    // таймер анимации мог бы тикнуть ещё раз и попытаться обратиться к
    // contentArea уже после того, как он начал уничтожаться.
    if (animation) {
        animation->stop();
    }

    // Дальше не хотим получать события mainWindow — он мог уже начать
    // закрываться, и нам незачем на это реагировать.
    if (mainWindow) {
        mainWindow->removeEventFilter(this);
    }

    // contentArea — НЕ ребёнок Sidebar, а сосед по mainWindow (см. конструктор),
    // поэтому Qt не гарантирует порядок его автоматического удаления
    // относительно Sidebar при разрушении mainWindow. Удаляем его сами,
    // детерминированно, прямо сейчас. contentArea — QPointer, так что если
    // mainWindow уже успел уничтожить его раньше нас, указатель уже nullptr,
    // и delete на нём ничего не делает (двойного удаления не будет).
    delete contentArea;
}

void Sidebar::setTabsWidget(QWidget* tabs) {
    tabsWidget = tabs;
    updateOverlayPosition();
}

QWidget* Sidebar::resolveAnchorWidget() const {
    if (tabsWidget) {
        return tabsWidget;
    }
    if (mainWindow) {
        // Как и в Python-версии (hasattr(main_window, "tabs")), пытаемся
        // найти виджет вкладок по имени объекта "tabs" — это даёт то же
        // поведение без необходимости знать конкретный класс MainWindow.
        if (QWidget* found = mainWindow->findChild<QWidget*>("tabs")) {
            return found;
        }
    }
    return nullptr;
}

void Sidebar::addItem(const QString& iconText, const QString& tooltip, QWidget* widget) {
    int index = stackedWidget->addWidget(widget);

    QPushButton* btn = new QPushButton(iconText, this);
    btn->setFixedSize(36, 36);
    btn->setToolTip(tooltip);
    btn->setCursor(Qt::PointingHandCursor);

    QFont font = btn->font();
    font.setFamily("Segoe UI Emoji");
    font.setPointSize(13);
    btn->setFont(font);

    updateButtonStyle(btn, false);
    connect(btn, &QPushButton::clicked, this, [this, index]() { togglePanel(index); });

    iconLayout->addWidget(btn);
    buttons.append(btn);
}

bool Sidebar::eventFilter(QObject* obj, QEvent* event) {
    if (obj == mainWindow && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        updateOverlayPosition();
    }
    return QWidget::eventFilter(obj, event);
}

void Sidebar::updateOverlayPosition() {
    // ВАЖНО: раньше здесь была проверка `|| !contentArea->isVisible()`, которая
    // обрывала пересчёт позиции. Но togglePanel() вызывает эту функцию ДО
    // contentArea->show(), то есть панель ещё скрыта — и позиция никогда не
    // обновлялась. Если окно двигали/ресайзили, пока панель была закрыта,
    // при следующем открытии она выскакивала на старом месте. В Python-версии
    // такой проверки нет вообще — убрали её и здесь.
    if (!mainWindow || !contentArea) return;

    QRect currentGeom = contentArea->geometry();

    if (QWidget* anchor = resolveAnchorWidget()) {
        // Привязываемся к виджету вкладок, а не к самому сайдбару — так же,
        // как это делает Python-версия (main_window.tabs.mapTo(...)).
        QPoint pos = anchor->mapTo(mainWindow, QPoint(0, 0));
        contentArea->setGeometry(pos.x(), pos.y(), currentGeom.width(), anchor->height());
    }
    else {
        // Запасной вариант: если виджет вкладок не найден и не задан явно
        // через setTabsWidget(...), привязываемся к правому краю сайдбара
        // (старое поведение). Работает корректно только если между сайдбаром
        // и вкладками нет отступов/spacing в layout'е.
        QPoint pos = this->mapTo(mainWindow, QPoint(this->width(), 0));
        contentArea->setGeometry(pos.x(), pos.y(), currentGeom.width(), this->height());
    }
}

void Sidebar::togglePanel(int index) {
    if (!contentArea) return;
    updateOverlayPosition();
    contentArea->raise();
    contentArea->show();

    QRect currentGeom = contentArea->geometry();

    if (activeIndex == index && currentGeom.width() > 0) {
        closePanel();
    }
    else {
        openPanel(index);
    }
}

void Sidebar::openPanel(int index) {
    if (!contentArea) return;
    int targetWidth = 340;
    QRect currentGeom = contentArea->geometry();

    for (int i = 0; i < buttons.size(); ++i) {
        updateButtonStyle(buttons[i], (i == index));
    }
    stackedWidget->setCurrentIndex(index);
    activeIndex = index;

    animation->stop();
    animation->setStartValue(currentGeom);
    animation->setEndValue(QRect(currentGeom.x(), currentGeom.y(), targetWidth, currentGeom.height()));
    animation->start();
}

void Sidebar::openItem(QWidget* widget) {
    if (!contentArea) return;

    // Находим индекс виджета в stackedWidget по указателю, который был передан
    // ранее в addItem(...) — так вызывающему коду (MainWindow) не нужно знать
    // числовой индекс вкладки, он просто передаёт указатель на свой виджет
    // (например, aiAssistantWidget).
    int index = stackedWidget->indexOf(widget);
    if (index < 0) {
        return; // виджет не был зарегистрирован через addItem(...)
    }

    updateOverlayPosition();
    contentArea->raise();
    contentArea->show();

    // Важно: всегда вызываем openPanel(), а не togglePanel() — эта функция
    // должна ГАРАНТИРОВАННО открывать панель. Если бы мы переиспользовали
    // togglePanel(), повторный вызов openItem() на уже открытой вкладке
    // закрыл бы панель вместо того, чтобы просто оставить её открытой.
    openPanel(index);
}

void Sidebar::closePanel() {
    if (!contentArea) return;
    QRect currentGeom = contentArea->geometry();
    animation->stop();
    animation->setStartValue(currentGeom);
    animation->setEndValue(QRect(currentGeom.x(), currentGeom.y(), 0, currentGeom.height()));
    animation->start();

    for (QPushButton* btn : buttons) {
        updateButtonStyle(btn, false);
    }
    activeIndex = -1;
}

void Sidebar::onAnimationFinished() {
    if (!contentArea) return;
    if (contentArea->width() == 0) {
        contentArea->hide();
    }
}

void Sidebar::updateButtonStyle(QPushButton* btn, bool isActive) {
    if (isActive) {
        btn->setStyleSheet("background-color: rgba(255, 255, 255, 0.12); border: 1px solid rgba(255, 255, 255, 0.2); border-radius: 8px; margin: 0px;");
    }
    else {
        btn->setStyleSheet(
            "QPushButton { background: transparent; border: none; border-radius: 8px; margin: 0px; }"
            "QPushButton:hover { background-color: rgba(255, 255, 255, 0.08); }"
        );
    }
}