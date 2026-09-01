#include "StormWebPage.h"
#include "PasswordCaptureBridge.h"
#include "PasswordSuggestBridge.h"
#include <QDebug>
#include <QWebChannel>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QFile>
#include <QTextStream>

StormWebPage::StormWebPage(QWebEngineProfile* profile, MainWindow* mw, QObject* parent)
    : QWebEnginePage(profile, parent), m_mainWindow(mw)
{
    // Обработка ошибок SSL
    connect(this, &QWebEnginePage::certificateError,
        this, &StormWebPage::handleCertificateError);

    // --- ВАЖНО: Разрешаем WebRTC доступ к камере и микрофону ---
    connect(this, &QWebEnginePage::featurePermissionRequested, this, [this](const QUrl& securityOrigin, QWebEnginePage::Feature feature) {
        if (feature == QWebEnginePage::MediaAudioCapture ||
            feature == QWebEnginePage::MediaVideoCapture ||
            feature == QWebEnginePage::MediaAudioVideoCapture) {

            // Автоматически даем разрешение от лица пользователя
            this->setFeaturePermission(securityOrigin, feature, QWebEnginePage::PermissionGrantedByUser);
        }
        });

    // ==========================================================
    // Авто-перехват и авто-подстановка логина/пароля на обычных сайтах.
    // Мост регистрируется в ИЗОЛИРОВАННОМ мире (ApplicationWorld), чтобы
    // JS самого сайта не мог получить к нему доступ — только наш
    // собственный инжектированный скрипт, работающий в этом же мире.
    // ==========================================================
    QWebChannel* pwChannel = new QWebChannel(this);
    PasswordCaptureBridge* pwBridge = new PasswordCaptureBridge(m_mainWindow, this);
    pwChannel->registerObject("pwCapture", pwBridge);
    this->setWebChannel(pwChannel, QWebEngineScript::ApplicationWorld);

    connect(pwBridge, &PasswordCaptureBridge::credentialsCaptured,
        this, &StormWebPage::credentialsCaptured);

    // ==========================================================
    // Подсказка сгенерированного пароля при РЕГИСТРАЦИИ (не путать с
    // pwCapture выше — тот про сохранение ПОСЛЕ входа). Аналог Python
    // setup_password_save_handler(). Отдельный объект на ТОМ ЖЕ канале —
    // заводить второй QWebChannel не нужно.
    // ==========================================================
    PasswordSuggestBridge* pwSuggestBridge = new PasswordSuggestBridge(this);
    pwChannel->registerObject("pwSuggest", pwSuggestBridge);
    connect(pwSuggestBridge, &PasswordSuggestBridge::passwordSuggestionRequested,
        this, &StormWebPage::passwordSuggestionRequested);

    // Скрипт вставляется ОДИН РАЗ на профиль, аналогично StormPasswordCapture
    // ниже. В Python это применялось к ОБОИМ профилям (profile и
    // private_profile) — здесь это устроено так же само по себе, поскольку
    // StormWebPage создаётся и для обычных, и для инкогнито-вкладок, и функция
    // ничего не сохраняет без явного согласия пользователя (см. MainWindow).
    if (profile && profile->scripts()->find("StormPasswordSuggest").isEmpty()) {
        QString qwebchannelJsSuggest;
        QFile qwcFileSuggest(":/qtwebchannel/qwebchannel.js");
        if (qwcFileSuggest.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qwebchannelJsSuggest = QTextStream(&qwcFileSuggest).readAll();
            qwcFileSuggest.close();
        }

        // Логика перенесена из script_injector.py -> setup_password_save_handler(),
        // с двумя отличиями от оригинала:
        // Первое: QWebChannel создаётся ОДИН РАЗ с повтором до готовности
        //    transport'а (initSuggestBridge), а не заново на каждый фокус на
        //    поле пароля, как было в Python, — это и есть основной паттерн,
        //    уже принятый в этом файле для StormPasswordCapture ниже.
        // Второе: мир исполнения — ApplicationWorld, а не MainWorld (см.
        //    пояснение в начале конструктора про pwCapture) — иначе сайт
        //    получил бы прямой доступ к мосту, что было бы шагом назад в безопасности.
        QString suggestJs = u8R"JS(
(function() {
    if (window.__stormPwSuggestInstalled) return;
    window.__stormPwSuggestInstalled = true;

    let suggestBridge = null;

    function initSuggestBridge() {
        if (typeof QWebChannel === 'undefined' || typeof qt === 'undefined' || !qt.webChannelTransport) {
            setTimeout(initSuggestBridge, 300);
            return;
        }
        new QWebChannel(qt.webChannelTransport, function(channel) {
            suggestBridge = channel.objects.pwSuggest;
        });
    }
    initSuggestBridge();

    document.addEventListener('focusin', function(e) {
        const el = e.target;
        if (!el || el.tagName !== 'INPUT' || (el.type || '').toLowerCase() !== 'password') return;

        const isRegistration = el.autocomplete === 'new-password' ||
            document.querySelectorAll('input[type="password"]').length >= 2;
        if (!isRegistration || el.hasAttribute('data-storm-suggested')) return;

        el.setAttribute('data-storm-suggested', 'true');

        const userField = document.querySelector(
            'input[type="text"], input[type="email"], input[name*="user"], input[name*="login"]');
        const login = userField ? userField.value : '';

        // Домен (hostname), а НЕ полный URL — PasswordManager хранит и
        // сопоставляет записи именно по домену (см. StormPasswordCapture
        // выше: reportCredentials/requestAutofill используют тот же формат).
        if (suggestBridge && suggestBridge.suggestPassword) {
            suggestBridge.suggestPassword(window.location.hostname, login);
        }
    }, true);
})();
)JS";

        QWebEngineScript suggestScript;
        suggestScript.setName("StormPasswordSuggest");
        suggestScript.setInjectionPoint(QWebEngineScript::DocumentReady);
        suggestScript.setWorldId(QWebEngineScript::ApplicationWorld);
        suggestScript.setRunsOnSubFrames(false);
        suggestScript.setSourceCode(qwebchannelJsSuggest + "\n" + suggestJs);
        profile->scripts()->insert(suggestScript);
    }

    // Скрипт вставляется ОДИН РАЗ на профиль (не на страницу) — иначе при
    // каждой новой вкладке добавлялся бы ещё один слушатель, и один и тот же
    // сабмит/клик вызывал бы reportCredentials() многократно.
    if (profile && profile->scripts()->find("StormPasswordCapture").isEmpty()) {
        QString qwebchannelJs;
        QFile qwcFile(":/qtwebchannel/qwebchannel.js");
        if (qwcFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qwebchannelJs = QTextStream(&qwcFile).readAll();
            qwcFile.close();
        }

        QString listenerJs = u8R"JS(
(function() {
    if (window.__stormPwCaptureInstalled) return;
    window.__stormPwCaptureInstalled = true;

    console.log('[PWCAPTURE] Скрипт установлен на ' + window.location.hostname);

    let bridge = null;
    let lastLoginValue = '';

    // Многие сайты (email/телефон) делают вход в ДВА ШАГА: сначала логин,
    // жмёшь "Далее" — поле логина исчезает из DOM или становится текстом,
    // ПОТОМ отдельно появляется поле пароля. К моменту отправки пароля
    // само поле логина уже не найти. Поэтому слушаем ввод по ВСЕЙ странице
    // и запоминаем последнее значение, введённое в похожее на логин поле,
    // пока оно ещё существует.
    // УНИВЕРСАЛЬНО: не полагаемся на конкретные атрибуты (type/name/autocomplete) —
    // слишком много сайтов их не проставляют или называют по-своему.
    // Запоминаем ЛЮБОЕ текстовое поле с непустым значением, кроме явно
    // непричастных типов (пароль/скрытое/чекбокс/радио/кнопка).
    const NON_LOGIN_TYPES = ['password', 'hidden', 'checkbox', 'radio', 'submit', 'button', 'file', 'range', 'color', 'date', 'time'];
    document.addEventListener('input', function(e) {
        const el = e.target;
        if (!el || el.tagName !== 'INPUT') return;
        const type = (el.type || 'text').toLowerCase();
        if (NON_LOGIN_TYPES.includes(type)) return;

        if (el.value && el.value.trim()) {
            lastLoginValue = el.value;
            console.log('[PWCAPTURE] Запомнили значение поля (type=' + type + '): "' + el.value + '"');
        }
    }, true);

    // Ищем логин РЯДОМ с полем пароля, поднимаясь по DOM вверх небольшими
    // шагами — а НЕ сразу по всему document. Раньше при отсутствии тега
    // <form> (JS-модалки логина без семантической формы — частый случай)
    // поиск по всей странице мог случайно найти совершенно не относящееся
    // поле (например, поиск по сайту в шапке) вместо реального поля логина.
    function findNearbyLoginField(pwdField) {
        let container = pwdField.parentElement;
        let depth = 0;
        const MAX_DEPTH = 8;

        while (container && depth < MAX_DEPTH) {
            const inputs = Array.from(container.querySelectorAll('input'));
            const candidates = inputs.filter(function(el) {
                const type = (el.type || 'text').toLowerCase();
                return el !== pwdField && !NON_LOGIN_TYPES.includes(type) && !el.disabled;
            });

            if (candidates.length) {
                const withValue = candidates.find(function(el) { return el.value && el.value.trim(); });
                return withValue || candidates[0];
            }

            container = container.parentElement;
            depth++;
        }
        return null;
    }

    function reportFromForm(container, source) {
        try {
            const pwdField = container.querySelector('input[type="password"]');
            if (!pwdField) {
                console.log('[PWCAPTURE] ' + source + ': поле пароля не найдено в контейнере');
                return;
            }
            if (!pwdField.value) {
                console.log('[PWCAPTURE] ' + source + ': поле пароля найдено, но оно пустое');
                return;
            }
            const loginField = findNearbyLoginField(pwdField);
            let login = loginField ? loginField.value : '';

            // Поле логина не нашлось (или пустое) в текущем DOM — используем
            // запомненное ранее значение (двухшаговый вход, см. выше).
            if (!login && lastLoginValue) {
                login = lastLoginValue;
                console.log('[PWCAPTURE] Поле логина не найдено в DOM — используем запомненное значение');
            }

            console.log('[PWCAPTURE] ' + source + ': отправляем данные, домен=' + window.location.hostname + ', логин="' + login + '", пароль_длина=' + pwdField.value.length);

            if (!bridge) {
                console.log('[PWCAPTURE] ОШИБКА: мост ещё не подключён, данные потеряны!');
                return;
            }
            if (!bridge.reportCredentials) {
                console.log('[PWCAPTURE] ОШИБКА: у моста нет метода reportCredentials!');
                return;
            }
            bridge.reportCredentials(window.location.hostname, login, pwdField.value);
            console.log('[PWCAPTURE] reportCredentials() вызван успешно');
        } catch (err) {
            console.log('[PWCAPTURE] ИСКЛЮЧЕНИЕ в reportFromForm: ' + err.message);
        }
    }

    document.addEventListener('submit', function(e) {
        console.log('[PWCAPTURE] Событие submit сработало');
        const form = e.target;
        if (form && form.querySelectorAll) reportFromForm(form, 'submit');
    }, true);

    document.addEventListener('click', function(e) {
        try {
            const target = e.target.closest('button, input[type="submit"], input[type="button"], a[role="button"]');
            if (!target) return;

            console.log('[PWCAPTURE] Клик по потенциальной кнопке отправки: "' + (target.innerText || target.value || '').trim().slice(0, 40) + '"');

            const container = target.closest('form') || document;
            const pwdField = container.querySelector('input[type="password"]');
            if (!pwdField || !pwdField.value) {
                console.log('[PWCAPTURE] Клик: поле пароля не найдено или пустое, пропускаем');
                return;
            }

            const text = (target.innerText || target.value || '').toLowerCase();
            const insideRealForm = !!target.closest('form');
            const looksLikeSubmit = (target.type === 'submit' && insideRealForm) ||
                /войти|вход|log ?in|sign ?in|далее|next/.test(text);

            console.log('[PWCAPTURE] Клик: type="' + target.type + '", внутри_формы=' + insideRealForm + ', текст="' + text + '", похоже_на_кнопку_входа=' + looksLikeSubmit);

            if (!looksLikeSubmit) return;
            reportFromForm(container, 'click');
        } catch (err) {
            console.log('[PWCAPTURE] ИСКЛЮЧЕНИЕ в click-обработчике: ' + err.message);
        }
    }, true);

    function tryAutofill(login, password) {
        try {
            console.log('[PWCAPTURE] Получен autofillAvailable, логин="' + login + '"');
            const pwdFields = document.querySelectorAll('input[type="password"]');
            if (!pwdFields.length) {
                console.log('[PWCAPTURE] Автозаполнение: полей пароля на странице нет');
                return;
            }

            pwdFields.forEach(function(pf) {
                pf.value = password;
                pf.dispatchEvent(new Event('input', { bubbles: true }));
                pf.dispatchEvent(new Event('change', { bubbles: true }));
            });

            if (login) {
                const loginField = findNearbyLoginField(pwdFields[0]);
                if (loginField) {
                    loginField.value = login;
                    loginField.dispatchEvent(new Event('input', { bubbles: true }));
                    loginField.dispatchEvent(new Event('change', { bubbles: true }));
                }
            }
            console.log('[PWCAPTURE] Автозаполнение выполнено');
        } catch (err) {
            console.log('[PWCAPTURE] ИСКЛЮЧЕНИЕ в tryAutofill: ' + err.message);
        }
    }

    function initBridge() {
        if (typeof QWebChannel === 'undefined' || typeof qt === 'undefined' || !qt.webChannelTransport) {
            console.log('[PWCAPTURE] QWebChannel/qt.webChannelTransport ещё не готовы, повтор через 300мс');
            setTimeout(initBridge, 300);
            return;
        }
        console.log('[PWCAPTURE] qt.webChannelTransport доступен, создаём QWebChannel...');
        new QWebChannel(qt.webChannelTransport, function(channel) {
            bridge = channel.objects.pwCapture;
            if (!bridge) {
                console.log('[PWCAPTURE] ОШИБКА: channel.objects.pwCapture не найден!');
                return;
            }
            console.log('[PWCAPTURE] Мост pwCapture успешно подключён');

            if (bridge.autofillAvailable) {
                bridge.autofillAvailable.connect(function(login, password) {
                    tryAutofill(login, password);
                });
            }
            if (bridge.requestAutofill) {
                bridge.requestAutofill(window.location.hostname);
                console.log('[PWCAPTURE] requestAutofill() отправлен для ' + window.location.hostname);
            }
        });
    }
    initBridge();
})();
)JS";

        QWebEngineScript pwScript;
        pwScript.setName("StormPasswordCapture");
        pwScript.setInjectionPoint(QWebEngineScript::DocumentReady);
        pwScript.setWorldId(QWebEngineScript::ApplicationWorld);
        pwScript.setRunsOnSubFrames(false);
        pwScript.setSourceCode(qwebchannelJs + "\n" + listenerJs);

        profile->scripts()->insert(pwScript);
    }
}

