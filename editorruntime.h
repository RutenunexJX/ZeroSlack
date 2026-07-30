#ifndef EDITORRUNTIME_H
#define EDITORRUNTIME_H

#include "editorappearance.h"
#include "completiontypes.h"
#include "editorcompletionui.h"
#include "editorcompletionworkflow.h"
#include "editorcolumnmodecontroller.h"
#include "editorcursornavigation.h"
#include "documentchange.h"
#include "editorfileidentity.h"
#include "editorfolding.h"
#include "formatterservice.h"
#include "editorgeometry.h"
#include "editorgutter.h"
#include "editormodecontroller.h"
#include "packagetoolservice.h"
#include "editorselection.h"
#include "editorsignalselectioncontroller.h"
#include "editorsemanticcontextservice.h"
#include "editorsemanticruntime.h"
#include "editorsourcenavigation.h"
#include "editorsyntaxstate.h"
#include "editortemplateslotcontroller.h"
#include "ghostannotationservice.h"
#include "sourcenavigationservice.h"

#include <cstdint>
#include <atomic>
#include <memory>
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
struct EditorAlwaysScopeTarget;
struct EditorModuleScopeTarget;
struct EditorStructuralContextMenuState;
struct EditorSynchronousEditState;

struct MyCodeEditorState
{
    EditorAppearance appearance;
    EditorGutter gutter;
    EditorDocumentGeometry geometry;
    EditorModeController modes;
    EditorTemplateSlotController templateSlots;
    EditorColumnModeController columnMode;
    EditorSignalSelectionController signalSelection;
    EditorFoldingController folding;
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
    std::uint64_t diagnosticComputationRevision = 0;
    QList<GhostAnnotation> ghostAnnotations;
    QList<SemanticDecoration> semanticDecorations;
    // Semantic publications are keyed to text content, not QTextDocument's
    // formatting revision. Appearance and syntax highlighting can advance the
    // latter without changing any source text.
    std::uint64_t semanticTextRevision = 0;
    std::uint64_t ghostQueryGeneration = 0;
    QString semanticRevisionText;
    bool inlineFilterTextOverlayActive = false;
    int inlineFilterTextOverlayStart = -1;
    int inlineFilterTextOverlayOriginalLength = 0;
    QString inlineFilterTextOverlayOriginalText;
    QString inlineFilterTextOverlayCurrentText;
    std::shared_ptr<std::atomic_bool> ghostQueryCancellation;
    EditorHotPathMetrics hotPathMetrics;
    bool hotPathTimingEnabled = false;
    FormatterProfile currentFormatterProfile = FormatterProfile::Structured;
    bool currentFormatOnSaveEnabled = false;
    EditorPackageToolAvailability lastPackageToolAvailability;
    bool packageToolAvailabilityInitialized = false;
    QString lastWavePreviewScopeKey;
    bool suppressNextCursorPresentation = false;
    bool editorPresentationPending = false;
    bool ghostPresentationPending = false;
    int synchronousEditTransactionDepth = 0;
    std::uint64_t completedSynchronousEditTransactions = 0;
    QPointer<QLineEdit> signalDefinitionEditor;
    int signalDefinitionIdentifierStart = -1;
    std::uint64_t signalDefinitionDocumentRevision = 0;
    QString signalDefinitionFileName;

    void initializeCore(MyCodeEditor* editor);
    void shutdown();
    void attachEditorConnections(MyCodeEditor* editor);
    void attachToEditor(MyCodeEditor* editor);
    void bindEditorModes(MyCodeEditor* editor);
    EditorModeSnapshot modeSnapshot() const;
    void exitInteractionModes(EditorModeExitReason reason);

    EditorSemanticContextService* semanticService() const;
    EditorSourceContextProvider sourceContextProvider(
        const MyCodeEditor* editor) const;
    QString currentModuleNameAt(int charPos) const;
    QString currentModuleName(const MyCodeEditor* editor) const;
    EditorAlwaysScopeTarget currentAlwaysScopeTarget(
        const MyCodeEditor* editor,
        bool allowLargeFileScopeBuild = true) const;
    EditorModuleScopeTarget currentModuleScopeTarget(
        const MyCodeEditor* editor,
        bool allowLargeFileScopeBuild = true) const;
    bool addPortRow(MyCodeEditor* editor, QString* message);
    bool addSignalRow(MyCodeEditor* editor, QString* message);
    bool addParameterRow(MyCodeEditor* editor, QString* message);
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
    void prepareVirtualCursorInput(MyCodeEditor* editor);
    void clearVirtualCursor(MyCodeEditor* editor);
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
    void beginSynchronousEditTransaction();
    void endSynchronousEditTransaction(MyCodeEditor* editor);
    EditorSynchronousEditState synchronousEditStateForTest() const;
    void finishEditorInput(MyCodeEditor* editor);
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
    void paintDiagnosticOverview(MyCodeEditor* editor,
                                 QPaintEvent* event) const;
    void paintFoldPlaceholders(MyCodeEditor* editor, QPaintEvent* event) const;
    void paintGhostAnnotations(MyCodeEditor* editor, QPaintEvent* event) const;
    void paintColumnSelection(MyCodeEditor* editor, QPaintEvent* event) const;
    void handleContextMenu(MyCodeEditor* editor, QContextMenuEvent* event);
    EditorStructuralContextMenuState structuralContextMenuState(
        const MyCodeEditor* editor,
        int cursorPosition) const;
    bool editInstanceSlotsAt(MyCodeEditor* editor,
                             int cursorPosition,
                             QString* message = nullptr);
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
    void cancelSignalDefinitionEditor();
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
    QList<GhostAnnotation> ghostAnnotationsForTest() const;
    QString syntaxTextForTest() const;
    EditorLargeFileSyntaxScopeSnapshot largeFileSyntaxScopeForTest() const;
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
    void setSemanticDecorations(
        MyCodeEditor* editor,
        const QList<SemanticDecoration>& decorations);
    void setGhostAnnotations(
        MyCodeEditor* editor,
        const QList<GhostAnnotation>& annotations);
    void handleDocumentContentsChange(MyCodeEditor* editor,
                                      int position,
                                      int charsRemoved,
                                      int charsAdded);
    void remapGhostAnnotations(MyCodeEditor* editor,
                               const DocumentChange& change);
    void remapSemanticDecorations(MyCodeEditor* editor,
                                  const DocumentChange& change);
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
