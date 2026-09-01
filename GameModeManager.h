#pragma once
#include <QObject>

class MainWindow;

class GameModeManager : public QObject {
    Q_OBJECT
public:
    // Включение/выключение игрового режима
    static void toggleGameMode(MainWindow* mw, bool enabled);

    // Ручной принудительный сброс памяти (RAM Boost)
    static void optimizeRAM(MainWindow* mw);
};