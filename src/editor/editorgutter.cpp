#include "editorgutter.h"

#include "editorruntime.h"
#include "mycodeeditor.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QWheelEvent>
#include <QWidget>

namespace {
constexpr int kLineNumberLeft = 18;
constexpr int kLineNumberRightPadding = 4;
QFont lineNumberFont(int) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSizeF(10.0);
    return font;
}

}

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
    updateNumberFont(editor);
}

void EditorGutter::destroy()
{
    delete widget;
    widget = nullptr;
    appliedLeftMargin = -1;
}

int EditorGutter::totalWidth(MyCodeEditor* editor) {
    const int count = editor ? editor->blockCount() : 1;
    const int digits = qMax(3, static_cast<int>(QString::number(qMax(1, count)).size()));
    const QFontMetricsF metrics(lineNumberFont(count));
    return 18 + qCeil(metrics.horizontalAdvance(QString(digits, QLatin1Char('9')))) + 4 + 16;
}

int EditorGutter::widthFor(MyCodeEditor* editor) const
{
    return totalWidth(editor);
}

void EditorGutter::updateNumberFont(MyCodeEditor* editor) const
{
    if (!widget || !editor)
        return;
    const QFont font = lineNumberFont(editor->blockCount());
    if (widget->font() != font)
        widget->setFont(font);
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
    updateNumberFont(editor);
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
            contentsRect.left(),
            editor->viewport()->geometry().top(),
            widthFor(editor),
            editor->viewport()->height());
}

void EditorGutter::paint(MyCodeEditor* editor, QPaintEvent* event) const
{
    QPainter painter(widget);
    const QPalette palette = editor->palette();
    painter.fillRect(
        event->rect(),
        palette.color(QPalette::AlternateBase));
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
        painter.setPen(
            cursorTop == top
                ? palette.color(QPalette::Text)
                : palette.color(QPalette::PlaceholderText));
        painter.drawText(
            kLineNumberLeft,
            top,
            widthFor(editor)
                - kLineNumberLeft
                - kLineNumberRightPadding - 16,
            bottom - top,
            Qt::AlignRight | Qt::AlignVCenter,
            QString::number(blockNumber + 1));

        block = editor->nextVisibleBlock(block);
        top = bottom;
        bottom = top + editor->blockBoundingRect(block).height();
        blockNumber = block.blockNumber();
    }
}

void EditorGutter::handleMousePress(MyCodeEditor* editor, QMouseEvent* event) const
{
    if (editor->state && editor->state->handleGutterMousePress(editor, event)) {
        event->accept();
        return;
    }

    const QTextCursor cursor = editor->cursorForPosition(
        QPoint(0, static_cast<int>(event->position().y())));
    if (!cursor.isNull())
        editor->setTextCursor(cursor);
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
            const int steps = qMax(1, qAbs(dy) / 120);
            bar->setValue(bar->value()
                          - (dy > 0 ? steps : -steps)
                                * 3 * bar->singleStep());
        } else if (dx != 0) {
            QScrollBar* bar = editor->horizontalScrollBar();
            const int steps = qMax(1, qAbs(dx) / 120);
            bar->setValue(bar->value()
                          - (dx > 0 ? steps : -steps)
                                * 3 * bar->singleStep());
        }
    }

    event->accept();
}
