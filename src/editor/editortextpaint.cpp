#include "editortextpaint.h"
#include "mycodeeditor.h"

#include <QGlyphRun>
#include <QPainter>
#include <QPaintEvent>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <algorithm>

namespace {
constexpr int semanticProperty = QTextFormat::UserProperty + 3;
constexpr int semanticMarker = 1003;

bool solidForeground(const QTextCharFormat& format)
{
    return format.foreground().style() == Qt::NoBrush
        || format.foreground().style() == Qt::SolidPattern;
}

bool plainDecoration(const QTextCharFormat& format)
{
    return solidForeground(format) && format.background().style() == Qt::NoBrush
        && format.textOutline().style() == Qt::NoPen
        && format.underlineStyle() == QTextCharFormat::NoUnderline
        && !format.fontOverline() && !format.fontStrikeOut();
}

struct ColoredRun { QGlyphRun glyphs; QColor color; };
struct Row {
    QTextBlock block;
    QPointF origin;
    QRectF bounds;
    QList<ColoredRun> runs;
    QList<QTextLayout::FormatRange> selections;
    bool native = false;
};
}

bool EditorTextPaint::paint(MyCodeEditor* editor, QPaintEvent* event,
                           const QAbstractTextDocumentLayout::PaintContext& context)
{
    if (editor->lineWrapMode() != QPlainTextEdit::NoWrap || editor->overwriteMode()
        || editor->backgroundVisible() || editor->layoutDirection() == Qt::RightToLeft
        || editor->document()->defaultTextOption().textDirection() == Qt::RightToLeft
        || context.cursorPosition < -1 || editor->textCursor().hasSelection()
        || editor->document()->defaultTextOption().flags() != QTextOption::Flags{})
        return false;

    bool hasSemantic = false;
    for (const auto& selection : context.selections) {
        const auto& format = selection.format;
        if (format.intProperty(semanticProperty) == semanticMarker && plainDecoration(format)) {
            hasSemantic = true;
        }
    }
    if (!hasSemantic) return false;

    // Keep Qt's own shaped glyphs (including fallback fonts and exact positions).
    // Pure semantic colors do not need the per-selection path clipping used by
    // QTextLayout::draw. Complex selections/typography retain Qt's full painter.
    QList<Row> rows;
    QPointF offset = editor->contentOffset();
    const QColor defaultColor = context.palette.color(QPalette::Text);
    for (auto block = editor->firstVisibleBlock(); block.isValid(); block = block.next()) {
        const QRectF bounds = editor->blockBoundingRect(block).translated(offset);
        if (bounds.top() > event->rect().bottom()) break;
        if (block.isVisible() && bounds.bottom() >= event->rect().top()) {
            auto* layout = block.layout();
            const QString text = block.text();
            if (rows.size() == 256 || text.size() > 4096 || layout->lineCount() != 1
                || !layout->preeditAreaText().isEmpty()
                || block.blockFormat().background().style() != Qt::NoBrush
                || text.contains(QChar::ObjectReplacementCharacter)) return false;

            Row row{block, offset + layout->position(), bounds, {}, {}, false};
            QList<QColor> colors(text.size(), defaultColor);
            auto applyColor = [&](int start, int length, const QTextCharFormat& format) {
                if (format.foreground().style() == Qt::NoBrush) return;
                const int end = qBound(0, start + length, int(colors.size()));
                for (int i = qMax(0, start); i < end; ++i) colors[i] = format.foreground().color();
            };
            for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
                const auto value = fragment.fragment();
                if (!plainDecoration(value.charFormat())) row.native = true;
                applyColor(value.position() - block.position(), value.length(), value.charFormat());
            }
            for (const auto& format : layout->formats()) {
                if (!plainDecoration(format.format)) row.native = true;
                applyColor(format.start, format.length, format.format);
            }

            QList<QPair<int, int>> coloredRanges;
            for (const auto& selection : context.selections) {
                const int relativeStart = selection.cursor.selectionStart() - block.position();
                const int relativeEnd = selection.cursor.selectionEnd() - block.position();
                if (selection.cursor.hasSelection() && relativeStart < block.length() && relativeEnd > 0) {
                    row.selections.append({relativeStart, relativeEnd - relativeStart, selection.format});
                } else if (!selection.cursor.hasSelection() && selection.cursor.block() == block
                           && selection.format.boolProperty(QTextFormat::FullWidthSelection)) {
                    row.selections.append({0, block.length(), selection.format});
                }
                const int start = qMax(0, relativeStart);
                const int end = qMin(int(text.size()), relativeEnd);
                if (!selection.cursor.hasSelection() && selection.cursor.block() == block
                    && selection.format.boolProperty(QTextFormat::FullWidthSelection)) {
                    // Qt clips a full-width highlight and its glyph overhangs
                    // together. Keep that exact path for the current line.
                    row.native = true;
                } else if (relativeStart < block.length() && relativeEnd > 0 && selection.cursor.hasSelection()) {
                    if (selection.format.intProperty(semanticProperty) != semanticMarker
                        || !plainDecoration(selection.format)) row.native = true;
                    for (const auto& range : coloredRanges)
                        if (start < range.second && end > range.first) row.native = true;
                    coloredRanges.append({start, end});
                    applyColor(start, end - start, selection.format);
                }
            }
            for (int start = 0; !row.native && start < colors.size();) {
                int end = start + 1;
                while (end < colors.size() && colors[end] == colors[start]) ++end;
                const auto glyphRuns = layout->glyphRuns(start, end - start);
                for (const auto& glyphs : glyphRuns) {
                    if (glyphs.flags() & (QGlyphRun::SplitLigature | QGlyphRun::RightToLeft
                                         | QGlyphRun::Underline | QGlyphRun::Overline | QGlyphRun::StrikeOut))
                        row.native = true;
                    row.runs.append({glyphs, colors[start]});
                }
                start = end;
            }
            rows.append(std::move(row));
        }
        offset.ry() += bounds.height();
    }

    QPainter painter(editor->viewport());
    QRect clip = event->rect();
    const int rightMargin = editor->contentOffset().x()
        + qMax(qreal(editor->viewport()->width()), editor->document()->documentLayout()->documentSize().width())
        - editor->document()->documentMargin() + editor->cursorWidth();
    clip.setRight(qMin(clip.right(), rightMargin));
    painter.setClipRect(clip);
    painter.setBrushOrigin(editor->contentOffset());
    for (const auto& row : rows) {
        painter.setPen(defaultColor);
        if (row.native) {
            row.block.layout()->draw(&painter, row.origin - row.block.layout()->position(), row.selections, clip);
        } else {
            for (const auto& run : row.runs) {
                painter.setPen(run.color);
                painter.drawGlyphRun(row.origin, run.glyphs);
            }
        }
        if (context.cursorPosition >= row.block.position()
            && context.cursorPosition < row.block.position() + row.block.length()) {
            painter.setPen(defaultColor);
            row.block.layout()->drawCursor(&painter, row.origin - row.block.layout()->position(),
                context.cursorPosition - row.block.position(), editor->cursorWidth());
        }
    }
    return true;
}
