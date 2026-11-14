#include "editorgutter.h"

#include "editorruntime.h"
#include "mycodeeditor.h"
#include "documentblockmodel.h"
#include "roundedicons.h"
#include "uicontrols.h"

#include <QFontDatabase>
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
int lineNumberLeft(MyCodeEditor* editor) {
    return editor && !editor->blockModel()->items().isEmpty() ? 62 : kLineNumberLeft;
}
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
    return lineNumberLeft(editor) + qCeil(metrics.horizontalAdvance(QString(digits, QLatin1Char('9')))) + 4 + 16;
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
            lineNumberLeft(editor),
            top,
            widthFor(editor)
                - lineNumberLeft(editor)
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
        QSet<int> paintedRows;
        for (const auto& gap:gaps) {
            const auto* associated=editor->blockModel()->find(gap.first); if (!associated) continue;
            int count=1;
            if(!editor->associatedBlocksVisible()) {
                const int row=qRound(gap.second.top());if(paintedRows.contains(row))continue;paintedRows.insert(row);
                count=0;for(const auto& other:gaps)if(qRound(other.second.top())==row)++count;
            }
            const int y=qRound(gap.second.top())+(editor->associatedBlocksVisible()?9:qMax(0,qRound(gap.second.height()-16)/2));
            RoundedIcons::icon(blockIcon(associated->type)).paint(&painter,QRect(3,y,16,16));
            painter.setPen(palette.color(QPalette::PlaceholderText));
            const auto ordinal=QString::number(projection.associatedBlockOrdinal(gap.first));
            painter.drawText(QRect(21,y,39,18),Qt::AlignLeft|Qt::AlignVCenter,
                             count>1 ? ordinal+"+"+QString::number(count-1) : ordinal);
        }
    }
}

void EditorGutter::handleMousePress(MyCodeEditor* editor, QMouseEvent* event) const
{
    if(editor->state && !editor->associatedBlocksVisible() && event->position().x()<lineNumberLeft(editor)
        && (event->button()==Qt::LeftButton || event->button()==Qt::RightButton)) {
        QStringList ids;
        for(const auto& gap:editor->state->projection.visibleAssociatedBlocks(editor))
            if(gap.second.top()<=event->position().y() && event->position().y()<=gap.second.bottom())ids.append(gap.first);
        if(!ids.isEmpty()) {
            auto* menu=UiControls::menu(widget);menu->setObjectName("collapsedBlockMenu");menu->setAttribute(Qt::WA_DeleteOnClose);
            for(const auto& id:ids)if(const auto* block=editor->blockModel()->find(id)) {
                const auto label=QString::number(editor->state->projection.associatedBlockOrdinal(id))+" · "+block->type;
                auto* target=ids.size()==1 ? menu : menu->addMenu(RoundedIcons::icon(blockIcon(block->type)),label);
                editor->populateAssociatedBlockMenu(target,id);
                if(ids.size()==1)editor->selectAssociatedBlock(id);
                else QObject::connect(target,&QMenu::aboutToShow,editor,[editor,id]{editor->selectAssociatedBlock(id);});
            }
            menu->popup(event->globalPosition().toPoint());event->accept();return;
        }
    }
    if (editor->state && event->button()==Qt::LeftButton) {
        for (const auto& gap:editor->state->projection.visibleAssociatedBlocks(editor))
            if (event->position().y()>=gap.second.top() && event->position().y()<=gap.second.bottom()) {
                const int line=editor->state->projection.sourceLineForAssociatedBlock(gap.first);
                const auto source=editor->document()->findBlockByNumber(line);
                if (source.isValid()) editor->setTextCursor(QTextCursor(source));
                editor->selectAssociatedBlock(gap.first); event->accept(); return;
            }
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
