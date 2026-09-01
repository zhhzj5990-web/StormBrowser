#include "ScreenshotEditor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>

// ==========================================
// --- КЛАСС ХОЛСТА (CANVAS) ---
// ==========================================

// Прозрачность маркера применяется один раз при компоновке tempPixmap
// на basePixmap (а не на каждый сегмент линии), чтобы штрихи не темнели
// на самопересечениях.
static constexpr qreal kHighlighterOpacity = 100.0 / 255.0;

Canvas::Canvas(const QPixmap& pixmap, QWidget* parent)
    : QWidget(parent), basePixmap(pixmap), drawing(false)
{
    setFixedSize(pixmap.size());
    tempPixmap = QPixmap(pixmap.size());
    tempPixmap.fill(Qt::transparent);

    currentTool = "pen";
    brushColor = QColor("#56d39b"); // Акцентный цвет по умолчанию
    brushSize = 4;
}

void Canvas::setTool(const QString& toolId) { currentTool = toolId; }
void Canvas::setColor(const QColor& color) { brushColor = color; }
void Canvas::setBrushSize(int size) { brushSize = size; }

void Canvas::saveToHistory() {
    if (history.size() >= 5) history.removeFirst(); // Лимит истории как в Python
    history.append(basePixmap.copy());
}

void Canvas::undo() {
    if (!history.isEmpty()) {
        basePixmap = history.takeLast();
        update();
    }
}

QPen Canvas::getCurrentPen() const {
    QPen pen(brushColor, brushSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    if (currentTool == "highlighter") {
        // Рисуем непрозрачным цветом во временный слой (tempPixmap);
        // полупрозрачность накладывается один раз при компоновке
        // (см. paintEvent / mouseReleaseEvent), а не на каждый сегмент.
        pen.setWidth(brushSize * 3);
    }
    else if (currentTool == "censor") {
        pen.setColor(QColor(20, 20, 20)); // Черный цвет для замазки
        pen.setWidth(brushSize * 4);
    }
    return pen;
}

void Canvas::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.drawPixmap(0, 0, basePixmap);
    if (drawing && !tempPixmap.isNull()) {
        if (currentTool == "rect") {
            painter.drawPixmap(0, 0, tempPixmap);
        }
        else if (currentTool == "highlighter") {
            painter.setOpacity(kHighlighterOpacity);
            painter.drawPixmap(0, 0, tempPixmap);
        }
    }
}

void Canvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        saveToHistory();
        drawing = true;
        lastPoint = event->pos();
        startPoint = event->pos();

        // Одиночный клик без последующего движения мыши раньше не оставлял
        // никакого следа, т.к. рисование происходило только в mouseMoveEvent.
        if (currentTool == "pen" || currentTool == "censor") {
            QPainter painter(&basePixmap);
            painter.setPen(getCurrentPen());
            painter.drawPoint(lastPoint);
        }
        else if (currentTool == "highlighter") {
            tempPixmap.fill(Qt::transparent);
            QPainter painter(&tempPixmap);
            painter.setPen(getCurrentPen());
            painter.drawPoint(lastPoint);
        }
        update();
    }
}

void Canvas::mouseMoveEvent(QMouseEvent* event) {
    if ((event->buttons() & Qt::LeftButton) && drawing) {
        if (currentTool == "pen" || currentTool == "censor") {
            QPainter painter(&basePixmap);
            painter.setPen(getCurrentPen());
            painter.drawLine(lastPoint, event->pos());
            lastPoint = event->pos();
            update();
        }
        else if (currentTool == "highlighter") {
            // Рисуем в прозрачный оверлей непрозрачным цветом, чтобы
            // пересекающиеся сегменты одного и того же штриха не темнели.
            QPainter painter(&tempPixmap);
            painter.setPen(getCurrentPen());
            painter.drawLine(lastPoint, event->pos());
            lastPoint = event->pos();
            update();
        }
        else if (currentTool == "rect") {
            tempPixmap.fill(Qt::transparent);
            QPainter painter(&tempPixmap);
            painter.setPen(getCurrentPen());
            QRect rect(startPoint, event->pos());
            painter.drawRect(rect.normalized());
            update();
        }
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && drawing) {
        drawing = false;
        if (currentTool == "rect") {
            QPainter painter(&basePixmap);
            painter.drawPixmap(0, 0, tempPixmap);
            tempPixmap.fill(Qt::transparent);
        }
        else if (currentTool == "highlighter") {
            QPainter painter(&basePixmap);
            painter.setOpacity(kHighlighterOpacity);
            painter.drawPixmap(0, 0, tempPixmap);
            tempPixmap.fill(Qt::transparent);
        }
        update();
    }
}

// ==========================================
// --- КЛАСС ОКНА РЕДАКТОРА ---
// ==========================================

