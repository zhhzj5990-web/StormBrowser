#include "WebPageAgent.h"
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QTimer>
#include <QUrl>
#include <QPixmap>
#include <QBuffer>

WebPageAgent::WebPageAgent(std::function<QWebEngineView* ()> activeViewProvider, QObject* parent)
    : QObject(parent), m_activeViewProvider(std::move(activeViewProvider)) {
}

QWebEngineView* WebPageAgent::targetView() const {
    // Пока задача выполняется — работаем СТРОГО с зафиксированной на её старте вкладкой,
    // а не с "текущей активной" в моменте вызова. Иначе, если пользователь переключится
    // на другую вкладку, пока ИИ отвечает, свечение/плашка рискуют "потеряться" на
    // исходной вкладке навсегда, а сам ИИ — начать действовать не на том сайте.
    if (m_taskActive && m_targetView) {
        return m_targetView.data();
    }
    return m_activeViewProvider ? m_activeViewProvider() : nullptr;
}

void WebPageAgent::beginTask() {
    m_targetView = m_activeViewProvider ? m_activeViewProvider() : nullptr;
    m_taskActive = true;
    // Подстраховка: если на этой вкладке остался "осиротевший" эффект с прошлого раза
    // (например, из-за ещё не обнаруженного похожего бага) — снимаем его перед стартом
    // новой задачи, а не показываем новый эффект поверх старого.
    showWorking(false);
    clearStatus();
}

void WebPageAgent::endTask() {
    if (!m_taskActive) return;
    // ВАЖНО: снимаем эффекты, пока m_taskActive ещё true — тогда targetView() по-прежнему
    // возвращает зафиксированную вкладку задачи, а не то, что может стать активным на
    // экране прямо в этот момент. Если сбросить флаг раньше — эффекты рискуют остаться
    // на исходной вкладке навсегда.
    showWorking(false);
    clearStatus();
    m_taskActive = false;
    m_targetView = nullptr;
}

// =========================================================================================
// --- СНИМОК СТРАНИЦЫ И ВЫПОЛНЕНИЕ ДЕЙСТВИЙ ИИ НА НЕЙ ---
// =========================================================================================

