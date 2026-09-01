#include "TabSpinner.h"
#include <QTabWidget>
#include <QWebEngineView>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QtMath>

TabSpinner::TabSpinner(QTabWidget* tabWidget, QWebEngineView* view, QObject* parent)
    : QObject(parent), m_tabWidget(tabWidget), m_view(view), m_rotation(0.0), m_phase(0.0) {
    m_timer = new QTimer(this);
    m_timer->setInterval(40); // ~25 fps — плавно, но не грузит CPU ради иконки 32x32
    connect(m_timer, &QTimer::timeout, this, &TabSpinner::onTick);

    // На случай, если вкладка создаётся ещё до первого loadStarted (или
    // сайт вообще ничего не грузит) — сразу показываем заглушку-молнию,
    // а не пустую системную иконку.
    applyStaticIcon();
}

int TabSpinner::currentTabIndex() const {
    return m_tabWidget->indexOf(m_view);
}

void TabSpinner::start() {
    m_state = State::Loading;
    m_rotation = 0.0;
    m_phase = 0.0;
    m_timer->start();
    onTick(); // сразу первый кадр, не дожидаясь первого tick таймера
}

void TabSpinner::stop() {
    // Favicon на этот момент мог ещё не подгрузиться (он прилетает
    // асинхронно через iconChanged — см. onIconChanged), а звук/видео
    // могли начать играть уже ПОСЛЕ окончания загрузки, например через
    // автовоспроизведение (см. onAudibleChanged). Поэтому здесь просто
    // передаём управление тому режиму, что актуален прямо сейчас, а не
    // жёстко ставим favicon.
    if (m_view->icon().isNull() == false) {
        m_favicon = m_view->icon();
    }

    if (m_audible) {
        enterAudibleMode();
    }
    else {
        enterStaticMode();
    }
}

void TabSpinner::onIconChanged(const QIcon& icon) {
    m_favicon = icon;
    // Если сейчас крутится спиннер загрузки или эквалайзер — просто
    // запомнили favicon на будущее, перерисовывать иконку вкладки не надо,
    // он и так каждый tick перерисовывается своим режимом.
    if (m_state == State::Static) {
        applyStaticIcon();
    }
}

void TabSpinner::onAudibleChanged(bool audible) {
    m_audible = audible;

    // Пока идёт загрузка — приоритет у спиннера, эквалайзер включится сам
    // из stop(), когда загрузка закончится (если звук всё ещё играет).
    if (m_state == State::Loading) {
        return;
    }

    if (audible) {
        enterAudibleMode();
    }
    else {
        enterStaticMode();
    }
}

void TabSpinner::enterAudibleMode() {
    m_state = State::Audible;
    m_phase = 0.0;
    m_timer->start();
    onTick();
}

void TabSpinner::enterStaticMode() {
    m_state = State::Static;
    m_timer->stop();
    applyStaticIcon();
}

void TabSpinner::applyStaticIcon() {
    int idx = currentTabIndex();
    if (idx < 0) return;
    m_tabWidget->setTabIcon(idx, m_favicon.isNull() ? renderFallbackIcon() : m_favicon);
}

void TabSpinner::onTick() {
    int idx = currentTabIndex();
    if (idx < 0) {
        // Вкладку закрыли/перетащили в другое окно — просто останавливаемся
        m_timer->stop();
        return;
    }

    if (m_state == State::Loading) {
        m_rotation += 9.0;
        if (m_rotation > 360.0) m_rotation -= 360.0;
        m_phase += 0.12;
        m_tabWidget->setTabIcon(idx, renderFrame());
    }
    else if (m_state == State::Audible) {
        m_phase += 0.22; // эквалайзер "живее" спиннера загрузки
        m_tabWidget->setTabIcon(idx, renderEqualizerFrame());
    }
    // State::Static сюда не попадает — таймер в этом режиме остановлен.
}

void TabSpinner::paintBolt(QPainter& painter, int size, const QColor& color) {
    // Одна и та же молния Storm используется и во время загрузки
    // (renderFrame, с пульсацией), и как статичная заглушка без favicon'а
    // (renderFallbackIcon) — единый узнаваемый символ бренда.
    QPainterPath bolt;
    bolt.moveTo(size * 0.56, size * 0.20);
    bolt.lineTo(size * 0.40, size * 0.52);
    bolt.lineTo(size * 0.50, size * 0.52);
    bolt.lineTo(size * 0.40, size * 0.82);
    bolt.lineTo(size * 0.63, size * 0.46);
    bolt.lineTo(size * 0.52, size * 0.46);
    bolt.lineTo(size * 0.63, size * 0.20);
    bolt.closeSubpath();

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPath(bolt);
}

