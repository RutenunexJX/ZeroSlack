#ifndef EDITORLINEOPERATIONCONTROLLER_H
#define EDITORLINEOPERATIONCONTROLLER_H

#include <QString>

class QClipboard;
class QTextCursor;

enum class EditorLineOperation
{
    Copy,
    Cut,
    DuplicateLines,
    DeleteLines,
    JoinWithNextLine,
    MoveLinesUp,
    MoveLinesDown,
};

struct EditorLineOperationResult
{
    bool handled = false;
    bool succeeded = false;
    bool documentChanged = false;
    QString clipboardText;
    QString failureReason;
};

class EditorLineOperationController
{
public:
    EditorLineOperationResult execute(
        EditorLineOperation operation,
        QTextCursor& cursor,
        QClipboard* clipboard = nullptr) const;

    EditorLineOperationResult copy(
        QTextCursor& cursor,
        QClipboard* clipboard = nullptr) const;
    EditorLineOperationResult cut(
        QTextCursor& cursor,
        QClipboard* clipboard = nullptr) const;
    EditorLineOperationResult duplicateLines(
        QTextCursor& cursor) const;
    EditorLineOperationResult deleteLines(
        QTextCursor& cursor) const;
    EditorLineOperationResult joinLines(
        QTextCursor& cursor) const;
    EditorLineOperationResult joinWithNextLine(
        QTextCursor& cursor) const;
    EditorLineOperationResult moveLines(
        QTextCursor& cursor,
        bool up) const;
};

#endif // EDITORLINEOPERATIONCONTROLLER_H
