#pragma once
#include <QDialog>
#include <QWidget>
#include <QPixmap>
#include <QMouseEvent>
#include <QPainter>
#include <QList>
#include <QPushButton>
#include <QSlider>
#include <QButtonGroup>
#include <QApplication>
#include <QClipboard>

class Canvas : public QWidget {
    Q_OBJECT
public:
    Canvas(const QPixmap& pixmap, QWidget* parent = nullptr);

    void setTool(const QString& toolId);
    void setColor(const QColor& color);
    void setBrushSize(int size);
    void undo();
    QPixmap getPixmap() const { return basePixmap; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void saveToHistory();
    QPen getCurrentPen() const;

    QPixmap basePixmap;
    QPixmap tempPixmap;
    QList<QPixmap> history;

    QString currentTool;
    QColor brushColor;
    int brushSize;
    bool drawing;
    QPoint startPoint;
    QPoint lastPoint;
};

class ScreenshotEditor : public QDialog {
    Q_OBJECT
public:
    explicit ScreenshotEditor(const QPixmap& pixmap, QWidget* parent = nullptr);

protected:
    // События для перетаскивания безрамочного окна
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void saveImage();
    void copyToClipboard();
    void changeTool(int id);

private:
    Canvas* canvas;
    QButtonGroup* toolGroup;

    // Переменные для перетаскивания окна
    bool isTracking;
    QPoint dragPosition;
};