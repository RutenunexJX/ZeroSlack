#ifndef EDITORSTRUCTURALINPUTCONTROLLER_H
#define EDITORSTRUCTURALINPUTCONTROLLER_H

class EditorSyntaxState;
class MyCodeEditor;
class QKeyEvent;

class EditorStructuralInputController
{
public:
    bool handleKeyPress(MyCodeEditor* editor,
                        QKeyEvent* event,
                        const EditorSyntaxState& syntax) const;

private:
    bool handleStructuralEnter(
        MyCodeEditor* editor,
        QKeyEvent* event,
        const EditorSyntaxState& syntax) const;
    bool handlePairInput(
        MyCodeEditor* editor,
        QKeyEvent* event,
        const EditorSyntaxState& syntax) const;
};

#endif // EDITORSTRUCTURALINPUTCONTROLLER_H
