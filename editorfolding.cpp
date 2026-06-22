#include "editorfolding.h"

#include "activitylogservice.h"
#include "mycodeeditor.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QInputDialog>
#include <QLineEdit>
#include <QFontMetrics>
#include <QPixmap>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>
#include <limits>

namespace {
bool betterFoldForLine(const TSFoldRange& candidate, const TSFoldRange& current)
{
    if (current.startLine < 0)
        return true;
    if (candidate.kind != current.kind)
        return candidate.kind == TSFoldRangeKind::Custom;
    return candidate.endLine > current.endLine;
}

Qt::DropAction shelfDropAction(Qt::KeyboardModifiers modifiers)
{
    return modifiers.testFlag(Qt::ControlModifier)
        ? Qt::CopyAction
        : Qt::MoveAction;
}

QPixmap foldDragPixmap(const FoldShelfItem& item, const QFont& font)
{
    const QSize size(168, 72);
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(37, 99, 235, 180), 2));
    painter.setBrush(QColor(219, 234, 254, 235));
    painter.drawRoundedRect(QRectF(1, 1, size.width() - 2, size.height() - 2), 6, 6);

    QFont labelFont(font);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    painter.setPen(QColor(30, 64, 175));
    painter.drawText(QRect(12, 10, size.width() - 24, 22),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     item.alias.isEmpty() ? QStringLiteral("fold block") : item.alias);

    painter.setFont(font);
    painter.setPen(QColor(55, 65, 81));
    painter.drawText(QRect(12, 36, size.width() - 24, 22),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1 lines").arg(item.lineCount));
    return pixmap;
}
}

void EditorFoldingController::refresh(MyCodeEditor* editor, const TSDocument* document)
{
    if (!editor || !document)
        return;

    ranges = document->foldingRanges();
    QSet<int> validStarts;
    for (const TSFoldRange& range : std::as_const(ranges))
        validStarts.insert(range.startLine);
    for (auto it = collapsedStartLines.begin(); it != collapsedStartLines.end();) {
        if (!validStarts.contains(*it))
            it = collapsedStartLines.erase(it);
        else
            ++it;
    }
    applyVisibility(editor);
}

bool EditorFoldingController::hasFoldAtLine(int line) const
{
    return foldAtLine(line).startLine >= 0;
}

bool EditorFoldingController::isCollapsedAtLine(int line) const
{
    return collapsedStartLines.contains(line);
}

TSFoldRange EditorFoldingController::foldAtLine(int line) const
{
    TSFoldRange best;
    for (const TSFoldRange& range : ranges) {
        if (range.startLine == line && betterFoldForLine(range, best))
            best = range;
    }
    return best;
}

bool EditorFoldingController::toggleFoldAtLine(MyCodeEditor* editor, int line)
{
    const TSFoldRange range = foldAtLine(line);
    if (range.startLine < 0 || range.endLine <= range.startLine)
        return false;

    if (collapsedStartLines.contains(range.startLine))
        collapsedStartLines.remove(range.startLine);
    else
        collapsedStartLines.insert(range.startLine);
    applyVisibility(editor);
    return true;
}

void EditorFoldingController::startFoldRegionMarkMode(MyCodeEditor* editor)
{
    shelfMode = false;
    hoveredShelfRange = {};
    dragShelfRange = {};
    markMode = FoldRegionMarkMode::WaitingForStart;
    pendingStartLine = -1;
    foldRegionHoverLine = -1;
    updateStatus(editor, QStringLiteral("Fold region: click start line"));
    if (editor)
        editor->viewport()->update();
}

void EditorFoldingController::cancelFoldRegionMarkMode(MyCodeEditor* editor)
{
    markMode = FoldRegionMarkMode::Inactive;
    pendingStartLine = -1;
    foldRegionHoverLine = -1;
    updateStatus(editor, QString());
    if (editor)
        editor->viewport()->update();
}

bool EditorFoldingController::foldRegionMarkModeActive() const
{
    return markMode != FoldRegionMarkMode::Inactive;
}

void EditorFoldingController::startFoldShelfMode(MyCodeEditor* editor)
{
    markMode = FoldRegionMarkMode::Inactive;
    pendingStartLine = -1;
    foldRegionHoverLine = -1;
    shelfMode = true;
    hoveredShelfRange = {};
    dragShelfRange = {};
    updateStatus(editor, QStringLiteral("Fold Shelf: drag custom fold blocks"));
    if (editor)
        editor->viewport()->update();
}

