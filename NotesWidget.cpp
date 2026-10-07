#include "NotesWidget.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>
#include <QTabWidget>
#include <QListWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QDateTime>
#include <QFont>
#include <QRegularExpression>

NotesWidget::NotesWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // v1.2.9: панель стала двухвкладочной — обычный блокнот как раньше
    // и новые заметки, привязанные к сайтам.
    tabs = new QTabWidget(this);
    root->addWidget(tabs);

    // ---------------- Вкладка 1: Блокнот (как раньше) ----------------
    QWidget* notesTab = new QWidget(this);
    QVBoxLayout* notesLayout = new QVBoxLayout(notesTab);
    notesLayout->setContentsMargins(4, 4, 4, 4);

    QLabel* titleLbl = new QLabel(u8"<b>📝 Блокнот</b>", notesTab);
    notesLayout->addWidget(titleLbl);

    textEdit = new QTextEdit(notesTab);
    textEdit->setPlaceholderText(u8"Пиши свои мысли и заметки здесь...\nОни сохраняются автоматически.");
    notesLayout->addWidget(textEdit);
    tabs->addTab(notesTab, u8"🗒️ Блокнот");

    // Определяем путь сохранения в AppData
    QDir dir(getNotesDirectory());
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    noteFilePath = dir.filePath("notes.md");

    // Таймер-дебаунсер: пишем на диск не на каждое нажатие клавиши,
    // а через 500мс после того, как пользователь перестал печатать.
    saveTimer = new QTimer(this);
    saveTimer->setSingleShot(true);
    connect(saveTimer, &QTimer::timeout, this, &NotesWidget::saveNotes);

    loadNotes();

    // Сохраняем при каждом изменении текста (с задержкой через таймер)
    connect(textEdit, &QTextEdit::textChanged, this, &NotesWidget::scheduleSave);

    // ---------------- Вкладка 2: По сайтам (v1.2.9) ----------------
    QWidget* sitesTab = new QWidget(this);
    QVBoxLayout* sitesLayout = new QVBoxLayout(sitesTab);
    sitesLayout->setContentsMargins(4, 4, 4, 4);
    sitesLayout->setSpacing(6);

    currentSiteLabel = new QLabel(u8"\U0001F310 Текущий сайт: —", sitesTab);
    currentSiteLabel->setWordWrap(true);
    currentSiteLabel->setStyleSheet("color:#56d39b; font-weight:600;");
    sitesLayout->addWidget(currentSiteLabel);

    attachBtn = new QPushButton(u8"\U0001F4CC Привязать заметку к текущему сайту", sitesTab);
    attachBtn->setEnabled(false); // пока не открыт обычный http(s)-сайт
    connect(attachBtn, &QPushButton::clicked, this, &NotesWidget::attachNoteToCurrentSite);
    sitesLayout->addWidget(attachBtn);

    sitesList = new QListWidget(sitesTab);
    sitesList->setMaximumHeight(110);
    connect(sitesList, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        openSiteNote();
        });
    sitesLayout->addWidget(new QLabel(u8"<b>Мои заметки по сайтам:</b>", sitesTab));
    sitesLayout->addWidget(sitesList);

    siteEditor = new QTextEdit(sitesTab);
    siteEditor->setPlaceholderText(
        u8"Выберите сайт из списка выше — или откройте нужный сайт во вкладке "
        u8"браузера и нажмите «Привязать заметку к текущему сайту».");
    siteEditor->setEnabled(false);
    sitesLayout->addWidget(siteEditor, 1);

    deleteSiteBtn = new QPushButton(u8"\U0001F5D1\uFE0F Удалить заметку этого сайта", sitesTab);
    deleteSiteBtn->setEnabled(false);
    connect(deleteSiteBtn, &QPushButton::clicked, this, &NotesWidget::deleteSiteNote);
    sitesLayout->addWidget(deleteSiteBtn);

    siteSaveTimer = new QTimer(this);
    siteSaveTimer->setSingleShot(true);
    connect(siteSaveTimer, &QTimer::timeout, this, &NotesWidget::saveSiteNote);
    // Автосохранение заметки сайта — тот же дебаунс, что у блокнота
    connect(siteEditor, &QTextEdit::textChanged, this, [this]() {
        if (!loadedSiteDomain.isEmpty()) siteSaveTimer->start(500);
        });

    tabs->addTab(sitesTab, u8"\U0001F310 По сайтам");

    refreshSitesList();
}

