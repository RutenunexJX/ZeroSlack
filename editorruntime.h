#ifndef EDITORRUNTIME_H
#define EDITORRUNTIME_H

#include "annotationlayer.h"
#include "editoranchoredrangeindex.h"
#include "editorappearance.h"
#include "completiontypes.h"
#include "declaresignalfactcollector.h"
#include "declaresignalservice.h"
#include "editorcompletionui.h"
#include "editorcompletionworkflow.h"
#include "editorcolumnmodecontroller.h"
#include "editorcursornavigation.h"
#include "documentchange.h"
#include "editorfileidentity.h"
#include "editorfolding.h"
#include "editorviewprojection.h"
#include "formatterservice.h"
#include "editorgeometry.h"
#include "editorgutter.h"
#include "editorkeywordghostcontroller.h"
#include "editorlineoperationcontroller.h"
#include "editormodecontroller.h"
#include "editormulticursorcontroller.h"
#include "packagetoolservice.h"
#include "editorselection.h"
#include "editorsignalselectioncontroller.h"
#include "editorsemanticcontextservice.h"
#include "editorsemanticruntime.h"
#include "editorsourcenavigation.h"
#include "editorsyntaxstate.h"
#include "editorstructuralinputcontroller.h"
#include "editortemplateslotcontroller.h"
#include "ghostannotationservice.h"
#include "sourcenavigationservice.h"

#include <cstdint>
#include <atomic>
#include <memory>
#include <QMetaObject>
#include <QPoint>
#include <QPointer>

class MyCodeEditor;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QLineEdit;
class QMenu;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QTextDocument;
struct EditorAlwaysScopeTarget;
struct EditorModuleScopeTarget;
struct EditorStructuralContextMenuState;
struct EditorSynchronousEditState;

struct EditorLogicalCursorState
{
    int anchor = -1;
    int position = -1;
    EditorColumnModeSnapshot column;
    EditorMultiCursorSnapshot multiCursor;
};

struct EditorUndoCursorEntry
{
    int beforeUndoSteps = 0;
    int afterUndoSteps = 0;
    EditorLogicalCursorState before;
    EditorLogicalCursorState after;
};

struct EditorGhostAnnotationAnchorTraits
{
    static int start(const GhostAnnotation& annotation)
    {
        return annotation.anchorPosition;
    }

    static int effectiveEnd(const GhostAnnotation& annotation)
    {
        return std::max(annotation.anchorPosition + 1,
                        annotation.anchorPosition
                            + annotation.anchorLength);
    }

    static int firstLine(const GhostAnnotation& annotation)
    {
        return annotation.line;
    }

    static int lastLine(const GhostAnnotation& annotation)
    {
        return annotation.line;
    }

    static void shift(GhostAnnotation& annotation,
                      int characterDelta,
                      int lineDelta)
    {
        annotation.anchorPosition += characterDelta;
        annotation.line = std::max(
            1, annotation.line + lineDelta);
    }
};

struct EditorSemanticDecorationAnchorTraits
{
    static int start(const SemanticDecoration& decoration)
    {
        return decoration.startPosition;
    }

    static int effectiveEnd(const SemanticDecoration& decoration)
    {
        return decoration.startPosition
            + std::max(0, decoration.length);
    }

    static int firstLine(const SemanticDecoration&)
    {
        return -1;
    }

    static int lastLine(const SemanticDecoration&)
    {
        return -1;
    }

    static void shift(SemanticDecoration& decoration,
                      int characterDelta,
                      int)
    {
        decoration.startPosition += characterDelta;
    }
};

struct EditorVisibleDocumentRange
{
    int firstLine = 0;
    int lastLine = -1;
    int startPosition = 0;
    int endPosition = 0;

    bool valid() const
    {
        return lastLine >= firstLine
            && endPosition >= startPosition;
    }
};

