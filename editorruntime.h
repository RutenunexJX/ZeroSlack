#ifndef EDITORRUNTIME_H
#define EDITORRUNTIME_H

#include "editorappearance.h"
#include "editorcompletionui.h"
#include "editorcompletionworkflow.h"
#include "editorcursornavigation.h"
#include "editorfileidentity.h"
#include "editorfolding.h"
#include "editorgeometry.h"
#include "editorgutter.h"
#include "editormodestate.h"
#include "editorselection.h"
#include "editorsemanticcontextservice.h"
#include "editorsemanticruntime.h"
#include "editorsourcenavigation.h"
#include "editorsyntaxstate.h"
#include "ghostannotationservice.h"
#include "sourcenavigationservice.h"

class MyCodeEditor;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPaintEvent;

struct MyCodeEditorState
{
    EditorAppearance appearance;
    EditorGutter gutter;
    EditorDocumentGeometry geometry;
    EditorFoldingController folding;
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
    QList<GhostAnnotation> ghostAnnotations;

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
    bool handleDragEnter(MyCodeEditor* editor, QDragEnterEvent* event);
    bool handleDragMove(MyCodeEditor* editor, QDragMoveEvent* event);
    bool handleDrop(MyCodeEditor* editor, QDropEvent* event);
    void handleResize(MyCodeEditor* editor) const;
    bool handleGutterMousePress(MyCodeEditor* editor, QMouseEvent* event);
    bool handleGutterMouseMove(MyCodeEditor* editor, QMouseEvent* event);
    void paintGutterDecorations(MyCodeEditor* editor,
                                QPainter& painter,
                                const QRect& rect) const;
    void paintFoldPlaceholders(MyCodeEditor* editor, QPaintEvent* event) const;
    void paintGhostAnnotations(MyCodeEditor* editor, QPaintEvent* event) const;
    void handleContextMenu(MyCodeEditor* editor, QContextMenuEvent* event);
    bool handleMousePress(MyCodeEditor* editor, QMouseEvent* event);
    bool handleMouseMove(MyCodeEditor* editor, QMouseEvent* event);
    void handleLeaveEvent(MyCodeEditor* editor);

    void refreshScopeAndCurrentLineHighlight(MyCodeEditor* editor);
    void setAlternateModeEnabled(bool enabled);
    void executeAlternateModeCommand(const QString& command);
    void executeEditorActionCommand(MyCodeEditor* editor, const QString& command);
    void startFoldRegionMarkMode(MyCodeEditor* editor);
    void cancelFoldRegionMarkMode(MyCodeEditor* editor);
    bool foldRegionMarkModeActive() const;
    void startFoldShelfMode(MyCodeEditor* editor);
    void cancelFoldShelfMode(MyCodeEditor* editor);
    bool foldShelfModeActive() const;
    bool insertCustomFoldMarkers(MyCodeEditor* editor,
                                 int startLine,
                                 int endLine,
                                 const QString& alias);
    FoldShelfItem foldShelfItemAtLine(MyCodeEditor* editor,
                                      int line,
                                      FoldShelfOriginKind origin) const;
    bool deleteCustomFoldAtLine(MyCodeEditor* editor, int line);
    bool insertFoldShelfItemAtLine(MyCodeEditor* editor,
                                   const FoldShelfItem& item,
                                   int line);
    void setSemanticContextService(EditorSemanticContextService* service);
    EditorBlockGeometry blockGeometry(
        const MyCodeEditor* editor,
        int blockNumber) const;
    qreal documentHeightPx(const MyCodeEditor* editor) const;
    void setDocumentFileName(MyCodeEditor* editor, QString fileName);
    QString documentFileName() const;
    void setDiagnosticHighlights(
        MyCodeEditor* editor,
        const QList<SemanticDiagnostic>& diagnostics);
    void refreshGhostAnnotations(MyCodeEditor* editor);
    void setSemanticDecorations(
        MyCodeEditor* editor,
        const QList<SemanticDecoration>& decorations);
    void setGhostAnnotations(
        MyCodeEditor* editor,
        const QList<GhostAnnotation>& annotations);
    void highlightSearchMatches(MyCodeEditor* editor,
                                const QString& text,
                                bool caseSensitive);
    void clearSearchMatches(MyCodeEditor* editor);
    void applyAppearanceSettings(
        MyCodeEditor* editor,
        const EditorAppearanceOptions& options);
    void applyLineNavigationTarget(
        MyCodeEditor* editor,
        const SourceLineNavigationTarget& target);
};

#endif // EDITORRUNTIME_H