void WebPageAgent::capturePageContext(std::function<void(const QJsonObject&)> callback) {
    QWebEngineView* view = targetView();
    if (!view) {
        QJsonObject empty;
        empty["hasPage"] = false;
        callback(empty);
        return;
    }

    // Собираем видимые интерактивные элементы страницы и присваиваем им атрибут
    // data-storm-aid — по этому числу ИИ будет ссылаться на элемент в своих действиях,
    // вместо того чтобы угадывать CSS-селекторы вручную.
    //
    // ВАЖНО: помимо "семантических" интерактивных тегов (a/button/input/[role]/[onclick]…)
    // многие современные SPA (например ВКонтакте) рисуют кликабельные карточки обычными
    // <div>/<span> без каких-либо признаков интерактивности, кроме cursor:pointer в CSS —
    // такие элементы тоже собираем отдельным проходом, иначе ИИ просто не увидит нужную
    // ссылку/карточку в списке elements и не сможет на неё нажать.
    QString js = QString::fromUtf8(u8R"JS(
        (function() {
            function isVisible(el) {
                if (!el) return false;
                const rect = el.getBoundingClientRect();
                if (rect.width <= 0 || rect.height <= 0) return false;
                const style = window.getComputedStyle(el);
                if (style.visibility === 'hidden' || style.display === 'none' || parseFloat(style.opacity) === 0) return false;
                return true;
            }
            function inViewport(el) {
                const rect = el.getBoundingClientRect();
                return rect.bottom > 0 && rect.top < window.innerHeight && rect.right > 0 && rect.left < window.innerWidth;
            }
            function shortText(el) {
                let t = (el.innerText || el.value || el.getAttribute('placeholder') || el.getAttribute('aria-label') || el.getAttribute('alt') || '').trim();
                t = t.replace(/\s+/g, ' ');
                return t.length > 80 ? t.slice(0, 80) + '…' : t;
            }

            const seen = new Set();
            const candidates = [];

            const semanticSelector = 'a[href], button, input, select, textarea, [role="button"], [role="link"], [role="tab"], [role="checkbox"], [onclick], summary, [contenteditable="true"]';
            for (const el of document.querySelectorAll(semanticSelector)) {
                if (seen.has(el)) continue;
                seen.add(el);
                candidates.push(el);
            }

            // Запасной проход по "неявно кликабельным" контейнерам (карточки/строки без
            // семантической разметки) — берём только те, у которых виден текст, иначе
            // список захламляется декоративными иконками без опознавательного текста.
            const pointerSelector = 'div, span, li, article, section, td, label, h1, h2, h3, h4, p, img, strong, b';
            for (const el of document.querySelectorAll(pointerSelector)) {
                if (seen.has(el)) continue;
                if (window.getComputedStyle(el).cursor !== 'pointer') continue;
                if (!shortText(el)) continue;
                seen.add(el);
                candidates.push(el);
            }

            let visible = candidates.filter(isVisible);
            // Сначала то, что видно пользователю в текущей прокрутке экрана прямо сейчас —
            // именно это он имеет в виду, говоря "нажми вот эту карточку/кнопку".
            visible.sort((a, b) => (inViewport(b) ? 1 : 0) - (inViewport(a) ? 1 : 0));

            const elements = [];
            let idCounter = 1;
            for (const el of visible) {
                if (elements.length >= 150) break;
                if (el.disabled) continue;

                const id = idCounter++;
                el.setAttribute('data-storm-aid', String(id));

                const item = {
                    id: id,
                    tag: el.tagName.toLowerCase(),
                    type: el.getAttribute('type') || '',
                    text: shortText(el)
                };
                if (el.tagName === 'A') item.href = (el.getAttribute('href') || '').slice(0, 200);
                if (el.type === 'checkbox' || el.type === 'radio') item.checked = !!el.checked;
                elements.push(item);
            }

            let bodyText = (document.body ? document.body.innerText : '').replace(/\s+/g, ' ').trim();
            if (bodyText.length > 3000) bodyText = bodyText.slice(0, 3000) + '…';

            return JSON.stringify({
                url: location.href,
                title: document.title,
                text: bodyText,
                elements: elements
            });
        })();
    )JS");

    view->page()->runJavaScript(js, [this, callback](const QVariant& res) {
        QJsonObject obj = QJsonDocument::fromJson(res.toString().toUtf8()).object();
        obj["hasPage"] = true;

        // Обновляем кэш id → текст — пригодится, если к следующему шагу элемент с этим
        // id уже пропадёт из DOM (перерисовка карточек и т.п.), чтобы найти его по тексту.
        m_lastElementsById.clear();
        for (const QJsonValue& v : obj.value("elements").toArray()) {
            QJsonObject el = v.toObject();
            m_lastElementsById.insert(el.value("id").toInt(), el.value("text").toString());
        }

        callback(obj);
        });
}

void WebPageAgent::executeAction(const QJsonObject& action, std::function<void(bool)> callback) {
    QString type = action.value("type").toString();

    if (type == "navigate") {
        QWebEngineView* view = targetView();
        QString urlStr = action.value("url").toString();
        if (!view || urlStr.isEmpty()) { callback(false); return; }
        QUrl url = QUrl(urlStr);
        if (url.isRelative()) url = view->url().resolved(url);
        view->setUrl(url);
        callback(true);
        return;
    }

    QWebEngineView* view = targetView();
    if (!view) { callback(false); return; }

    if (type == "click") {
        int targetId = action.value("target").toInt(0);
        QString targetText = action.value("target_text").toString();

        if (targetId > 0) {
            clickElementById(targetId, [this, callback, targetId, targetText](bool success) {
                if (success) { callback(true); return; }
                // Элемента с этим id уже нет в DOM (карточки перерисовались) — пробуем
                // найти его заново по тексту: сперва явный target_text от ИИ, иначе —
                // последний известный текст этого id из предыдущего снимка страницы.
                QString fallbackText = !targetText.isEmpty() ? targetText : m_lastElementsById.value(targetId);
                if (fallbackText.isEmpty()) { callback(false); return; }
                clickByVisibleText(fallbackText, callback);
                });
            return;
        }

        if (!targetText.isEmpty()) {
            clickByVisibleText(targetText, callback);
            return;
        }

        callback(false);
        return;
    }

    if (type == "type") {
        QString targetId = QString::number(action.value("target").toInt());
        QString value = action.value("value").toString();
        value.replace("\\", "\\\\").replace("'", "\\'").replace("\n", "\\n");
        QString js = QString::fromUtf8(u8R"JS(
            (function() {
                let el = document.querySelector('[data-storm-aid="%1"]');
                if (!el) return false;
                el.scrollIntoView({behavior: 'instant', block: 'center'});
                el.focus();
                if (el.isContentEditable) {
                    el.textContent = '%2';
                } else {
                    const proto = el.tagName === 'TEXTAREA' ? window.HTMLTextAreaElement.prototype : window.HTMLInputElement.prototype;
                    const desc = Object.getOwnPropertyDescriptor(proto, 'value');
                    if (desc && desc.set) desc.set.call(el, '%2'); else el.value = '%2';
                }
                el.dispatchEvent(new Event('input', { bubbles: true }));
                el.dispatchEvent(new Event('change', { bubbles: true }));
                return true;
            })();
        )JS").arg(targetId, value);
        view->page()->runJavaScript(js, [callback](const QVariant& res) { callback(res.toBool()); });
        return;
    }

    if (type == "scroll") {
        int targetId = action.value("target").toInt(0);
        QString js;
        if (targetId > 0) {
            js = QString::fromUtf8(u8R"JS(
                (function() {
                    let el = document.querySelector('[data-storm-aid="%1"]');
                    if (!el) return false;
                    el.scrollIntoView({behavior: 'smooth', block: 'center'});
                    return true;
                })();
            )JS").arg(QString::number(targetId));
        }
        else {
            js = u8"window.scrollBy({top: window.innerHeight * 0.8, behavior: 'smooth'}); true;";
        }
        view->page()->runJavaScript(js, [callback](const QVariant& res) { callback(res.toBool()); });
        return;
    }

    // "done" или неизвестный тип действия — выполнять на странице нечего
    callback(true);
}

