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
    struct Edit {
        int start = -1, end = -1, caretOffset = 0;
        QString text;
        bool generatedClosingKeyword = false;
        bool valid() const { return start >= 0; }
    };
    static Edit closingKeywordEdit(const QTextCursor& cursor,
                                   const EditorSyntaxState& syntax);
    static Edit newlineEdit(const QTextCursor& cursor,
                            const EditorSyntaxState& syntax,
                            int writableStart = 0);
    static void markGeneratedClosing(QTextDocument* document, const Edit& edit);
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
    bool handleKeywordBoundary(MyCodeEditor* editor,
                               QKeyEvent* event,
                               const EditorSyntaxState& syntax);
    bool alignClosingKeyword(MyCodeEditor* editor,
                             QTextCursor* cursor,
                             const EditorSyntaxState& syntax);

    struct TrackedCloser {
        QPointer<QTextDocument> document;
        QTextCursor cursor;
        QTextCursor endCursor;
        QChar value;
    };
    QList<TrackedCloser> trackedClosers;

    void pruneTrackedClosers();
    bool skipTrackedCloser(MyCodeEditor* editor,
                           QTextCursor* cursor,
                           QChar typed);
    void trackCloser(QTextDocument* document,
                     int position,
                     QChar value);
};

#endif // EDITORSTRUCTURALINPUTCONTROLLER_H
