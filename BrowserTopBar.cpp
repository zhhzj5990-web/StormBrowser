#include "BrowserTopBar.h"  // <-- САМОЕ ГЛАВНОЕ: подключаем свой же заголовок!
#include "MainWindow.h"     // <-- Подключаем MainWindow, чтобы вызывать его методы
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QMouseEvent>
#include <QUrl>
#include <QCompleter>
#include <QStringListModel>
#include <QMenu>
#include <QAction>
#include <QGuiApplication>
#include <QClipboard>
#include <QTimer>

BrowserTopBar::BrowserTopBar(MainWindow* mw, QWidget* parent)
    : QWidget(parent), mainWindow(mw), isTracking(false)
{
    setObjectName("browserTopBar");
    setFixedHeight(44);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(6);

    // ==========================================
    // 0. ЛОГОТИП
    // ==========================================
    QLabel* logoLabel = new QLabel(this);
    QString logoPath = QCoreApplication::applicationDirPath() + "/resources/logo.png";
    QPixmap logo(logoPath);
    if (!logo.isNull()) {
        logoLabel->setPixmap(logo.scaled(26, 26, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    else {
        logoLabel->setText("🌩️");
    }
    logoLabel->setStyleSheet("padding-right: 4px; font-size: 18px;");
    layout->addWidget(logoLabel, 0, Qt::AlignVCenter);

    // ==========================================
    // 1. МЕНЮ БРАУЗЕРА (кастомная кнопка ☰ — сама панель строится в MenuBuilder)
    // ==========================================
    btnMenu = new QPushButton(u8"☰", this);
    btnMenu->setObjectName("hamburgerMenuBtn");
    btnMenu->setToolTip(u8"Меню Storm Browser");
    btnMenu->setFixedSize(34, 34);
    btnMenu->setCursor(Qt::PointingHandCursor);
    layout->addWidget(btnMenu, 0, Qt::AlignVCenter);

    // ==========================================
    // 2. НАВИГАЦИЯ (Слева от адресной строки)
    // ==========================================
    btnBack = createNavButton("◀", u8"Назад", "navBtn");
    btnForward = createNavButton("▶", u8"Вперёд", "navBtn");
    // Объединённая кнопка Обновить/Стоп: изначально в состоянии "не грузится",
    // поэтому стартовая иконка — ↻. Переключается в setLoadingState().
    btnReload = createNavButton("↻", u8"Обновить", "navBtn");
    btnStartPage = createNavButton(u8"🧭", u8"На главную страницу", "navBtn");
    btnHome = createNavButton("🏠", u8"Домой", "navBtn");

    layout->addWidget(btnBack);
    layout->addWidget(btnForward);
    layout->addWidget(btnReload);
    layout->addWidget(btnStartPage);
    layout->addWidget(btnHome);

    // ==========================================
    // 3. АДРЕСНАЯ СТРОКА И ВНУТРЕННИЕ КНОПКИ
    // ==========================================
    addressBar = new QLineEdit(this);
    addressBar->setObjectName("addressBar");
    addressBar->setPlaceholderText(u8"🔎 Введите адрес или поисковый запрос...");
    addressBar->setFixedHeight(32);
    addressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Индикатор безопасности соединения — самый левый элемент внутри
    // адресной строки. Обновляется в refreshSecurityIndicator() по сигналу
    // textChanged (реагирует и на ввод, и на программные mw->updateAddressBar(url)).
    securityIndicator = new QLabel(addressBar);
    securityIndicator->setObjectName("addressSecurityIndicator");
    securityIndicator->setFixedSize(22, 22);
    securityIndicator->setAlignment(Qt::AlignCenter);
    securityIndicator->setStyleSheet("font-size: 13px;");

    // Кнопки, которые живут внутри адресной строки справа.
    // Объединённая кнопка закладки: ☆ — страницы нет в закладках (клик
    // добавляет), ★ — уже добавлена (клик удаляет). Какая из иконок сейчас
    // показана, решает не клик, а состояние из setBookmarked() — MainWindow
    // должен вызывать его при навигации, переключении вкладок и после
    // добавления/удаления закладки.
    btnBookmark = createNavButton(u8"☆", u8"Добавить в закладки", "navBtn");
    btnBookmark->setFixedSize(26, 26);

    // Статус Storm Shield для текущего сайта: 🛡 — защита включена,
    // ⚠ — для сайта добавлено исключение. Состояние приходит через
    // setShieldException(), см. пояснение у объявления в .h.
    btnShield = createNavButton(u8"🛡", u8"Storm Shield: защита включена", "navBtn");
    btnShield->setFixedSize(26, 26);

    // Копирование текущей ссылки — чисто клиентское действие.
    btnCopyLink = createNavButton(u8"🔗", u8"Скопировать ссылку", "navBtn");
    btnCopyLink->setFixedSize(26, 26);

    // Собираем индикатор слева и кнопки справа внутри адресной строки
    QHBoxLayout* addrLayout = new QHBoxLayout(addressBar);
    addrLayout->setContentsMargins(4, 0, 6, 0);
    addrLayout->setSpacing(2);
    addrLayout->addWidget(securityIndicator, 0, Qt::AlignVCenter); // самый левый элемент
    addrLayout->addStretch(); // остальное толкаем в правый край
    addrLayout->addWidget(btnBookmark);
    addrLayout->addWidget(btnShield);
    addrLayout->addWidget(btnCopyLink);

    // Отступы для текста: слева — под индикатор (22px + внутренние поля),
    // справа — под 3 кнопки по 26px + интервалы (как и раньше).
    addressBar->setTextMargins(32, 0, 90, 0);

    // ==========================================
    // 3.1 АВТОДОПОЛНЕНИЕ АДРЕСНОЙ СТРОКИ (ОМНИБОКС)
    // ==========================================
    // Аналог top_bar.py: omnibox_model (QStringListModel) + completer (QCompleter)
    // + textChanged -> mw._update_omnibox. В C++-версии этого не было вовсе —
    // адресная строка работала без единой подсказки из истории/закладок.
    omniboxModel = new QStringListModel(this);
    addressCompleter = new QCompleter(omniboxModel, this);
    addressCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    addressCompleter->setFilterMode(Qt::MatchContains);
    addressCompleter->setCompletionMode(QCompleter::PopupCompletion);
    addressBar->setCompleter(addressCompleter);

    // textEdited (не textChanged): нужен именно ввод пользователем, а не
    // программные mw->updateAddressBar(url) при переходе по ссылкам — иначе
    // при каждой навигации мы бы зря пересобирали список подсказок.
    connect(addressBar, &QLineEdit::textEdited, this, &BrowserTopBar::updateOmniboxSuggestions);

    // А вот индикатору безопасности программные обновления адреса как раз
    // нужны — иначе после навигации по ссылке иконка не поменяется, поэтому
    // здесь подписываемся на textChanged, а не textEdited.
    connect(addressBar, &QLineEdit::textChanged, this, &BrowserTopBar::refreshSecurityIndicator);
    refreshSecurityIndicator(addressBar->text()); // начальное состояние иконки

    layout->addWidget(addressBar, 1); // <--- ВАЖНО: "1" заставляет адресную строку занимать всё свободное место!

    // ==========================================
    // 4. ПРАВЫЕ ИНСТРУМЕНТЫ (Справа от адресной строки)
    // ==========================================
    btnScreenshot = createNavButton("📸", u8"Скриншот", "navBtn");
    btnIncognito = createNavButton("🕶", u8"Инкогнито", "navBtn");
    btnDownloads = createNavButton("📥", u8"Загрузки", "navBtn");
    // downloads_btn в top_bar.py — checkable, подсвечивается, пока открыта
    // downloads_sidebar. Раньше здесь кнопка была обычной: она вызывала
    // MainWindow::openDownloads(), панель загрузок открывалась/закрывалась,
    // но сама кнопка никогда не выглядела "нажатой" — см. синхронизацию
    // checked-состояния в MainWindow::openDownloads().
    btnDownloads->setCheckable(true);
    btnProfile = createNavButton("👤", u8"Профиль", "navBtn");

    layout->addWidget(btnScreenshot);
    layout->addWidget(btnIncognito);
    layout->addWidget(btnDownloads);
    layout->addWidget(btnProfile);

    layout->addSpacing(10);

    // ==========================================
    // 5. КНОПКИ УПРАВЛЕНИЯ ОКНОМ
    // ==========================================
    btnMin = new QPushButton("—", this);
    btnMin->setObjectName("titleMinBtn");

    btnMax = new QPushButton("⬜", this);
    btnMax->setObjectName("titleMaxBtn");

    btnClose = new QPushButton("✕", this);
    btnClose->setObjectName("titleCloseBtn");

    btnMin->setFixedSize(38, 30);
    btnMax->setFixedSize(38, 30);
    btnClose->setFixedSize(38, 30);

    layout->addWidget(btnMin);
    layout->addWidget(btnMax);
    layout->addWidget(btnClose);

    // ==========================================
    // --- ПОДКЛЮЧЕНИЯ (CONNECTS) ---
    // ==========================================
    connect(btnMin, &QPushButton::clicked, [this]() { window()->showMinimized(); });
    connect(btnMax, &QPushButton::clicked, this, &BrowserTopBar::toggleMaxRestore);
    connect(btnClose, &QPushButton::clicked, [this]() { window()->close(); });

    connect(btnScreenshot, &QPushButton::clicked, mainWindow, &MainWindow::takeScreenshot);
    connect(btnIncognito, &QPushButton::clicked, mainWindow, &MainWindow::openIncognitoTab);
    connect(btnDownloads, &QPushButton::clicked, mainWindow, &MainWindow::openDownloads);
    connect(btnProfile, &QPushButton::clicked, mainWindow, &MainWindow::openProfile);

    connect(addressBar, &QLineEdit::returnPressed, mainWindow, &MainWindow::navigateToUrl);
    connect(btnBack, &QPushButton::clicked, mainWindow, &MainWindow::goBack);
    connect(btnForward, &QPushButton::clicked, mainWindow, &MainWindow::goForward);
    // Клик по объединённой кнопке: во время загрузки страницы — стоп,
    // иначе — обновить. Какая именно функция вызовется, решает m_isLoading,
    // которое выставляет setLoadingState() из MainWindow.
    connect(btnReload, &QPushButton::clicked, mainWindow, [this]() {
        if (m_isLoading) {
            mainWindow->stopLoading();
        } else {
            mainWindow->reloadPage();
        }
    });
    // btnHome (домик, последняя кнопка) — всегда storm://home, как и раньше.
    // btnStartPage (🧭) — ведёт на страницу, заданную в настройках стартовой
    // страницы (startup/mode), через MainWindow::goHome().
    connect(btnHome, &QPushButton::clicked, mainWindow, [this]() { mainWindow->addNewTab(QUrl("storm://home")); });
    connect(btnStartPage, &QPushButton::clicked, mainWindow, &MainWindow::goHome);

    // Клик по объединённой кнопке закладки: какую функцию вызвать, решает
    // текущее состояние m_isBookmarked — оно приходит извне через
    // setBookmarked(), не подменяется локально сразу после клика.
    connect(btnBookmark, &QPushButton::clicked, mainWindow, [this]() {
        if (m_isBookmarked) {
            mainWindow->removeCurrentBookmark();
        } else {
            mainWindow->addCurrentBookmark();
        }
    });

    // Клик по иконке Storm Shield — переключить исключение для текущего
    // сайта. ВНИМАНИЕ: MainWindow::toggleShieldException() — новый метод,
    // которого пока нет в MainWindow; его нужно добавить (дёрнуть
    // ShieldInterceptor::addException()/removeException() для активной
    // вкладки и вызвать topBar->setShieldException() с новым состоянием).
    connect(btnShield, &QPushButton::clicked, mainWindow, [this]() {
        mainWindow->toggleShieldException();
    });

    // Копирование ссылки из адресной строки — чисто клиентское действие,
    // MainWindow не нужен. На 1.2 секунды показываем ✓ как подтверждение.
    connect(btnCopyLink, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(addressBar->text());
        btnCopyLink->setText(u8"✓");
        btnCopyLink->setToolTip(u8"Скопировано!");
        QTimer::singleShot(1200, this, [this]() {
            btnCopyLink->setText(u8"🔗");
            btnCopyLink->setToolTip(u8"Скопировать ссылку");
        });
    });

}

QPushButton* BrowserTopBar::createNavButton(const QString& text, const QString& tooltip, const QString& objectName) {
    QPushButton* btn = new QPushButton(text, this);
    btn->setObjectName(objectName);
    btn->setToolTip(tooltip);
    btn->setFixedSize(34, 34);
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}

void BrowserTopBar::updateOmniboxSuggestions(const QString& text) {
    QStringList suggestions;
    const QString needle = text.trimmed();

    if (!needle.isEmpty()) {
        // 1. История посещений — тот же источник, что и history_manager.py /
        // DatabaseManager::getHistoryEntries(), уже используемый в showHistory().
        const auto historyEntries = mainWindow->getDatabaseManager().getHistoryEntries(300);
        for (const auto& entry : historyEntries) {
            if (entry.url.contains(needle, Qt::CaseInsensitive) ||
                entry.title.contains(needle, Qt::CaseInsensitive)) {
                if (!suggestions.contains(entry.url)) {
                    suggestions.append(entry.url);
                }
            }
        }

        // 2. Закладки — читаем напрямую из "bookmarksMenu" (см. MenuBuilder.cpp
        // и MainWindow::loadBookmarksIntoMenu()), где каждый QAction хранит
        // URL в QAction::data(), а название закладки — в QAction::text().
        if (QMenu* bmMenu = mainWindow->findChild<QMenu*>("bookmarksMenu")) {
            for (QAction* action : bmMenu->actions()) {
                if (action->isSeparator()) continue;
                const QString url = action->data().toString();
                if (url.isEmpty()) continue;
                if (url.contains(needle, Qt::CaseInsensitive) ||
                    action->text().contains(needle, Qt::CaseInsensitive)) {
                    if (!suggestions.contains(url)) {
                        suggestions.append(url);
                    }
                }
            }
        }
    }

    omniboxModel->setStringList(suggestions);
}

void BrowserTopBar::toggleMaxRestore() {
    if (window()->isMaximized()) {
        window()->showNormal();
        btnMax->setText("⬜");
    }
    else {
        window()->showMaximized();
        btnMax->setText("🗗");
    }
}

void BrowserTopBar::setLoadingState(bool isLoading) {
    if (m_isLoading == isLoading) return;
    m_isLoading = isLoading;

    if (m_isLoading) {
        btnReload->setText("✕");
        btnReload->setToolTip(u8"Остановить загрузку");
    }
    else {
        btnReload->setText("↻");
        btnReload->setToolTip(u8"Обновить");
    }
}

void BrowserTopBar::setBookmarked(bool isBookmarked) {
    if (m_isBookmarked == isBookmarked) return;
    m_isBookmarked = isBookmarked;

    if (m_isBookmarked) {
        btnBookmark->setText(u8"★");
        btnBookmark->setToolTip(u8"Удалить из закладок");
    }
    else {
        btnBookmark->setText(u8"☆");
        btnBookmark->setToolTip(u8"Добавить в закладки");
    }
}

void BrowserTopBar::setShieldException(bool isExcepted) {
    if (m_isShieldExcepted == isExcepted) return;
    m_isShieldExcepted = isExcepted;

    if (m_isShieldExcepted) {
        btnShield->setText(u8"⚠");
        btnShield->setToolTip(u8"Storm Shield: защита отключена для этого сайта");
    }
    else {
        btnShield->setText(u8"🛡");
        btnShield->setToolTip(u8"Storm Shield: защита включена");
    }
}

void BrowserTopBar::refreshSecurityIndicator(const QString& text) {
    const QString trimmed = text.trimmed();
    const QString scheme = QUrl(trimmed, QUrl::TolerantMode).scheme().toLower();

    if (trimmed.isEmpty()) {
        securityIndicator->setText(QString());
        securityIndicator->setToolTip(QString());
    }
    else if (scheme == "https") {
        securityIndicator->setText(u8"🔒");
        securityIndicator->setToolTip(u8"Безопасное соединение (HTTPS)");
    }
    else if (scheme == "http") {
        securityIndicator->setText(u8"⚠");
        securityIndicator->setToolTip(u8"Небезопасное соединение (HTTP) — данные передаются в открытом виде");
    }
    else if (scheme == "storm") {
        securityIndicator->setText(u8"🌩️");
        securityIndicator->setToolTip(u8"Внутренняя страница Storm Browser");
    }
    else {
        // Поисковый запрос или адрес без схемы (например, введённый вручную
        // "example.com") — нейтральная иконка до реальной навигации.
        securityIndicator->setText(u8"🔍");
        securityIndicator->setToolTip(u8"Поиск или неполный адрес");
    }
}

void BrowserTopBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        isTracking = true;
        startPos = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
        event->accept();
    }
}

void BrowserTopBar::mouseMoveEvent(QMouseEvent* event) {
    if (isTracking && (event->buttons() & Qt::LeftButton)) {
        window()->move(event->globalPosition().toPoint() - startPos);
        event->accept();
    }
}

void BrowserTopBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        isTracking = false;
        event->accept();
    }
}

void BrowserTopBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        toggleMaxRestore();
        event->accept();
    }
}