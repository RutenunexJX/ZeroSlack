#ifndef EDITORRUNTIME_H
#define EDITORRUNTIME_H

#include "editorappearance.h"
#include "editorcompletionui.h"
#include "editorcompletionworkflow.h"
#include "editorcursornavigation.h"
#include "editorfileidentity.h"
#include "editorgeometry.h"
#include "editorgutter.h"
#include "editormodestate.h"
#include "editorselection.h"
#include "editorsemanticcontextservice.h"
#include "editorsemanticruntime.h"
#include "editorsourcenavigation.h"
#include "editorsyntaxstate.h"
#include "sourcenavigationservice.h"

class MyCodeEditor;
class QContextMenuEvent;
class QKeyEvent;
class QMouseEvent;

struct MyCodeEditorState
{
    EditorAppearance appearance;
    EditorGutter gutter;
    EditorDocumentGeometry geometry;
    EditorCursorNavigation cursorNavigation;
    EditorSyntaxState syntax;
    EditorFileIdentity identity;
    EditorSemanticRuntime semantic;
    EditorModeState modes;
    EditorCompletionUi completion;
    EditorCompletionWorkflow completionWorkflow;
    EditorHighlightRefresh highlightRefresh;
    EditorSourceNavigationUi sourceNavigation;
    EditorSelection selections;

    void initializeCore(MyCodeEditor* editor);
    void shutdown();
    void attachEditorConnections(MyCodeEditor* editor);
    void attachToEditor(MyCodeEditor* editor);

    EditorSemanticContextService* semanticService() const;
    EditorSourceContextProvider sourceContextProvider(
        const MyCodeEditor* editor) const;
    QString currentModuleNameAt(int charPos) const;
    QString currentModuleName(const MyCodeEditor* editor) const;
    EditorSemanticContext semanticContextForPosition(
        const MyCodeEditor* editor,
        int cursorPosition,
        bool includeDocumentText) const;

    void handleControlKeyPress(MyCodeEditor* editor, QKeyEvent* event);
    void handleControlKeyRelease(MyCodeEditor* editor, QKeyEvent* event);
    bool handleKeyPress(MyCodeEditor* editor, QKeyEvent* event);
    bool handleKeyRelease(MyCodeEditor* editor, QKeyEvent* event);
    void handleResize(MyCodeEditor* editor) const;
    void handleContextMenu(MyCodeEditor* editor, QContextMenuEvent* event);
    bool handleMousePress(MyCodeEditor* editor, QMouseEvent* event);
    void handleMouseMove(MyCodeEditor* editor, QMouseEvent* event);
    void handleLeaveEvent(MyCodeEditor* editor);

    void refreshScopeAndCurrentLineHighlight(MyCodeEditor* editor);
    void setAlternateModeEnabled(bool enabled);
    void executeAlternateModeCommand(const QString& command);
    void setSemanticContextService(EditorSemanticContextService* service);
    EditorBlockGeometry blockGeometry(
        const MyCodeEditor* editor,
        int blockNumber) const;
    qreal documentHeightPx(const MyCodeEditor* editor) const;
    void setDocumentFileName(MyCodeEditor* editor, QString fileName);
    QString documentFileName() const;
    void applyLineNavigationTarget(
        MyCodeEditor* editor,
        const SourceLineNavigationTarget& target) const;
};

#endif // EDITORRUNTIME_H
