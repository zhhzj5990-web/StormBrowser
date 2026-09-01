#pragma once
#include <QString>

class MainWindow;

// Все HTML/CSS/JS-шаблоны внутренних страниц браузера (storm://home,
// storm://settings, storm://cloud, storm://newtab, инкогнито, Arcade,
// Storm Talk) — вынесены сюда из MainWindow.cpp, где раньше занимали
// ~2700 строк (59% файла) чистого текста вперемешку с логикой.
//
// PageTemplates не хранит собственного состояния — только указатель на
// MainWindow, через который читает нужные данные (dbManager, текущий зум
// и т.п.), в точности как это уже устроено у SettingsBridge/StormCloudBridge.
class PageTemplates {
public:
    explicit PageTemplates(MainWindow* mw);

    QString getHomePageHtml();
    QString getSettingsHtml();
    QString getStormCloudHtml();
    QString getNewTabHtml();
    QString getIncognitoHtml();
    QString getArcadeGameHtml(const QString& swfPath, const QString& gameName);
    QString getTalkHtml();

private:
    MainWindow* mw;
};