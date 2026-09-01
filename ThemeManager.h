#pragma once
#pragma once
#include <QString>
#include <QMap>
#include <QColor>
#include <QMainWindow>

struct ThemeColors {
    QString window;
    QString text;
    QString base;
    QString button;
    QString button_hover;
    QString border;
    QString tab_bg;
    QString tab_selected;
    QString icon_color;
    QString menu_bg;
    QString menu_text;
    QString menu_hover;
    QString menu_border;
    QString accent;
};

class ThemeManager {
public:
    static ThemeManager& instance() {
        static ThemeManager instance;
        return instance;
    }

    void applyTheme(QMainWindow* mainWindow, const QString& themeName = "dark");
    ThemeColors getColors(const QString& themeName);

private:
    ThemeManager();
    QMap<QString, ThemeColors> themes;
};