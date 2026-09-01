#pragma once

#include <QTabBar>
#include <QString>
#include <QHash>
#include <QPointer>
#include <QPainter>


class StormTabBar : public QTabBar {
Q_OBJECT
public:
    explicit StormTabBar(QWidget* parent = nullptr);

    // =====================================================================
    // ЕДИНСТВЕННОЕ МЕСТО НАСТРОЙКИ крестика и "+". buildTabBarStyleSheet()
    // собирает QSS из этих же констант — синхронизировать вручную ничего
    // не нужно.
    // =====================================================================
    static constexpr int kTabWidth = 190;      // ширина обычной вкладки
    static constexpr int kTabRowHeight = 35;   // высота ВСЕЙ строки вкладок — держи >= kPlusHeight и >= kCloseBtnSize

    static constexpr int kCloseBtnSize = 16;   // сторона квадрата крестика (клик+подсветка)
    static constexpr int kCloseRightGap = 8;   // отступ крестика от правого края вкладки
    static constexpr int kCloseLineInset = 5;  // отступ линий "X" от краёв своего квадрата
    static constexpr int kCloseTextGap = 3;    // "буфер" между концом текста и крестиком (уходит в QSS padding-right)

    static constexpr int kPlusWidth = 24;          // ширина кнопки "+" (клик + подсветка)
    static constexpr int kPlusHeight = 22;         // высота кнопки "+" — центрируется внутри kTabRowHeight
    static constexpr int kPlusLeftGap = 14;        // "воздух" от последней вкладки до кнопки "+"
    static constexpr int kPlusGlyphPointSize = 14; // размер символа "+" (font point size)
    static constexpr int kPlusCornerRadius = 5;    // скругление зелёной подсветки при наведении

    static constexpr int kTabCornerRadius = 8;    // скругление самой вкладки (чем больше — тем ближе к "пилюле" как на референсе)

    static_assert(kTabRowHeight >= kPlusHeight, "kTabRowHeight должен быть >= kPlusHeight, иначе '+' обрежется по высоте");
    static_assert(kTabRowHeight >= kCloseBtnSize, "kTabRowHeight должен быть >= kCloseBtnSize, иначе крестик обрежется по высоте");

    // =====================================================================
    // ВНЕШНИЙ ВИД: чёткие рамки вкладки + крестик, видимый ВСЕГДА (как в
    // старой python-версии), а не только при наведении — но в покое он
    // нейтрального цвета и БЕЗ фона, а красным "загорается" именно и
    // только при наведении. Позиционирование (константы выше) не
    // меняется — меняется только то, чем закрашивается уже посчитанный
    // closeButtonRect()/tabRect().
    // =====================================================================
    static constexpr const char* kTabBorderColor = "#30363d";         // рамка обычной вкладки
    static constexpr const char* kTabBorderColorSelected = "#454f5c"; // рамка активной вкладки
    static constexpr const char* kSelectedTopAccent = "#56d39b";      // полоска сверху активной вкладки (акцент Storm Shield)

    // Крестик: в покое — просто линии нейтрального цвета, БЕЗ цветной
    // подложки (иначе она сливалась с фоном вкладки и выглядела так,
    // будто крестика нет вообще, только "+"). При наведении появляется
    // сплошной красный кружок-подложка — это и есть "загорается красным".
    static constexpr const char* kCloseIdleColor = "#9aa4af";       // цвет линий "X" в покое (нейтральный, как текст вкладки)
    static constexpr const char* kCloseColor = "#e5484d";           // цвет подложки крестика ПРИ НАВЕДЕНИИ
    static constexpr const char* kCloseHoverGlyphColor = "#ffffff"; // цвет линий "X" при наведении (на красном фоне)

    // --- Тайминги декоративных анимаций (мс) ---
    static constexpr int kCloseFlashMs = 260; // молния при закрытии
    static constexpr int kGustMs = 340;       // порыв ветра на "+"

    // Собирает QSS для tabWidget->setStyleSheet(...) ИЗ констант выше.
    static QString buildTabBarStyleSheet();

    QSize tabSizeHint(int index) const override;
    QSize minimumTabSizeHint(int index) const override;
    QSize sizeHint() const override;

    QRect closeButtonRect(int index) const;
    QRect plusButtonRect() const;

signals:
    void plusClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    int m_hoveredCloseIndex = -1;
    bool m_hoveredPlus = false;

    // Молния: своя анимация на каждую вкладку, которую сейчас закрывают.
    // Ключ — указатель на СТРАНИЦУ (виджет вкладки), а не index: index
    // может уехать, если за время анимации закрыли/добавили что-то ещё, а
    // указатель на страницу стабилен вплоть до реального removeTab().
    QHash<QWidget*, double> m_closeFlash; // page -> прогресс молнии, 0..1

    // Порыв ветра на кнопке "+" — глобальный, кнопка одна.
    double m_gustProgress = 0.0; // 0..1

    void startCloseFlash(QWidget* page);
    void startGust();

    void paintCloseFlash(QPainter& painter, const QRect& tabRect, double progress) const;
    void paintGust(QPainter& painter, const QRect& plusRect, double progress) const;
};