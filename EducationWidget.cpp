#include "EducationWidget.h"
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>

EducationWidget::EducationWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    QLabel* header = new QLabel(u8"<b>🎓 Обучение и Помощь</b>", this);
    header->setStyleSheet("font-size: 16px; color: #a371f7;");
    layout->addWidget(header);

    QLabel* desc = new QLabel(u8"Здесь собраны лучшие ресурсы для новичков и опытных юзеров: от базовой настройки ПК до сложного кодинга.", this);
    desc->setWordWrap(true);
    desc->setStyleSheet("color: #adbac7; font-size: 14px; margin-bottom: 5px;");
    layout->addWidget(desc);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent;");

    QWidget* content = new QWidget(scroll);
    cLayout = new QVBoxLayout(content);
    cLayout->setAlignment(Qt::AlignTop);
    cLayout->setSpacing(10);
    cLayout->setContentsMargins(0, 0, 5, 0);

    addCard(u8"🚀 Справочник+", u8"Гайды: от настройки звука до создания ботов.", "https://info-sc.ru/", "#a371f7");
    addCard(u8"🔧 Hardware-Forum", u8"Обсуждение железа, разгона и сборок ПК.", "https://forums.overclockers.ru/", "#ffc857");
    addCard(u8"🎮 GameSetup Pro", u8"Тонкая настройка графики и FPS в играх.", "https://www.pcgamingwiki.com/", "#56d39b");
    addCard(u8"💻 MDN Web Docs", u8"Справочник по веб-разработке (HTML, CSS, JS).", "https://developer.mozilla.org", "#7aa2ff");
    addCard(u8"🧠 Хабр", u8"Крупнейшее IT-сообщество и туториалы.", "https://habr.com", "#ff5f5f");
    addCard(u8"📚 Stepik", u8"Бесплатные курсы по программированию.", "https://stepik.org", "#56d39b");

    scroll->setWidget(content);
    layout->addWidget(scroll);
}

void EducationWidget::addCard(const QString& title, const QString& desc, const QString& url, const QString& accentColor) {
    QFrame* card = new QFrame(this);
    card->setStyleSheet(QString("QFrame { background-color: #1c2128; border: 1px solid #30363d; border-left: 3px solid %1; border-radius: 6px; } "
        "QFrame:hover { background-color: #22272e; }").arg(accentColor));

    QVBoxLayout* vLayout = new QVBoxLayout(card);
    vLayout->setContentsMargins(10, 8, 10, 8);
    vLayout->setSpacing(6);

    QLabel* tLabel = new QLabel(QString(u8"<b>%1</b>").arg(title), card);
    tLabel->setStyleSheet(QString("color: %1; font-size: 15px;").arg(accentColor));
    tLabel->setWordWrap(true);

    QLabel* dLabel = new QLabel(desc, card);
    dLabel->setWordWrap(true);
    dLabel->setStyleSheet("color: #8b949e; font-size: 13px;");

    QPushButton* btn = new QPushButton(u8"Открыть", card);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(26);
    btn->setStyleSheet("QPushButton { background: #30363d; color: white; border: none; border-radius: 4px; font-size: 14px; font-weight: bold; } "
        "QPushButton:hover { background: #444c56; }");
    connect(btn, &QPushButton::clicked, this, [this, url]() { openUrl(url); });

    vLayout->addWidget(tLabel);
    vLayout->addWidget(dLabel);
    vLayout->addWidget(btn);

    cLayout->addWidget(card);
}

void EducationWidget::openUrl(const QString& url) {
    QDesktopServices::openUrl(QUrl(url));
}