void EditorFoldingController::cancelFoldShelfMode(MyCodeEditor* editor)
{
    if (!shelfMode)
        return;
    shelfMode = false;
    hoveredShelfRange = {};
    dragShelfRange = {};
    updateStatus(editor, QString());
    if (editor)
        editor->viewport()->update();
}

bool EditorFoldingController::foldShelfModeActive() const
{
    return shelfMode;
}

bool EditorFoldingController::handleFoldRegionHoverLine(
    MyCodeEditor* editor,
    int line)
{
    if (!foldRegionMarkModeActive() || line < 0)
        return false;

    if (foldRegionHoverLine == line)
        return true;

    foldRegionHoverLine = line;
    if (editor)
        editor->viewport()->update();
    return true;
}

bool EditorFoldingController::handleFoldRegionMouseMove(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (!foldRegionMarkModeActive() || !editor || !event)
        return false;

    const int line =
        editor->cursorForPosition(event->position().toPoint()).blockNumber();
    return handleFoldRegionHoverLine(editor, line);
}

void EditorFoldingController::handleFoldShelfHover(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (!shelfMode || !editor || !event)
        return;

    const int line =
        editor->cursorForPosition(event->position().toPoint()).blockNumber();
    const TSFoldRange range = customFoldContainingLine(line);
    if (range.startLine == hoveredShelfRange.startLine
        && range.endLine == hoveredShelfRange.endLine) {
        return;
    }
    hoveredShelfRange = range;
    editor->viewport()->update();
}

bool EditorFoldingController::handleFoldShelfMousePress(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (!shelfMode || !editor || !event || event->button() != Qt::LeftButton)
        return false;

    const int line =
        editor->cursorForPosition(event->position().toPoint()).blockNumber();
    dragShelfRange = customFoldContainingLine(line);
    dragStartPosition = event->position().toPoint();
    return dragShelfRange.startLine >= 0;
}

bool EditorFoldingController::handleFoldShelfMouseMove(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (!shelfMode || !editor || !event || dragShelfRange.startLine < 0)
        return false;
    if (!(event->buttons() & Qt::LeftButton))
        return false;
    if ((event->position().toPoint() - dragStartPosition).manhattanLength()
        < QApplication::startDragDistance()) {
        return true;
    }

    const Qt::DropAction defaultAction = shelfDropAction(event->modifiers());
    FoldShelfItem item = foldShelfItemAtLine(
        editor,
        dragShelfRange.startLine,
        defaultAction == Qt::MoveAction
            ? FoldShelfOriginKind::Moved
            : FoldShelfOriginKind::Copied);
    if (item.text.isEmpty())
        return false;

    auto* drag = new QDrag(editor);
    auto* mime = new QMimeData;
    mime->setData(foldShelfBlockMimeType(), encodeFoldShelfItem(item));
    mime->setText(item.text);
    drag->setMimeData(mime);
    drag->setPixmap(foldDragPixmap(item, editor->font()));
    drag->setHotSpot(QPoint(18, 18));

    const Qt::DropAction result =
        drag->exec(Qt::MoveAction | Qt::CopyAction, defaultAction);
    if (result == Qt::MoveAction)
        deleteRange(editor, dragShelfRange);
    if (result == Qt::MoveAction || result == Qt::CopyAction) {
        ActivityLogService::getInstance()->append(
            QStringLiteral("Fold Shelf"),
            ActivityLogLevel::Info,
            QStringLiteral("%1 fold block \"%2\" from editor")
                .arg(result == Qt::MoveAction
                         ? QStringLiteral("Moved")
                         : QStringLiteral("Copied"),
                     item.alias));
    }
    dragShelfRange = {};
    return true;
}

bool EditorFoldingController::handleFoldShelfDragEnter(
    MyCodeEditor*,
    QDragEnterEvent* event) const
{
    if (!event || !event->mimeData()->hasFormat(foldShelfItemMimeType()))
        return false;
    event->setDropAction(shelfDropAction(event->modifiers()));
    event->accept();
    return true;
}

bool EditorFoldingController::handleFoldShelfDragMove(
    MyCodeEditor*,
    QDragMoveEvent* event) const
{
    if (!event || !event->mimeData()->hasFormat(foldShelfItemMimeType()))
        return false;
    event->setDropAction(shelfDropAction(event->modifiers()));
    event->accept();
    return true;
}

