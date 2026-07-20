#ifndef EDITORRUNTIME_H
#define EDITORRUNTIME_H

#include "editorappearance.h"
#include "completiontypes.h"
#include "editorcompletionui.h"
#include "editorcompletionworkflow.h"
#include "editorcursornavigation.h"
#include "editorfileidentity.h"
#include "editorfolding.h"
#include "formatterservice.h"
#include "editorgeometry.h"
#include "editorgutter.h"
#include "editormodestate.h"
#include "packagetoolservice.h"
#include "editorselection.h"
#include "editorsemanticcontextservice.h"
#include "editorsemanticruntime.h"
#include "editorsourcenavigation.h"
#include "editorsyntaxstate.h"
#include "ghostannotationservice.h"
#include "sourcenavigationservice.h"

#include <cstdint>

class MyCodeEditor;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QTimer;
struct EditorAlwaysScopeTarget;
struct EditorModuleScopeTarget;

struct MyCodeEditorState
{
    struct TemplateSlotRange {
        QString name;
        int start = -1;
        int end = -1;
    };

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
    HierarchyInstanceContext hierarchyInstance;
    QList<GhostAnnotation> ghostAnnotations;
    // Semantic publications are keyed to text content, not QTextDocument's
    // formatting revision. Appearance and syntax highlighting can advance the
    // latter without changing any source text.
    std::uint64_t semanticTextRevision = 0;
    QString semanticRevisionText;
    FormatterProfile currentFormatterProfile = FormatterProfile::Structured;
    bool currentFormatOnSaveEnabled = false;
    bool columnSelectionActive = false;
    bool columnSelectionDragging = false;
    bool columnSelectionAwaitingEndpoint = false;
    bool columnSelectionDragMoved = false;
    int columnAnchorLine = -1;
    int columnAnchorColumn = -1;
    int columnCurrentLine = -1;
    int columnCurrentColumn = -1;
    QList<TemplateSlotRange> templateSlotRanges;
    int templateSlotActiveIndex = -1;
    int templateSlotSessionStart = -1;
    int templateSlotSessionEnd = -1;
    QTimer* templateSlotBlinkTimer = nullptr;
    bool templateSlotBlinkOn = true;
    bool templateSlotIgnoreNextCursorCheck = false;

    void initializeCore(MyCodeEditor* editor);
    void shutdown();
    void attachEditorConnections(MyCodeEditor* editor);
    void attachToEditor(MyCodeEditor* editor);

