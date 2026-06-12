#include "editorgeometry.h"

#include "mycodeeditor.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextDocument>

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

qreal EditorDocumentGeometry::documentHeightPx(
    const MyCodeEditor* editor) const
{
    QAbstractTextDocumentLayout* layout =
        editor->document()->documentLayout();
    return layout ? layout->documentSize().height() : 0;
}
