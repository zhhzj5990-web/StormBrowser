#pragma once
#include <QString>

class MainWindow;

// HTML/CSS/JS для storm://settings — вынесено из PageTemplates::getSettingsHtml(),
// чтобы не раздувать PageTemplates.cpp дальше (см. комментарий в PageTemplates.h
// про уже вынесенные из MainWindow.cpp ~2700 строк). Тот же принцип: класс не
// хранит состояния, только строит HTML-строку по указателю на MainWindow
// (и через него — читает QSettings). PageTemplates::getSettingsHtml() теперь
// просто делегирует сюда.
class SettingsPageHtml {
public:
    static QString build(MainWindow* mw);
};
