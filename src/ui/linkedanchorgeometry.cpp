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

QJsonObject webItem(const Sample& sample, const Selection& selection,
                    const QPoint& origin, int viewportHeight)
{
    const bool available = sample.anchor.available;
    return {{"id", sample.anchor.id},
            {"y", origin.y() + (available ? sample.first.center().y() : 0)},
            {"visible", available && sample.first.bottom() >= 0 && sample.first.top() < viewportHeight},
            {"active", active(sample.anchor, selection)}};
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
    const int y = qBound(0, sample.first.center().y(), height - 1);
    result.leading = QLine(0, y, qMax(0, sample.first.x() - 3), y);
    result.trailing = QLine(qMin(width, sample.first.x() + 6), y, width, y);
    result.outline = sample.first.adjusted(-2, -1, 2, 1);
    return result;
}
}
