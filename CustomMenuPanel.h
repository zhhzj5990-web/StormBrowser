#pragma once
#include <QWidget>
#include <QString>
#include <QVector>
#include <QPair>
#include <QPoint>
#include <functional>

class QVBoxLayout;
class QPushButton;

class CustomMenuPanel : public QWidget {
    Q_OBJECT
public:
    explicit CustomMenuPanel(QWidget* parent = nullptr);

    // onContextMenu (опционально) — вызывается при правом клике по пункту, получает
    // глобальные координаты клика; используется, например, чтобы показать
    // редактировать/удалить для конкретной закладки.
    void addAction(const QString& text, std::function<void()> onClick, bool closeMenu = true,
        std::function<void(const QPoint&)> onContextMenu = nullptr);
    void addCheckable(const QString& text, bool checked, std::function<void(bool)> onToggle, bool closeMenu = true);
    void addRadioGroup(const QVector<QPair<QString, QString>>& idsAndLabels,
        const QString& currentId,
        std::function<void(QString)> onSelect);
    void addSeparator();
    void addSectionLabel(const QString& text);
    void addCustomWidget(QWidget* widget);
    CustomMenuPanel* addSubmenu(const QString& text);

    void popup(QWidget* anchor);

    // Публичная обёртка над closeRoot() — нужна, чтобы внешний код (например,
    // обработчик контекстного меню пункта закладки) мог закрыть всё меню целиком
    // после выполнения действия.
    void closeWholeMenu();

    static void setThemeColors(const QString& bg, const QString& text, const QString& hover,
        const QString& border, const QString& accent);

    // Геттеры текущей темы — нужны, чтобы другие окна (например, отдельное
    // окно "Закладки") могли использовать те же цвета, что и выпадающее меню,
    // не дублируя и не рассинхронизируя палитру.
    static QString themeBg();
    static QString themeText();
    static QString themeHover();
    static QString themeBorder();
    static QString themeAccent();

private:
    QPushButton* makeRowButton(const QString& text);
    void closeRoot();
    void showFlyout(QWidget* header);  // показывает это подменю сбоку от header (с переносом влево, если не влезает)
    void closeFlyoutChain();           // рекурсивно закрывает открытое дочернее подменю (и его дочерние)
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;

    QVBoxLayout* contentLayout;
    CustomMenuPanel* rootPopup = nullptr;
    CustomMenuPanel* parentPanel = nullptr;      // логический родитель в цепочке подменю (для закрытия/сброса состояния)
    CustomMenuPanel* openChildFlyout = nullptr;  // текущее открытое дочернее подменю сбоку, если есть

    static QString s_bg, s_text, s_hover, s_border, s_accent;
};