#include "ArcadeWidget.h"
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QHBoxLayout>
#include <QMessageBox>

ArcadeWidget::ArcadeWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 20, 15, 20);
    layout->setAlignment(Qt::AlignTop);

    QLabel* title = new QLabel("<b>🕹️ Storm Arcade</b>", this);
    title->setStyleSheet("font-size: 18px; color: #0078D7; margin-bottom: 5px;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    QLabel* desc = new QLabel("Классические Flash-игры из вашей коллекции.", this);
    desc->setWordWrap(true);
    desc->setAlignment(Qt::AlignCenter);
    desc->setStyleSheet("color: #adbac7; font-size: 13px; margin-bottom: 10px;");
    layout->addWidget(desc);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* addGameBtn = new QPushButton("➕ Добавить", this);
    addGameBtn->setStyleSheet("background-color: #28a745; color: white; border-radius: 6px; padding: 8px; font-weight: bold;");

    QPushButton* openFolderBtn = new QPushButton("📂 Папка", this);
    openFolderBtn->setStyleSheet("background-color: #ffc857; color: black; border-radius: 6px; padding: 8px; font-weight: bold;");

    connect(addGameBtn, &QPushButton::clicked, this, &ArcadeWidget::importGame);
    connect(openFolderBtn, &QPushButton::clicked, this, &ArcadeWidget::openGamesFolder);

    btnLayout->addWidget(addGameBtn);
    btnLayout->addWidget(openFolderBtn);
    layout->addLayout(btnLayout);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("background: transparent;");

    QWidget* contentWidget = new QWidget(scrollArea);
    gamesLayout = new QVBoxLayout(contentWidget);
    gamesLayout->setSpacing(8);
    gamesLayout->setAlignment(Qt::AlignTop);

    scrollArea->setWidget(contentWidget);
    layout->addWidget(scrollArea);

    QLabel* info = new QLabel("<i>Игры загружаются локально через Ruffle.</i>", this);
    info->setWordWrap(true);
    info->setAlignment(Qt::AlignCenter);
    info->setStyleSheet("color: #666; margin-top: 15px; font-size: 11px;");
    layout->addWidget(info);

    loadGames();
}

QString ArcadeWidget::getGamesDir() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/StormBrowser/plugins/games";
    QDir dir(path);
    if (!dir.exists()) dir.mkpath(".");
    return path;
}

void ArcadeWidget::clearLayout() {
    QLayoutItem* item;
    while ((item = gamesLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void ArcadeWidget::loadGames() {
    clearLayout();
    QDir dir(getGamesDir());
    QStringList filters;
    filters << "*.swf";
    QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files, QDir::Name);

    if (fileList.isEmpty()) {
        QLabel* msg = new QLabel("Игры не найдены.\nНажмите «Добавить», чтобы загрузить .swf файлы!", this);
        msg->setStyleSheet("color: #ffc857; font-size: 12px;");
        msg->setAlignment(Qt::AlignCenter);
        gamesLayout->addWidget(msg);
        return;
    }

    for (const QFileInfo& fileInfo : fileList) {
        // completeBaseName() (не baseName()!) — иначе имя файла вида "sonic.2.swf"
        // обрежется до "sonic", потеряв всё после первой точки
        QString rawName = fileInfo.completeBaseName().replace("_", " ");
        // Делаем первую букву заглавной
        if (!rawName.isEmpty()) rawName[0] = rawName[0].toUpper();

        QPushButton* btn = new QPushButton("🕹️ " + rawName, this);
        btn->setMinimumHeight(40);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet("QPushButton { background-color: rgba(255,255,255,0.05); color: #0078D7; border: 1px solid rgba(0,120,215,0.3); border-radius: 6px; text-align: left; padding-left: 15px; font-weight: bold; }"
            "QPushButton:hover { background-color: rgba(0,120,215,0.2); }");

        QString filename = fileInfo.fileName();
        connect(btn, &QPushButton::clicked, this, [this, filename]() {
            emit openGameRequested("storm-game:" + filename);
            });

        gamesLayout->addWidget(btn);
    }
}

void ArcadeWidget::importGame() {
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите Flash-игру", "", "Flash Games (*.swf)");
    if (filePath.isEmpty())
        return;

    QFileInfo fi(filePath);
    QString safeFilename = fi.fileName().replace(" ", "_");
    QString destPath = getGamesDir() + "/" + safeFilename;

    // Файл уже находится в папке игр (например, пользователь открыл её же
    // в диалоге выбора) — копировать некуда, просто обновляем список
    if (QFileInfo(filePath).canonicalFilePath() == QFileInfo(destPath).canonicalFilePath()) {
        loadGames();
        return;
    }

    // QFile::copy() ничего не делает и возвращает false, если файл с таким
    // именем уже существует — раньше это приводило к тихому "не-импорту"
    // без единого сообщения об ошибке. Удаляем старую версию перед копированием.
    if (QFile::exists(destPath) && !QFile::remove(destPath)) {
        QMessageBox::warning(this, "Ошибка", "Не удалось заменить существующий файл игры.");
        return;
    }

    if (!QFile::copy(filePath, destPath)) {
        QMessageBox::warning(this, "Ошибка", "Не удалось скопировать файл игры.");
        return;
    }

    loadGames();
}

void ArcadeWidget::openGamesFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(getGamesDir()));
}