// Общий механизм клика: locateJs — это готовое JS-выражение (IIFE), которое находит
// нужный элемент, скроллит к нему и возвращает JSON {x,y} — координаты его центра в
// CSS-пикселях viewport'а, либо null, если элемент не найден или невидим.
//
// Дальше НЕ вызывается el.click() из JS — вместо этого в Qt-виджет страницы
// отправляются настоящие QMouseEvent (движение мыши → нажатие → отпускание). Chromium
// обрабатывает такие события как обычный пользовательский ввод, и страница видит их
// с isTrusted:true — в отличие от el.click(), который всегда даёт isTrusted:false.
// Это важно для чувствительных сценариев (кнопка выдачи API-ключа, оплата, логин,
// капча-чекбоксы), которые нередко явно игнорируют синтетические клики со страницы.
void WebPageAgent::performRealClick(QWebEngineView* view, const QString& locateJs, std::function<void(bool)> callback) {
    if (!view) { callback(false); return; }

    view->page()->runJavaScript(locateJs, [this, view, callback](const QVariant& res) {
        QString resStr = res.toString();
        if (resStr.isEmpty() || resStr == "null") { callback(false); return; }

        QJsonObject pt = QJsonDocument::fromJson(resStr.toUtf8()).object();
        if (!pt.contains("x") || !pt.contains("y")) { callback(false); return; }

        double cssX = pt.value("x").toDouble();
        double cssY = pt.value("y").toDouble();
        double zoom = view->page() ? view->page()->zoomFactor() : 1.0;
        if (zoom <= 0) zoom = 1.0;

        // Настоящий рендер-виджет, который фактически получает ввод от Chromium
        // (сам QWebEngineView — просто контейнер). Координаты пересчитываем через
        // глобальные экранные, чтобы не зависеть от взаимного расположения виджетов.
        QWidget* target = view->focusProxy() ? view->focusProxy() : static_cast<QWidget*>(view);
        QPoint viewLocal(qRound(cssX * zoom), qRound(cssY * zoom));
        QPoint globalPos = view->mapToGlobal(viewLocal);
        QPoint targetLocal = target->mapFromGlobal(globalPos);

        // Видимая "рябь" в точке клика — пользователь должен видеть, куда именно нажал ИИ,
        // а не только общее свечение по краям страницы.
        showClickRipple(view, cssX, cssY);

        QMouseEvent moveEvent(QEvent::MouseMove, targetLocal, globalPos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &moveEvent);

        QMouseEvent pressEvent(QEvent::MouseButtonPress, targetLocal, globalPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &pressEvent);

        // Небольшая пауза между нажатием и отпусканием — как у настоящего клика;
        // мгновенный press+release "за один тик" — один из простых признаков синтетики.
        QTimer::singleShot(60, this, [target, targetLocal, globalPos, callback]() {
            QMouseEvent releaseEvent(QEvent::MouseButtonRelease, targetLocal, globalPos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(target, &releaseEvent);
            callback(true);
            });
        });
}

void WebPageAgent::showClickRipple(QWebEngineView* view, double cssX, double cssY) {
    if (!view) return;
    // Маленький кружок быстро расширяется от точки чуть больше курсора и тут же гаснет —
    // видно, куда именно "нажал" ИИ, но эффект не мешает разглядеть саму страницу.
    QString js = QString::fromUtf8(u8R"JS(
        (function() {
            if (!document.getElementById('storm-ai-ripple-style')) {
                let style = document.createElement('style');
                style.id = 'storm-ai-ripple-style';
                style.innerHTML = '@keyframes stormClickRipple { 0% { transform: scale(0.15); opacity: 0.9; } 100% { transform: scale(1); opacity: 0; } }';
                document.head.appendChild(style);
            }
            let dot = document.createElement('div');
            dot.style.cssText = 'position:fixed; left:%1px; top:%2px; width:22px; height:22px; margin-left:-11px; margin-top:-11px; border-radius:50%; border:2px solid #a371f7; background:rgba(163,113,247,0.35); pointer-events:none; z-index:1000000; transform: scale(0.15); animation: stormClickRipple 0.35s ease-out forwards;';
            document.body.appendChild(dot);
            setTimeout(function () { dot.remove(); }, 500);
        })();
    )JS").arg(QString::number(cssX, 'f', 1), QString::number(cssY, 'f', 1));
    view->page()->runJavaScript(js);
}

void WebPageAgent::clickElementById(int targetId, std::function<void(bool)> callback) {
    QWebEngineView* view = targetView();
    if (!view) { callback(false); return; }

    QString js = QString::fromUtf8(u8R"JS(
        (function() {
            let el = document.querySelector('[data-storm-aid="%1"]');
            if (!el) return null;
            el.scrollIntoView({behavior: 'instant', block: 'center'});
            let r = el.getBoundingClientRect();
            if (r.width <= 0 || r.height <= 0) return null;
            return JSON.stringify({ x: r.left + r.width / 2, y: r.top + r.height / 2 });
        })();
    )JS").arg(QString::number(targetId));

    performRealClick(view, js, callback);
}

void WebPageAgent::clickByVisibleText(const QString& text, std::function<void(bool)> callback) {
    QWebEngineView* view = targetView();
    if (!view || text.trimmed().isEmpty()) { callback(false); return; }

    QString needle = text;
    needle.replace("\\", "\\\\").replace("'", "\\'").replace("\n", " ");

    // Живой поиск по всей странице (не ограничен списком, который ИИ видел в прошлом
    // снимке) — ищем видимый элемент, чей текст точно совпадает или содержит нужную
    // фразу, и берём самый маленький/конкретный из подходящих (чтобы не промахнуться
    // мимо всей карточки, если совпал текст только у одного из её вложенных элементов).
    // Координаты найденного элемента дальше уходят в performRealClick — клик, опять же,
    // настоящий (QMouseEvent), а не el.click() из JS.
    QString js = QString::fromUtf8(u8R"JS(
        (function() {
            function isVisible(el) {
                if (!el) return false;
                const rect = el.getBoundingClientRect();
                if (rect.width <= 0 || rect.height <= 0) return false;
                const style = window.getComputedStyle(el);
                if (style.visibility === 'hidden' || style.display === 'none' || parseFloat(style.opacity) === 0) return false;
                return true;
            }

            const needle = '%1'.trim().toLowerCase();
            if (!needle) return null;

            const pool = document.querySelectorAll('a, button, [role], div, span, li, td, label, img, h1, h2, h3, h4, p, strong, b');
            const candidates = [];
            for (const el of pool) {
                if (!isVisible(el)) continue;
                const t = (el.innerText || el.value || el.getAttribute('aria-label') || el.getAttribute('alt') || '').trim();
                if (!t) continue;
                const tl = t.toLowerCase();
                if (tl === needle || tl.includes(needle)) {
                    candidates.push({ el: el, len: t.length, exact: tl === needle });
                }
            }
            if (candidates.length === 0) return null;

            candidates.sort((a, b) => {
                if (a.exact !== b.exact) return a.exact ? -1 : 1;
                return a.len - b.len;
            });

            const target = candidates[0].el;
            target.scrollIntoView({behavior: 'instant', block: 'center'});
            let r = target.getBoundingClientRect();
            if (r.width <= 0 || r.height <= 0) return null;
            return JSON.stringify({ x: r.left + r.width / 2, y: r.top + r.height / 2 });
        })();
    )JS").arg(needle);

    performRealClick(view, js, callback);
}

void WebPageAgent::showWorking(bool show) {
    QWebEngineView* view = targetView();
    if (!view) return;
    if (show) {
        view->page()->runJavaScript(QString::fromUtf8(u8R"JS(
            (function() {
                if (!document.getElementById('storm-ai-edge-style')) {
                    let animStyle = document.createElement('style');
                    animStyle.id = 'storm-ai-edge-style';
                    animStyle.innerHTML = '@keyframes stormEdgePulse { 0% { box-shadow: inset 0 0 50px 20px rgba(163,113,247,0.7), inset 0 0 100px 40px rgba(163,113,247,0.3); } 50% { box-shadow: inset 0 0 70px 30px rgba(88,166,255,0.8), inset 0 0 140px 50px rgba(88,166,255,0.4); } 100% { box-shadow: inset 0 0 50px 20px rgba(163,113,247,0.7), inset 0 0 100px 40px rgba(163,113,247,0.3); } }';
                    document.head.appendChild(animStyle);
                }
                if (!document.getElementById('storm-ai-edge-anim')) {
                    let edgeDiv = document.createElement('div');
                    edgeDiv.id = 'storm-ai-edge-anim';
                    edgeDiv.style.cssText = 'position:fixed; top:0; left:0; right:0; bottom:0; pointer-events:none; z-index:999998; animation: stormEdgePulse 2s infinite;';
                    document.body.appendChild(edgeDiv);
                }
            })();
        )JS"));
    }
    else {
        // ВАЖНО: обёрнуто в (function(){...})() — иначе "let e" объявляется на верхнем
        // уровне скрипта, а Chromium хранит такие top-level let/const в постоянной
        // лексической области страницы между отдельными вызовами runJavaScript (пока
        // страница не перезагрузится). При повторном вызове ЭТОГО ЖЕ текста скрипта это
        // валило "Uncaught SyntaxError: Identifier 'e' has already been declared", из-за
        // чего до e.remove() дело просто не доходило и свечение оставалось висеть навсегда.
        view->page()->runJavaScript(u8"(function(){ let e = document.getElementById('storm-ai-edge-anim'); if (e) e.remove(); })();");
    }
}

void WebPageAgent::updateStatus(const QString& text) {
    QWebEngineView* view = targetView();
    if (!view) return;
    QString safe = text;
    safe.replace("\\", "\\\\").replace("'", "\\'").replace("\n", " ");
    QString js = QString::fromUtf8(u8R"JS(
        (function() {
            let banner = document.getElementById('storm-ai-search-banner');
            if (!banner) {
                banner = document.createElement('div');
                banner.id = 'storm-ai-search-banner';
                banner.style.cssText = 'position:fixed; left:50%; bottom:20px; transform:translateX(-50%); z-index:999999; background:rgba(20,20,30,0.92); color:#fff; padding:10px 18px; border-radius:10px; font:14px/1.4 sans-serif; box-shadow:0 4px 20px rgba(0,0,0,0.4); border:1px solid rgba(163,113,247,0.6); max-width:80vw; text-align:center; transition: opacity 0.2s ease;';
                document.body.appendChild(banner);
            }
            banner.innerHTML = '🤖 <b>Storm AI:</b> ' + '%1';
        })();
    )JS").arg(safe);
    view->page()->runJavaScript(js);
}

void WebPageAgent::clearStatus() {
    QWebEngineView* view = targetView();
    if (!view) return;
    // Та же причина, что и в showWorking(false) — оборачиваем в IIFE, чтобы "let b" не
    // оседал в постоянной области видимости страницы между вызовами.
    view->page()->runJavaScript(u8"(function(){ let b = document.getElementById('storm-ai-search-banner'); if (b) b.remove(); })();");
}

void WebPageAgent::captureScreenshotBase64(std::function<void(const QString&)> callback) {
    QWebEngineView* view = targetView();
    if (!view) { callback(QString()); return; }

    QPixmap pix = view->grab();
    if (pix.isNull()) { callback(QString()); return; }

    // Уменьшаем перед кодированием — экономим токены и объём запроса; для того чтобы
    // ИИ сориентировался на странице, полное разрешение не нужно.
    if (pix.width() > 1280) {
        pix = pix.scaledToWidth(1280, Qt::SmoothTransformation);
    }

    QByteArray pngBytes;
    QBuffer buffer(&pngBytes);
    buffer.open(QIODevice::WriteOnly);
    pix.save(&buffer, "PNG");
    buffer.close();

    // Только в памяти — на диск ничего не пишется.
    callback(QString::fromLatin1(pngBytes.toBase64()));
}