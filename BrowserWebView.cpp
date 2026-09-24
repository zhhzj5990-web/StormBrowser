#include "BrowserWebView.h"
#include "MainWindow.h"
#include "PasswordManager.h"
#include "DownloadManager.h"
#include <QMenu>
#include <QAction>
#include <QWebEnginePage>
#include <QWebEngineContextMenuRequest>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QStatusBar>
#include <QUrl>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QSettings>
#include <QMessageBox>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

BrowserWebView::BrowserWebView(MainWindow* mw, QWidget* parent)
    : QWebEngineView(parent), mainWindow(mw) {
}

void BrowserWebView::contextMenuEvent(QContextMenuEvent* event) {
    qCritical().noquote() << "[DIAG] real page profile:" << page()->profile()->objectName()
        << "enabled:" << page()->profile()->isSpellCheckEnabled()
        << "langs:" << page()->profile()->spellCheckLanguages();
    event->accept(); // Явно указываем, что мы сами обрабатываем клик
    QMenu menu(mainWindow); // Меняем this на mainWindow, чтобы вынести меню из-под контроля Chromium
    menu.setStyleSheet(mainWindow->styleSheet());

    QWebEnginePage* p = page();
    using WA = QWebEnginePage::WebAction;

    QWebEngineContextMenuRequest* request = lastContextMenuRequest();
    QString selectedText = request ? request->selectedText() : QString();
    bool isEditable = request && request->isContentEditable();
    bool hasSelection = p->hasSelection() || !selectedText.isEmpty();
    QUrl linkUrl = request ? request->linkUrl() : QUrl();
    bool isLink = !linkUrl.isEmpty();
    auto mediaType = request ? request->mediaType() : QWebEngineContextMenuRequest::MediaTypeNone;
    bool isImage = (mediaType == QWebEngineContextMenuRequest::MediaTypeImage);
    bool isMedia = (mediaType == QWebEngineContextMenuRequest::MediaTypeVideo
        || mediaType == QWebEngineContextMenuRequest::MediaTypeAudio);
    QString misspelledWord = request ? request->misspelledWord() : QString();

    auto addWebAction = [&](WA webAction, const QString& russianText) {
        QAction* action = p->action(webAction);
        if (action) {
            action->setText(russianText);
            action->setVisible(true); // Заставляем пункт быть видимым ВСЕГДА
            menu.addAction(action);
        }
        };

    // ==========================================
    // 1. НАВИГАЦИЯ — всегда сверху
    // ==========================================
    addWebAction(WA::Back, u8"Назад");
    addWebAction(WA::Forward, u8"Вперёд");
    QAction* stopAction = p->action(WA::Stop);
    if (stopAction && stopAction->isEnabled()) {
        addWebAction(WA::Stop, u8"Остановить загрузку");
    }
    else {
        addWebAction(WA::Reload, u8"Обновить");
    }
    menu.addSeparator();

    // ==========================================
    // 2. ОРФОГРАФИЯ
    // ==========================================
    if (!misspelledWord.isEmpty()) {
        QStringList suggestions = request->spellCheckerSuggestions();

        // Chromium/Hunspell сам, как правило, не отдаёт больше 5-10 вариантов,
        // так что этот предел почти никогда не обрезает реальный список —
        // но если движок вернёт больше, показываем их все, до 10 штук,
        // вместо старого жёсткого потолка в 5 (пользователи жаловались,
        // что нужного варианта не было видно).
        constexpr int kMaxSuggestions = 10;
        if (suggestions.size() > kMaxSuggestions) {
            suggestions = suggestions.mid(0, kMaxSuggestions);
        }

        if (!suggestions.isEmpty()) {
            for (const QString& suggestion : suggestions) {
                QAction* action = menu.addAction(suggestion);
                QFont font = action->font();
                font.setBold(true);
                action->setFont(font);

                connect(action, &QAction::triggered, this, [this, suggestion]() {
                    page()->replaceMisspelledWord(suggestion);
                    });
            }
        }
        else {
            menu.addAction(u8"Нет вариантов исправления")->setEnabled(false);
        }

        // "Добавить в словарь": слово копится в пользовательской дельте
        // словаря, чтобы движок больше не считал его ошибкой — словарь
        // растёт вместе с тем, как им пользуются.
        QAction* addToDictAction = menu.addAction(
            QString(u8"➕ Добавить «%1» в словарь").arg(misspelledWord));
        connect(addToDictAction, &QAction::triggered, this, [this, misspelledWord]() {
            addWordToUserDictionary(misspelledWord);
            });

        menu.addSeparator();
    }

    // ==========================================
    // 🤖 3. STORM AI — МЕНЮ ТЕПЕРЬ ВИДНО ВСЕГДА!
    // ==========================================
    auto triggerAi = [this, selectedText](const QString& actionName) {
        if (!selectedText.isEmpty()) {
            mainWindow->processAiAction(actionName, selectedText);
        }
        else {
            page()->runJavaScript(
                "window.getSelection().toString()",
                [this, actionName](const QVariant& result) {
                    mainWindow->processAiAction(actionName, result.toString());
                });
        }
        };

    QMenu* aiMenu = menu.addMenu(u8"🤖 Storm AI");

    // Отдельная кнопка "умного поиска по сайту" убрана: теперь ИИ в панели Storm AI
    // сам видит открытую страницу и, если нужно, сам действует на ней (клики, формы,
    // переходы по ссылкам) — просто напишите задачу в чат, отдельный триггер не нужен.

    connect(aiMenu->addAction(u8"🌍 Быстрый Перевод"), &QAction::triggered, this, [triggerAi]() { triggerAi("translate"); });
    connect(aiMenu->addAction(u8"🔍 Объяснить текст"), &QAction::triggered, this, [triggerAi]() { triggerAi("explain"); });
    connect(aiMenu->addAction(u8"📝 Сделать саммари (Выжимку)"), &QAction::triggered, this, [triggerAi]() { triggerAi("summarize"); });
    connect(aiMenu->addAction(u8"✉️ Написать пост/ответ"), &QAction::triggered, this, [triggerAi]() { triggerAi("write_post"); });
    connect(aiMenu->addAction(u8"✨ Улучшить текст (Грамматика)"), &QAction::triggered, this, [triggerAi]() { triggerAi("fix_grammar"); });
    connect(aiMenu->addAction(u8"💻 Объяснить код / Найти баги"), &QAction::triggered, this, [triggerAi]() { triggerAi("explain_code"); });

    menu.addSeparator();

    // ==========================================
    // 4. ПОЛЕ ВВОДА — клик по input/textarea/contenteditable
    // ==========================================
    if (isEditable) {
        addWebAction(WA::Undo, u8"Отменить");
        addWebAction(WA::Redo, u8"Повторить");
        menu.addSeparator();
        addWebAction(WA::Cut, u8"Вырезать");
        addWebAction(WA::Copy, u8"Копировать");
        addWebAction(WA::Paste, u8"Вставить");

        QAction* genPwdAction = menu.addAction(u8"🔑 Сгенерировать надежный пароль");
        connect(genPwdAction, &QAction::triggered, this, &BrowserWebView::generateAndSavePassword);

        QAction* insertAddressAction = menu.addAction(u8"📇 Вставить сохранённый адрес");
        connect(insertAddressAction, &QAction::triggered, this, &BrowserWebView::insertSavedAddress);

        addWebAction(WA::SelectAll, u8"Выделить всё");
        menu.addSeparator();
    }
    // ==========================================
    // 5. ВЫДЕЛЕННЫЙ ТЕКСТ
    // ==========================================
    else if (hasSelection) {
        addWebAction(WA::Copy, u8"Копировать");
        addWebAction(WA::SelectAll, u8"Выделить всё");
        menu.addSeparator();
    }

    // ==========================================
    // 6. ССЫЛКА
    // ==========================================
    if (isLink) {
        addWebAction(WA::OpenLinkInNewTab, u8"Открыть ссылку в новой вкладке");
        QAction* incognitoLinkAction = menu.addAction(u8"🕶 Открыть ссылку в инкогнито");
        connect(incognitoLinkAction, &QAction::triggered, this, [this, linkUrl]() { mainWindow->addNewTab(linkUrl, true); });

        addWebAction(WA::CopyLinkToClipboard, u8"Копировать адрес ссылки");
        addWebAction(WA::DownloadLinkToDisk, u8"Скачать файл по ссылке");

        QAction* downloadVideoLinkAction = menu.addAction(u8"🎥 Скачать видео по этой ссылке");
        connect(downloadVideoLinkAction, &QAction::triggered, this, [this, linkUrl]() { mainWindow->getDownloadManager()->startVideoDownload(linkUrl.toString()); });
        menu.addSeparator();
    }

    // ==========================================
    // 7. КАРТИНКА
    // ==========================================
    if (isImage) {
        QUrl imgUrl = request->mediaUrl();
        QAction* openImgTabAction = menu.addAction(u8"Открыть картинку в новой вкладке");
        connect(openImgTabAction, &QAction::triggered, this, [this, imgUrl]() { mainWindow->addNewTab(imgUrl); });

        addWebAction(WA::CopyImageToClipboard, u8"Копировать картинку");
        addWebAction(WA::CopyImageUrlToClipboard, u8"Копировать адрес картинки");
        addWebAction(WA::DownloadImageToDisk, u8"Сохранить картинку как...");
        menu.addSeparator();
    }

    // ==========================================
    // 8. МЕДИА (видео / аудио)
    // ==========================================
    if (isMedia) {
        addWebAction(WA::ToggleMediaPlayPause, u8"Пуск / Пауза");
        addWebAction(WA::ToggleMediaMute, u8"Выключить звук");
        addWebAction(WA::ToggleMediaLoop, u8"Повторять воспроизведение");
        addWebAction(WA::DownloadMediaToDisk, u8"Сохранить медиафайл как...");
        addWebAction(WA::CopyMediaUrlToClipboard, u8"Копировать адрес медиафайла");
        menu.addSeparator();
    }

    // ==========================================
    // 9. ПУСТОЕ МЕСТО СТРАНИЦЫ
    // ==========================================
    if (!isEditable && !hasSelection && !isLink && !isImage && !isMedia) {
        bool bookmarked = mainWindow->isCurrentPageBookmarked();
        QAction* bookmarkAction = menu.addAction(bookmarked ? u8"🗑 Удалить из закладок" : u8"🔖 Добавить в закладки");
        connect(bookmarkAction, &QAction::triggered, this, [this, bookmarked]() {
            if (bookmarked) mainWindow->removeCurrentBookmark();
            else mainWindow->addCurrentBookmark();
            });

        QAction* screenshotAction = menu.addAction(u8"📸 Сделать скриншот страницы");
        connect(screenshotAction, &QAction::triggered, mainWindow, &MainWindow::takeScreenshot);

        QAction* downloadVideoAction = menu.addAction(u8"🎥 Скачать видео с этой страницы");
        QUrl pageUrl = url();
        connect(downloadVideoAction, &QAction::triggered, this, [this, pageUrl]() {
            mainWindow->getDownloadManager()->startVideoDownload(pageUrl.toString());
            });
        menu.addSeparator();

        addWebAction(WA::SavePage, u8"Сохранить страницу как...");
        addWebAction(WA::SelectAll, u8"Выделить всё");
        addWebAction(WA::ViewSource, u8"Исходный код страницы");
    }

    // ==========================================
    // 10. DEVTOOLS / STORM SHIELD — внизу меню, вне зависимости от контекста
    // клика (как "Inspect" и "Block element" в обычных браузерах/адблокерах)
    // ==========================================
    menu.addSeparator();

    QAction* inspectAction = menu.addAction(u8"🔍 Просмотреть код");
    connect(inspectAction, &QAction::triggered, this, [this]() {
        mainWindow->inspectElementAt(this);
        });

    QPoint clickPos = event->pos();
    QAction* blockElementAction = menu.addAction(u8"🚫 Заблокировать элемент");
    connect(blockElementAction, &QAction::triggered, this, [this, clickPos]() {
        mainWindow->blockElementAt(this, clickPos);
        });

    menu.exec(event->globalPos());
}