bool EditorFoldingController::handleFoldShelfDrop(
    MyCodeEditor* editor,
    QDropEvent* event)
{
    if (!editor
        || !event
        || !event->mimeData()->hasFormat(foldShelfItemMimeType())) {
        return false;
    }

    const FoldShelfItem item = decodeFoldShelfItem(
        event->mimeData()->data(foldShelfItemMimeType()));
    if (item.text.isEmpty())
        return false;

    const int line =
        editor->cursorForPosition(event->position().toPoint()).blockNumber();
    if (!insertShelfItemAtLine(editor, item, line))
        return false;

    const Qt::DropAction action = shelfDropAction(event->modifiers());
    event->setDropAction(action);
    event->accept();
    if (action == Qt::MoveAction && !item.id.isEmpty())
        emit editor->foldShelfItemConsumed(item.id);
    ActivityLogService::getInstance()->append(
        QStringLiteral("Fold Shelf"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 shelf item \"%2\" into editor")
            .arg(action == Qt::MoveAction
                     ? QStringLiteral("Consumed")
                     : QStringLiteral("Copied"),
                 item.alias));
    return true;
}

bool EditorFoldingController::handleFoldRegionGutterLine(
    MyCodeEditor* editor,
    int line)
{
    if (!foldRegionMarkModeActive() || line < 0)
        return false;

    if (markMode == FoldRegionMarkMode::WaitingForStart) {
        pendingStartLine = line;
        foldRegionHoverLine = line;
        markMode = FoldRegionMarkMode::WaitingForEnd;
        updateStatus(editor, QStringLiteral("Fold region: click end line"));
        if (editor)
            editor->viewport()->update();
        return true;
    }

    int startLine = pendingStartLine;
    int endLine = line;
    if (endLine < startLine)
        std::swap(startLine, endLine);

    bool accepted = false;
    const QString suggestedAlias = defaultAlias();
    const QString alias = QInputDialog::getText(
        editor,
        QStringLiteral("Fold Region"),
        QStringLiteral("Alias"),
        QLineEdit::Normal,
        suggestedAlias,
        &accepted);
    if (!accepted) {
        cancelFoldRegionMarkMode(editor);
        return true;
    }

    insertCustomFoldMarkers(editor, startLine, endLine, alias.isEmpty() ? suggestedAlias : alias);
    cancelFoldRegionMarkMode(editor);
    return true;
}

bool EditorFoldingController::insertCustomFoldMarkers(
    MyCodeEditor* editor,
    int startLine,
    int endLine,
    const QString& alias)
{
    if (!editor || startLine < 0 || endLine < 0)
        return false;
    if (endLine < startLine)
        std::swap(startLine, endLine);

    QTextDocument* doc = editor->document();
    QTextBlock startBlock = doc->findBlockByNumber(startLine);
    QTextBlock endBlock = doc->findBlockByNumber(endLine);
    if (!startBlock.isValid() || !endBlock.isValid())
        return false;

    QString finalAlias = alias.trimmed();
    if (finalAlias.isEmpty())
        finalAlias = defaultAlias();

    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    cursor.setPosition(endBlock.position() + endBlock.length() - 1);
    cursor.insertText(QStringLiteral("\n// endfold"));
    cursor.setPosition(startBlock.position());
    cursor.insertText(QStringLiteral("// fold %1\n").arg(finalAlias));
    cursor.endEditBlock();
    return true;
}

FoldShelfItem EditorFoldingController::foldShelfItemAtLine(
    MyCodeEditor* editor,
    int line,
    FoldShelfOriginKind origin) const
{
    FoldShelfItem item;
    const TSFoldRange range = customFoldContainingLine(line);
    if (!editor || range.startLine < 0)
        return item;

    item.alias = range.label.isEmpty()
        ? QStringLiteral("fold block")
        : range.label;
    item.text = rangeText(editor, range);
    item.sourceFile = editor->documentFileName();
    item.sourceModule = editor->currentModuleName();
    item.sourceStartLine = range.startLine;
    item.sourceEndLine = range.endLine;
    item.originKind = origin;
    item.lineCount = qMax(0, range.endLine - range.startLine + 1);
    return item;
}

bool EditorFoldingController::deleteCustomFoldAtLine(
    MyCodeEditor* editor,
    int line)
{
    return deleteRange(editor, customFoldContainingLine(line));
}

