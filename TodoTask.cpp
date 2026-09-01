#include "TodoTask.h"
#include <QJsonArray>
#include <QUuid>

QString TodoTask::newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonObject TodoTask::toJson() const {
    QJsonObject obj;
    obj["id"] = id;
    obj["type"] = static_cast<int>(type);
    obj["text"] = text;
    obj["done"] = done;

    switch (type) {
    case TodoTaskType::Context:
        obj["contextUrls"] = QJsonArray::fromStringList(contextUrls);
        break;
    case TodoTaskType::Media:
        obj["mediaFilePath"] = mediaFilePath;
        break;
    case TodoTaskType::AntiSub:
        obj["dayX"] = dayX.toString(Qt::ISODate);
        obj["notified"] = notified;
        break;
    case TodoTaskType::VacancyRadar:
        obj["vacancyPrompt"] = vacancyPrompt;
        obj["vacancyUrls"] = QJsonArray::fromStringList(vacancyUrls);
        obj["vacancyLastResult"] = vacancyLastResult;
        obj["vacancyLastCheck"] = vacancyLastCheck.toString(Qt::ISODate);
        break;
    case TodoTaskType::Simple:
    default:
        break;
    }

    return obj;
}

TodoTask TodoTask::fromJson(const QJsonObject &obj) {
    TodoTask task;

    const QString idVal = obj.value("id").toString();
    task.id = idVal.isEmpty() ? newId() : idVal;
    task.type = static_cast<TodoTaskType>(obj.value("type").toInt(0));
    task.text = obj.value("text").toString();
    task.done = obj.value("done").toBool(false);

    switch (task.type) {
    case TodoTaskType::Context: {
        const QJsonArray arr = obj.value("contextUrls").toArray();
        for (const QJsonValue &v : arr) task.contextUrls << v.toString();
        break;
    }
    case TodoTaskType::Media:
        task.mediaFilePath = obj.value("mediaFilePath").toString();
        break;
    case TodoTaskType::AntiSub:
        task.dayX = QDate::fromString(obj.value("dayX").toString(), Qt::ISODate);
        task.notified = obj.value("notified").toBool(false);
        break;
    case TodoTaskType::VacancyRadar: {
        task.vacancyPrompt = obj.value("vacancyPrompt").toString();
        const QJsonArray arr = obj.value("vacancyUrls").toArray();
        for (const QJsonValue &v : arr) task.vacancyUrls << v.toString();
        task.vacancyLastResult = obj.value("vacancyLastResult").toString();
        task.vacancyLastCheck = QDateTime::fromString(obj.value("vacancyLastCheck").toString(), Qt::ISODate);
        break;
    }
    case TodoTaskType::Simple:
    default:
        break;
    }

    return task;
}

// Миграция со старого формата, где tasks_save.json был просто JSON-массивом строк.
TodoTask TodoTask::fromLegacyString(const QString &text) {
    TodoTask task;
    task.id = newId();
    task.type = TodoTaskType::Simple;
    task.text = text;
    task.done = false;
    return task;
}