void BrowserWebView::generateAndSavePassword() {
    PasswordManager* pm = mainWindow->getPasswordManager();
    QString newPassword = pm->generatePassword();
    QString domain = url().host();

    bool ok = false;
    QString login = QInputDialog::getText(
        mainWindow,
        u8"Генератор паролей Storm",
        QString(u8"Сгенерирован надежный пароль:\n%1\n\nВведите ваш Логин/Email для сайта %2:").arg(newPassword, domain),
        QLineEdit::Normal, "", &ok);

    if (!ok || login.trimmed().isEmpty()) return;

    pm->savePassword(domain, login.trimmed(), newPassword);

    QString safePwd = newPassword;
    safePwd.replace("'", "\\'");
    QString jsFill = QString(u8R"JS(
        (function() {
            let pwdInputs = document.querySelectorAll('input[type="password"]');
            pwdInputs.forEach(inp => {
                inp.value = '%1';
                inp.dispatchEvent(new Event('input', { bubbles: true }));
                inp.dispatchEvent(new Event('change', { bubbles: true }));
            });
        })();
    )JS").arg(safePwd);

    page()->runJavaScript(jsFill);
    mainWindow->statusBar()->showMessage(u8"✅ Пароль сгенерирован, сохранен и вставлен!", 5000);
}