bool EditorFoldingController::insertShelfItemAtLine(
    MyCodeEditor* editor,
    const FoldShelfItem& item,
    int line)
{
    if (!editor || item.text.isEmpty() || line < 0)
        return false;

    QTextBlock block = editor->document()->findBlockByNumber(line);
    if (!block.isValid())
        block = editor->document()->lastBlock();
    if (!block.isValid())
        return false;

    QString text = item.text;
    if (!text.endsWith(QLatin1Char('\n')))
        text.append(QLatin1Char('\n'));

    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.setPosition(block.position());
    cursor.insertText(text);
    cursor.endEditBlock();
    return true;
}

void EditorFoldingController::applyVisibility(MyCodeEditor* editor)
{
    if (!editor)
        return;

    QTextDocument* doc = editor->document();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        block.setVisible(true);
        block.setLineCount(1);
    }

    for (const TSFoldRange& range : std::as_const(ranges)) {
        if (!collapsedStartLines.contains(range.startLine))
            continue;
        for (int line = range.startLine + 1; line <= range.endLine; ++line) {
            QTextBlock block = doc->findBlockByNumber(line);
            if (!block.isValid())
                continue;
            block.setVisible(false);
            block.setLineCount(0);
        }
    }

    doc->markContentsDirty(0, doc->characterCount());
    editor->viewport()->update();
}

void EditorFoldingController::paintGutter(
    MyCodeEditor* editor,
    QPainter& painter,
    const QRect& rect) const
{
    if (!editor)
        return;

    QTextBlock block = editor->firstVisibleBlock();
    int top = static_cast<int>(editor->blockBoundingGeometry(block)
                                   .translated(editor->contentOffset())
                                   .top());
    int bottom = top + static_cast<int>(editor->blockBoundingRect(block).height());
    while (block.isValid() && top <= rect.bottom()) {
        const int line = block.blockNumber();
        if (markMode == FoldRegionMarkMode::WaitingForEnd
            && line == pendingStartLine) {
            painter.save();
            painter.setPen(QColor(59, 130, 246));
            painter.drawLine(1, top + 1, 1, bottom - 1);
            painter.restore();
        }
        if (foldRegionMarkModeActive()
            && (line == foldRegionHoverLine
                || line == pendingStartLine)) {
            const bool isPendingStart =
                line == pendingStartLine
                && markMode == FoldRegionMarkMode::WaitingForEnd;
            const bool isHoverEnd =
                line == foldRegionHoverLine
                && markMode == FoldRegionMarkMode::WaitingForEnd
                && line != pendingStartLine;
            const QString badge = isPendingStart
                ? QStringLiteral("1")
                : (isHoverEnd ? QStringLiteral("E") : QStringLiteral("S"));
            const QColor badgeColor = isPendingStart
                ? QColor(59, 130, 246)
                : QColor(16, 185, 129);
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(badgeColor);
            const QRect badgeRect(1, top + 2, 12, qMax(12, bottom - top - 4));
            painter.drawRoundedRect(badgeRect, 4, 4);
            painter.setPen(Qt::white);
            painter.drawText(badgeRect, Qt::AlignCenter, badge);
            painter.restore();
        }
        if (shelfMode
            && hoveredShelfRange.startLine >= 0
            && (line == hoveredShelfRange.startLine
                || line == hoveredShelfRange.endLine)) {
            painter.save();
            painter.setPen(QPen(QColor(245, 158, 11), 2));
            painter.drawLine(1, top + 1, 1, bottom - 1);
            painter.restore();
        }
        if (hasFoldAtLine(line)) {
            const bool collapsed = isCollapsedAtLine(line);
            const int midY = top + (bottom - top) / 2;
            QPolygon triangle;
            if (collapsed) {
                triangle << QPoint(5, midY - 4)
                         << QPoint(5, midY + 4)
                         << QPoint(10, midY);
            } else {
                triangle << QPoint(4, midY - 3)
                         << QPoint(11, midY - 3)
                         << QPoint(7, midY + 4);
            }
            painter.save();
            painter.setBrush(QColor(90, 100, 115));
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(triangle);
            painter.restore();
        }

        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(editor->blockBoundingRect(block).height());
    }
}