struct MyCodeEditorState
{
    EditorAppearance appearance;
    EditorGutter gutter;
    EditorDocumentGeometry geometry;
    EditorModeController modes;
    EditorTemplateSlotController templateSlots;
    EditorKeywordGhostController keywordGhost;
    EditorStructuralInputController structuralInput;
    EditorLineOperationController lineOperations;
    EditorMultiCursorController multiCursor;
    EditorColumnModeController columnMode;
    EditorSignalSelectionController signalSelection;
    EditorFoldingController folding;
    EditorViewProjection projection;
    EditorCursorNavigation cursorNavigation;
    EditorSyntaxState syntax;
    EditorFileIdentity identity;
    EditorSemanticRuntime semantic;
    EditorCompletionUi completion;
    EditorCompletionWorkflow completionWorkflow;
    EditorHighlightRefresh highlightRefresh;
    EditorSourceNavigationUi sourceNavigation;
    EditorSelection selections;
    HierarchyInstanceContext hierarchyInstance;
    QList<SemanticDiagnostic> diagnostics;
    QHash<int, QList<int>> diagnosticIndexesByLine;
    QHash<int, SemanticDiagnostic::Severity> diagnosticSeverityByLine;
    QHash<int, SemanticDiagnostic::Severity>
        diagnosticOverviewSeverityByBucket;
    std::uint64_t diagnosticComputationRevision = 0;
    EditorAnchoredRangeIndex<
        GhostAnnotation,
        EditorGhostAnnotationAnchorTraits> ghostAnnotations;
    EditorRuntimeAnnotationLayer annotationLayer;
    EditorAnnotationDisplayOptions annotationDisplayOptions;
    EditorAnchoredRangeIndex<
        SemanticDecoration,
        EditorSemanticDecorationAnchorTraits> semanticDecorations;
    // Semantic publications are keyed to text content, not QTextDocument's
    // formatting revision. Appearance and syntax highlighting can advance the
    // latter without changing any source text.
    std::uint64_t semanticTextRevision = 0;
    std::uint64_t ghostQueryGeneration = 0;
    // Keep the live editor cache in the same incremental UTF-16 piece storage
    // used by Tree-sitter. Ordinary typing must not move the untouched suffix
    // of a multi-megabyte document merely to keep cachedDocumentText current.
    TSUTF16Text semanticRevisionText;
    bool inlineFilterTextOverlayActive = false;
    int inlineFilterTextOverlayStart = -1;
    int inlineFilterTextOverlayOriginalLength = 0;
    QString inlineFilterTextOverlayOriginalText;
    QString inlineFilterTextOverlayCurrentText;
    std::shared_ptr<std::atomic_bool> ghostQueryCancellation;
    QList<QPointer<QObject>> ghostQueryWatchers;
    EditorHotPathMetrics hotPathMetrics;
    bool hotPathTimingEnabled = false;
    FormatterProfile currentFormatterProfile = FormatterProfile::Structured;
    EditorPackageToolAvailability lastPackageToolAvailability;
    bool packageToolAvailabilityInitialized = false;
    QString lastWavePreviewScopeKey;
    bool suppressNextCursorPresentation = false;
    bool editorPresentationPending = false;
    bool ghostPresentationPending = false;
    bool rebindingDocument = false;
    int lifecycleDiagnosticRank = -1;
    int synchronousEditTransactionDepth = 0;
    std::uint64_t completedSynchronousEditTransactions = 0;
    int synchronousEditStartRevision = -1;
    int synchronousEditStartUndoSteps = 0;
    bool synchronousEditIsUndoRedo = false;
    bool undoRedoViewportCaptured = false;
    int undoRedoVerticalScroll = 0;
    int undoRedoHorizontalScroll = 0;
    EditorLogicalCursorState synchronousEditStartCursor;
    QList<EditorUndoCursorEntry> undoCursorEntries;
    QPointer<EditorHoverPopup> signalDefinitionPeek;
    QPointer<QLineEdit> signalDefinitionEditor;
    QMetaObject::Connection signalDefinitionPeekClosedConnection;
    QMetaObject::Connection signalDefinitionReturnConnection;
    bool signalDefinitionPeekActive = false;
    std::uint64_t signalDefinitionSessionGeneration = 0;
    QMetaObject::Connection documentContentsChangeConnection;
    int signalDefinitionIdentifierStart = -1;
    int signalDefinitionIdentifierEnd = -1;
    QString signalDefinitionIdentifier;
    QPointer<QTextDocument> signalDefinitionDocument;
    std::uint64_t signalDefinitionDocumentRevision = 0;
    QString signalDefinitionFileName;
    QString signalDefinitionFileIdentityKey;
    HierarchyInstanceContext signalDefinitionHierarchyInstance;
    SemanticSnapshotToken signalDefinitionSemanticToken;
    QString signalDefinitionCandidateId;
    DeclareSignalScopeKind signalDefinitionScopeKind =
        DeclareSignalScopeKind::Module;
    QString signalDefinitionBlockScopeId;
    QPointer<EditorHoverPopup> semanticRenamePeek;
    QPointer<QLineEdit> semanticRenameEditor;
    QMetaObject::Connection semanticRenamePeekClosedConnection;
    QMetaObject::Connection semanticRenameTextConnection;
    QMetaObject::Connection semanticRenameReturnConnection;
    std::uint64_t semanticRenameSessionGeneration = 0;
    QPointer<QTextDocument> semanticRenameDocument;
    std::uint64_t semanticRenameDocumentRevision = 0;
    QString semanticRenameFileName;
    QString semanticRenameFileIdentityKey;
    SemanticSnapshotToken semanticRenameToken;
    SemanticSymbolRecord semanticRenameSubject;
    int lineBoundarySelectionAnchor = -1;
    int lineBoundarySelectionDirection = 0;
    bool lineBoundarySelectionAtPhysicalEdge = false;
    QList<int> multiLineBoundarySelectionAnchors;
    int multiLineBoundarySelectionDirection = 0;

