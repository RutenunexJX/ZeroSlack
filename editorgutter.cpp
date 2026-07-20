#include "editorgutter.h"

#include "editorruntime.h"
#include "mycodeeditor.h"

#include <QFontMetrics>
#include <QHash>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QSet>
#include <QScrollBar>
#include <QStringList>
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

class FormalPortGhostWidget : public QWidget
{
public:
    explicit FormalPortGhostWidget(
        MyCodeEditor* editor = nullptr,
        EditorGutter* gutterUi = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

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
    setMouseTracking(true);
}

FormalPortGhostWidget::FormalPortGhostWidget(
    MyCodeEditor* editor,
    EditorGutter* gutterUi)
    : QWidget(editor)
    , codeEditor(editor)
    , gutter(gutterUi)
{
    setObjectName(QStringLiteral("FormalPortGhostLane"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
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

void FormalPortGhostWidget::paintEvent(QPaintEvent* event)
{
    if (!codeEditor || !gutter)
        return;

    gutter->paintFormalPortLane(codeEditor, event);
}

void EditorGutter::init(MyCodeEditor* editor)
{
    widget = new LineNumberWidget(editor, this);
    formalPortWidget = new FormalPortGhostWidget(editor, this);
    appliedLeftMargin = -1;
    appliedRightMargin = -1;
}

void EditorGutter::destroy()
{
    delete formalPortWidget;
    formalPortWidget = nullptr;
    delete widget;
    widget = nullptr;
    appliedLeftMargin = -1;
    appliedRightMargin = -1;
}

int EditorGutter::widthFor(MyCodeEditor* editor) const
{
    return 22
        + QString::number(editor->blockCount() + 1).length()
            * editor->fontMetrics().horizontalAdvance(QChar('0'));
}

int EditorGutter::formalPortLaneWidthFor(MyCodeEditor* editor) const
{
    if (!editor || !editor->state)
        return 0;

    QFont ghostFont = editor->font();
    ghostFont.setItalic(true);
    const QFontMetrics metrics(ghostFont);
    int desiredWidth = 0;
    for (const GhostAnnotation& annotation :
         editor->state->ghostAnnotations) {
        if (annotation.kind != GhostAnnotationKind::FormalPort
            || !annotation.isValid()) {
            continue;
        }
        desiredWidth = qMax(desiredWidth,
                            metrics.horizontalAdvance(annotation.text) + 20);
    }
    if (desiredWidth <= 0)
        return 0;

    const int sourceAndLaneWidth = qMax(
        0, editor->contentsRect().width() - widthFor(editor));
    if (sourceAndLaneWidth <= 1)
        return 0;
    const int maximumLaneWidth = qMin(
        sourceAndLaneWidth - 1,
        qMin(360, qMax(16, sourceAndLaneWidth / 3)));
    const int preferredMinimum = qMin(72, maximumLaneWidth);
    return qMin(maximumLaneWidth,
                qMax(preferredMinimum, desiredWidth));
}

void EditorGutter::refresh(const QRect& rect, int dy, int width) const
{
    if (widget) {
        if (dy)
            widget->scroll(0, dy);
        else
            widget->update(0, rect.y(), width, rect.height());
    }
    if (formalPortWidget && formalPortWidget->isVisible()) {
        if (dy)
            formalPortWidget->scroll(0, dy);
        else
            formalPortWidget->update(
                0,
                rect.y(),
                formalPortWidget->width(),
                rect.height());
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
    const int laneWidth = formalPortLaneWidthFor(editor);
    if (leftWidth != appliedLeftMargin
        || laneWidth != appliedRightMargin) {
        editor->setViewportMargins(leftWidth, 0, laneWidth, 0);
        appliedLeftMargin = leftWidth;
        appliedRightMargin = laneWidth;
    }
    if (formalPortWidget) {
        formalPortWidget->setVisible(laneWidth > 0);
        if (laneWidth > 0)
            formalPortWidget->raise();
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
    const int laneWidth = formalPortLaneWidthFor(editor);
    if (formalPortWidget) {
        const QRect viewportRect = editor->viewport()->geometry();
        formalPortWidget->setGeometry(
            viewportRect.right() + 1,
            viewportRect.top(),
            laneWidth,
            viewportRect.height());
    }
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

void EditorGutter::paintFormalPortLane(
    MyCodeEditor* editor,
    QPaintEvent* event) const
{
    if (!editor || !editor->state || !formalPortWidget || !event)
        return;

    QPainter painter(formalPortWidget);
    painter.fillRect(event->rect(), editor->palette().color(QPalette::Base));
    QColor separator = editor->palette().color(QPalette::Mid);
    separator.setAlpha(80);
    painter.setPen(separator);
    painter.drawLine(0, event->rect().top(), 0, event->rect().bottom());

    QFont ghostFont = editor->font();
    ghostFont.setItalic(true);
    painter.setFont(ghostFont);
    QColor ghostColor = editor->palette().color(QPalette::Text);
    ghostColor.setAlpha(96);
    painter.setPen(ghostColor);
    const QFontMetrics metrics(ghostFont);

    QHash<int, QStringList> textByLine;
    QHash<int, QSet<QString>> seenTextByLine;
    for (const GhostAnnotation& annotation :
         editor->state->ghostAnnotations) {
        if (annotation.kind != GhostAnnotationKind::FormalPort
            || !annotation.isValid() || annotation.line <= 0
            || seenTextByLine[annotation.line].contains(annotation.text)) {
            continue;
        }
        seenTextByLine[annotation.line].insert(annotation.text);
        textByLine[annotation.line].append(annotation.text);
    }

    QTextBlock block = editor->firstVisibleBlock();
    int top = static_cast<int>(editor->blockBoundingGeometry(block)
                                   .translated(editor->contentOffset())
                                   .top());
    int bottom = top
        + static_cast<int>(editor->blockBoundingRect(block).height());
    while (block.isValid() && top <= event->rect().bottom()) {
        const QString text = textByLine.value(block.blockNumber() + 1)
                                 .join(QStringLiteral("  |  "));
        if (!text.isEmpty() && bottom >= event->rect().top()) {
            const int horizontalPadding = qMin(
                8, qMax(1, formalPortWidget->width() / 4));
            const int availableWidth = qMax(
                0,
                formalPortWidget->width() - 2 * horizontalPadding);
            const QString visibleText = metrics.elidedText(
                text, Qt::ElideRight, availableWidth);
            painter.drawText(QRect(horizontalPadding,
                                   top,
                                   availableWidth,
                                   bottom - top),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             visibleText);
        }

        block = block.next();
        top = bottom;
        bottom = top
            + static_cast<int>(editor->blockBoundingRect(block).height());
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
