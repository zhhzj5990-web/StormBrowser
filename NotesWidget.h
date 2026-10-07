#pragma once
#ifndef NOTESWIDGET_H
#define NOTESWIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QString>
#include <QTimer>
#include <QUrl>

class QTabWidget;
class QListWidget;
class QPushButton;
class QLabel;

class NotesWidget : public QWidget {
    Q_OBJECT
public:
    explicit NotesWidget(QWidget* parent = nullptr);

    // v1.2.9 Web-clipper: дописать вырезку с страницы в блокнот — с датой и
    // строкой «Источник: <url>». Вызывается из контекстного меню страницы
    // («📝 Сохранить в Заметки», см. BrowserWebView::saveClippingToNotes).
    void appendClipping(const QString& text, const QUrl& sourceUrl);

    // v1.2.9 «Заметки о сайтах»: панель узнаёт домен активной вкладки этого
    // окна — MainWindow дёргает это при смене вкладки. Сама ничего не
    // опрашивает (никаких таймеров и поллинга). Внутренние storm://-страницы
    // доменом не считаются — заметки только про обычные сайты http(s).
    void updateCurrentSite(const QUrl& url);

private slots:
    void scheduleSave();
    void saveNotes();
    void saveSiteNote();
    void openSiteNote();
    void deleteSiteNote();
    void attachNoteToCurrentSite();

private:
    void loadNotes();
    QString getNotesDirectory();
    QString sitesDirectory();
    QString domainFromUrl(const QUrl& url) const;
    void refreshSitesList();
    void loadSiteNoteIntoEditor(const QString& domain);

    QTextEdit* textEdit;
    QString noteFilePath;
    QTimer* saveTimer;

    // --- v1.2.9: вкладка «🌐 По сайтам» ---
    QTabWidget* tabs = nullptr;
    QLabel* currentSiteLabel = nullptr;
    QPushButton* attachBtn = nullptr;
    QListWidget* sitesList = nullptr;
    QTextEdit* siteEditor = nullptr;
    QPushButton* deleteSiteBtn = nullptr;
    QString currentDomain;     // домен активной вкладки (может быть пуст)
    QString loadedSiteDomain;  // чья заметка сейчас открыта в siteEditor
    QTimer* siteSaveTimer = nullptr;
};

#endif // NOTESWIDGET_H