void BrowserWebView::insertSavedAddress() {
    // Тот же QSettings-ключ, что читает/пишет SettingsBridge (getSavedAddressesJson/
    // addSavedAddress) — отдельного менеджера для адресов пока нет, чтобы не
    // плодить сущность только ради списка из нескольких полей.
    QJsonDocument doc = QJsonDocument::fromJson(QSettings().value("autofill/addresses").toByteArray());
    QJsonArray addresses = doc.isArray() ? doc.array() : QJsonArray();

    if (addresses.isEmpty()) {
        mainWindow->statusBar()->showMessage(
            u8"Нет сохранённых адресов — добавьте их в Настройках → Пароли", 5000);
        return;
    }

    QJsonObject chosen;
    if (addresses.size() == 1) {
        chosen = addresses.first().toObject();
    }
    else {
        QStringList labels;
        for (const QJsonValue& v : addresses) {
            QJsonObject a = v.toObject();
            QString label = a["fullName"].toString();
            if (!a["city"].toString().isEmpty()) label += " — " + a["city"].toString();
            labels << (label.trimmed().isEmpty() ? u8"(без имени)" : label);
        }
        bool ok = false;
        QString pick = QInputDialog::getItem(mainWindow, u8"Выбор адреса",
            u8"Какой адрес вставить?", labels, 0, false, &ok);
        if (!ok) return;
        chosen = addresses.at(labels.indexOf(pick)).toObject();
    }

    auto esc = [](QString s) { s.replace("\\", "\\\\").replace("'", "\\'"); return s; };

    // Эвристика по atrибуту autocomplete (стандартный способ, которым сами
    // сайты помечают поля форм для автозаполнения — тот же принцип
    // используют настоящие браузеры), с запасным поиском по name/id, если
    // autocomplete не проставлен.
    QString js = QString(u8R"JS(
        (function() {
            const data = {
                fullName: '%1', phone: '%2', email: '%3',
                addressLine: '%4', city: '%5', zip: '%6'
            };
            function fillAll(selector, value) {
                if (!value) return;
                document.querySelectorAll(selector).forEach(function(inp) {
                    inp.value = value;
                    inp.dispatchEvent(new Event('input', { bubbles: true }));
                    inp.dispatchEvent(new Event('change', { bubbles: true }));
                });
            }
            fillAll('[autocomplete="name"], input[name*="name" i], input[id*="name" i]', data.fullName);
            fillAll('[autocomplete="tel"], input[type="tel"], input[name*="phone" i], input[id*="phone" i]', data.phone);
            fillAll('[autocomplete="email"], input[type="email"], input[name*="email" i], input[id*="email" i]', data.email);
            fillAll('[autocomplete="address-line1"], input[name*="address" i], input[id*="address" i]', data.addressLine);
            fillAll('[autocomplete="address-level2"], input[name*="city" i], input[id*="city" i]', data.city);
            fillAll('[autocomplete="postal-code"], input[name*="zip" i], input[name*="postal" i], input[id*="zip" i]', data.zip);
        })();
    )JS").arg(esc(chosen["fullName"].toString()), esc(chosen["phone"].toString()), esc(chosen["email"].toString()),
    esc(chosen["addressLine"].toString()), esc(chosen["city"].toString()), esc(chosen["zip"].toString()));

    page()->runJavaScript(js);
    mainWindow->statusBar()->showMessage(u8"✅ Адрес вставлен (проверьте поля — эвристика не идеальна)", 5000);
}

