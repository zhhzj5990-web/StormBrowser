// =========================================================================
// AnalyticsClient — анонимная и ОБЕЗЛИЧЕННАЯ статистика использования (v1.2.9)
// =========================================================================
// Что это и зачем: автору браузера важно понимать, сколько РЕАЛЬНЫХ
// установок Storm Browser существует и насколько интенсивно им пользуются,
// — чтобы решать, куда вкладывать силы дальше. Гугловской телеметрии здесь
// нет и не будет: один POST-запрос на СОБСТВЕННЫЙ сервер (storm-browser.online)
// раз в 30 минут плюс одно событие при первой установке.
//
// ЧТО именно уходит (полный список — больше НИЧЕГО):
//   • install_id — СЛУЧАЙНЫЙ UUID, сгенерированный при первом запуске и
//     хранящийся локально. Не HWID, не серийник, не хеш железа: случайное
//     число, никак не связанное с личностью пользователя.
//   • event — "install" (один раз) или "heartbeat" (обычный пульс).
//   • version — версия браузера ("1.2.9"), чтобы видеть, как обновляются.
//   • os — просто "Windows 10" / "Windows 11" и т.п.
//   • minutes — сколько минут браузер непрерывно работал к моменту пульса.
//
// ЧТО НЕ уходит никогда: посещённые адреса, заголовки вкладок, поисковые
// запросы, закладки, пароли, логин Storm Cloud, IP не сохраняется на приёме
// (FastAPI пишет только то, что прислано в JSON), содержимое страниц, клики.
//
// Выключается в один клик: Настройки → Конфиденциальность →
// «📊 Анонимная статистика использования» (ключ analytics/enabled).
// Выключение действует сразу — каждый пульс перечитывает настройку.
// =========================================================================
#pragma once
#ifndef ANALYTICSCLIENT_H
#define ANALYTICSCLIENT_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QDateTime>

class QNetworkAccessManager;

class AnalyticsClient : public QObject {
    Q_OBJECT
public:
    explicit AnalyticsClient(QObject* parent = nullptr);
    ~AnalyticsClient() override = default;

    // Текущее состояние выключателя (Настройки → Конфиденциальность).
    // По умолчанию ВКЛ — иначе цифры установок будут в разы занижены,
    // а смысл фичи пропадёт. Отключение — право пользователя, см. SettingsBridge.
    static bool isEnabled();
    static const char* kSettingsKey; // "analytics/enabled"

private slots:
    void sendHeartbeat();

private:
    void sendEvent(const QString& event, int minutesActive);

    QNetworkAccessManager* net = nullptr;
    QTimer* heartbeatTimer = nullptr;
    QDateTime startedAt;
};

#endif // ANALYTICSCLIENT_H