ScreenshotEditor::ScreenshotEditor(const QPixmap& pixmap, QWidget* parent)
    : QDialog(parent), isTracking(false)
{
    // 1. УБИРАЕМ СТАНДАРТНУЮ БЕЛУЮ РАМКУ WINDOWS
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);

    // 2. ЗАДАЕМ ФОН ВСЕГО ОКНА (Темная тема)
    setStyleSheet("QDialog { background-color: #0d1117; border: 1px solid #30363d; }");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- КАСТОМНАЯ ВЕРХНЯЯ ПАНЕЛЬ ИНСТРУМЕНТОВ ---
    QWidget* topBar = new QWidget(this);
    topBar->setFixedHeight(45);
    topBar->setStyleSheet("background-color: #1c2128; border-bottom: 1px solid #30363d;");

    QHBoxLayout* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(10, 0, 10, 0);
    topLayout->setSpacing(10);

    // Инструменты
    toolGroup = new QButtonGroup(this);
    QStringList tools = { "🖊️", "🖍️", "⬜", "⬛" };
    QStringList toolIds = { "pen", "highlighter", "rect", "censor" };
    QStringList tips = { u8"Перо", u8"Маркер", u8"Рамка", u8"Замазать" };

    for (int i = 0; i < tools.size(); ++i) {
        QPushButton* btn = new QPushButton(tools[i], this);
        btn->setFixedSize(32, 32);
        btn->setCheckable(true);
        btn->setToolTip(tips[i]);
        btn->setStyleSheet(
            "QPushButton { background: transparent; border: 1px solid transparent; border-radius: 6px; font-size: 16px; }"
            "QPushButton:checked { background: rgba(86, 211, 155, 0.15); border: 1px solid #56d39b; }"
            "QPushButton:hover:!checked { background: #30363d; }"
        );
        toolGroup->addButton(btn, i);
        topLayout->addWidget(btn);
        if (i == 0) btn->setChecked(true); // Перо по умолчанию
    }

    connect(toolGroup, &QButtonGroup::idClicked, this, &ScreenshotEditor::changeTool);

    topLayout->addSpacing(15);

    // Палитра цветов
    QList<QColor> colors = { QColor("#FF3232"), QColor("#32FF32"), QColor("#3232FF"), QColor("#FFFF32"), QColor("#FFFFFF") };
    for (const QColor& c : colors) {
        QPushButton* cBtn = new QPushButton(this);
        cBtn->setFixedSize(20, 20);
        cBtn->setCursor(Qt::PointingHandCursor);
        cBtn->setStyleSheet(QString("background-color: %1; border-radius: 10px; border: 2px solid #333;").arg(c.name()));
        connect(cBtn, &QPushButton::clicked, [this, c]() { canvas->setColor(c); });
        topLayout->addWidget(cBtn);
    }

    topLayout->addSpacing(15);

    // Ползунок размера
    QSlider* sizeSlider = new QSlider(Qt::Horizontal, this);
    sizeSlider->setRange(2, 30);
    sizeSlider->setValue(4);
    sizeSlider->setFixedWidth(80);
    sizeSlider->setStyleSheet("QSlider::handle:horizontal { background: #56d39b; border-radius: 6px; width: 12px; }");
    connect(sizeSlider, &QSlider::valueChanged, [this](int v) { canvas->setBrushSize(v); });
    topLayout->addWidget(sizeSlider);

    topLayout->addStretch();

    // Кнопки действий
    QPushButton* undoBtn = new QPushButton(u8"↩️ Назад", this);
    QPushButton* copyBtn = new QPushButton(u8"📋 В буфер", this);
    QPushButton* saveBtn = new QPushButton(u8"💾 Сохранить", this);
    QPushButton* closeBtn = new QPushButton(u8"✕", this);

    QString actionStyle = "QPushButton { background: transparent; color: white; border: 1px solid #30363d; border-radius: 6px; padding: 5px 10px; font-weight: bold; } "
        "QPushButton:hover { background: #30363d; }";
    undoBtn->setStyleSheet(actionStyle);
    copyBtn->setStyleSheet(actionStyle);
    saveBtn->setStyleSheet(actionStyle);

    closeBtn->setFixedSize(30, 30);
    closeBtn->setStyleSheet("QPushButton { background: transparent; color: #ff5f5f; border: none; font-size: 16px; font-weight: bold; } QPushButton:hover { background: rgba(255, 95, 95, 0.2); border-radius: 6px; }");

    connect(undoBtn, &QPushButton::clicked, [this]() { canvas->undo(); });
    connect(copyBtn, &QPushButton::clicked, this, &ScreenshotEditor::copyToClipboard);
    connect(saveBtn, &QPushButton::clicked, this, &ScreenshotEditor::saveImage);
    connect(closeBtn, &QPushButton::clicked, this, &ScreenshotEditor::reject);

    topLayout->addWidget(undoBtn);
    topLayout->addWidget(copyBtn);
    topLayout->addWidget(saveBtn);
    topLayout->addSpacing(10);
    topLayout->addWidget(closeBtn);

    mainLayout->addWidget(topBar);

    // --- ДОБАВЛЯЕМ ХОЛСТ ---
    canvas = new Canvas(pixmap, this);

    // Центрируем холст, если он меньше экрана
    QHBoxLayout* canvasLayout = new QHBoxLayout();
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->addWidget(canvas, 0, Qt::AlignCenter);

    mainLayout->addLayout(canvasLayout);

    // Подстраиваем размер окна под размер скриншота + высота панели
    resize(pixmap.width() + 20, pixmap.height() + topBar->height() + 20);
}

void ScreenshotEditor::changeTool(int id) {
    QStringList toolIds = { "pen", "highlighter", "rect", "censor" };

    if (id >= 0 && id < toolIds.size()) {
        canvas->setTool(toolIds[id]);
    }
}

void ScreenshotEditor::saveImage() {
    QString fileName = QFileDialog::getSaveFileName(this, u8"Сохранить скриншот", "Storm_Screenshot.png", "Images (*.png)");
    if (!fileName.isEmpty()) {
        canvas->getPixmap().save(fileName);
        accept(); // Закрываем окно после сохранения
    }
}

void ScreenshotEditor::copyToClipboard() {
    QApplication::clipboard()->setPixmap(canvas->getPixmap());
    accept(); // Закрываем окно
}

// --- ЛОГИКА ПЕРЕТАСКИВАНИЯ БЕЗРАМОЧНОГО ОКНА ---
void ScreenshotEditor::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        isTracking = true;
        dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void ScreenshotEditor::mouseMoveEvent(QMouseEvent* event) {
    if (isTracking && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - dragPosition);
        event->accept();
    }
}

void ScreenshotEditor::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        isTracking = false;
        event->accept();
    }
}