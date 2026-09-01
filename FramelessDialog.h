#pragma once
#include <QDialog>
#include <QPoint>

class QVBoxLayout;

// Диалог без нативной рамки Windows — та же идея, что у главного окна
// (кастомный customTitleBar), только упакована в переиспользуемый базовый
// класс для любых QDialog'ов (менеджер паролей и т.д.).
//
// Использование:
//   FramelessDialog* dlg = new FramelessDialog(u8"Заголовок", parent);
//   dlg->resize(650, 480);
//   dlg->contentLayout()->addWidget(myWidget);
//   dlg->exec();
class FramelessDialog : public QDialog {
    Q_OBJECT
public:
    explicit FramelessDialog(const QString& title, QWidget* parent = nullptr);

    // Сюда вызывающий код добавляет своё содержимое
    QVBoxLayout* contentLayout() const { return m_contentLayout; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QVBoxLayout* m_contentLayout;
    QPoint m_dragPos;
    static const int HEADER_HEIGHT = 42;
};