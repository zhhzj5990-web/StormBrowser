#pragma once
#include <QWebEngineView>
#include <QWebEnginePage>

// Предварительное объявление вместо #include "MainWindow.h", чтобы не тянуть весь заголовок
class MainWindow;

class BrowserWebView : public QWebEngineView {
    Q_OBJECT

public:
    explicit BrowserWebView(MainWindow* mw, QWidget* parent = nullptr);

    // Разрешения сайта (камера/микрофон/геолокация/уведомления и т.д.) — НЕ
    // подключает сигнал сама (это уже делает существующий обработчик
    // featurePermissionRequested в MainWindow::addNewTab(), который решает
    // локально "по умолчанию" случаи вроде буфера обмена и доверенной
    // страницы Storm Talk на localhost) — вызывается ИЗ него как запасной
    // вариант для всех остальных сайтов и разрешений, которые раньше
    // молча отклонялись без вопроса.
    //
    // Выбор пользователя (разрешить/заблокировать) запоминается в QSettings
    // ("permissions/<feature>/<host>"), чтобы не спрашивать повторно —
    // список выданных разрешений можно посмотреть и отозвать в Настройках →
    // Конфиденциальность (см. SettingsBridge::getSitePermissionsJson()).
    static void handlePermissionRequest(QWebEnginePage* page, MainWindow* mw,
        const QUrl& securityOrigin, QWebEnginePage::Feature feature);

    // Человекочитаемое название разрешения — используется и в диалоге
    // запроса здесь, и в списке разрешений на странице настроек.
    static QString featureDisplayName(QWebEnginePage::Feature feature);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // Генерирует надёжный пароль, сохраняет его в PasswordManager и вставляет
    // во все поля password на текущей странице.
    void generateAndSavePassword();

    // Вставляет один из сохранённых адресов (Настройки → Пароли →
    // Автозаполнение) в поля формы на текущей странице — эвристика по
    // атрибуту autocomplete (name/email/tel/address-line1/address-level2/
    // postal-code), с запасным поиском по name/id, если autocomplete не
    // проставлен. Если сохранённых адресов несколько — сначала спрашивает,
    // какой из них использовать.
    void insertSavedAddress();

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