// ==========================================
// Разрешения сайтов (камера/микрофон/геолокация/уведомления)
// ==========================================
namespace {
    QString permissionSettingsKey(const QUrl& origin, QWebEnginePage::Feature feature) {
        return QString("permissions/%1/%2").arg(int(feature)).arg(origin.host());
    }
}

QString BrowserWebView::featureDisplayName(QWebEnginePage::Feature feature) {
    switch (feature) {
    case QWebEnginePage::Geolocation: return u8"Геолокация";
    case QWebEnginePage::MediaAudioCapture: return u8"Микрофон";
    case QWebEnginePage::MediaVideoCapture: return u8"Камера";
    case QWebEnginePage::MediaAudioVideoCapture: return u8"Камера и микрофон";
    case QWebEnginePage::MouseLock: return u8"Захват курсора мыши";
    case QWebEnginePage::DesktopVideoCapture: return u8"Запись экрана";
    case QWebEnginePage::DesktopAudioVideoCapture: return u8"Запись экрана со звуком";
    case QWebEnginePage::Notifications: return u8"Уведомления";
    default: return u8"Разрешение";
    }
}

void BrowserWebView::handlePermissionRequest(QWebEnginePage* page, MainWindow* mw,
    const QUrl& securityOrigin, QWebEnginePage::Feature feature) {
    if (!page) return;

    const QString key = permissionSettingsKey(securityOrigin, feature);

    QSettings s;
    // Уже решали для этого сайта и этого разрешения раньше — применяем
    // сохранённый выбор молча, без повторного вопроса. Сбросить можно в
    // Настройках → Конфиденциальность → Разрешения сайтов.
    if (s.contains(key)) {
        bool allowed = s.value(key).toBool();
        page->setFeaturePermission(securityOrigin, feature,
            allowed ? QWebEnginePage::PermissionGrantedByUser : QWebEnginePage::PermissionDeniedByUser);
        return;
    }

    QMessageBox box(mw);
    box.setWindowTitle(u8"Запрос разрешения");
    box.setText(QString(u8"Сайт %1 запрашивает разрешение:\n%2")
        .arg(securityOrigin.host(), BrowserWebView::featureDisplayName(feature)));
    QPushButton* allowBtn = box.addButton(u8"Разрешить", QMessageBox::AcceptRole);
    QPushButton* blockBtn = box.addButton(u8"Заблокировать", QMessageBox::RejectRole);
    box.setDefaultButton(blockBtn);
    box.exec();

    bool allow = (box.clickedButton() == allowBtn);
    page->setFeaturePermission(securityOrigin, feature,
        allow ? QWebEnginePage::PermissionGrantedByUser : QWebEnginePage::PermissionDeniedByUser);
    s.setValue(key, allow);
}

