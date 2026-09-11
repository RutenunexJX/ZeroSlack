#include "editorsourcehover.h"

#include "editorsemanticcontextservice.h"
#include "insightvisualstyle.h"

#include <QPainter>
#include <QPen>
#include <QPixmap>

bool EditorSourceHover::setCtrlPressed(bool pressed)
{
    if (ctrlPressed == pressed)
        return false;

    ctrlPressed = pressed;
    return true;
}

bool EditorSourceHover::isCtrlPressed() const
{
    return ctrlPressed;
}

bool EditorSourceHover::hasTarget() const
{
    return startPos >= 0 && endPos > startPos;
}

QCursor EditorSourceHover::cursorForTarget(
    const EditorSourceNavigationTarget& target) const
{
    return target.jumpable ? createJumpableCursor()
                           : createNonJumpableCursor();
}

QCursor EditorSourceHover::nonJumpableCursor() const
{
    return createNonJumpableCursor();
}

bool EditorSourceHover::matches(
    const EditorSourceNavigationTarget& target) const
{
    return word == target.text
        && startPos == target.startPos
        && endPos == target.endPos;
}

void EditorSourceHover::setTarget(
    const EditorSourceNavigationTarget& target)
{
    word = target.text;
    startPos = target.startPos;
    endPos = target.endPos;
}

void EditorSourceHover::clearTarget()
{
    word.clear();
    clearRange();
}

void EditorSourceHover::clearRange()
{
    startPos = -1;
    endPos = -1;
}

QCursor EditorSourceHover::createJumpableCursor() const
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    if (!painter.isActive())
        return QCursor(Qt::PointingHandCursor);

    painter.setRenderHint(QPainter::Antialiasing);

    QPen pen(InsightVisualStyle::theme().semantic.read, 4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    painter.drawLine(7, 12, 11, 16);
    painter.drawLine(11, 16, 18, 6);

    painter.end();

    return QCursor(pixmap, 12, 12);
}

QCursor EditorSourceHover::createNonJumpableCursor() const
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QPen pen(
        InsightVisualStyle::theme().syntax.errorUnderline, 3);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);

    painter.drawLine(5, 5, 15, 15);
    painter.drawLine(15, 5, 5, 15);

    return QCursor(pixmap, 10, 10);
}
