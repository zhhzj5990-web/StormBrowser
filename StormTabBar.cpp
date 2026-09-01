#include "StormTabBar.h"

#include <QTabWidget>
#include <QMouseEvent>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QFont>
#include <QPolygon>
#include <cmath>

namespace {
    // Не используем M_PI (не гарантирован стандартом, на MSVC требует
    // _USE_MATH_DEFINES до <cmath>) — свою константу надёжнее.
    constexpr double kPi = 3.14159265358979323846;
}

StormTabBar::StormTabBar(QWidget* parent) : QTabBar(parent) {
    // КЛЮЧЕВОЙ ФИКС: у QTabBar по умолчанию expanding == true — Qt сам
    // растягивает вкладки, чтобы заполнить всю свободную ширину панели,
    // когда открыто мало вкладок. Из-за этого tabRect(i) "плавал" в
    // зависимости от ширины окна и количества вкладок — крестик и "+"
    // считались верно ОТНОСИТЕЛЬНО tabRect(i), но сам tabRect(i) был не
    // тем, что мы задавали в tabSizeHint(). В Chrome/Brave вкладки НЕ
    // растягиваются — фиксированная ширина, прижаты к левому краю,
    // отключаем то же самое здесь.
    setExpanding(false);
}

QString StormTabBar::buildTabBarStyleSheet() {
    // Высчитываем место под крестик и отступы
    int rightPad = kCloseBtnSize + kCloseRightGap + kCloseTextGap;

    // МАГИЯ ЗДЕСЬ: реальная ширина контента = общая ширина минус все отступы и рамки (box-model)
    // Теперь логический tabRect и визуальная вкладка будут совпадать до пикселя!
    int cssWidth = kTabWidth - 10 - rightPad - 7;

    return QString(
        "QTabWidget::pane {"
        "   border: none;"
        "   border-top: 1px solid #30363d;"
        "}"
        "QTabBar::tab {"
        "   background: #1c2128;"
        "   color: #8b949e;"
        "   width: %2px;"
        "   min-width: %2px;"
        "   max-width: %2px;"
        "   border: 1px solid %3;"
        "   border-radius: %5px;"
        "   margin: 4px 4px 0px 4px;"
        "   text-align: left;"
        "   padding-left: 10px;"
        "   padding-right: %1px;"
        "}"
        "QTabBar::tab:selected {"
        "   background: #3d444d;"
        "   color: #ffffff;"
        "   border: 1px solid %4;"
        "   margin: 4px 4px 0px 4px;"
        "}"
        "QTabBar::tab:hover:!selected {"
        "   background: #2d333b;"
        "   border: 1px solid %4;"
        "}"
    )
        .arg(rightPad)                        // %1
        .arg(cssWidth)                        // %2
        .arg(kTabBorderColor)                 // %3
        .arg(kTabBorderColorSelected)         // %4
        .arg(kTabCornerRadius);               // %5
}

QSize StormTabBar::tabSizeHint(int index) const {
    QSize hint = QTabBar::tabSizeHint(index);
    hint.setHeight(kTabRowHeight);
    hint.setWidth(kTabWidth);
    return hint;
}

QSize StormTabBar::minimumTabSizeHint(int index) const {
    return tabSizeHint(index);
}

// ПОЧЕМУ КРЕСТИК И "+" РИСУЮТСЯ ВРУЧНУЮ, А НЕ ЧЕРЕЗ QSS/native close-button:
// Встроенный close-button у QTabBar Qt позиционирует через
// subElementRect(SE_TabBarTabRightButton) — этот прямоугольник Qt строит
// "встык" сразу после конца ОТРИСОВАННОГО (уже усечённого элайдом) текста
// вкладки, а не от правого края всей вкладки. subcontrol-position в QSS
// влияет только на то, ГДЕ ИКОНКА рисуется ВНУТРИ этого прямоугольника —
// сам прямоугольник не двигает. Вместо этого оба прямоугольника считаются
// напрямую от tabRect(i) — они ВСЕГДА строго там, где заданы константы
// выше, независимо от текста и от того, что придумает стиль.
QRect StormTabBar::closeButtonRect(int index) const {
    QRect r = tabRect(index);

    // r.center().y() - (kCloseBtnSize / 2) — это строгий центр.
    // Добавляем +2 пикселя, чтобы сместить крестик вниз. 
    // Если нужно еще ниже — ставь +3 или +4.
    int yPos = r.center().y() - (kCloseBtnSize / 2) + 2;

    return QRect(r.right() - kCloseRightGap - kCloseBtnSize,
        yPos, kCloseBtnSize, kCloseBtnSize);
}

