#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QPoint>

// Предварительное объявление класса MainWindow (вместо #include "MainWindow.h")
class MainWindow;
class QCompleter;
class QStringListModel;

class BrowserTopBar : public QWidget {
    Q_OBJECT

public:
    explicit BrowserTopBar(MainWindow* mw, QWidget* parent = nullptr);
    QLineEdit* getAddressBar() const { return addressBar; }

    // Нужен MainWindow::openDownloads(), чтобы синхронизировать checked-состояние
    // кнопки с реальной видимостью панели загрузок (кнопка стала checkable —
    // см. конструктор .cpp и аналог downloads_btn в top_bar.py).
    QPushButton* getDownloadsButton() const { return btnDownloads; }

public slots:
    void toggleMaxRestore();

    // Переключает объединённую кнопку Обновить/Стоп: во время загрузки
    // страницы показывает ✕ (остановить), иначе — ↻ (обновить). Вызывается
    // из MainWindow на loadStarted()/loadFinished() активной вкладки (и при
    // переключении вкладок — чтобы кнопка отражала состояние новой активной).
    void setLoadingState(bool isLoading);

    // Переключает объединённую кнопку закладки: ★ + "Удалить из закладок",
    // если текущая страница уже в закладках, иначе ☆ + "Добавить в
    // закладки". Вызывается из MainWindow после addCurrentBookmark()/
    // removeCurrentBookmark(), а также при навигации и переключении вкладок.
    void setBookmarked(bool isBookmarked);

    // Переключает иконку Storm Shield для текущего сайта: ⚠ , если для сайта
    // добавлено исключение (защита выключена), иначе 🛡. Вызывается из
    // MainWindow при навигации, переключении вкладок и после переключения
    // исключения через ShieldInterceptor::addException()/removeException().
    void setShieldException(bool isExcepted);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QPushButton* createNavButton(const QString& text, const QString& tooltip, const QString& objectName);

    // Пересобирает список подсказок омнибокса (история + закладки) по введённому
    // тексту — аналог MainWindow._update_omnibox из Python-версии, которого
    // не было в C++ вовсе (адресная строка была без автодополнения).
    void updateOmniboxSuggestions(const QString& text);

    // Обновляет иконку-индикатор безопасности слева в адресной строке
    // (🔒 https / ⚠ http / 🌩️ storm:// / 🔍 поиск-незавершённый ввод).
    // Подключён к QLineEdit::textChanged, поэтому реагирует и на ввод
    // пользователя, и на программные mw->updateAddressBar(url) при навигации.
    void refreshSecurityIndicator(const QString& text);

    MainWindow* mainWindow;
    QLineEdit* addressBar;
    QCompleter* addressCompleter;
    QStringListModel* omniboxModel;
    // Индикатор безопасности соединения — самый левый элемент внутри
    // адресной строки (🔒/⚠/🌩️/🔍), см. refreshSecurityIndicator().
    QLabel* securityIndicator;
    QPushButton* btnMenu; // кастомная кнопка-гамбургер ☰ вместо нативного QMenuBar

    QPushButton* btnBack;
    QPushButton* btnForward;
    // Объединённая кнопка Обновить/Стоп — иконка и обработчик клика
    // переключаются в setLoadingState(); отдельной кнопки btnStop больше нет.
    QPushButton* btnReload;
    // Ведёт на страницу, заданную в настройках стартовой страницы (см.
    // MainWindow::goHome() / startup/mode) — в отличие от btnHome ниже,
    // который всегда открывает именно storm://home.
    QPushButton* btnStartPage;
    QPushButton* btnHome;

    // Живут внутри адресной строки (справа), см. конструктор .cpp:
    // объединённая кнопка закладки (☆/★), статус Storm Shield (🛡/⚠)
    // и копирование ссылки (🔗). Кнопки ➕ "Новая вкладка" здесь больше нет —
    // команда создания вкладки не относится к адресной строке.
    QPushButton* btnBookmark;
    QPushButton* btnShield;
    QPushButton* btnCopyLink;

    QPushButton* btnScreenshot;
    QPushButton* btnIncognito;
    QPushButton* btnDownloads;
    QPushButton* btnProfile;

    QPushButton* btnMin;
    QPushButton* btnMax;
    QPushButton* btnClose;

    bool isTracking;
    QPoint startPos;

    bool m_isLoading = false;
    bool m_isBookmarked = false;
    bool m_isShieldExcepted = false;
};