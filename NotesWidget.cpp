#include "NotesWidget.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>

NotesWidget::NotesWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* titleLbl = new QLabel(u8"<b>📝 Блокнот</b>", this);
    layout->addWidget(titleLbl);

    textEdit = new QTextEdit(this);
    textEdit->setPlaceholderText(u8"Пиши свои мысли и заметки здесь...\nОни сохраняются автоматически.");
    layout->addWidget(textEdit);

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
}

QString NotesWidget::getNotesDirectory() {
    // Аналог get_user_data_dir("notes") из Python
    QString appData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return appData + "/StormBrowser/plugins/notes";
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
            u8"## 💻 Код и ссылки\n"
            u8"Вы можете выделять `командные строки` или писать полноценные ссылки.\n";
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