QRect StormTabBar::plusButtonRect() const {
    int plusSize = 26; // Размер квадратной кнопки
    int leftGap = 8;   // Отступ от последней вкладки

    if (count() == 0) {
        return QRect(leftGap, (kTabRowHeight - plusSize) / 2, plusSize, plusSize);
    }
    QRect r = tabRect(count() - 1);
    return QRect(r.right() + leftGap, r.top() + (r.height() - plusSize) / 2, plusSize, plusSize);
}

QSize StormTabBar::sizeHint() const {
    QSize s = QTabBar::sizeHint();
    if (count() > 0) {
        s.setWidth(s.width() + 45); // Запас ширины панели под плюсик
    }
    return s;
}

// =========================================================================
// Анимации
// =========================================================================

void StormTabBar::startCloseFlash(QWidget* page) {
    if (!page) return;
    m_closeFlash[page] = 0.0;

    auto* anim = new QVariantAnimation(this);
    anim->setDuration(kCloseFlashMs);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::Linear);

    QPointer<QWidget> guardedPage(page);

    connect(anim, &QVariantAnimation::valueChanged, this, [this, guardedPage](const QVariant& v) {
        if (!guardedPage) return;
        m_closeFlash[guardedPage] = v.toDouble();
        update();
        });

    connect(anim, &QVariantAnimation::finished, this, [this, guardedPage, anim]() {
        m_closeFlash.remove(guardedPage);
        anim->deleteLater();
        update();

        if (!guardedPage) return; // страницу уже удалили откуда-то ещё — закрывать нечего

        auto* tabs = qobject_cast<QTabWidget*>(parentWidget());
        if (!tabs) return;
        const int liveIndex = tabs->indexOf(guardedPage);
        // Реальное закрытие — только теперь, ПОСЛЕ вспышки. liveIndex
        // пересчитан заново на случай, если индекс сдвинулся за время
        // анимации (закрыли/добавили другую вкладку).
        if (liveIndex >= 0 && liveIndex < count()) {
            emit tabCloseRequested(liveIndex);
        }
        });

    anim->start();
}

void StormTabBar::startGust() {
    auto* anim = new QVariantAnimation(this);
    anim->setDuration(kGustMs);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);

    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_gustProgress = v.toDouble();
        update();
        });
    connect(anim, &QVariantAnimation::finished, this, [this, anim]() {
        m_gustProgress = 0.0;
        anim->deleteLater();
        update();
        });
    anim->start();
}

