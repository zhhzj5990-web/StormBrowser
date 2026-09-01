#pragma once
#include <QObject>
#include <QIcon>

class QPainter;
class QColor;
class QTabWidget;
class QWebEngineView;
class QTimer;

// Полноценный владелец иконки вкладки на протяжении всей её жизни — не
// только "спиннер загрузки", как раньше. Четыре режима, которые он сам
// переключает по сигналам страницы:
//
//   Loading  — вращающаяся дуга + молния (как было) — пока грузится страница;
//   Audible  — анимированный "эквалайзер" — пока на странице играет
//              видео/аудио (QWebEnginePage::recentlyAudibleChanged);
//   Favicon  — обычная иконка сайта, приходит асинхронно через
//              QWebEngineView::iconChanged (может обновиться и во время
//              Loading/Audible — тогда просто запоминаем на потом);
//   Fallback — сайт не отдал favicon вообще — статичная молния Storm на
//              тёмной круглой подложке вместо пустой иконки.
//
// Приоритет ровно такой сверху вниз: Loading перекрывает Audible, Audible
// перекрывает Favicon/Fallback. MainWindow сам ничего не решает — только
// подключает 4 сигнала к соответствующим слотам ниже, дальше это забота
// TabSpinner'а (см. addNewTab() в MainWindow.cpp).
class TabSpinner : public QObject {
    Q_OBJECT
public:
    explicit TabSpinner(QTabWidget* tabWidget, QWebEngineView* view, QObject* parent = nullptr);

public slots:
    void start();                          // loadStarted
    void stop();                           // loadFinished
    void onIconChanged(const QIcon& icon); // QWebEngineView::iconChanged
    void onAudibleChanged(bool audible);   // QWebEnginePage::recentlyAudibleChanged

private slots:
    void onTick();

private:
    enum class State { Loading, Audible, Static };

    QIcon renderFrame() const;          // кадр спиннера загрузки
    QIcon renderEqualizerFrame() const; // кадр "эквалайзера" (звук/видео играет)
    QIcon renderFallbackIcon() const;   // статичная заглушка без favicon'а
    static void paintBolt(QPainter& painter, int size, const QColor& color);

    void enterAudibleMode();
    void enterStaticMode();
    void applyStaticIcon(); // favicon, если есть, иначе renderFallbackIcon()

    int currentTabIndex() const;

    QTabWidget* m_tabWidget;
    QWebEngineView* m_view;
    QTimer* m_timer;
    qreal m_rotation;
    qreal m_phase;

    State m_state = State::Static;
    bool m_audible = false; // последнее известное состояние recentlyAudible
    QIcon m_favicon;        // последний известный favicon (может быть null)
};