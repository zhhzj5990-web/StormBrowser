#include "CustomMenuPanel.h"
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QScreen>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QHideEvent>

QString CustomMenuPanel::s_bg = "#1a1f2b";
QString CustomMenuPanel::s_text = "#eef3ff";
QString CustomMenuPanel::s_hover = "#2d374f";
QString CustomMenuPanel::s_border = "rgba(255, 255, 255, 0.10)";
QString CustomMenuPanel::s_accent = "#6e8cff";

void CustomMenuPanel::setThemeColors(const QString& bg, const QString& text, const QString& hover,
    const QString& border, const QString& accent)
{
    s_bg = bg;
    s_text = text;
    s_hover = hover;
    s_border = border;
    s_accent = accent;
}

QString CustomMenuPanel::themeBg() { return s_bg; }
QString CustomMenuPanel::themeText() { return s_text; }
QString CustomMenuPanel::themeHover() { return s_hover; }
QString CustomMenuPanel::themeBorder() { return s_border; }
QString CustomMenuPanel::themeAccent() { return s_accent; }

CustomMenuPanel::CustomMenuPanel(QWidget* parent)
    : QWidget(parent)
{
    contentLayout = new QVBoxLayout(this);
    contentLayout->setContentsMargins(6, 6, 6, 6);
    contentLayout->setSpacing(1);
    contentLayout->setAlignment(Qt::AlignTop);

    // КЛЮЧЕВОЙ ФИКС растягивания: SetMinAndMaxSize привязывает min/max размер
    // этого QWidget напрямую к layout()->sizeHint(). Без этого constraint виджет
    // после resize() на большую высоту (когда было открыто подменю) никогда сам
    // не сжимается обратно — Qt оставляет ему старый большой размер, а лишнее
    // пространство раскладка распределяет между видимыми пунктами (отсюда и
    // "дыры" между всеми пунктами меню на скриншоте, а не только у подменю).
    contentLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

    setStyleSheet("CustomMenuPanel { border-radius: 10px; }");
}

void CustomMenuPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // Теперь любая панель (и корень, и боковое подменю) — самостоятельное
    // всплывающее окно, поэтому фон рисует каждая из них сама за себя.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(rect(), 10, 10);
    painter.fillPath(path, QColor(s_bg));
    painter.setPen(QPen(QColor(s_border), 1));
    painter.drawPath(path);
}

QPushButton* CustomMenuPanel::makeRowButton(const QString& text)
{
    QPushButton* btn = new QPushButton(text, this);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFlat(true);

    // Пункты меню не должны забирать клавиатурный фокус — иначе клик по кнопке
    // (например, чтобы открыть подменю "Закладки") оставляет на ней системную
    // focus-рамку, которая не всегда чисто убирается при переходе к другой
    // кнопке (особенно теперь, когда подменю — отдельное окно). Из-за этого
    // две кнопки визуально "подсвечивались синим" одновременно. NoFocus
    // полностью убирает эту рамку — как и в обычных меню, где пункты не
    // получают фокус, а только подсвечиваются по наведению.
    btn->setFocusPolicy(Qt::NoFocus);

    btn->setStyleSheet(QString(
        "QPushButton { text-align: left; padding: 7px 10px; border: none; border-radius: 6px; "
        "color: %1; background: transparent; font-size: 13px; outline: none; }"
        "QPushButton:hover, QPushButton:pressed { background-color: %2; }"
        "QPushButton:focus { border: none; outline: none; }"
    ).arg(s_text, s_hover));
    return btn;
}

// Закрывает текущее открытое дочернее подменю (если оно есть), которое само
// рекурсивно закроет свою собственную открытую цепочку через hideEvent().
void CustomMenuPanel::closeFlyoutChain()
{
    if (openChildFlyout) {
        CustomMenuPanel* child = openChildFlyout;
        openChildFlyout = nullptr;
        child->hide(); // вызовет child->hideEvent() -> child сам закроет свою цепочку
    }
}

// Срабатывает и при явном hide()/closeRoot() по цепочке, и когда Qt сам
// закрывает Qt::Popup окно по клику мимо него (стандартное поведение,
// на котором и держатся каскадные подменю у настоящих QMenu).
void CustomMenuPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);

    // Если у самого себя было открыто дочернее подменю следующего уровня — закрыть и его.
    closeFlyoutChain();

    // Сообщаем родительской панели, что её дочернее подменю больше не открыто,
    // чтобы состояние openChildFlyout не разъезжалось с реальностью (иначе
    // повторный клик по тому же пункту решит, что подменю всё ещё открыто).
    if (parentPanel && parentPanel->openChildFlyout == this) {
        parentPanel->openChildFlyout = nullptr;
    }
}

void CustomMenuPanel::closeRoot()
{
    CustomMenuPanel* target = rootPopup ? rootPopup : this;
    target->close(); // hide() внутри close() вызовет hideEvent() -> закроет всю цепочку подменю
}

void CustomMenuPanel::closeWholeMenu()
{
    closeRoot();
}

void CustomMenuPanel::addAction(const QString& text, std::function<void()> onClick, bool closeMenu,
    std::function<void(const QPoint&)> onContextMenu)
{
    QPushButton* btn = makeRowButton(text);
    contentLayout->addWidget(btn);
    connect(btn, &QPushButton::clicked, this, [this, onClick, closeMenu]() {
        if (closeMenu) closeRoot();
        if (onClick) onClick();
        });

    if (onContextMenu) {
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QPushButton::customContextMenuRequested, this,
            [btn, onContextMenu](const QPoint& pos) {
                onContextMenu(btn->mapToGlobal(pos));
            });
    }
}

