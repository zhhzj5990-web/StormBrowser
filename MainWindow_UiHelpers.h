#pragma once
// ==========================================================================
// MainWindow_UiHelpers.h
// Мелкие служебные классы/функции, которые нужны более чем одному
// MainWindow_*.cpp одновременно (используются и в setupUi(), и в
// отдельных слотах). Вынесены в общий internal-заголовок при разбиении
// MainWindow.cpp на тематические файлы, чтобы не дублировать код.
// Публичный интерфейс MainWindow не менялся.
// ==========================================================================

#include <QObject>
#include <QWidget>
#include <QMouseEvent>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QString>
#include <QSettings>
#include <QRegularExpression>
#include <QUrl>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>

// --- Хелпер для перетаскивания кастомных безрамочных окон ---
class WindowDragFilter : public QObject {
    QPoint dragPosition;
public:
    WindowDragFilter(QObject* parent = nullptr) : QObject(parent) {}
    bool eventFilter(QObject* obj, QEvent* event) override {
        QWidget* titleBar = qobject_cast<QWidget*>(obj);
        if (!titleBar) return false;

        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                dragPosition = me->globalPosition().toPoint() - titleBar->window()->frameGeometry().topLeft();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->buttons() & Qt::LeftButton) {
                titleBar->window()->move(me->globalPosition().toPoint() - dragPosition);
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
};


// --- User-Agent основного/приватного профиля ---
// Раньше везде ставился Firefox 128 UA, хотя реальный движок — Chromium.
// YouTube (и Google в целом) сверяет заявленный браузер с реальными
// возможностями движка; на такой "смеси" плеер может начать отдавать
// первые сегменты видео, а потом получать отказ на следующих — то есть
// картинка идёт ~минуту и обрывается ошибкой/чёрным экраном.
// Теперь по умолчанию ("chrome") берём НАСТОЯЩИЙ UA движка и лишь убираем
// токен "QtWebEngine/x.y.z" (он выдаёт встраиваемый браузер) — получается
// честный Chrome/<версия движка> без несовпадений.
// Старое поведение: QSettings "browser/ua_mode" = "firefox".
// ВАЖНО: вызывать ДО profile->setHttpUserAgent(), пока httpUserAgent()
// ещё возвращает дефолтную строку движка.
static QString stormUserAgentFor(QWebEngineProfile* profile) {
    const QString mode = QSettings().value("browser/ua_mode", "chrome").toString();
    if (mode == QLatin1String("firefox") || !profile) {
        return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0");
    }
    QString ua = profile->httpUserAgent();
    ua.remove(QRegularExpression(QStringLiteral("\\s*QtWebEngine/[0-9.]+")));
    return ua;
}

// --- Вход в Google: точечная "маска Firefox" ---
// Google блокирует вход ("This browser or app may not be secure") во
// встраиваемых браузерах. С глобальным Chrome-UA (см. stormUserAgentFor)
// страница входа это видит, поэтому ТОЛЬКО для страниц входа
// (accounts.google.*, accounts.youtube.com) подменяем идентичность на
// Firefox — как было раньше глобально: (1) заголовок User-Agent через
// interceptor, (2) navigator.userAgent и связанные поля через скрипт.
// Остальные сайты (YouTube и т.д.) видят честный Chrome.
static bool isGoogleLoginHost(const QString& host) {
    const QString h = host.toLower();
    return h.startsWith(QLatin1String("accounts.google.")) || h == QLatin1String("accounts.youtube.com");
}

// Вызывается на IO-потоке из interceptRequest(); ничего не читает из
// QSettings/UI, только смотрит на хост запроса.
static void applyGoogleLoginUserAgent(QWebEngineUrlRequestInfo& info) {
    if (isGoogleLoginHost(info.requestUrl().host())) {
        info.setHttpHeader("User-Agent",
            QByteArrayLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0"));
    }
}

// Для профилей, у которых нет собственного interceptor (приватные вкладки).
// У основного профиля то же самое делает HttpsUpgradeInterceptor в MainWindow.cpp.
class GoogleLoginUaInterceptor : public QWebEngineUrlRequestInterceptor {
public:
    explicit GoogleLoginUaInterceptor(QObject* parent = nullptr) : QWebEngineUrlRequestInterceptor(parent) {}
    void interceptRequest(QWebEngineUrlRequestInfo& info) override { applyGoogleLoginUserAgent(info); }
};

static void applyGoogleLoginUaScript(QWebEngineProfile* profile) {
    if (!profile) return;

    QWebEngineScript script;
    script.setName("GoogleLoginFirefoxIdentity");
    QString js = QStringLiteral(R"JS(
        (function() {
            const h = (location.hostname || '').toLowerCase();
            if (!(h.startsWith('accounts.google.') || h === 'accounts.youtube.com')) return;
            const def = (k, v) => { try { Object.defineProperty(Navigator.prototype, k, { get: () => v, configurable: true }); } catch (e) {} };
            def('userAgent', 'Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0');
            def('appVersion', '5.0 (Windows)');
            def('platform', 'Win32');
            def('vendor', '');
            def('userAgentData', undefined);
        })();
    )JS");
    script.setSourceCode(js);
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(true);
    profile->scripts()->insert(script);
}

static void applyMediaCodecFix(QWebEngineProfile* profile) {
    if (!profile) return;

    QWebEngineScript script;
    script.setName("MediaCodecFix");
    QString js = QStringLiteral(R"JS(
        (function() {
            try {
                        const host = window.location.hostname.toLowerCase();
                        if (!(host.includes('vk.com') || host.includes('vk.ru') || host.includes('vkvideo.ru'))) return;

                        const originalCanPlayType = HTMLMediaElement.prototype.canPlayType;
                HTMLMediaElement.prototype.canPlayType = function(type) {
                    if (typeof type === 'string' && (type.toLowerCase().includes('mp4') || type.toLowerCase().includes('avc'))) return '';
                    return originalCanPlayType.apply(this, arguments);
                };

                if (window.MediaSource) {
                    const originalIsTypeSupported = window.MediaSource.isTypeSupported;
                    window.MediaSource.isTypeSupported = function(type) {
                        if (typeof type === 'string' && (type.toLowerCase().includes('mp4') || type.toLowerCase().includes('avc'))) return false;
                        return originalIsTypeSupported.call(window.MediaSource, type);
                    };
                }
            } catch(e) {}
        })();
    )JS");
    script.setSourceCode(js);
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(true);
    profile->scripts()->insert(script);
}