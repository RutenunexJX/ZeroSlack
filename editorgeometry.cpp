#include "editorgeometry.h"

#include "mycodeeditor.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

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
