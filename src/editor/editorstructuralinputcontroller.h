#ifndef EDITORSTRUCTURALINPUTCONTROLLER_H
#define EDITORSTRUCTURALINPUTCONTROLLER_H

#include <QChar>
#include <QList>
#include <QPointer>
#include <QTextCursor>

class EditorSyntaxState;
class MyCodeEditor;
class QKeyEvent;
class QTextDocument;

class EditorStructuralInputController
{
public:
    bool handleKeyPress(MyCodeEditor* editor,
                        QKeyEvent* event,
                        const EditorSyntaxState& syntax);

private:
    bool handleStructuralEnter(
        MyCodeEditor* editor,
        QKeyEvent* event,
        const EditorSyntaxState& syntax);
    bool handlePairInput(
        MyCodeEditor* editor,
        QKeyEvent* event,
        const EditorSyntaxState& syntax);

    struct TrackedCloser {
        QPointer<QTextDocument> document;
        QTextCursor cursor;
        QChar value;
    };
    QList<TrackedCloser> trackedClosers;

    void pruneTrackedClosers();
    bool consumeTrackedCloser(MyCodeEditor* editor,
                              QTextCursor* cursor,
                              QChar typed);
    void trackCloser(QTextDocument* document,
                     int position,
                     QChar value);
};

#endif // EDITORSTRUCTURALINPUTCONTROLLER_H
