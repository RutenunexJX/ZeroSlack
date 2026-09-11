#ifndef EDITORKEYWORDGHOSTCONTROLLER_H
#define EDITORKEYWORDGHOSTCONTROLLER_H

#include "tsdocument.h"

class AnnotationLayer;
class EditorModeController;
class EditorSyntaxState;
class MyCodeEditor;
class QKeyEvent;

class EditorKeywordGhostController
{
public:
    void bind(EditorModeController* modes,
              AnnotationLayer* annotations,
              MyCodeEditor* editor);
    void shutdown(MyCodeEditor* editor);

    void refresh(MyCodeEditor* editor,
                 const EditorSyntaxState& syntax);
    void clear(MyCodeEditor* editor,
               bool exitMode = true);
    bool handleKeyPress(MyCodeEditor* editor,
                        QKeyEvent* event);

    TSKeywordCompletionTarget targetForTest() const;

private:
    EditorModeController* modeController = nullptr;
    AnnotationLayer* annotationLayer = nullptr;
    TSKeywordCompletionTarget currentTarget;

    void publish(MyCodeEditor* editor);
};

#endif // EDITORKEYWORDGHOSTCONTROLLER_H
