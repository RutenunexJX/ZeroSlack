#ifndef EDITORMULTICURSORCONTROLLER_H
#define EDITORMULTICURSORCONTROLLER_H

#include <QList>
#include <QString>

#include <memory>

class EditorModeController;
class MyCodeEditor;
class QPaintEvent;
class TSDocument;

enum class EditorMultiCursorMove
{
    Left,
    Right,
    Up,
    Down,
    LineStart,
    LineEnd,
};

struct EditorMultiCursorCaret
{
    int anchor = -1;
    int position = -1;
    int virtualColumn = -1;

    bool isValid() const
    {
        return anchor >= 0 && position >= 0;
    }

    bool hasSelection() const
    {
        return anchor != position;
    }

    int selectionStart() const
    {
        return anchor < position ? anchor : position;
    }

    int selectionEnd() const
    {
        return anchor < position ? position : anchor;
    }
};

struct EditorMultiCursorOccurrence
{
    int start = -1;
    int end = -1;

    bool isValid() const
    {
        return start >= 0 && end > start;
    }
};

struct EditorMultiCursorSnapshot
{
    QList<EditorMultiCursorCaret> carets;
    int primaryIndex = -1;
    bool active = false;
};

class EditorMultiCursorController
{
public:
    struct State;

    EditorMultiCursorController();
    ~EditorMultiCursorController();
    EditorMultiCursorController(
        const EditorMultiCursorController&) = delete;
    EditorMultiCursorController& operator=(
        const EditorMultiCursorController&) = delete;

    void bind(EditorModeController* modes,
              MyCodeEditor* editor);
    void shutdown(MyCodeEditor* editor = nullptr);

    bool active() const;
    bool applyingDocumentEdit() const;
    int caretCount() const;
    EditorMultiCursorSnapshot snapshot() const;
    bool resetToEditorCursor();

    bool setCarets(const QList<EditorMultiCursorCaret>& carets,
                   int primaryIndex = 0);
    bool addCaret(const EditorMultiCursorCaret& caret,
                  bool makePrimary = false);
    bool addCaretAt(int position,
                    int virtualColumn = -1,
                    bool makePrimary = false);
    bool setVirtualColumn(int caretIndex, int virtualColumn);

    bool addNextOccurrence(
        const QList<EditorMultiCursorOccurrence>& occurrences,
        int scopeStart = -1,
        int scopeEnd = -1);
    int selectAllOccurrences(
        const QList<EditorMultiCursorOccurrence>& occurrences,
        int scopeStart = -1,
        int scopeEnd = -1);

    bool insertText(const QString& text);
    bool insertTextStructurally(
        const QString& text,
        const TSDocument* syntaxDocument);
    bool insertNewline();
    bool insertStructuralNewline(
        const TSDocument* syntaxDocument,
        int indentWidth = 4);
    bool backspace();
    bool deleteForward();
    bool unindent(int spaceCount = 4);
    bool moveCarets(EditorMultiCursorMove move,
                    bool keepSelection = false);

    QString copySelections() const;
    bool cutSelections(QString* copiedText = nullptr);
    bool pasteText(const QString& clipboardText,
                   bool distributeMatchingRows = true);

    bool escapeToSingleCursor();
    void paint(MyCodeEditor* editor,
               QPaintEvent* event) const;

private:
    std::unique_ptr<State> state;
};

#endif // EDITORMULTICURSORCONTROLLER_H