// ==========================================
// Пользовательский словарь спеллчекера
// ==========================================
//
// У Qt WebEngine (в отличие от нативного Chrome/Edge) НЕТ готового
// WebAction'а вида "Add to dictionary" — в публичном API его просто не
// экспортировали (это известное ограничение, с той же проблемой сталкиваются
// и другие Chromium-обёртки, например WebView2). Поэтому "рост словаря"
// приходится собирать руками поверх штатного механизма Hunspell/Chromium:
//
//   1. У каждого языкового словаря есть необязательный файл-дельта
//      <lang>.dic_delta — список дополнительных слов (по одному на строку,
//      формат как в .dic), которые подмешиваются в словарь при сборке,
//      не трогая основной .dic. Именно туда мы и дописываем новые слова.
//   2. Готовый .bdic, который движок грузит во время работы, — это
//      скомпилированные .aff+.dic+.dic_delta. Пересборка делается штатной
//      утилитой qwebengine_convert_dict, которая идёт в поставке Qt:
//          qwebengine_convert_dict <lang>.aff <lang>.bdic
//      (сама подхватит одноимённые .dic и .dic_delta из той же папки).
//
// ВАЖНО — что нужно подготовить, чтобы это заработало:
//   • Рядом с приложением должны лежать ИСХОДНИКИ словаря — .aff и .dic
//     (не только итоговый .bdic!) — см. userDictionarySourceDir().
//   • В комплект поставки приложения нужно добавить сам бинарник
//     qwebengine_convert_dict (в Qt 6 он обычно лежит в libexec Qt) —
//     см. convertDictToolPath().
//   • Официальной гарантии, что Chromium подхватит пересобранный .bdic
//     "на лету" без перезапуска процесса рендерера, в документации Qt нет.
//     Ниже мы пытаемся форсировать перечитывание через повторный вызов
//     setSpellCheckLanguages(), но если подсветка слова исчезнет не сразу —
//     это ожидаемо, слово уже сохранено и точно подхватится после
//     следующего перезапуска приложения.

