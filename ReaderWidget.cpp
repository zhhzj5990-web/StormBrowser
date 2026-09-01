#include "ReaderWidget.h"
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QMessageBox>

ReaderWidget::ReaderWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 20, 15, 20);
    layout->setAlignment(Qt::AlignTop);

    QLabel* title = new QLabel(u8"<b>📚 Storm Reader</b>", this);
    title->setStyleSheet("font-size: 18px; color: #a371f7; margin-bottom: 5px;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    QLabel* desc = new QLabel(u8"Локальная библиотека PDF и TXT книг.", this);
    desc->setWordWrap(true);
    desc->setAlignment(Qt::AlignCenter);
    desc->setStyleSheet("color: #adbac7; font-size: 13px; margin-bottom: 10px;");
    layout->addWidget(desc);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* addBookBtn = new QPushButton(u8"➕ Добавить книгу", this);
    addBookBtn->setStyleSheet("background-color: #a371f7; color: white; border-radius: 6px; padding: 8px; font-weight: bold;");

    QPushButton* openFolderBtn = new QPushButton(u8"📂 Папка", this);
    openFolderBtn->setStyleSheet("background-color: #ffc857; color: black; border-radius: 6px; padding: 8px; font-weight: bold;");

    connect(addBookBtn, &QPushButton::clicked, this, &ReaderWidget::importBook);
    connect(openFolderBtn, &QPushButton::clicked, this, &ReaderWidget::openBooksFolder);

    btnLayout->addWidget(addBookBtn);
    btnLayout->addWidget(openFolderBtn);
    layout->addLayout(btnLayout);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("background: transparent;");

    QWidget* contentWidget = new QWidget(scrollArea);
    booksLayout = new QVBoxLayout(contentWidget);
    booksLayout->setSpacing(8);
    booksLayout->setAlignment(Qt::AlignTop);

    scrollArea->setWidget(contentWidget);
    layout->addWidget(scrollArea);

    loadBooks();
}

// static — тело не изменилось, метод и раньше не трогал состояние экземпляра.
QString ReaderWidget::getBooksDir() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/StormBrowser/plugins/books";
    QDir dir(path);
    if (!dir.exists()) dir.mkpath(".");
    return path;
}

void ReaderWidget::clearLayout() {
    QLayoutItem* item;
    while ((item = booksLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void ReaderWidget::loadBooks() {
    clearLayout();
    QDir dir(getBooksDir());
    QStringList filters;
    filters << "*.pdf" << "*.txt";
    QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files);

    if (fileList.isEmpty()) {
        QLabel* msg = new QLabel(u8"Книги не найдены.\nНажмите «Добавить книгу»!", this);
        msg->setStyleSheet("color: #ffc857; font-size: 12px;");
        msg->setAlignment(Qt::AlignCenter);
        booksLayout->addWidget(msg);
        return;
    }

    for (const QFileInfo& fileInfo : fileList) {
        QString rawName = fileInfo.baseName();
        QString displayName = rawName.length() > 30 ? rawName.left(30) + "..." : rawName;

        QPushButton* btn = new QPushButton(u8"📖 " + displayName, this);
        btn->setMinimumHeight(40);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet("QPushButton { background-color: rgba(255,255,255,0.05); color: #a371f7; border: 1px solid rgba(163,113,247,0.3); border-radius: 6px; text-align: left; padding-left: 10px; font-weight: bold; }"
            "QPushButton:hover { background-color: rgba(163,113,247,0.2); }");

        QString filePath = fileInfo.absoluteFilePath();
        connect(btn, &QPushButton::clicked, this, [this, filePath]() {
            emit openBookRequested(QUrl::fromLocalFile(filePath));
            });

        booksLayout->addWidget(btn);
    }
}

void ReaderWidget::importBook() {
    QString filePath = QFileDialog::getOpenFileName(this, u8"Выберите книгу", "", "Books (*.pdf *.txt)");
    if (!filePath.isEmpty()) {
        QFileInfo fi(filePath);
        QString destPath = getBooksDir() + "/" + fi.fileName();

        if (QFile::exists(destPath)) {
            QFile::remove(destPath); // Перезаписываем, если книга уже импортирована
        }

        if (!QFile::copy(filePath, destPath)) {
            QMessageBox::warning(this, u8"Ошибка", u8"Не удалось добавить книгу.");
            return;
        }
        loadBooks();
    }
}

void ReaderWidget::openBooksFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(getBooksDir()));
}