#ifndef EDITORSELECTION_H
#define EDITORSELECTION_H

#include <functional>

class MyCodeEditor;
class QPlainTextEdit;
class QTimer;
struct EditorSourceNavigationTarget;

class EditorSelection
{
public:
    void highlightCurrentLine(MyCodeEditor* editor);
    void highlightCommand(MyCodeEditor* editor, int prefixPosition);
    void clearCommand(QPlainTextEdit* editor);
    void highlightHoveredSymbol(
        MyCodeEditor* editor,
        const EditorSourceNavigationTarget& target);
    void clearHoveredSymbol(QPlainTextEdit* editor);

private:
    void removeByProperty(QPlainTextEdit* editor, int property, int value);
};

class EditorHighlightRefresh
{
public:
    void attachToEditor(MyCodeEditor* editor, const std::function<void()>& refresh);
    void schedule() const;

private:
    QTimer* timer = nullptr;
};

#endif // EDITORSELECTION_H
