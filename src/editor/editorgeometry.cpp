#include "editorgeometry.h"

#include "mycodeeditor.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

namespace {
constexpr int kDefaultTabColumns = 4;

QTextLine firstTextLine(const QTextBlock& block)
{
    QTextLayout* layout = block.isValid() ? block.layout() : nullptr;
    if (!layout || layout->lineCount() <= 0)
        return {};
    return layout->lineAt(0);
}

int plainVisualColumnForOffset(const QString& text,
                               int offset,
                               int tabWidth)
{
    int visual = 0;
    const int bounded = qBound(0, offset, text.size());
    for (int i = 0; i < bounded; ++i) {
        visual += EditorVisualColumnGeometry::advanceForCharacter(
            text.at(i), visual, tabWidth);
    }
    return visual;
}

int plainOffsetForVisualColumn(const QString& text,
                               int visualColumn,
                               int tabWidth,
                               EditorVisualBoundary boundary)
{
    const int target = qMax(0, visualColumn);
    int visual = 0;
    for (int i = 0; i < text.size(); ++i) {
        const int next = visual
            + EditorVisualColumnGeometry::advanceForCharacter(
                text.at(i), visual, tabWidth);
        if (target == visual)
            return i;
        if (target > visual && target < next) {
            return boundary == EditorVisualBoundary::End ? i + 1 : i;
        }
        if (target == next)
            return i + 1;
        visual = next;
    }
    return text.size();
}
}

int EditorVisualColumnGeometry::tabStopColumns(
    const MyCodeEditor* editor)
{
    if (!editor)
        return kDefaultTabColumns;
    return qMax(1, qRound(editor->tabStopDistance() / spaceAdvance(editor)));
}

int EditorVisualColumnGeometry::advanceForCharacter(
    QChar character,
    int visualColumn,
    int tabWidth)
{
    if (character != QLatin1Char('\t'))
        return 1;
    const int width = qMax(1, tabWidth);
    const int remainder = qMax(0, visualColumn) % width;
    return remainder == 0 ? width : width - remainder;
}

qreal EditorVisualColumnGeometry::spaceAdvance(
    const MyCodeEditor* editor)
{
    if (!editor)
        return 1.0;
    return qMax<qreal>(
        1.0,
        QFontMetricsF(editor->font()).horizontalAdvance(QLatin1Char(' ')));
}

int EditorVisualColumnGeometry::visualColumnForOffset(
    const MyCodeEditor* editor,
    const QTextBlock& block,
    int offset)
{
    return plainVisualColumnForOffset(
        block.text(), offset, tabStopColumns(editor));
}

int EditorVisualColumnGeometry::offsetForVisualColumn(
    const MyCodeEditor* editor,
    const QTextBlock& block,
    int visualColumn,
    EditorVisualBoundary boundary)
{
    return plainOffsetForVisualColumn(
        block.text(), visualColumn, tabStopColumns(editor), boundary);
}

int EditorVisualColumnGeometry::viewportXForVisualColumn(
    const MyCodeEditor* editor,
    const QTextBlock& block,
    int visualColumn,
    EditorVisualBoundary boundary)
{
    if (!editor || !block.isValid())
        return 0;
    const int target = qMax(0, visualColumn);
    const int endVisual = visualColumnForOffset(
        editor, block, block.text().size());
    QTextCursor cursor(block);
    if (target <= endVisual) {
        cursor.setPosition(
            block.position()
            + offsetForVisualColumn(editor, block, target, boundary));
        return editor->cursorRect(cursor).left();
    }
    cursor.setPosition(block.position() + block.text().size());
    return qRound(editor->cursorRect(cursor).left()
                  + (target - endVisual) * spaceAdvance(editor));
}

int EditorVisualColumnGeometry::visualColumnForViewportX(
    const MyCodeEditor* editor,
    const QTextBlock& block,
    qreal viewportX)
{
    if (!editor || !block.isValid())
        return 0;
    QTextCursor start(block);
    start.setPosition(block.position());
    QTextCursor end(block);
    end.setPosition(block.position() + block.text().size());
    const qreal startX = editor->cursorRect(start).left();
    const qreal endX = editor->cursorRect(end).left();
    const qreal cellWidth = spaceAdvance(editor);
    const int endVisual = visualColumnForOffset(
        editor, block, block.text().size());
    if (viewportX >= endX - cellWidth * 0.5) {
        return endVisual
            + qMax(0, qRound((viewportX - endX) / cellWidth));
    }
    const QTextLine line = firstTextLine(block);
    if (!line.isValid())
        return qMax(0, qRound((viewportX - startX) / spaceAdvance(editor)));
    int zero = 0;
    const qreal zeroX = line.cursorToX(&zero, QTextLine::Leading);
    const int offset = qBound(
        0,
        line.xToCursor(zeroX + viewportX - startX,
                       QTextLine::CursorBetweenCharacters),
        block.text().size());
    return visualColumnForOffset(editor, block, offset);
}

EditorBlockGeometry EditorDocumentGeometry::blockGeometry(
    const MyCodeEditor* editor,
    int blockNumber) const
{
    QTextBlock block = editor->document()->findBlockByNumber(blockNumber);
    if (!block.isValid())
        return {0, qreal(editor->fontMetrics().height())};

    const qreal top = editor->blockBoundingGeometry(block).translated(
        editor->contentOffset()).top();
    return {top, editor->blockBoundingRect(block).height()};
}

EditorCodeLineTailGeometry EditorDocumentGeometry::codeLineTailGeometry(
    const MyCodeEditor* editor,
    int blockNumber) const
{
    if (!editor || !editor->document())
        return {};

    const QTextBlock block =
        editor->document()->findBlockByNumber(blockNumber);
    QTextLayout* layout = block.isValid() ? block.layout() : nullptr;
    if (!block.isValid()
        || !editor->sourceLineVisible(block.blockNumber()) || !layout
        || layout->lineCount() <= 0) {
        return {};
    }

    const QString blockText = block.text();
    int visibleEnd = blockText.size();
    while (visibleEnd > 0 && blockText.at(visibleEnd - 1).isSpace())
        --visibleEnd;

    const int probePosition = visibleEnd > 0 ? visibleEnd - 1 : 0;
    QTextLine line = layout->lineForTextPosition(probePosition);
    if (!line.isValid())
        line = layout->lineAt(layout->lineCount() - 1);
    if (!line.isValid())
        return {};

    int adjustedEnd = visibleEnd;
    const qreal cursorX = line.cursorToX(
        &adjustedEnd, QTextLine::Leading);
    qreal localRight = cursorX;
    if (visibleEnd == blockText.size()) {
        localRight = qMax(localRight,
                          line.naturalTextRect().right());
    }

    const QPointF blockOrigin = editor->blockBoundingGeometry(block)
                                    .translated(editor->contentOffset())
                                    .topLeft();
    EditorCodeLineTailGeometry result;
    result.textRight = blockOrigin.x() + localRight;
    result.top = blockOrigin.y() + line.y();
    result.height = line.height();
    result.baseline = result.top + line.ascent();
    result.textPosition = block.position() + visibleEnd;
    result.valid = true;
    return result;
}

qreal EditorDocumentGeometry::documentHeightPx(
    const MyCodeEditor* editor) const
{
    QAbstractTextDocumentLayout* layout =
        editor->document()->documentLayout();
    return layout ? layout->documentSize().height() : 0;
}