QString BrowserWebView::userDictionarySourceDir() const {
    // TODO: вынести в конфигурацию приложения. По умолчанию — папка рядом
    // с exe; именно сюда при сборке инсталлятора нужно класть исходные
    // .aff/.dic для языка(ов) спеллчекера.
    return QCoreApplication::applicationDirPath() + "/spellcheck_src";
}

QString BrowserWebView::convertDictToolPath() const {
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + "/qwebengine_convert_dict.exe";
#else
    return QCoreApplication::applicationDirPath() + "/qwebengine_convert_dict";
#endif
}

// НАЙДЕННЫЙ БАГ: раньше здесь всегда бралось langs.first(), то есть первый
// язык из списка, настроенного в профиле (в MainWindow это "en-US, ru-RU" —
// см. m_mainProfile->setSpellCheckLanguages()). На практике это значило,
// что ЛЮБОЕ слово — хоть русское, хоть английское — всегда дописывалось
// в английскую дельту (en_US.dic_delta) и пересобиралось в en_US.bdic.
//
// Для русских слов (а это подавляющее большинство того, что реально
// добавляют через это меню) это давало двойной эффект:
//   1) Слово попадало не в тот словарь — ru_RU словарь не менялся вообще,
//      то есть само слово всё равно продолжало подчёркиваться как ошибка.
//   2) Хуже: кириллица дописывалась в дельту английского словаря, чьи
//      .aff/.dic обычно объявляют латинский алфавит/однобайтовую
//      кодировку. qwebengine_convert_dict завершался с кодом 0 (ошибки
//      не было), но собранный en_US.bdic получался повреждённым для
//      Chromium-загрузчика словарей. А последующий сброс/перечитывание
//      языков профиля (setSpellCheckLanguages({}) → setSpellCheckLanguages(langs))
//      подхватывало уже испорченный файл и роняло спеллчекер целиком —
//      именно поэтому пропадало подчёркивание у ДРУГИХ слов сразу же,
//      и у новых слов — до перезапуска приложения (когда битый .bdic
//      с диска подхватывался заново и падал повторно).
//
// Исправление: определяем, к какому из настроенных языков реально
// относится слово, по алфавиту (кириллица → ru*, иначе — первый
// нероссийский язык из списка), и пишем дельту именно туда.
QString BrowserWebView::pickDictionaryLanguageFor(const QString& word, const QStringList& langs) const {
    bool hasCyrillic = false;
    for (const QChar& ch : word) {
        // Диапазон кириллицы (основной блок Unicode U+0400–U+04FF)
        // достаточен для русского языка.
        if (ch.unicode() >= 0x0400 && ch.unicode() <= 0x04FF) {
            hasCyrillic = true;
            break;
        }
    }

    for (const QString& lang : langs) {
        const bool langIsRussian = lang.startsWith(u8"ru", Qt::CaseInsensitive);
        if (hasCyrillic == langIsRussian) {
            return lang;
        }
    }

    // Ни один настроенный язык не подошёл по алфавиту (например, слово
    // смешанное или язык профиля не ru/en) — не падаем, а откатываемся
    // на старое поведение (первый язык списка), чтобы функция всё равно
    // что-то сохранила.
    return langs.first();
}

