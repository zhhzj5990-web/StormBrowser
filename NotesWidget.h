#pragma once
#ifndef NOTESWIDGET_H
#define NOTESWIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QString>
#include <QTimer>

class NotesWidget : public QWidget {
    Q_OBJECT
public:
    explicit NotesWidget(QWidget* parent = nullptr);

private slots:
    void scheduleSave();
    void saveNotes();

private:
    void loadNotes();
    QString getNotesDirectory();

    QTextEdit* textEdit;
    QString noteFilePath;
    QTimer* saveTimer;
};

#endif // NOTESWIDGET_H