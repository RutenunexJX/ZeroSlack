#include "editorgutter.h"

#include "editorruntime.h"
#include "mycodeeditor.h"
#include "documentblockmodel.h"
#include "roundedicons.h"
#include "uicontrols.h"

#include <QFontDatabase>
#include <QApplication>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QMenu>
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
RoundedIcons::Kind blockIcon(const QString& type) {
    return RoundedIcons::blockKind(type);
}
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
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    MyCodeEditor* codeEditor = nullptr;
    EditorGutter* gutter = nullptr;
    QPointF blockPressPosition;
    QString pressedBlock;
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

    blockPressPosition = event->position();
    pressedBlock = EditorGutter::associatedBlockAt(codeEditor, blockPressPosition);
    setProperty("blockGutterDragStarted", false);
    gutter->handleMousePress(codeEditor, event);
}

void LineNumberWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (codeEditor && gutter && !codeEditor->associatedBlocksVisible()
        && event->button() == Qt::LeftButton && !pressedBlock.isEmpty()
        && !property("blockGutterDragStarted").toBool()
        && (event->position() - blockPressPosition).manhattanLength() < QApplication::startDragDistance()
        && EditorGutter::associatedBlockAt(codeEditor, event->position()) == pressedBlock)
        gutter->handleMousePress(codeEditor, event);
    pressedBlock.clear();
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
    return kLineNumberLeft + qCeil(metrics.horizontalAdvance(QString(digits, QLatin1Char('9')))) + 4 + 16;
}

int EditorGutter::blockMarkerHeight() {
    return 20 + qCeil(QFontMetricsF(lineNumberFont(0)).height());
}

QRect EditorGutter::blockMarkerRect(MyCodeEditor* editor, const QRectF& gap) {
    if (gap.isEmpty()) return {};
    return {kLineNumberLeft, qRound(gap.top()), foldLeft(editor) - kLineNumberLeft,
            qMin(blockMarkerHeight(), qRound(gap.height()))};
}

QRect EditorGutter::blockDragHandleRect(MyCodeEditor* editor, const QRectF& gap) {
    const auto marker = blockMarkerRect(editor, gap);
    return QRect(marker.center() - QPoint(12, 12), QSize(26, 26));
}

QString EditorGutter::associatedBlockAt(MyCodeEditor* editor, const QPointF& position) {
    if (editor && editor->state)
        for (const auto& gap : editor->state->projection.visibleAssociatedBlocks(editor))
            if (blockMarkerRect(editor, gap.second).contains(position.toPoint())) return gap.first;
    return {};
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
        top = editor->blockBoundingGeometry(block).translated(
            editor->contentOffset()).top();
        bottom = top + editor->blockBoundingRect(block).height();
        blockNumber = block.blockNumber();
    }
    if (editor->state) {
        const auto& projection=editor->state->projection;
        const auto gaps=projection.visibleAssociatedBlocks(editor);
        for (const auto& gap:gaps) {
            const auto* associated=editor->blockModel()->find(gap.first); if (!associated) continue;
            const auto marker = blockMarkerRect(editor, gap.second);
            const auto* handle = widget->childAt(marker.center());
            if (handle && handle->objectName() == "editorBlockDragHandle"
                && handle->property("blockId").toString() == gap.first) continue;
            RoundedIcons::icon(blockIcon(associated->type)).paint(&painter,
                QRect(marker.center().x() - 7, marker.top() + 2, 16, 16));
            painter.setPen(palette.color(QPalette::PlaceholderText));
            const auto ordinal=QString::number(projection.associatedBlockOrdinal(gap.first));
            painter.drawText(marker.adjusted(0,18,0,-2),Qt::AlignHCenter|Qt::AlignVCenter,
                painter.fontMetrics().elidedText(ordinal,Qt::ElideRight,marker.width()));
        }
    }
}

void EditorGutter::handleMousePress(MyCodeEditor* editor, QMouseEvent* event) const
{
    const auto id = associatedBlockAt(editor, event->position());
    if(!id.isEmpty() && (event->button()==Qt::LeftButton || event->button()==Qt::RightButton)) {
        if (!editor->associatedBlocksVisible() && event->button() == Qt::LeftButton
            && event->type() == QEvent::MouseButtonPress) { event->accept(); return; }
        if (!editor->associatedBlocksVisible() || event->button() == Qt::RightButton) {
            auto* menu=UiControls::menu(widget);menu->setObjectName("collapsedBlockMenu");menu->setAttribute(Qt::WA_DeleteOnClose);
            editor->populateAssociatedBlockMenu(menu,id);
            editor->selectAssociatedBlock(id);
            menu->popup(event->globalPosition().toPoint());event->accept();return;
        }
        const int line=editor->state->projection.sourceLineForAssociatedBlock(id);
        const auto source=editor->document()->findBlockByNumber(line);
        if (source.isValid()) editor->setTextCursor(QTextCursor(source));
        editor->selectAssociatedBlock(id); event->accept(); return;
    }
    if (editor->state && editor->state->handleGutterMousePress(editor, event)) {
        event->accept();
        return;
    }

    const QTextCursor cursor = editor->cursorForPosition(
        QPoint(0, static_cast<int>(event->position().y())));
    if (!cursor.isNull()) {
        editor->setTextCursor(cursor);
        editor->setProperty("blockPasteLine",cursor.blockNumber());
        editor->setProperty("blockPasteRevision",QVariant::fromValue<qulonglong>(editor->semanticDocumentRevision()));
        editor->setFocus(Qt::MouseFocusReason);
    }
}

void EditorGutter::handleMouseMove(MyCodeEditor* editor, QMouseEvent* event) const
{
    if (!editor || !event || !editor->state)
        return;

    editor->state->handleGutterMouseMove(editor, event);
    const auto id = associatedBlockAt(editor, event->position());
    const auto* block = editor->blockModel()->find(id);
    widget->setToolTip(block ? QString("%1 · %2\nSelect or drag block")
        .arg(editor->state->projection.associatedBlockOrdinal(id)).arg(block->type) : QString());
    if (block && !editor->documentReadOnly()) widget->setCursor(Qt::OpenHandCursor);
    else widget->unsetCursor();
}

void EditorGutter::handleWheel(MyCodeEditor* editor, QWheelEvent* event) const
{
    const QPoint angle = event->angleDelta();
    if (event->modifiers().testFlag(Qt::ShiftModifier)
        && !event->modifiers().testFlag(Qt::ControlModifier)) {
        QScrollBar* bar = editor->horizontalScrollBar();
        const QPoint pixels = event->pixelDelta();
        if (!pixels.isNull()) {
            const int delta = pixels.y() != 0 ? pixels.y() : pixels.x();
            bar->setValue(bar->value() - delta);
        } else {
            const int delta = angle.y() != 0 ? angle.y() : angle.x();
            if (delta != 0) {
                const int steps = qMax(1, qAbs(delta) / 120);
                const int scrollStep = qMax(
                    24, editor->fontMetrics().horizontalAdvance(QLatin1Char('M')) * 6);
                bar->setValue(bar->value()
                              - (delta > 0 ? steps : -steps)
                                    * scrollStep);
            }
        }
        event->accept();
        return;
    }
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