    EditorSemanticContextService* semanticService() const;
    EditorSourceContextProvider sourceContextProvider(
        const MyCodeEditor* editor) const;
    QString currentModuleNameAt(int charPos) const;
    QString currentModuleName(const MyCodeEditor* editor) const;
    EditorAlwaysScopeTarget currentAlwaysScopeTarget(
        const MyCodeEditor* editor) const;
    EditorModuleScopeTarget currentModuleScopeTarget(
        const MyCodeEditor* editor) const;
    bool executeComPortAppend(MyCodeEditor* editor, QString* message);
    bool executeComSignalInsert(MyCodeEditor* editor, QString* message);
    bool executeComInstanceInsert(MyCodeEditor* editor, QString* message);
    bool executeComAssignInsert(MyCodeEditor* editor, QString* message);
    bool executeComParameterInsert(MyCodeEditor* editor, QString* message);
    bool executeComModuleEndInsert(MyCodeEditor* editor, QString* message);
    EditorPackageToolAvailability currentPackageToolAvailability(
        const MyCodeEditor* editor) const;
    bool executePackageToolInsert(MyCodeEditor* editor,
                                  PackageToolKind kind,
                                  QString* message);
    bool selectInsideBeginEnd(MyCodeEditor* editor, QString* message);
    bool comModeActive() const;
    QString comModeBuffer() const;
    void enterComMode(MyCodeEditor* editor, const QString& message = QString());
    void exitComMode(MyCodeEditor* editor);
    void showComModeMessage(MyCodeEditor* editor, const QString& message);
    void startTemplateSlotMode(MyCodeEditor* editor,
                               int insertionStart,
                               int insertedLength,
                               const CodeTemplateSlotList& slotMetadata);
    bool templateSlotModeActive() const;
    int templateSlotModeActiveIndex() const;
    int templateSlotModeSlotCount() const;
    bool templateSlotModeBlinkOn() const;
    bool columnSelectionActiveForCommand() const;
    QStringList columnSelectionRowTexts(MyCodeEditor* editor) const;
    bool applyColumnSelectionRowTexts(MyCodeEditor* editor,
                                      const QStringList& rows,
                                      bool replaceSelection,
                                      QString* message = nullptr);
    void clearTemplateSlotMode(MyCodeEditor* editor,
                               const QString& message = QString());
    bool handleTemplateSlotKeyPress(MyCodeEditor* editor, QKeyEvent* event);
    void handleTemplateSlotContentsChange(MyCodeEditor* editor,
                                          int position,
                                          int charsRemoved,
                                          int charsAdded);
    void handleTemplateSlotCursorChanged(MyCodeEditor* editor);
    void publishComModeState(MyCodeEditor* editor,
                             const QString& message = QString()) const;
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
    void paintColumnSelection(MyCodeEditor* editor, QPaintEvent* event) const;
    void paintComModeOverlay(MyCodeEditor* editor, QPaintEvent* event) const;
    void handleContextMenu(MyCodeEditor* editor, QContextMenuEvent* event);
    bool handleMousePress(MyCodeEditor* editor, QMouseEvent* event);
    bool handleMouseDoubleClick(MyCodeEditor* editor, QMouseEvent* event);
    bool handleMouseMove(MyCodeEditor* editor, QMouseEvent* event);
    bool handleMouseRelease(MyCodeEditor* editor, QMouseEvent* event);
    void handleLeaveEvent(MyCodeEditor* editor);

    void refreshScopeAndCurrentLineHighlight(MyCodeEditor* editor);
    void refreshSemanticPresentation(MyCodeEditor* editor);
    std::uint64_t semanticDocumentRevision() const;
    void acceptLoadedTextAsSemanticBaseline(const MyCodeEditor* editor);
    void setIncludeFileProvider(
        EditorCompletionWorkflow::IncludeFileProvider provider);
    void setIncludeNewHeaderCreator(
        EditorCompletionWorkflow::IncludeNewHeaderCreator creator);
    void executeEditorActionCommand(MyCodeEditor* editor, const QString& command);
    void setFormatterProfile(FormatterProfile profile);
    FormatterProfile formatterProfile() const;
    void setFormatOnSaveEnabled(bool enabled);
    bool formatOnSaveEnabled() const;
    void formatDocument(MyCodeEditor* editor);
    void formatSelection(MyCodeEditor* editor);
    bool formatDocumentForSave(MyCodeEditor* editor);
    void commentSelectionOrLine(MyCodeEditor* editor);
    void uncommentSelectionOrLine(MyCodeEditor* editor);
    void indentSelectionOrLine(MyCodeEditor* editor);
    void unindentSelectionOrLine(MyCodeEditor* editor);
    bool clearSelectedAssignmentRhs(MyCodeEditor* editor,
                                    QString* message = nullptr);
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
    void setHierarchyInstanceContext(
        const HierarchyInstanceContext& context);
    HierarchyInstanceContext hierarchyInstanceContext() const;
    void closeSemanticPopup(MyCodeEditor* editor);
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
    void flashLine(MyCodeEditor* editor, int lineNumber);
    void applyAppearanceSettings(
        MyCodeEditor* editor,
        const EditorAppearanceOptions& options);
    void applyLineNavigationTarget(
        MyCodeEditor* editor,
        const SourceLineNavigationTarget& target);
};

#endif // EDITORRUNTIME_H