void EditorFoldingController::paintPlaceholders(
    MyCodeEditor* editor,
    QPainter& painter) const
{
    if (!editor || collapsedStartLines.isEmpty())
    {
        paintCustomFoldBackgrounds(editor, painter);
        paintFoldRegionPreview(editor, painter);
        paintFoldShelfHighlight(editor, painter);
        if (!editor || collapsedStartLines.isEmpty())
            return;
    }

    painter.save();
    paintCustomFoldBackgrounds(editor, painter);
    paintFoldRegionPreview(editor, painter);
    paintFoldShelfHighlight(editor, painter);
    painter.setPen(QColor(115, 125, 140));
    const QFontMetrics metrics(editor->font());
    for (int startLine : collapsedStartLines) {
        const TSFoldRange range = foldAtLine(startLine);
        if (range.startLine < 0)
            continue;
        QTextBlock block = editor->document()->findBlockByNumber(startLine);
        if (!block.isValid() || !block.isVisible())
            continue;
        const QRectF rect = editor->blockBoundingGeometry(block)
                                .translated(editor->contentOffset());
        if (rect.bottom() < 0 || rect.top() > editor->viewport()->height())
            continue;
        const QString label = range.kind == TSFoldRangeKind::Custom
            ? QStringLiteral(" ... fold: %1")
                  .arg(range.label.isEmpty()
                           ? QStringLiteral("fold block")
                           : range.label)
            : QStringLiteral(" ...");
        const int textWidth = metrics.horizontalAdvance(block.text());
        const int x = static_cast<int>(rect.left()) + textWidth + 14;
        const int y = static_cast<int>(rect.top())
            + metrics.ascent()
            + qMax(0, static_cast<int>(rect.height()) - metrics.height()) / 2;
        painter.drawText(x, y, label);
    }
    painter.restore();
}

void EditorFoldingController::paintCustomFoldBackgrounds(
    MyCodeEditor* editor,
    QPainter& painter) const
{
    if (!editor)
        return;

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(251, 191, 36, 26));
    for (const TSFoldRange& range : ranges) {
        if (range.kind != TSFoldRangeKind::Custom
            || collapsedStartLines.contains(range.startLine)) {
            continue;
        }
        QRectF firstRect;
        QRectF lastRect;
        for (int line = range.startLine; line <= range.endLine; ++line) {
            QTextBlock block = editor->document()->findBlockByNumber(line);
            if (!block.isValid() || !block.isVisible())
                continue;
            const QRectF rect =
                editor->blockBoundingGeometry(block).translated(editor->contentOffset());
            if (rect.bottom() < 0 || rect.top() > editor->viewport()->height())
                continue;
            painter.drawRect(QRectF(0, rect.top(), editor->viewport()->width(), rect.height()));
            if (!firstRect.isValid())
                firstRect = rect;
            lastRect = rect;
        }
        if (firstRect.isValid() && lastRect.isValid()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(245, 158, 11, 120), 1));
            const qreal topY = qBound<qreal>(0,
                                             firstRect.top() + 1,
                                             editor->viewport()->height() - 1);
            const qreal bottomY = qBound<qreal>(0,
                                                lastRect.bottom() - 1,
                                                editor->viewport()->height() - 1);
            painter.drawLine(QPointF(0, topY),
                             QPointF(editor->viewport()->width(), topY));
            painter.drawLine(QPointF(0, bottomY),
                             QPointF(editor->viewport()->width(), bottomY));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(251, 191, 36, 26));
        }
    }
    painter.restore();
}

void EditorFoldingController::paintFoldRegionPreview(
    MyCodeEditor* editor,
    QPainter& painter) const
{
    if (!editor || !foldRegionMarkModeActive())
        return;

    int startLine = -1;
    int endLine = -1;
    if (markMode == FoldRegionMarkMode::WaitingForStart) {
        startLine = foldRegionHoverLine;
        endLine = foldRegionHoverLine;
    } else if (pendingStartLine >= 0) {
        startLine = pendingStartLine;
        endLine = foldRegionHoverLine >= 0
            ? foldRegionHoverLine
            : pendingStartLine;
        if (endLine < startLine)
            std::swap(startLine, endLine);
    }
    if (startLine < 0 || endLine < startLine)
        return;

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(16, 185, 129, 34));
    QRectF firstRect;
    QRectF lastRect;
    for (int line = startLine; line <= endLine; ++line) {
        QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid() || !block.isVisible())
            continue;
        const QRectF rect =
            editor->blockBoundingGeometry(block).translated(editor->contentOffset());
        if (rect.bottom() < 0 || rect.top() > editor->viewport()->height())
            continue;
        painter.drawRect(QRectF(0, rect.top(), editor->viewport()->width(), rect.height()));
        if (!firstRect.isValid())
            firstRect = rect;
        lastRect = rect;
    }
    if (firstRect.isValid() && lastRect.isValid()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(16, 185, 129, 150), 2));
        const qreal topY = qBound<qreal>(0,
                                         firstRect.top() + 1,
                                         editor->viewport()->height() - 1);
        const qreal bottomY = qBound<qreal>(0,
                                            lastRect.bottom() - 1,
                                            editor->viewport()->height() - 1);
        painter.drawLine(QPointF(0, topY),
                         QPointF(editor->viewport()->width(), topY));
        painter.drawLine(QPointF(0, bottomY),
                         QPointF(editor->viewport()->width(), bottomY));
    }
    painter.restore();
}

