#include "GamesWidget.h"
#include "MainWindow.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QDesktopServices>
#include <QTabWidget>
#include <QWebEngineView>
#include <QUrl>

// --- Реализация бегущей строки ---
MarqueeLabel::MarqueeLabel(const QString& text, QWidget* parent)
    : QWidget(parent), m_text(text), m_offset(0) {
    setMinimumHeight(35);
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MarqueeLabel::updateScroll);
    m_timer->start(30);
}

void MarqueeLabel::updateScroll() {
    QFontMetrics fm(font());
    int textWidth = fm.horizontalAdvance(m_text);
    if (textWidth > width()) {
        m_offset--;
        if (m_offset < -textWidth) {
            m_offset = width();
        }
        update();
    }
    else if (m_offset != 0) {
        m_offset = 0;
        update();
    }
}

void MarqueeLabel::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFontMetrics fm(font());
    int textWidth = fm.horizontalAdvance(m_text);
    int y = (height() + fm.ascent() - fm.descent()) / 2;

    if (textWidth <= width()) {
        painter.drawText(rect(), Qt::AlignCenter, m_text);
    }
    else {
        painter.drawText(m_offset, y, m_text);
    }
}

// --- Реализация виджета Игр ---
GamesWidget::GamesWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(15, 10, 15, 15);

    MarqueeLabel* title = new MarqueeLabel(u8"🎮 Игровые платформы и Стримы", this);
    QFont titleFont("Segoe UI", 14, QFont::Bold);
    title->setFont(titleFont);
    mainLayout->addWidget(title);

    QFrame* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("background-color: rgba(255, 255, 255, 0.1); max-height: 1px;");
    mainLayout->addWidget(line);

    // ПЛАТФОРМЫ
    QLabel* header1 = new QLabel(u8"<b>🎲 Платформы</b>", this);
    header1->setStyleSheet("font-size: 14px; color: #a371f7;");
    mainLayout->addWidget(header1);

    QGridLayout* platformsLayout = new QGridLayout();
    platformsLayout->setSpacing(8);

    struct Platform { QString name; QString url; QString emoji; };
    QList<Platform> platforms = {
        {"Steam", "https://store.steampowered.com", u8"🕹"},
        {"Epic Games", "https://www.epicgames.com/store", u8"⚔"},
        {"Roblox", "https://www.roblox.com", u8"🟥"},
        {"Minecraft", "https://www.minecraft.net", u8"⛏"},
        {"Faceit", "https://www.faceit.com", u8"🏆"},
        {"Battle.net", "https://battle.net", u8"🌀"},
        {"GOG", "https://www.gog.com", u8"🌌"}
    };

    int row = 0, col = 0;
    for (const auto& p : platforms) {
        platformsLayout->addWidget(createGameButton(p.name, p.url, p.emoji), row, col);
        col++;
        if (col > 1) { col = 0; row++; }
    }
    mainLayout->addLayout(platformsLayout);
    mainLayout->addSpacing(10);

    // СТРИМЫ
    QLabel* header2 = new QLabel(u8"<b>📺 Стримы</b>", this);
    header2->setStyleSheet("font-size: 14px; color: #ffc857;");
    mainLayout->addWidget(header2);

    QGridLayout* streamsLayout = new QGridLayout();
    streamsLayout->setSpacing(8);

    struct Stream { QString name; QString url; QString color; QString icon; };
    QList<Stream> streams = {
        {"Twitch", "https://www.twitch.tv", "#9146FF", u8"🔴"},
        {"YouTube", "https://www.youtube.com/gaming", "#FF0000", u8"▶"},
        {"Kick", "https://kick.com", "#53FC19", u8"🚀"},
        {"Trovo", "https://trovo.live", "#FF5C00", u8"🌟"}
    };

    row = 0; col = 0;
    for (const auto& s : streams) {
        streamsLayout->addWidget(createStreamButton(s.name, s.url, s.color, s.icon), row, col);
        col++;
        if (col > 1) { col = 0; row++; }
    }
    mainLayout->addLayout(streamsLayout);
    mainLayout->addStretch();
}

QPushButton* GamesWidget::createGameButton(const QString& name, const QString& url, const QString& emoji) {
    QPushButton* btn = new QPushButton(emoji + " " + name, this);
    btn->setMinimumHeight(38);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(
        "QPushButton { background-color: #2a2a2a; color: #ffffff; border: 1px solid #555555; border-radius: 6px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background-color: #363636; border-color: #7aa2ff; }"
    );
    connect(btn, &QPushButton::clicked, this, [this, url]() { openLink(url); });
    return btn;
}

QPushButton* GamesWidget::createStreamButton(const QString& name, const QString& url, const QString& color, const QString& icon) {
    QPushButton* btn = new QPushButton(icon + " " + name, this);
    btn->setMinimumHeight(38);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(
        QString("QPushButton { background-color: %1; color: white; border: none; border-radius: 6px; font-size: 12px; font-weight: bold; }"
            "QPushButton:hover { background-color: rgba(255, 255, 255, 0.2); }").arg(color)
    );
    connect(btn, &QPushButton::clicked, this, [this, url]() { openLink(url); });
    return btn;
}

void GamesWidget::openLink(const QString& url) {
    // Открываем ссылку прямо во внутреннем браузере (новая вкладка),
    // а не через системный браузер по умолчанию.
    if (auto* mw = qobject_cast<MainWindow*>(this->window())) {
        if (QTabWidget* tabs = mw->getTabWidget()) {
            auto* view = new QWebEngineView(tabs);
            view->setUrl(QUrl(url));

            int insertIndex = qMax(tabs->count() - 1, 0); // вставляем перед кнопкой "+"
            int newIndex = tabs->insertTab(insertIndex, view, u8"⏳ Загрузка...");
            tabs->setCurrentIndex(newIndex);

            connect(view, &QWebEngineView::titleChanged, tabs, [tabs, view](const QString& t) {
                int idx = tabs->indexOf(view);
                if (idx != -1) tabs->setTabText(idx, t);
                });
            connect(view, &QWebEngineView::iconChanged, tabs, [tabs, view](const QIcon& icon) {
                int idx = tabs->indexOf(view);
                if (idx != -1) tabs->setTabIcon(idx, icon);
                });

            emit openUrlRequested(url);
            return;
        }
    }

    // Резервный вариант — если по какой-то причине окно браузера не найдено
    QDesktopServices::openUrl(QUrl(url));
}