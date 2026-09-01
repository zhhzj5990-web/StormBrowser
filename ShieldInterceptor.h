#pragma once
#include <QWebEngineUrlRequestInterceptor>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QMutex>
#include <atomic>

class ShieldInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit ShieldInterceptor(QObject* parent = nullptr, int initialBlockedCount = 0);

    void interceptRequest(QWebEngineUrlRequestInfo& info) override;
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled.load(); }
    int blockedCount() const { return m_blockedCount.load(); }

    // --- Исключения (сайты, для которых Shield полностью выключен) ---
    // Как и в большинстве блокировщиков (uBlock Origin, Brave Shields), исключение —
    // это полный обход, а не только для рекламы: для хоста из списка не проверяется
    // ни реклама/трекеры, ни малварь/фишинг (см. isExcepted() в interceptRequest).
    // Совпадение по хосту нестрогое: "vk.com" в списке подходит и для "www.vk.com",
    // и для "m.vk.com" — но это эвристика (сравнение суффикса строки), а не разбор
    // по публичному списку доменов верхнего уровня.
    void setExceptions(const QStringList& hosts); // Полная замена списка — используется при старте из MainWindow
    void addException(const QString& host);
    void removeException(const QString& host);
    QStringList exceptions() const;
    bool isExcepted(const QString& host) const;

signals:
    void threatDetected(const QString& url);
    void blockedCountChanged(int total);

private:
    static QString normalizeHost(const QString& host); // нижний регистр, без "www."

    // std::atomic вместо bool: setEnabled() вызывается с UI-потока
    // (например, из SettingsBridge::toggleShield()), а interceptRequest()
    // читает это значение на IO-потоке QtWebEngine — тот же паттерн гонки,
    // что уже был учтён для m_blockedCount, но раньше не был учтён здесь.
    std::atomic<bool> m_enabled;
    std::atomic<int> m_blockedCount;

    // QSet не потокобезопасен сам по себе (в отличие от atomic<bool>/atomic<int>
    // выше) — тот же UI/IO-поток конфликт, поэтому защищаем мьютексом.
    mutable QMutex m_exceptionsMutex;
    QSet<QString> m_exceptions;
};