#pragma once
#ifndef TODOTASK_H
#define TODOTASK_H

#include <QString>
#include <QStringList>
#include <QDate>
#include <QDateTime>
#include <QJsonObject>

// Тип задачи. Значения фиксированы (используются в JSON), не переставлять.
enum class TodoTaskType {
    Simple = 0,
    Context = 1,       // Задача-контекст: снимок открытых вкладок
    Media = 2,          // Медиа-задача: файл книги для Storm Reader
    AntiSub = 3,        // Анти-подписка: локальный таймер на "День Х"
    VacancyRadar = 4    // Радар вакансий: фоновый парсер по ключевым словам
};

// Единая модель задачи. Поля, не относящиеся к текущему типу, просто не заполняются.
struct TodoTask {
    QString id;
    TodoTaskType type = TodoTaskType::Simple;
    QString text;
    bool done = false;

    // --- Context ---
    QStringList contextUrls;

    // --- Media ---
    QString mediaFilePath;

    // --- AntiSub ---
    QDate dayX;
    bool notified = false;

    // --- VacancyRadar ---
    QString vacancyPrompt;
    QStringList vacancyUrls;
    QString vacancyLastResult;
    QDateTime vacancyLastCheck;

    QJsonObject toJson() const;
    static TodoTask fromJson(const QJsonObject &obj);
    static TodoTask fromLegacyString(const QString &text);
    static QString newId();
};

#endif // TODOTASK_H
