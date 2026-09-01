#pragma once
#include <QObject>
#include <QPointer>
#include <QMap>
#include <QJsonObject>
#include <functional>

class QWebEngineView;

// Отвечает за "руки и глаза" ИИ-агента на веб-странице: снимок текущего состояния
// страницы (текст + интерактивные элементы), выполнение действий (клик/ввод/скролл/
// переход) и визуальную индикацию работы (свечение по краям, плашка статуса внизу,
// анимация "ряби" в точке клика). Ничего не знает про чат/переписку с ИИ — только про
// саму страницу и про то, на какой вкладке сейчас выполняется задача.
//
// Вынесен из AIAssistantWidget в отдельный класс, чтобы UI-виджет (чат, кнопки, история
// сообщений) не разрастался вместе с логикой работы со страницей — у них разная зона
// ответственности и разный темп изменений.
class WebPageAgent : public QObject {
    Q_OBJECT
public:
    // activeViewProvider — как узнать "текущую активную вкладку браузера" в моменте
    // (используется и чтобы понять, с какой вкладки стартовать новую задачу, и как
    // запасной вариант, если зафиксированная вкладка задачи вдруг закрылась).
    explicit WebPageAgent(std::function<QWebEngineView* ()> activeViewProvider, QObject* parent = nullptr);

    void beginTask(); // Фиксирует текущую активную вкладку как цель задачи; подчищает возможные старые эффекты на ней
    void endTask();   // Снимает свечение/плашку с зафиксированной вкладки (пока она ещё зафиксирована) и отпускает её

    QWebEngineView* targetView() const; // Вкладка задачи, если она сейчас выполняется, иначе — текущая активная

    void capturePageContext(std::function<void(const QJsonObject&)> callback);
    void executeAction(const QJsonObject& action, std::function<void(bool)> callback);

    void showWorking(bool show);            // Пульсирующее свечение по краям страницы — идёт, пока агент активно что-то делает
    void updateStatus(const QString& text); // Плашка внизу страницы с текстом текущего шага
    void clearStatus();

    // "Последний вариант" анализа: снимок текущей вкладки в виде base64 PNG — только в
    // памяти, ничего не пишется на диск. Пустая строка — если вкладки нет или снимок
    // не удался.
    void captureScreenshotBase64(std::function<void(const QString&)> callback);

private:
    void clickElementById(int targetId, std::function<void(bool)> callback);
    void clickByVisibleText(const QString& text, std::function<void(bool)> callback);
    void performRealClick(QWebEngineView* view, const QString& locateJs, std::function<void(bool)> callback); // Локатор находит координаты, дальше шлются НАСТОЯЩИЕ QMouseEvent (isTrusted:true), а не JS el.click()
    void showClickRipple(QWebEngineView* view, double cssX, double cssY); // Короткая анимация "кружка" в точке клика — чтобы было видно, куда именно нажал ИИ

    std::function<QWebEngineView* ()> m_activeViewProvider;
    QPointer<QWebEngineView> m_targetView;
    bool m_taskActive = false;
    QMap<int, QString> m_lastElementsById; // id → видимый текст элемента из последнего снимка страницы (запасной вариант для клика по тексту)
};