QString NotesWidget::getNotesDirectory() {
    // Аналог get_user_data_dir("notes") из Python
    QString appData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return appData + "/StormBrowser/plugins/notes";
}

QString NotesWidget::sitesDirectory() {
    // v1.2.9: заметки, привязанные к сайтам, — по одному .md на домен
    return getNotesDirectory() + "/sites";
}

void NotesWidget::loadNotes() {
    QFile file(noteFilePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);
        QString content = in.readAll();
        textEdit->setMarkdown(content);
        file.close();
    }
    else {
        // Приветственное сообщение, если файла нет
        QString welcomeText = u8"# 📝 Мои Заметки\n\n"
            u8"Используйте возможности **Markdown** для оформления:\n"
            u8"* **Жирный текст** для важных мыслей\n"
            u8"* *Курсив* для примечаний\n"
            u8"* Использовать списки задач через дефис `-`\n\n"
            u8"## \U0001F4BB Код и ссылки\n"
            u8"Вы можете выделять `командные строки` или писать полноценные ссылки.\n\n"
            u8"\U0001F4C4 Совет (v1.2.9): выделите текст на любой странице, кликните "
            u8"правой кнопкой и выберите «\U0001F4DD Сохранить в Заметки» — вырезка "
            u8"попадёт сюда с указанием источника.\n";
        textEdit->setMarkdown(welcomeText);
        saveNotes(); // Сразу сохраняем
    }
}

void NotesWidget::scheduleSave() {
    // Перезапускаем таймер при каждом изменении, чтобы фактическая
    // запись на диск произошла один раз после паузы в наборе текста.
    saveTimer->start(500);
}

void NotesWidget::saveNotes() {
    QFile file(noteFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << textEdit->toMarkdown(); // Сохраняем в формате Markdown
        file.close();
    }
}

// =========================================================================
// v1.2.9: WEB-CLIPPER — вырезка текста со страницы в блокнот
// =========================================================================
void NotesWidget::appendClipping(const QString& text, const QUrl& sourceUrl) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;

    // Ограничение длины: заметки не должны превращаться в дамп всей
    // страницы. Больше 20 000 символов — обрезаем: вырезка, а не архив.
    QString clipped = trimmed;
    if (clipped.size() > 20000) {
        clipped = clipped.left(20000) + u8"…";
    }

    // Строка источника: полный URL, чтобы потом можно было вернуться.
    QString sourceLine;
    if (sourceUrl.isValid() && !sourceUrl.host().isEmpty()) {
        sourceLine = u8"\n\n*Источник: " + sourceUrl.toString() + u8"*";
        if (sourceLine.size() > 300) {
            sourceLine = sourceLine.left(297) + u8"…*";
        }
    }

    const QString stamp = QDateTime::currentDateTime().toString(u8"dd.MM HH:mm");
    const QString block = QString(u8"\n\n---\n\n## \u2702\uFE0F Вырезка — %1%2\n\n%3\n")
        .arg(stamp, sourceLine, clipped);

    // Гоняем через markdown-цикл ЦЕЛИКОМ: QTextEdit::append() вставил бы
    // разметку как обычный текст, и при следующем сохранении toMarkdown()
    // заэскейпил бы звёздочки и решётки — вырезка превратилась бы в кашу.
    textEdit->setMarkdown(textEdit->toMarkdown() + block);
    scheduleSave();

    // Показываем результат сразу: раскрываем вкладку блокнота
    if (tabs) tabs->setCurrentIndex(0);
}

// =========================================================================
// v1.2.9: ЗАМЕТКИ ПО САЙТАМ
// =========================================================================
QString NotesWidget::domainFromUrl(const QUrl& url) const {
    // Только обычные сайты: внутренние storm://-страницы (настройки,
    // закладки, новая вкладка...) доменом не считаются — заметки про них
    // не имеют смысла и только замусорили бы список.
    if (!url.isValid()) return QString();
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) return QString();

    QString host = url.host().toLower();
    if (host.isEmpty() || host == QLatin1String("localhost")) return QString();
    if (host.startsWith(QLatin1String("www."))) host.remove(0, 4);

    // Из домена — безопасное имя файла: выкидываем символы, которые
    // Windows не терпит в именах файлов (\ / : * ? " < > |).
    static const QRegularExpression badChars("[\\\\/:*?\"<>|]");
    host.remove(badChars);
    return host;
}

