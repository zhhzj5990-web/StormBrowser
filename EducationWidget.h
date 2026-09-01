#pragma once
#ifndef EDUCATIONWIDGET_H
#define EDUCATIONWIDGET_H

#include <QWidget>
#include <QVBoxLayout>

class EducationWidget : public QWidget {
    Q_OBJECT
public:
    explicit EducationWidget(QWidget* parent = nullptr);

private:
    void addCard(const QString& title, const QString& desc, const QString& url, const QString& accentColor);
    void openUrl(const QString& url);

    QVBoxLayout* cLayout;
};

#endif // EDUCATIONWIDGET_H