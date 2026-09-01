#include "ThemeManager.h"
#include "CustomMenuPanel.h"
#include <QPalette>
#include <QApplication>


ThemeManager::ThemeManager() {
    // Темная тема
    themes["dark"] = {
        "#11141c", "#eef3ff", "#0c0e14", "#1c212d", "#2d3548",
        "rgba(255, 255, 255, 0.09)", "#1b2130", "#2d3f75", "#aaaaaa",
        "#1a1f2b", "#eef3ff", "#2d374f", "rgba(255, 255, 255, 0.10)", "#6e8cff"
    };

    // Сакура
    themes["sakura"] = {
        "#32272e", "#ffeeed", "#241b21", "#4a3843", "#634c5a",
        "rgba(255, 183, 203, 0.2)", "#32272e", "#e08ba6", "#f2b1c6",
        "#32272e", "#ffeeed", "#c9748f", "rgba(255, 183, 203, 0.25)", "#f2b1c6"
    };

    // Матрица
    themes["matrix"] = {
        "#000000", "#00ff00", "#050505", "#002200", "#004400",
        "rgba(0, 255, 0, 0.4)", "#001100", "#004400", "#00ff00",
        "#000000", "#00ff00", "#003300", "rgba(0, 255, 0, 0.5)", "#00ff00"
    };

    // Лес
    themes["forest"] = {
        "#0f1a15", "#e2f1e8", "#0a120e", "#172921", "#233d32",
        "rgba(46, 204, 113, 0.2)", "#0f1a15", "#27ae60", "#a8d5ba",
        "#0f1a15", "#e2f1e8", "#2ecc71", "rgba(46, 204, 113, 0.3)", "#2ecc71"
    };

    // Океан
    themes["ocean"] = {
        "#0f172a", "#f8fafc", "#020617", "#1e293b", "#334155",
        "rgba(56, 189, 248, 0.2)", "#0f172a", "#0284c7", "#94a3b8",
        "#0f172a", "#f8fafc", "#0ea5e9", "rgba(56, 189, 248, 0.3)", "#38bdf8"
    };

    // Киберпанк (Неон)
    themes["cyberpunk"] = {
        "#0b0b1a", "#e0e0e0", "#05050f", "#1a0b2e", "#e90064",
        "rgba(0, 240, 255, 0.3)", "#0b0b1a", "#00f0ff", "#fce205",
        "#0b0b1a", "#e0e0e0", "#e90064", "rgba(0, 240, 255, 0.4)", "#00f0ff"
    };

    // Nord (Спокойная, холодная)
    themes["nord"] = {
        "#2e3440", "#d8dee9", "#242933", "#3b4252", "#434c5e",
        "rgba(136, 192, 208, 0.2)", "#2e3440", "#88c0d0", "#81a1c1",
        "#2e3440", "#d8dee9", "#434c5e", "rgba(136, 192, 208, 0.3)", "#5e81ac"
    };
}

void applyCustomMenuTheme(const ThemeColors& colors) {
    // Наша кастомная панель меню (☰) — не QMenu, поэтому не подхватывает стиль
    // автоматически через setStyleSheet(mainWindow). Обновляем её цвета здесь явно,
    // при каждой смене темы, теми же полями, что уже используются в QSS для QMenu (%10-%14).
    CustomMenuPanel::setThemeColors(colors.menu_bg, colors.menu_text, colors.menu_hover,
        colors.menu_border, colors.accent);
}

ThemeColors ThemeManager::getColors(const QString& themeName) {
    if (themes.contains(themeName)) return themes[themeName];
    return themes["dark"];
}