QIcon TabSpinner::renderFrame() const {
    const int size = 32; // рисуем крупнее — Qt аккуратно уменьшит под реальный размер вкладки

    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // --- Вращающаяся дуга переменной длины (эффект "дыхания", как в Chrome) ---
    const qreal margin = 3.0;
    QRectF arcRect(margin, margin, size - margin * 2, size - margin * 2);

    qreal spanDeg = 40.0 + 250.0 * (0.5 + 0.5 * qSin(m_phase));
    qreal startDeg = m_rotation;

    QPen arcPen;
    arcPen.setWidthF(3.0);
    arcPen.setCapStyle(Qt::RoundCap);

    QLinearGradient grad(arcRect.topLeft(), arcRect.bottomRight());
    grad.setColorAt(0.0, QColor("#58a6ff"));  // синий — фирменный цвет Storm
    grad.setColorAt(1.0, QColor("#a371f7"));  // фиолетовый — акцент Storm AI
    arcPen.setBrush(grad);

    painter.setPen(arcPen);
    // QPainter::drawArc считает углы в 1/16 градуса, против часовой стрелки от 3 часов
    painter.drawArc(arcRect, static_cast<int>(startDeg * 16), static_cast<int>(-spanDeg * 16));

    // --- Молния в центре, будто "бьёт" — лёгкая пульсация яркости ---
    qreal flash = 0.55 + 0.45 * (0.5 + 0.5 * qSin(m_phase * 2.3));
    QColor boltColor("#ffd60a");
    boltColor.setAlphaF(flash);
    paintBolt(painter, size, boltColor);

    painter.end();
    return QIcon(pixmap);
}

QIcon TabSpinner::renderEqualizerFrame() const {
    const int size = 32;

    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Тёмная круглая подложка — тот же приём, что и у fallback-иконки
    // (renderFallbackIcon), чтобы зелёные бары были одинаково хорошо видны
    // независимо от темы страницы под ними.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#1c2128"));
    painter.drawEllipse(QRectF(1, 1, size - 2, size - 2));

    // Три полоски "эквалайзера", каждая со своей частотой/фазой, чтобы
    // они прыгали вразнобой, как настоящий индикатор звука, а не дышали
    // синхронно всей группой.
    struct Bar { qreal freq; qreal phaseOffset; qreal x; };
    const Bar bars[3] = {
        { 1.00, 0.0, size * 0.32 },
        { 1.45, 2.1, size * 0.50 },
        { 0.80, 4.0, size * 0.68 },
    };

    const qreal barWidth = size * 0.11;
    const qreal minHeight = size * 0.12;
    const qreal maxHeight = size * 0.46;
    const qreal baselineY = size * 0.74;

    // Акцентный зелёный Storm Shield (см. StormTabBar::kSelectedTopAccent) —
    // тот же цвет, что и полоска активной вкладки, чтобы "играет звук"
    // читалось как часть той же цветовой системы, а не случайный цвет.
    painter.setBrush(QColor("#56d39b"));
    painter.setPen(Qt::NoPen);

    for (const Bar& b : bars) {
        qreal wave = 0.5 + 0.5 * qSin(m_phase * b.freq + b.phaseOffset);
        qreal h = minHeight + (maxHeight - minHeight) * wave;
        QRectF barRect(b.x - barWidth / 2.0, baselineY - h, barWidth, h);
        QPainterPath path;
        path.addRoundedRect(barRect, barWidth * 0.4, barWidth * 0.4);
        painter.drawPath(path);
    }

    painter.end();
    return QIcon(pixmap);
}

QIcon TabSpinner::renderFallbackIcon() const {
    const int size = 32;

    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Та же тёмная подложка, что и у эквалайзера — единый стиль всех
    // "не-favicon" иконок вкладки.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#1c2128"));
    painter.drawEllipse(QRectF(1, 1, size - 2, size - 2));

    // Статичная молния (без пульсации renderFrame) — узнаваемый символ
    // Storm вместо пустого места, когда сайт не отдал свой favicon.
    paintBolt(painter, size, QColor("#ffd60a"));

    painter.end();
    return QIcon(pixmap);
}