void CustomMenuPanel::addCheckable(const QString& text, bool checked,
    std::function<void(bool)> onToggle, bool closeMenu)
{
    QPushButton* btn = makeRowButton((checked ? QString(u8"✓  ") : QString(u8"    ")) + text);
    contentLayout->addWidget(btn);
    connect(btn, &QPushButton::clicked, this, [this, btn, text, onToggle, closeMenu]() {
        bool wasChecked = btn->text().startsWith(u8"✓");
        bool newChecked = !wasChecked;
        btn->setText((newChecked ? QString(u8"✓  ") : QString(u8"    ")) + text);
        if (onToggle) onToggle(newChecked);
        if (closeMenu) closeRoot();
        });
}

void CustomMenuPanel::addRadioGroup(const QVector<QPair<QString, QString>>& idsAndLabels,
    const QString& currentId,
    std::function<void(QString)> onSelect)
{
    for (const auto& pair : idsAndLabels) {
        const QString id = pair.first;
        const QString label = pair.second;
        bool isCurrent = (id == currentId);
        QPushButton* btn = makeRowButton((isCurrent ? QString(u8"●  ") : QString(u8"○  ")) + label);
        contentLayout->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, id, onSelect]() {
            closeRoot();
            if (onSelect) onSelect(id);
            });
    }
}

void CustomMenuPanel::addSeparator()
{
    QFrame* line = new QFrame(this);
    line->setFixedHeight(1);
    line->setStyleSheet(QString("background-color: %1; margin: 4px 6px;").arg(s_border));
    contentLayout->addWidget(line);
}

void CustomMenuPanel::addSectionLabel(const QString& text)
{
    QLabel* lbl = new QLabel(text.toUpper(), this);
    lbl->setStyleSheet(QString(
        "color: %1; font-size: 10px; font-weight: bold; "
        "padding: 6px 10px 2px 10px; background: transparent;"
    ).arg(s_accent));
    contentLayout->addWidget(lbl);
}

void CustomMenuPanel::addCustomWidget(QWidget* widget)
{
    widget->setParent(this);
    contentLayout->addWidget(widget);
}

CustomMenuPanel* CustomMenuPanel::addSubmenu(const QString& text)
{
    // Стрелка всегда указывает вбок (▸) — подменю больше не разворачивается
    // вниз аккордеоном, а открывается отдельной панелью сбоку, поэтому
    // направление стрелки не меняется от того, открыто оно или нет.
    QPushButton* header = makeRowButton(text + u8"   ▸");
    contentLayout->addWidget(header);

    CustomMenuPanel* child = new CustomMenuPanel(this);
    child->rootPopup = rootPopup ? rootPopup : this;
    child->parentPanel = this;

    // Подменю — самостоятельное всплывающее окно (как и корневой popup()),
    // а не виджет внутри текущего contentLayout, поэтому НЕ добавляем его в layout.
    child->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    child->setAttribute(Qt::WA_TranslucentBackground);

    connect(header, &QPushButton::clicked, this, [this, header, child]() {
        if (openChildFlyout == child) {
            // Повторный клик по тому же пункту — закрыть подменю.
            child->hide();
            return;
        }
        if (openChildFlyout) {
            openChildFlyout->hide();
        }
        openChildFlyout = child;
        child->showFlyout(header);
        });

    return child;
}

void CustomMenuPanel::popup(QWidget* anchor)
{
    rootPopup = this;
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    setFixedWidth(300);
    adjustSize();

    QScreen* scr = anchor->screen() ? anchor->screen() : QGuiApplication::primaryScreen();
    QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1920, 1080);

    QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height() + 4));

    int w = width();
    int h = height();
    if (pos.x() + w > avail.right())  pos.setX(avail.right() - w - 8);
    if (pos.y() + h > avail.bottom()) pos.setY(avail.bottom() - h - 8);
    if (pos.x() < avail.left())       pos.setX(avail.left() + 8);

    move(pos);
    show();
    raise();
    activateWindow();
}

// Показывает это подменю сбоку от пункта header, который его открыл.
// По умолчанию — справа от родительского меню, на уровне пункта header;
// если справа не хватает места на экране — открывается слева от родителя
// (так же ведут себя вложенные подменю в обычных десктопных приложениях).
void CustomMenuPanel::showFlyout(QWidget* header)
{
    setFixedWidth(260);
    adjustSize();

    QScreen* scr = header->screen() ? header->screen() : QGuiApplication::primaryScreen();
    QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1920, 1080);

    int w = width();
    int h = height();

    QPoint topRight = header->mapToGlobal(QPoint(header->width(), 0));
    QPoint pos = topRight + QPoint(2, -6);

    if (pos.x() + w > avail.right()) {
        QPoint topLeft = header->mapToGlobal(QPoint(0, 0));
        pos.setX(topLeft.x() - w - 2);
    }
    if (pos.x() < avail.left())       pos.setX(avail.left() + 8);
    if (pos.y() + h > avail.bottom()) pos.setY(avail.bottom() - h - 8);
    if (pos.y() < avail.top())        pos.setY(avail.top() + 8);

    move(pos);
    show();
    raise();
    activateWindow();
}