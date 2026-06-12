#ifndef EDITORCURSORNAVIGATION_H
#define EDITORCURSORNAVIGATION_H

class MyCodeEditor;
struct SourceLineNavigationTarget;

class EditorCursorNavigation
{
public:
    void applyLineTarget(
        MyCodeEditor* editor,
        const SourceLineNavigationTarget& target) const;

private:
    void moveMouseToCursor(MyCodeEditor* editor) const;
};

#endif // EDITORCURSORNAVIGATION_H
