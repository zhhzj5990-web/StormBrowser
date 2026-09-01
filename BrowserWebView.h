#pragma once
#include <QWebEngineView>

// Предварительное объявление вместо #include "MainWindow.h", чтобы не тянуть весь заголовок
class MainWindow;

class BrowserWebView : public QWebEngineView {
    Q_OBJECT

public:
    explicit BrowserWebView(MainWindow* mw, QWidget* parent = nullptr);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // Генерирует надёжный пароль, сохраняет его в PasswordManager и вставляет
    // во все поля password на текущей странице.
    void generateAndSavePassword();

    // Добавляет слово в пользовательский словарь спеллчекера: дописывает
    // его в .dic_delta текущего языка, пересобирает .bdic через
    // qwebengine_convert_dict и просит профиль перечитать словари.
    // Требует, чтобы рядом с приложением лежали исходники словаря
    // (.aff/.dic, см. userDictionarySourceDir()) и сам конвертер
    // (см. convertDictToolPath()) — см. комментарии в .cpp.
    void addWordToUserDictionary(const QString& word);

    // Из нескольких языков, настроенных в профиле (en-US, ru-RU, ...),
    // выбирает тот, к которому реально относится слово (по алфавиту:
    // кириллица/латиница), вместо того чтобы слепо брать langs.first().
    // См. подробный комментарий в .cpp — раньше это было источником бага,
    // из-за которого русские слова дописывались в английский словарь.
    QString pickDictionaryLanguageFor(const QString& word, const QStringList& langs) const;

    // Каталог с исходниками словаря (.aff/.dic/.dic_delta) для текущего
    // языка спеллчекера. Это НЕ каталог с готовыми .bdic, которые движок
    // использует во время работы (см. QTWEBENGINE_DICTIONARIES_PATH) —
    // исходники нужны отдельно, только для пересборки.
    QString userDictionarySourceDir() const;

    // Путь к утилите qwebengine_convert_dict, которую нужно класть рядом
    // с приложением при сборке инсталлятора (она входит в поставку Qt).
    QString convertDictToolPath() const;

    MainWindow* mainWindow;
};