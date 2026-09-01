#pragma once
#ifndef READERWIDGET_H
#define READERWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QUrl>

class ReaderWidget : public QWidget {
    Q_OBJECT
public:
    explicit ReaderWidget(QWidget* parent = nullptr);

    // Публичный и static, чтобы другие модули, кладущие файлы в ту же общую
    // библиотеку (например, ResearchManager из модуля Deep Research Report),
    // могли получить ЭТОТ ЖЕ путь, не копируя его вычисление у себя. Метод
    // не трогает состояние экземпляра (только QStandardPaths/QDir), поэтому
    // static — его можно звать без создания ReaderWidget и из любого потока.
    static QString getBooksDir();

signals:
    void openBookRequested(const QUrl& fileUrl);

public slots:
    // Публичный, чтобы внешний код (MainWindow) мог попросить обновить
    // список книг после того, как в папку books/ извне добавили новый файл —
    // например, когда Deep Research Report дописал туда свежий отчёт.
    void loadBooks();

private slots:
    void importBook();
    void openBooksFolder();

private:
    void clearLayout();

    QVBoxLayout* booksLayout;
};

#endif // READERWIDGET_H