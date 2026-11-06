#include "linkedanchorgeometry.h"
#include "mycodeeditor.h"
#include <QTextCursor>
#include <QTextDocument>

namespace LinkedAnchorGeometry {
Selection sampleSelection(const MyCodeEditor& editor, const QString& selected)
{
    const auto cursor = editor.textCursor();
    return {cursor.selectionStart(), cursor.selectionEnd(), selected};
}

Sample sampleAnchor(const MyCodeEditor& editor, const Anchor& anchor, bool sampleEnd)
{
    Sample result{anchor, {}, {}};
    if (!anchor.available)
        return result;
    QTextCursor cursor(editor.document());
    cursor.setPosition(qBound(0, anchor.start, editor.document()->characterCount() - 1));
    result.first = editor.cursorRect(cursor);
    if (sampleEnd) {
        cursor.setPosition(qBound(0, anchor.end, editor.document()->characterCount() - 1));
        result.last = editor.cursorRect(cursor);
    }
    return result;
}

bool active(const Anchor& anchor, const Selection& selection)
{
    return anchor.id == selection.selected
        || (anchor.available && selection.start <= anchor.end && selection.end >= anchor.start);
}

Overlay overlay(const Sample& sample, const Selection& selection, const QSize& viewportSize)
{
    Overlay result;
    const int width = viewportSize.width(), height = viewportSize.height();
    if (!sample.anchor.available || !active(sample.anchor, selection)
        || sample.last.bottom() < 0 || sample.first.top() > height)
        return result;
    result.draw = true;
    result.band = QRect(0, qMax(0, sample.first.top()), width,
                        qMin(height, sample.last.bottom() + 1) - qMax(0, sample.first.top()));
    result.leading = QLine();
    result.trailing = QLine();
    result.outline = sample.first.adjusted(-2, -1, 2, 1);
    return result;
}
}