void EditorFoldingController::updateStatus(
    MyCodeEditor* editor,
    const QString& message) const
{
    if (editor)
        emit editor->editorStatusMessageRequested(message);
}

QString EditorFoldingController::defaultAlias()
{
    return QStringLiteral("fold_block_%1").arg(defaultAliasCounter++);
}

TSFoldRange EditorFoldingController::customFoldContainingLine(int line) const
{
    TSFoldRange best;
    int bestSpan = std::numeric_limits<int>::max();
    for (const TSFoldRange& range : ranges) {
        if (range.kind != TSFoldRangeKind::Custom
            || line < range.startLine
            || line > range.endLine) {
            continue;
        }
        const int span = range.endLine - range.startLine;
        if (span < bestSpan) {
            best = range;
            bestSpan = span;
        }
    }
    return best;
}

QString EditorFoldingController::rangeText(
    MyCodeEditor* editor,
    const TSFoldRange& range) const
{
    if (!editor || range.startLine < 0 || range.endLine < range.startLine)
        return QString();

    QTextBlock startBlock =
        editor->document()->findBlockByNumber(range.startLine);
    QTextBlock endBlock =
        editor->document()->findBlockByNumber(range.endLine);
    if (!startBlock.isValid() || !endBlock.isValid())
        return QString();

    QTextCursor cursor(editor->document());
    cursor.setPosition(startBlock.position());
    const int endPosition =
        qMin(editor->document()->characterCount() - 1,
             endBlock.position() + endBlock.length());
    cursor.setPosition(endPosition, QTextCursor::KeepAnchor);
    return cursor.selectedText().replace(QChar::ParagraphSeparator,
                                         QLatin1Char('\n'));
}

bool EditorFoldingController::deleteRange(
    MyCodeEditor* editor,
    const TSFoldRange& range)
{
    if (!editor || range.startLine < 0 || range.endLine < range.startLine)
        return false;

    QTextBlock startBlock =
        editor->document()->findBlockByNumber(range.startLine);
    QTextBlock endBlock =
        editor->document()->findBlockByNumber(range.endLine);
    if (!startBlock.isValid() || !endBlock.isValid())
        return false;

    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.setPosition(startBlock.position());
    const int endPosition =
        qMin(editor->document()->characterCount() - 1,
             endBlock.position() + endBlock.length());
    cursor.setPosition(endPosition, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    return true;
}

void EditorFoldingController::paintFoldShelfHighlight(
    MyCodeEditor* editor,
    QPainter& painter) const
{
    if (!editor || !shelfMode || hoveredShelfRange.startLine < 0)
        return;

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(59, 130, 246, 28));
    QRectF bottomRect;
    for (int line = hoveredShelfRange.startLine;
         line <= hoveredShelfRange.endLine;
         ++line) {
        QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid() || !block.isVisible())
            continue;
        const QRectF rect =
            editor->blockBoundingGeometry(block).translated(editor->contentOffset());
        if (rect.bottom() < 0 || rect.top() > editor->viewport()->height())
            continue;
        painter.drawRect(QRectF(0, rect.top(), editor->viewport()->width(), rect.height()));
        if (line == hoveredShelfRange.endLine)
            bottomRect = rect;
    }
    if (bottomRect.isValid()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(59, 130, 246, 150), 2));
        const qreal y = qBound<qreal>(0,
                                      bottomRect.bottom() - 1,
                                      editor->viewport()->height() - 1);
        painter.drawLine(QPointF(0, y),
                         QPointF(editor->viewport()->width(), y));
    }
    painter.restore();
}