    void initializeCore(MyCodeEditor* editor);
    void shutdown(MyCodeEditor* editor);
    void attachEditorConnections(MyCodeEditor* editor);
    void attachDocumentConnection(MyCodeEditor* editor);
    void attachToEditor(MyCodeEditor* editor);
    void rebindDocument(MyCodeEditor* editor,
                        QTextDocument* document,
                        std::uint64_t textRevision);
    void bindEditorModes(MyCodeEditor* editor);
    EditorModeSnapshot modeSnapshot() const;
    void exitInteractionModes(EditorModeExitReason reason);

    EditorSemanticContextService* semanticService() const;
    EditorSourceContextProvider sourceContextProvider(
        const MyCodeEditor* editor) const;
    QString currentModuleNameAt(int charPos) const;
    QString currentModuleName(const MyCodeEditor* editor) const;
    EditorAlwaysScopeTarget currentAlwaysScopeTarget(
        const MyCodeEditor* editor) const;
    EditorModuleScopeTarget currentModuleScopeTarget(
        const MyCodeEditor* editor) const;
    bool goToFinalEndmodule(MyCodeEditor* editor, QString* message);
    EditorPackageToolAvailability currentPackageToolAvailability(
        const MyCodeEditor* editor) const;
    bool executePackageToolInsert(MyCodeEditor* editor,
                                  PackageToolKind kind,
                                  QString* message);
    bool selectInsideBeginEnd(MyCodeEditor* editor, QString* message);
    void startTemplateSlotMode(MyCodeEditor* editor,
                               int insertionStart,
                               int insertedLength,
                               const CodeTemplateSlotList& slotMetadata);
    bool templateSlotModeActive() const;
    int templateSlotModeActiveIndex() const;
    int templateSlotModeSlotCount() const;
    bool templateSlotModeBlinkOn() const;
    bool columnSelectionActiveForCommand() const;
    EditorColumnModeSnapshot columnModeSnapshotForTest() const;
    bool virtualCursorActiveForTest() const;
    int virtualCursorLineForTest() const;
    int virtualCursorColumnForTest() const;
    QStringList columnSelectionRowTexts(MyCodeEditor* editor) const;
    bool applyColumnSelectionRowTexts(MyCodeEditor* editor,
                                      const QStringList& rows,
                                      bool replaceSelection,
                                      QString* message = nullptr);
    void clearTemplateSlotMode(MyCodeEditor* editor,
                               const QString& message = QString(),
                               bool updatePresentation = true);
    bool handleTemplateSlotKeyPress(MyCodeEditor* editor, QKeyEvent* event);
    bool handleVirtualCursorKeyPress(MyCodeEditor* editor,
                                     QKeyEvent* event);
    bool handleLexicalNavigationOrDeletion(MyCodeEditor* editor,
                                           QKeyEvent* event);
    bool handleStructuralNavigation(MyCodeEditor* editor,
                                    QKeyEvent* event);
    bool handleLineBoundarySelection(MyCodeEditor* editor,
                                     QKeyEvent* event);
    bool handleMultiCursorLineBoundarySelection(
        MyCodeEditor* editor,
        QKeyEvent* event);
    void prepareVirtualCursorInput(MyCodeEditor* editor);
    void clearVirtualCursor(MyCodeEditor* editor);
    void clearPendingColumnAnchor();
    void handleVirtualCursorChanged(MyCodeEditor* editor);
    void handleTemplateSlotContentsChange(MyCodeEditor* editor,
                                          int position,
                                          int charsRemoved,
                                          int charsAdded,
                                          bool updatePresentation = true);
    void handleTemplateSlotCursorChanged(MyCodeEditor* editor);
    EditorSemanticContext semanticContextForPosition(
        const MyCodeEditor* editor,
        int cursorPosition,
        bool includeDocumentText) const;

