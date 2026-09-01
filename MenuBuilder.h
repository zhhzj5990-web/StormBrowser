#pragma once
#pragma once
#include <QObject>

// Сообщаем компилятору, что такой класс есть, чтобы не подключать весь хидер
class MainWindow;

class MenuBuilder : public QObject {
    Q_OBJECT
public:
    // Статическая функция, как в Python (@staticmethod)
    static void buildMenu(MainWindow* mw);
};