void StormTabBar::paintCloseFlash(QPainter& painter, const QRect& tr, double progress) const {
    // Треугольная огибающая: 0 -> 1 -> 0, пик ровно на середине анимации —
    // вспышка нарастает и тут же гаснет, а не просто исчезает рывком.
    const double half = progress > 0.5 ? (progress - 0.5) : (0.5 - progress);
    const double intensity = 1.0 - half * 2.0;
    if (intensity <= 0.0) return;

    painter.save();

    // Лёгкая белая вспышка фона вкладки
    QColor flashBg(255, 255, 255);
    flashBg.setAlphaF(0.35 * intensity);
    painter.fillRect(tr, flashBg);

    // Зигзаг "молнии" наискось через вкладку
    const int x0 = tr.left() + static_cast<int>(tr.width() * 0.30);
    const int x1 = tr.left() + static_cast<int>(tr.width() * 0.70);
    const int yTop = tr.top() + 3;
    const int yBot = tr.bottom() - 3;
    const int midY1 = yTop + static_cast<int>((yBot - yTop) * 0.35);
    const int midY2 = yTop + static_cast<int>((yBot - yTop) * 0.65);

    QPolygon bolt;
    bolt << QPoint(x0, yTop)
        << QPoint((x0 + x1) / 2 + 6, midY1)
        << QPoint((x0 + x1) / 2 - 8, midY2)
        << QPoint(x1, yBot);

    // Сначала широкое голубоватое свечение под молнией...
    QColor glow(140, 210, 255);
    glow.setAlphaF(0.5 * intensity);
    QPen glowPen(glow, 5.0);
    glowPen.setJoinStyle(Qt::RoundJoin);
    glowPen.setCapStyle(Qt::RoundCap);
    painter.setPen(glowPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(bolt);

    // ...а затем сам яркий тёплый разряд поверх него.
    QColor boltColor(255, 244, 140);
    boltColor.setAlphaF(intensity);
    QPen boltPen(boltColor, 2.2);
    boltPen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(boltPen);
    painter.drawPolyline(bolt);

    painter.restore();
}

void StormTabBar::paintGust(QPainter& painter, const QRect& plusRect, double progress) const {
    const double alpha = std::sin(progress * kPi); // 0 -> 1 -> 0, плавный "порыв"
    if (alpha <= 0.0) return;

    painter.save();
    painter.setBrush(Qt::NoBrush);

    const int baseX = plusRect.center().x();
    const int travel = plusRect.width() + 14; // насколько уезжают штрихи вправо за всё время анимации

    const int laneOffsets[3] = { -6, 0, 6 }; // три "струи" ветра на разной высоте
    for (int i = 0; i < 3; ++i) {
        const int y = plusRect.center().y() + laneOffsets[i];
        const int x = baseX + static_cast<int>(travel * progress) - 4 + i * 3;
        const int laneAbs = laneOffsets[i] < 0 ? -laneOffsets[i] : laneOffsets[i];
        const int len = 10 - laneAbs; // средняя струя чуть длиннее крайних

        QColor streak(190, 225, 255);
        streak.setAlphaF(alpha * 0.8);
        QPen pen(streak, 2.0, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(x, y, x + len, y);
    }

    painter.restore();
}

// =========================================================================
// Отрисовка и события мыши
// =========================================================================

void StormTabBar::paintEvent(QPaintEvent* event) {
    QTabBar::paintEvent(event);

    auto* tabs = qobject_cast<QTabWidget*>(parentWidget());
    if (!tabs) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    for (int i = 0; i < count(); ++i) {
        QWidget* page = tabs->widget(i);
        if (!page) continue;

        // Цветная полоска категории
        const QString colorName = page->property("tabColor").toString();
        if (!colorName.isEmpty()) {
            QRect rect = tabRect(i);
            painter.fillRect(rect.x() + 10, rect.y() + 2, rect.width() - 20, 3, QColor(colorName));
        }

        // Крестик закрытия
        const QRect closeRect = closeButtonRect(i);
        const bool closeHovered = (i == m_hoveredCloseIndex);

        if (closeHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(kCloseColor));
            painter.drawEllipse(closeRect);
            painter.setPen(QPen(QColor(kCloseHoverGlyphColor), 1.4));
        }
        else {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(kCloseIdleColor), 1.4));
        }

        painter.drawLine(closeRect.left() + kCloseLineInset, closeRect.top() + kCloseLineInset,
            closeRect.right() - kCloseLineInset, closeRect.bottom() - kCloseLineInset);
        painter.drawLine(closeRect.right() - kCloseLineInset, closeRect.top() + kCloseLineInset,
            closeRect.left() + kCloseLineInset, closeRect.bottom() - kCloseLineInset);

        // Вспышка при закрытии
        const auto flashIt = m_closeFlash.constFind(page);
        if (flashIt != m_closeFlash.constEnd()) {
            paintCloseFlash(painter, tabRect(i), flashIt.value());
        }
    }
        QRect plusRect = plusButtonRect();

        if (m_hoveredPlus) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 25)); // Полупрозрачный белый фон при наведении
            painter.drawRoundedRect(plusRect, 6, 6);     // 6px - радиус скругления углов
        }

        painter.setPen(m_hoveredPlus ? QColor("#ffffff") : QColor("#8b949e"));
        QFont f = painter.font();
        f.setPointSize(16); // Размер самого крестика (шрифта)
        painter.setFont(f);
        painter.drawText(plusRect, Qt::AlignCenter, "+");
    
}

void StormTabBar::mouseMoveEvent(QMouseEvent* event) {
    int hoveredClose = -1;
    int idx = tabAt(event->pos());
    if (idx >= 0 && idx < count() && closeButtonRect(idx).contains(event->pos())) {
        hoveredClose = idx;
    }

    if (hoveredClose != m_hoveredCloseIndex) {
        m_hoveredCloseIndex = hoveredClose;
        update();
    }
    bool hoveredPlus = plusButtonRect().contains(event->pos());
    if (hoveredPlus != m_hoveredPlus) {
        m_hoveredPlus = hoveredPlus;
        update();
    }
    QTabBar::mouseMoveEvent(event);
}

void StormTabBar::leaveEvent(QEvent* event) {
    if (m_hoveredCloseIndex != -1) {
        m_hoveredCloseIndex = -1;
        update();
    }
    if (m_hoveredPlus) {
        m_hoveredPlus = false;
        update();
    }
    QTabBar::leaveEvent(event);
}

void StormTabBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (plusButtonRect().contains(event->pos())) {
            emit plusClicked();
            return;
        }
        int idx = tabAt(event->pos());

        if (idx >= 0 && idx < count() && closeButtonRect(idx).contains(event->pos())) {
            auto* tabs = qobject_cast<QTabWidget*>(parentWidget());
            QWidget* page = tabs ? tabs->widget(idx) : nullptr;
            if (page) {
                startCloseFlash(page);
            }
            else {
                emit tabCloseRequested(idx);
            }
            return;
        }
    }
    QTabBar::mousePressEvent(event);
}