    void handleControlKeyPress(MyCodeEditor* editor, QKeyEvent* event);
    void handleControlKeyRelease(MyCodeEditor* editor, QKeyEvent* event);
    bool handleKeyPress(MyCodeEditor* editor, QKeyEvent* event);
    bool executeClipboardAction(
        MyCodeEditor* editor,
        const QString& actionId,
        QString* failureReason = nullptr);
    EditorLineOperationResult executeLineOperation(
        MyCodeEditor* editor,
        EditorLineOperation operation);
    bool selectSymbolOccurrences(
        MyCodeEditor* editor,
        bool allInScope,
        QString* failureReason = nullptr);
    bool expandSmartSelection(
        MyCodeEditor* editor,
        QString* message = nullptr);
    bool navigateSelectedSymbolOccurrence(
        MyCodeEditor* editor,
        bool previous,
        QString* message = nullptr);
    void beginSynchronousEditTransaction(MyCodeEditor* editor);
    void endSynchronousEditTransaction(MyCodeEditor* editor);
    void beginUndoRedo(MyCodeEditor* editor);
    void restoreCursorAfterUndoRedo(MyCodeEditor* editor,
                                    bool redo,
                                    int beforeUndoSteps,
                                    int afterUndoSteps);
    EditorSynchronousEditState synchronousEditStateForTest() const;
    void finishEditorInput(MyCodeEditor* editor);
    bool handleKeyRelease(MyCodeEditor* editor, QKeyEvent* event);
    bool handleDragEnter(MyCodeEditor* editor, QDragEnterEvent* event);
    bool handleDragMove(MyCodeEditor* editor, QDragMoveEvent* event);
    bool handleDrop(MyCodeEditor* editor, QDropEvent* event);
    void handleResize(MyCodeEditor* editor);
    bool handleGutterMousePress(MyCodeEditor* editor, QMouseEvent* event);
    bool handleGutterMouseMove(MyCodeEditor* editor, QMouseEvent* event);
    void paintGutterDecorations(MyCodeEditor* editor,
                                QPainter& painter,
                                const QRect& rect) const;
    void paintDiagnosticOverview(MyCodeEditor* editor,
                                 QPaintEvent* event);
    void paintFoldPlaceholders(MyCodeEditor* editor, QPaintEvent* event) const;
    void paintGhostAnnotations(MyCodeEditor* editor, QPaintEvent* event);
    void paintMultiCursor(MyCodeEditor* editor, QPaintEvent* event) const;
    void paintColumnSelection(MyCodeEditor* editor, QPaintEvent* event) const;
    void handleContextMenu(MyCodeEditor* editor, QContextMenuEvent* event);
    EditorStructuralContextMenuState structuralContextMenuState(
        const MyCodeEditor* editor,
        int cursorPosition) const;
    bool editInstanceSlotsAt(MyCodeEditor* editor,
                             int cursorPosition,
                             QString* message = nullptr);
    DeclareSignalFactCollectionResult
        signalDefinitionFactsAt(
            const MyCodeEditor* editor,
            int cursorPosition,
            SemanticSnapshotToken* semanticToken = nullptr,
            std::uint64_t expectedSemanticGeneration = 0,
            std::uint64_t expectedDocumentRevision = 0) const;
    DeclareSignalProposal signalDefinitionProposalAt(
        const MyCodeEditor* editor,
        int cursorPosition,
        DeclareSignalFactCollectionResult* facts = nullptr,
        SemanticSnapshotToken* semanticToken = nullptr,
        std::uint64_t expectedSemanticGeneration = 0,
        std::uint64_t expectedDocumentRevision = 0) const;
    QString signalDefinitionCandidateAt(
        const MyCodeEditor* editor,
        int cursorPosition,
        QString* failureReason = nullptr) const;
    bool beginSignalDefinitionEditor(
        MyCodeEditor* editor,
        int cursorPosition,
        QString* failureReason = nullptr);
    bool confirmSignalDefinition(
        MyCodeEditor* editor,
        const QString& declaration,
        QString* failureReason = nullptr);
    void clearSignalDefinitionEditorState();
    void cancelSignalDefinitionEditor();
    bool beginSemanticRenameEditor(
        MyCodeEditor* editor,
        QString* failureReason = nullptr);
    void clearSemanticRenameEditorState();
    void cancelSemanticRenameEditor();
    bool startSignalSelectionMode(MyCodeEditor* editor,
                                  QString* message = nullptr);
    void cancelSignalSelectionMode(MyCodeEditor* editor);
    bool signalSelectionModeActive() const;
    QStringList selectedSignalNames() const;
    bool toggleSignalSelectionAt(MyCodeEditor* editor,
                                 int cursorPosition,
                                 bool toggle,
                                 bool desiredState = true);
    std::optional<EditorSignalSelectionCandidate>
        resolveSignalSelectionCandidate(
            const MyCodeEditor* editor,
            int cursorPosition) const;
    bool createAssignmentQueueAt(MyCodeEditor* editor,
                                 int cursorPosition,
                                 QString* message = nullptr);
    bool handleMousePress(MyCodeEditor* editor, QMouseEvent* event);
    bool handleMouseDoubleClick(MyCodeEditor* editor, QMouseEvent* event);
    bool handleMouseMove(MyCodeEditor* editor, QMouseEvent* event);
    bool handleMouseRelease(MyCodeEditor* editor, QMouseEvent* event);
    void handleLeaveEvent(MyCodeEditor* editor);