void ThemeManager::applyTheme(QMainWindow* mainWindow, const QString& themeName) {
    ThemeColors colors = getColors(themeName);

    // 0. Обновляем цвета кастомного выпадающего меню (☰), чтобы следующее его открытие
    // уже было в новой теме.
    applyCustomMenuTheme(colors);

    // 1. Настраиваем системную палитру
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(colors.window));
    palette.setColor(QPalette::WindowText, QColor(colors.text));
    palette.setColor(QPalette::Base, QColor(colors.base));
    palette.setColor(QPalette::Text, QColor(colors.text));
    palette.setColor(QPalette::Button, QColor(colors.button));
    palette.setColor(QPalette::ButtonText, QColor(colors.text));
    palette.setColor(QPalette::Highlight, QColor(colors.tab_selected));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    qApp->setPalette(palette);

    // 2. Генерируем QSS
    QString stylesheet = QString(R"(
        /* Главное окно */
        QMainWindow, QDialog, QMessageBox, QInputDialog, QWidget#centralWidget {
            background-color: %7;
            color: %2;
        }

        /* --- ВСПЛЫВАЮЩИЕ ПОДСКАЗКИ (TOOLTIPS) --- */
        QToolTip {
            background-color: #000000;
            color: #ffffff;
            border: 1px solid rgba(255, 255, 255, 0.2);
            border-radius: 4px;
            padding: 5px;
        }

        /* Наша кастомная шапка */
        QWidget#customTitleBar {
            background-color: %1;
            border-bottom: 1px solid %6;
        }

        /* Главное окно */
        QMainWindow, QDialog, QMessageBox, QInputDialog, QWidget#centralWidget {
            background-color: %1;
            color: %2;
        }

        /* Наша кастомная шапка */
        QWidget#customTitleBar {
            background-color: %1;
            border-bottom: 1px solid %6;
        }

        QLabel#customTitleLabel {
            color: %9;
            font-weight: bold;
            font-size: 12px;
        }

        /* --- МЕНЮ БРАУЗЕРА --- */
        QMenuBar {
            background: transparent;
            color: %11;
        }
        QMenuBar::item {
            background: transparent;
            padding: 6px 12px; /* Сделали отступы побольше, чтобы было похоже на кнопку */
            color: %11;
            font-size: 14px;
            border-radius: 6px; /* Закругления */
        }
        QMenuBar::item:selected {
            background: %13; /* Цвет выделения берем из текущей темы (border color) */
        }
        QMenu {
            background-color: %10;
            color: %11;
            border: 1px solid %13;
            border-radius: 8px;
            padding: 4px;
        }
        QMenu::item {
            padding: 6px 25px 6px 15px;
            border-radius: 4px;
            margin: 2px;
        }
        QMenu::item:selected {
            background-color: %12;
            color: #ffffff;
        }
        /* КРАСИВЫЙ РАЗДЕЛИТЕЛЬ ДЛЯ МЕНЮ */
        QMenu::separator {
            height: 1px;
            background-color: %13;
            margin: 4px 15px;
        }

        
        /* Адресная строка */
        QLineEdit, QTextBrowser, QTextEdit, QListView, QListWidget, QComboBox {
            background-color: %3;
            color: %2;
            border: 1px solid %6;
            border-radius: 6px;
            padding: 6px;
        }

        /* Обычные кнопки */
        QPushButton, QToolButton {
            background-color: %4;
            color: %2;
            border: 1px solid %6;
            border-radius: 6px;
            padding: 5px 12px;
        }
        QPushButton:hover, QToolButton:hover {
            background-color: %5;
            border: 1px solid %14;
        }

