#ifndef EDITORCOLUMNMODECONTROLLER_H
#define EDITORCOLUMNMODECONTROLLER_H

#include <QStringList>

#include <memory>

class AnnotationLayer;
class EditorModeController;
class MyCodeEditor;
class QKeyEvent;
class QMouseEvent;

struct EditorColumnModeSnapshot
{
    bool selectionActive = false;
    bool virtualCursorActive = false;
    int anchorLine = -1;
    int anchorColumn = -1;
    int currentLine = -1;
    int currentColumn = -1;
    int virtualCursorLine = -1;
    int virtualCursorColumn = -1;
};

class EditorColumnModeController
{
public:
    struct State;

    EditorColumnModeController();
    ~EditorColumnModeController();
    EditorColumnModeController(
        const EditorColumnModeController&) = delete;
    EditorColumnModeController& operator=(
        const EditorColumnModeController&) = delete;

    void bind(EditorModeController* modes,
              AnnotationLayer* annotations,
              MyCodeEditor* editor);
    void shutdown(MyCodeEditor* editor);

    bool selectionActive() const;
    bool virtualCursorActive() const;
    int virtualCursorLine() const;
    int virtualCursorColumn() const;
    EditorColumnModeSnapshot snapshotForTest() const;
    void restoreSnapshot(MyCodeEditor* editor,
                         const EditorColumnModeSnapshot& snapshot);

    QStringList selectedRows(
        MyCodeEditor* editor) const;
    bool applyRows(MyCodeEditor* editor,
                   const QStringList& rows,
                   bool replaceSelection,
                   QString* message = nullptr);

    void clearSelection(MyCodeEditor* editor);
    bool beginSelection(MyCodeEditor* editor,
                        QMouseEvent* event);
    bool handlePlainVirtualCursorClick(
        MyCodeEditor* editor,
        QMouseEvent* event);
    bool handleClipboard(MyCodeEditor* editor,
                         QKeyEvent* event);
    bool handleSelectionNavigation(
        MyCodeEditor* editor,
        QKeyEvent* event);
    bool handleSelectionKeyInput(
        MyCodeEditor* editor,
        QKeyEvent* event);
    bool updateSelectionDrag(MyCodeEditor* editor,
                             QMouseEvent* event);
    bool endSelectionDrag(MyCodeEditor* editor,
                          QMouseEvent* event);

    void clearVirtualCursor(MyCodeEditor* editor);
    void clearPendingColumnAnchor();
    void handleVirtualCursorChanged(
        MyCodeEditor* editor);
    void prepareVirtualCursorInput(
        MyCodeEditor* editor);
    bool handleVirtualCursorKeyPress(
        MyCodeEditor* editor,
        QKeyEvent* event);

    void publishVisibleAnnotations(MyCodeEditor* editor,
                                   int firstVisibleLine = -1,
                                   int lastVisibleLine = -1);

private:
    std::unique_ptr<State> state;
};

#endif // EDITORCOLUMNMODECONTROLLER_H