    void refreshScopeAndCurrentLineHighlight(MyCodeEditor* editor);
    void refreshSemanticPresentation(MyCodeEditor* editor);
    std::uint64_t semanticDocumentRevision() const;
    QString materializeDocumentText(const MyCodeEditor* editor);
    const QString& cachedDocumentText();
    int cachedDocumentLength() const;
    QString cachedDocumentSlice(int position, int length);
    TSTextStorageMetrics cachedTextStorageMetricsForTest() const;
    bool beginInlineFilterTextOverlay(int startPosition,
                                      int endPosition);
    void finishInlineFilterTextOverlay();
    void acceptLoadedTextAsSemanticBaseline(const MyCodeEditor* editor);
    EditorHotPathMetrics hotPathMetricsForTest() const;
    bool inlineFilterTextOverlayActiveForTest() const;
    EditorOccurrenceIndexStats occurrenceIndexStatsForTest() const;
    QList<int> occurrencePositionsForTest(const QString& word) const;
    void resetHotPathMetricsForTest();
    void setIncludeFileProvider(
        EditorCompletionWorkflow::IncludeFileProvider provider);
    void setIncludeNewHeaderCreator(
        EditorCompletionWorkflow::IncludeNewHeaderCreator creator);
    QStringList includeFileCandidates() const;
    bool insertPackageImportAtCursor(MyCodeEditor* editor,
                                     const QString& packageName,
                                     QString* failureReason = nullptr);
    bool insertHeaderIncludeAtCursor(MyCodeEditor* editor,
                                     const QString& includePath,
                                     QString* failureReason = nullptr);
    bool createAndInsertHeaderAtCursor(MyCodeEditor* editor,
                                       const QString& fileName,
                                       QString* failureReason = nullptr);
    void executeEditorActionCommand(MyCodeEditor* editor, const QString& command);
    void setFormatterProfile(FormatterProfile profile);
    FormatterProfile formatterProfile() const;
    FormatterReport formatDocument(MyCodeEditor* editor);
    FormatterReport formatSelection(MyCodeEditor* editor);
    void commentSelectionOrLine(MyCodeEditor* editor);
    void uncommentSelectionOrLine(MyCodeEditor* editor);
    void indentSelectionOrLine(MyCodeEditor* editor);
    void unindentSelectionOrLine(MyCodeEditor* editor);
    bool clearSelectedAssignmentRhs(MyCodeEditor* editor,
                                    QString* message = nullptr);
    QList<GhostAnnotation> ghostAnnotationsForTest() const;
    AnnotationLayerReport annotationLayerReportForTest(
        const AnnotationLayerQuery& query = {}) const;
    QString syntaxTextForTest() const;
    EditorLargeFileSyntaxSnapshot largeFileSyntaxSnapshotForTest() const;
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
    void clearDiagnosticHighlights(MyCodeEditor* editor);
    QString diagnosticTooltipForLine(int zeroBasedLine) const;
    QList<int> diagnosticOverviewLinesForTest() const;
    SemanticDiagnostic::Severity diagnosticSeverityForLineForTest(
        int zeroBasedLine,
        bool* available = nullptr) const;
    void refreshGhostAnnotations(MyCodeEditor* editor);
    void shutdownGhostQueries(MyCodeEditor* editor);
    void setSemanticDecorations(
        MyCodeEditor* editor,
        const QList<SemanticDecoration>& decorations);
    void setGhostAnnotations(
        MyCodeEditor* editor,
        const QList<GhostAnnotation>& annotations);
    void setAnnotationDisplayOptions(
        MyCodeEditor* editor,
        const EditorAnnotationDisplayOptions& options);
    EditorAnnotationDisplayOptions
    currentAnnotationDisplayOptions() const;
    void refreshGhostAnnotationLayer(MyCodeEditor* editor);
    void handleDocumentContentsChange(MyCodeEditor* editor,
                                      int position,
                                      int charsRemoved,
                                      int charsAdded);
    void remapGhostAnnotations(MyCodeEditor* editor,
                               const DocumentChange& change);
    void remapSemanticDecorations(MyCodeEditor* editor,
                                  const DocumentChange& change);
    void rebuildSemanticDecorationPositionIndex();
    EditorVisibleDocumentRange visibleDocumentRange(
        const MyCodeEditor* editor) const;
    void refreshVisibleRegionPresentation(MyCodeEditor* editor);
    void rebuildDiagnosticOverviewIndex(
        const MyCodeEditor* editor);
    void refreshDiagnosticPresentation(MyCodeEditor* editor);
    void refreshSemanticDecorationPresentation(MyCodeEditor* editor);
    void refreshDerivedEditorState(MyCodeEditor* editor,
                                   bool allowWavePreviewSignal);
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
