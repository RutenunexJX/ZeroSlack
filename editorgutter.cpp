#include "editorgutter.h"

#include "editorruntime.h"
#include "mycodeeditor.h"

#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QWheelEvent>
#include <QWidget>

class LineNumberWidget : public QWidget
{
public:
    explicit LineNumberWidget(
        MyCodeEditor* editor = nullptr,
        EditorGutter* gutterUi = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    MyCodeEditor* codeEditor = nullptr;
    EditorGutter* gutter = nullptr;
};

LineNumberWidget::LineNumberWidget(
    MyCodeEditor* editor,
    EditorGutter* gutterUi)
    : QWidget(editor)
    , codeEditor(editor)
    , gutter(gutterUi)
{
    setObjectName(QStringLiteral("editorLineNumberGutter"));
    setMouseTracking(true);
}

void LineNumberWidget::paintEvent(QPaintEvent* event)
{
    if (!codeEditor || !gutter)
        return;

    gutter->paint(codeEditor, event);
}

void LineNumberWidget::mousePressEvent(QMouseEvent* event)
{
    if (!codeEditor || !gutter)
        return;

    gutter->handleMousePress(codeEditor, event);
}

void LineNumberWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!codeEditor || !gutter)
        return;

    gutter->handleMouseMove(codeEditor, event);
}

void LineNumberWidget::wheelEvent(QWheelEvent* event)
{
    if (!codeEditor || !gutter)
        return;

    gutter->handleWheel(codeEditor, event);
}

void EditorGutter::init(MyCodeEditor* editor)
{
    widget = new LineNumberWidget(editor, this);
    appliedLeftMargin = -1;
}

void EditorGutter::destroy()
{
    delete widget;
    widget = nullptr;
    appliedLeftMargin = -1;
}

int EditorGutter::widthFor(MyCodeEditor* editor) const
{
    return 22
        + QString::number(editor->blockCount() + 1).length()
            * editor->fontMetrics().horizontalAdvance(QChar('0'));
}

void EditorGutter::refresh(const QRect& rect, int dy, int width) const
{
    if (widget) {
        if (dy)
            widget->scroll(0, dy);
        else
            widget->update(0, rect.y(), width, rect.height());
    }
}

void EditorGutter::handleUpdateRequest(
    MyCodeEditor* editor,
    const QRect& rect,
    int dy) const
{
    refresh(rect, dy, widthFor(editor));
}

void EditorGutter::updateViewportMargins(MyCodeEditor* editor) const
{
    const int leftWidth = widthFor(editor);
    if (leftWidth != appliedLeftMargin) {
        editor->setViewportMargins(leftWidth, 0, 0, 0);
        appliedLeftMargin = leftWidth;
    }
}

void EditorGutter::resizeTo(MyCodeEditor* editor, const QRect& contentsRect) const
{
    if (widget)
        widget->setGeometry(
            0,
            0,
            widthFor(editor),
            contentsRect.height());
}

void EditorGutter::paint(MyCodeEditor* editor, QPaintEvent* event) const
{
    QPainter painter(widget);
    painter.fillRect(event->rect(), QColor(100, 100, 100, 20));
    if (editor->state)
        editor->state->paintGutterDecorations(editor, painter, event->rect());

    QTextBlock block = editor->firstVisibleBlock();
    int blockNumber = block.blockNumber();
    const int cursorTop = editor->blockBoundingGeometry(
        editor->textCursor().block()).translated(
            editor->contentOffset()).top();
    int top = editor->blockBoundingGeometry(block).translated(
        editor->contentOffset()).top();
    int bottom = top + editor->blockBoundingRect(block).height();

    while (block.isValid() && top <= event->rect().bottom()) {
        painter.setPen(cursorTop == top ? Qt::black : Qt::gray);
        painter.drawText(
            14,
            top,
            widthFor(editor) - 17,
            bottom - top,
            Qt::AlignRight,
            QString::number(blockNumber + 1));

        block = block.next();
        top = bottom;
        bottom = top + editor->blockBoundingRect(block).height();
        blockNumber++;
    }
}

void EditorGutter::handleMousePress(MyCodeEditor* editor, QMouseEvent* event) const
{
    if (editor->state && editor->state->handleGutterMousePress(editor, event)) {
        event->accept();
        return;
    }

    QTextBlock block = editor->document()->findBlockByLineNumber(
        static_cast<int>(event->position().y())
            / editor->fontMetrics().height()
        + editor->verticalScrollBar()->value());
    editor->setTextCursor(QTextCursor(block));
}

void EditorGutter::handleMouseMove(MyCodeEditor* editor, QMouseEvent* event) const
{
    if (!editor || !event || !editor->state)
        return;

    editor->state->handleGutterMouseMove(editor, event);
}

void EditorGutter::handleWheel(MyCodeEditor* editor, QWheelEvent* event) const
{
    const QPoint angle = event->angleDelta();
    if (!angle.isNull()) {
        const int dy = angle.y();
        const int dx = angle.x();
        if (dy != 0) {
            QScrollBar* bar = editor->verticalScrollBar();
            bar->setValue(bar->value() - dy);
        } else if (dx != 0) {
            QScrollBar* bar = editor->horizontalScrollBar();
            bar->setValue(bar->value() - dx);
        }
    }

    event->accept();
}