bool StormWebPage::acceptNavigationRequest(const QUrl& url, QWebEnginePage::NavigationType type, bool isMainFrame) {
    if (url.scheme() == "magnet") {
        emit magnetLinkActivated(url.toString());
        return false;
    }
    if (url.scheme() == "storm-settings") {
        emit settingsActionRequested(url.toString());
        return false;
    }
    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

void StormWebPage::handleCertificateError(QWebEngineCertificateError error) {
    qWarning() << "⚠️ [Storm Shield / SSL] Ошибка сертификата:" << error.description()
        << "URL:" << error.url();

    error.acceptCertificate();
}

void StormWebPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString& message, int lineNumber, const QString& sourceID) {
    if (message.startsWith("[PWCAPTURE]")) {
        // Пишем в файл только явные сбои. Никаких "успешно" —
        // лог не история действий, а список того, что пошло не так.
        const bool isError = message.contains("ОШИБКА:") || message.contains("ИСКЛЮЧЕНИЕ");

        if (isError) {
            qWarning().noquote() << "🔑 [Password Capture]" << sourceID << "(стр." << lineNumber << "):" << message;
        }
        // Иначе — штатный шаг работы скрипта: в файл не пишем.
    }
    else if (message.startsWith("[ARCADE]")) {
        qWarning().noquote() << "🕹️ [Arcade Widget]" << sourceID << "(стр." << lineNumber << "):" << message;
    }
    else if (message.startsWith("[TALK]")) {
        qWarning().noquote() << "📹 [Talk Widget]" << sourceID << "(стр." << lineNumber << "):" << message;
    }
    else if (level == QWebEnginePage::ErrorMessageLevel) {
        // Не трогаем: Logger всё равно отбрасывает эти строки (нет
        // эмодзи из allow-list), они физически не попадают в browser.log.
        qWarning() << "❌ [Web Console Error]" << sourceID << "(стр." << lineNumber << "):" << message;
    }
    else if (level == QWebEnginePage::WarningMessageLevel) {
        // Аналогично — отбрасывается Logger'ом, в файл не идёт.
        qWarning() << "⚠️ [Web Console Warning]" << sourceID << "(стр." << lineNumber << "):" << message;
    }

    QWebEnginePage::javaScriptConsoleMessage(level, message, lineNumber, sourceID);
}