/* --- АДРЕСНАЯ СТРОКА И НЕОНОВЫЕ КНОПКИ --- */
        QLineEdit#addressBar {
            background-color: %3;
            border: 1px solid %6;
            border-radius: 19px; /* Идеальное закругление */
            color: %2;
            font-size: 14px;
        }
        QLineEdit#addressBar:focus {
            border: 1px solid %14; /* Подсветка акцентом темы */
            background-color: %1;
        }

        /* --- ЕДИНЫЙ СТАНДАРТ ДЛЯ ВСЕХ КНОПОК ВЕРХНЕЙ ПАНЕЛИ --- */
        QWidget#browserTopBar QPushButton {
            background-color: transparent; /* Делаем их прозрачными в покое */
            border: none;
            border-radius: 6px;
            color: %11; /* Цвет иконки берем из темы */
            font-size: 16px;
            padding: 0px;
            margin: 0px;
        }
        
        /* Эффект при наведении на любую кнопку (Навигация, Инструменты, Свернуть) */
        QWidget#browserTopBar QPushButton:hover {
            background-color: %12; /* Полупрозрачный цвет выделения из темы */
            border: 1px solid %14; /* Легкая акцентная рамка */
        }
        
        /* Красим сами значки управления окном */
        QWidget#browserTopBar QPushButton#titleMinBtn { color: #ffc857; font-weight: bold; }
        QWidget#browserTopBar QPushButton#titleMaxBtn { color: #56d39b; font-weight: bold; }
        QWidget#browserTopBar QPushButton#titleCloseBtn { color: #ff5f5f; font-weight: bold; }

        /* Уникальный стиль только для кнопки Закрыть */
        QWidget#browserTopBar QPushButton#titleCloseBtn:hover {
            background-color: #ff5f5f;
            border: none;
            color: white;
        }

         /* --- СТИЛИ ДЛЯ ОКНА ИСТОРИИ --- */
        QDialog#historyDialog {
            background-color: %10; /* Основной фон темы */
        }
        
        QLineEdit#historySearchBox {
            background-color: %7; /* Цвет поля ввода из темы */
            color: %11;          /* Цвет текста из темы */
            border: 1px solid %13; /* Граница */
            border-radius: 6px;
            padding: 5px 10px;
        }

        QTableWidget#historyTable {
            background-color: %10;
            color: %11;
            border: 1px solid %13;
            gridline-color: transparent;
            selection-background-color: %12; /* Цвет выделения активной темы */
            selection-color: #ffffff;
        }

        QHeaderView::section {
            background-color: %7;
            color: %11;
            padding: 8px;
            border: none;
            border-bottom: 2px solid %13;
            font-weight: bold;
        }

        /* Универсальный стиль для кнопок в окне истории */
        QPushButton#historyBtnOpen, QPushButton#historyBtnDelete, QPushButton#historyBtnClose {
            background-color: %7;
            color: %11;
            border: 1px solid %13;
            border-radius: 6px;
            padding: 0px 20px;
            font-size: 13px;
        }

        QPushButton#historyBtnOpen:hover, QPushButton#historyBtnDelete:hover, QPushButton#historyBtnClose:hover {
            background-color: %12; /* Подсветка при наведении в цвет темы */
            color: #ffffff;
            border: 1px solid %14;
        }

        /* Настройка скроллбара под общую тему */
        QScrollBar:vertical {
            background: %10;
            width: 12px;
        }
        QScrollBar::handle:vertical {
            background: %13;
            border-radius: 6px;
            min-height: 20px;
        }
        QScrollBar::handle:vertical:hover {
            background: %14;
        }

        /* Вкладки — ТОЛЬКО ЦВЕТ. Геометрию (width/padding/margin/
        border-radius/:last/:hover/:pane) сюда НЕ добавлять и не трогать —
        за неё целиком и единолично отвечает
        StormTabBar::buildTabBarStyleSheet() (см. MainWindow.cpp),
        применяемый напрямую на tabWidget. Раньше здесь дублировался тот же
        селектор QTabBar::tab со своими padding/margin/border-radius —
        конфликт этих двух стилей на разных уровнях (QMainWindow здесь,
        tabWidget там) и был причиной того, что вкладка теряла
        фиксированную ширину, а крестик и "+" разъезжались кто куда. */
        QTabBar::tab {
            background-color: %7;
            color: #8b949e;
        }
        QTabBar::tab:selected {
            background-color: %8;
            color: #ffffff;
        }
        /* --- САЙДБАР --- */
        QWidget#sidebarManager {
            background-color: %7;
            border-right: 1px solid %6;
        }
        
        QFrame#sidebarContentArea {
            background-color: %3;
            border-right: 1px solid %6;
            border-bottom: 1px solid %6;
            border-bottom-right-radius: 12px;
        }

        QPushButton#sidebarCloseBtn {
            background: %5;
            color: %2;
            border-radius: 14px;
            font-size: 15px;
            font-weight: bold;
        }
        QPushButton#sidebarCloseBtn:hover { background: #ff5f5f; color: white; }

    )").arg(colors.window, colors.text, colors.base, colors.button, colors.button_hover,
    colors.border, colors.tab_bg, colors.tab_selected, colors.icon_color,
    colors.menu_bg, colors.menu_text, colors.menu_hover, colors.menu_border, colors.accent);

    mainWindow->setStyleSheet(stylesheet);
}