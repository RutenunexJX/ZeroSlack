#ifndef EDITORSOURCEHOVER_H
#define EDITORSOURCEHOVER_H

#include <QCursor>
#include <QString>

struct EditorSourceNavigationTarget;

class EditorSourceHover
{
public:
    bool setCtrlPressed(bool pressed);
    bool isCtrlPressed() const;
    bool hasTarget() const;
    QCursor cursorForTarget(const EditorSourceNavigationTarget& target) const;
    QCursor nonJumpableCursor() const;
    bool matches(const EditorSourceNavigationTarget& target) const;
    void setTarget(const EditorSourceNavigationTarget& target);
    void clearTarget();
    void clearRange();

private:
    QCursor createJumpableCursor() const;
    QCursor createNonJumpableCursor() const;

    bool ctrlPressed = false;
    QString word;
    int startPos = -1;
    int endPos = -1;
};

#endif // EDITORSOURCEHOVER_H