void BrowserWebView::addWordToUserDictionary(const QString& word) {
    const QString trimmed = word.trimmed();
    if (trimmed.isEmpty()) return;

    QWebEngineProfile* profile = page()->profile();
    const QStringList langs = profile->spellCheckLanguages();
    if (langs.isEmpty()) {
        mainWindow->statusBar()->showMessage(u8"⚠ Не задан язык словаря — не могу добавить слово", 5000);
        return;
    }

    // Имена .bdic используют подчёркивание ("ru_RU"), а код языка в
    // spellCheckLanguages() приходит через дефис ("ru-RU") — приводим.
    // Раньше здесь стояло langs.first() — см. подробный разбор бага в
    // комментарии над pickDictionaryLanguageFor() выше.
    QString baseName = pickDictionaryLanguageFor(trimmed, langs);
    baseName.replace('-', '_');

    const QString srcDir = userDictionarySourceDir();
    const QString affPath = srcDir + "/" + baseName + ".aff";
    const QString dicPath = srcDir + "/" + baseName + ".dic";
    const QString deltaPath = srcDir + "/" + baseName + ".dic_delta";

    // Проверяем ОБА исходника, а не только .aff: qwebengine_convert_dict
    // молча "успешно" соберёт .bdic даже без базового .dic (по факту —
    // только из дельты), а такой урезанный словарь потом ведёт себя как
    // повреждённый — именно так выглядела часть исходного бага.
    if (!QFile::exists(affPath) || !QFile::exists(dicPath)) {
        mainWindow->statusBar()->showMessage(
            QString(u8"⚠ Не найдены исходники словаря (%1.aff/.dic) в %2")
            .arg(baseName, srcDir), 8000);
        return;
    }

    // Не дублируем слово, если оно уже в дельте.
    QStringList existingWords;
    {
        QFile deltaRead(deltaPath);
        if (deltaRead.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&deltaRead);
            in.setEncoding(QStringConverter::Utf8);
            while (!in.atEnd()) {
                existingWords << in.readLine().split('/').first().trimmed();
            }
        }
    }
    if (existingWords.contains(trimmed, Qt::CaseInsensitive)) {
        mainWindow->statusBar()->showMessage(QString(u8"«%1» уже есть в словаре").arg(trimmed), 3000);
        return;
    }

    QDir().mkpath(srcDir);
    QFile deltaWrite(deltaPath);
    if (!deltaWrite.open(QIODevice::Append | QIODevice::Text)) {
        mainWindow->statusBar()->showMessage(u8"⚠ Не удалось открыть файл словаря для записи", 5000);
        return;
    }
    {
        QTextStream out(&deltaWrite);
        out.setEncoding(QStringConverter::Utf8);
        out << trimmed << "\n";
    }
    deltaWrite.close();

    // Пересобираем .bdic из .aff + .dic + .dic_delta в папку, откуда движок
    // грузит словари во время работы (qtwebengine_dictionaries рядом с exe).
    const QString bdicOutDir = QCoreApplication::applicationDirPath() + "/qtwebengine_dictionaries";
    QDir().mkpath(bdicOutDir);
    const QString bdicOutPath = bdicOutDir + "/" + baseName + ".bdic";

    QProcess convert;
    convert.setProgram(convertDictToolPath());
    convert.setArguments({ affPath, bdicOutPath });
    convert.start();
    if (!convert.waitForStarted(3000) || !convert.waitForFinished(15000) || convert.exitCode() != 0) {
        mainWindow->statusBar()->showMessage(
            QString(u8"⚠ Слово сохранено, но не удалось пересобрать словарь сейчас (%1). "
                u8"Подключится при следующем запуске приложения.")
            .arg(QString::fromUtf8(convert.readAllStandardError())), 8000);
        return;
    }

    // Пробуем заставить движок перечитать словари без перезапуска.
    // Не гарантировано публичным API — воспринимайте как best-effort.
    profile->setSpellCheckLanguages({});
    profile->setSpellCheckLanguages(langs);

    mainWindow->statusBar()->showMessage(QString(u8"✅ «%1» добавлено в словарь").arg(trimmed), 5000);
}