void NotesWidget::updateCurrentSite(const QUrl& url) {
    const QString domain = domainFromUrl(url);
    if (domain == currentDomain) return; // ничего не изменилось
    currentDomain = domain;

    if (domain.isEmpty()) {
        currentSiteLabel->setText(u8"\U0001F310 Текущий сайт: — (откройте обычный сайт)");
        attachBtn->setEnabled(false);
    }
    else {
        currentSiteLabel->setText(QString(u8"\U0001F310 Текущий сайт: %1").arg(domain));
        attachBtn->setEnabled(true);
    }
    // В списке подсветить домен активной вкладки (если заметка уже есть)
    refreshSitesList();
}

void NotesWidget::refreshSitesList() {
    if (!sitesList) return;
    sitesList->clear();

    QDir dir(sitesDirectory());
    if (dir.exists()) {
        const QStringList files = dir.entryList(QStringList() << "*.md", QDir::Files, QDir::Name);
        for (const QString& f : files) {
            QString domain = f;
            domain.chop(3); // срезаем ".md"
            QListWidgetItem* item = new QListWidgetItem(
                domain == currentDomain ? (u8"\U0001F4CC " + domain) : (u8"\U0001F310 " + domain));
            item->setData(Qt::UserRole, domain);
            if (domain == currentDomain) {
                QFont bold = item->font();
                bold.setBold(true);
                item->setFont(bold);
                item->setToolTip(u8"Это сайт активной вкладки");
            }
            sitesList->addItem(item);
        }
    }

    if (sitesList->count() == 0) {
        QListWidgetItem* empty = new QListWidgetItem(u8"(пока нет заметок по сайтам)");
        empty->setFlags(empty->flags() & ~Qt::ItemIsEnabled); // серо, не кликается
        sitesList->addItem(empty);
    }
}

void NotesWidget::attachNoteToCurrentSite() {
    if (currentDomain.isEmpty()) return;

    QDir dir(sitesDirectory());
    if (!dir.exists()) dir.mkpath(".");

    // Файла ещё нет — создаём с заголовком; есть — просто открываем
    const QString path = dir.filePath(currentDomain + ".md");
    QFile f(path);
    if (!f.exists() && f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << QString(u8"# Заметки о %1\n\n").arg(currentDomain);
        f.close();
    }

    loadSiteNoteIntoEditor(currentDomain);
    refreshSitesList();
}

void NotesWidget::loadSiteNoteIntoEditor(const QString& domain) {
    QFile f(QDir(sitesDirectory()).filePath(domain + ".md"));
    QString content;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        content = in.readAll();
        f.close();
    }

    loadedSiteDomain = domain;
    // textChanged от setPlainText зарядит siteSaveTimer — это безвредно:
    // сохранится ровно тот же текст, что и так лежит в файле.
    siteEditor->setPlainText(content);
    siteEditor->setEnabled(true);
    deleteSiteBtn->setEnabled(true);

    if (tabs) tabs->setCurrentIndex(1); // сразу показать вкладку «По сайтам»
}

void NotesWidget::openSiteNote() {
    QListWidgetItem* item = sitesList->currentItem();
    if (!item) return;
    const QString domain = item->data(Qt::UserRole).toString();
    if (domain.isEmpty()) return; // служебная строка «(пока нет заметок)»
    loadSiteNoteIntoEditor(domain);
}

void NotesWidget::saveSiteNote() {
    if (loadedSiteDomain.isEmpty()) return; // ничего не открыто — нечего сохранять
    QFile f(QDir(sitesDirectory()).filePath(loadedSiteDomain + ".md"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << siteEditor->toPlainText(); // заметки сайтов храним как текст
        f.close();
    }
}

void NotesWidget::deleteSiteNote() {
    if (loadedSiteDomain.isEmpty()) return;
    const QMessageBox::StandardButton btn = QMessageBox::question(this,
        u8"Удалить заметку",
        QString(u8"Удалить заметку о «%1»? Действие необратимо.").arg(loadedSiteDomain));
    if (btn != QMessageBox::Yes) return;

    QFile::remove(QDir(sitesDirectory()).filePath(loadedSiteDomain + ".md"));
    loadedSiteDomain.clear();
    siteEditor->clear();
    siteEditor->setEnabled(false);
    deleteSiteBtn->setEnabled(false);
    refreshSitesList();
}
