#ifndef MYCODEEDITOR_H
#define MYCODEEDITOR_H

#include "zeroslackexport.h"

#include "annotationlayer.h"
#include "semanticindex.h"
#include "semanticdecorationservice.h"
#include "ghostannotationservice.h"
#include "foldblockshelfmodel.h"
#include "formatterservice.h"
#include "editorfoldviewstate.h"
#include "editorviewprojection.h"
#include "includeheaderworkflowtypes.h"
#include "completiontypes.h"
#include "documentchange.h"
#include "editorinsighttargetpickcontroller.h"
#include "editormodecontroller.h"
#include "packagetoolservice.h"
#include "symbolpresentationservice.h"

#include <QList>
#include <QPlainTextEdit>
#include <QStringList>
#include <QVariantMap>
#include <cstdint>
#include <functional>

class QMenu;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QFocusEvent;
class QMouseEvent;
class QPaintEvent;
class QRect;
class QContextMenuEvent;
class QKeyEvent;
class QInputMethodEvent;
class QResizeEvent;
class QWheelEvent;
class QTextDocument;
class TSDocument;
struct TSTextStorageMetrics;
class EditorDocumentGeometry;
class EditorSemanticContextService;
class EditorGutter;
class EditorFoldingController;
struct EditorLargeFileSyntaxSnapshot;
class EditorCompletionWorkflow;
class EditorSourceNavigationUi;
struct EditorAppearanceOptions;
struct EditorOccurrenceIndexStats;
struct EditorColumnModeSnapshot;
struct EditorMultiCursorSnapshot;
struct EditorSemanticContext;
struct EditorSourceNavigationTarget;
struct SourceLineNavigationTarget;
enum class SourceSymbolAction;
struct MyCodeEditorState;

struct EditorAlwaysScopeTarget {
    int startPosition = -1;
    int endPosition = -1;
    int startLine = 0;
    int endLine = 0;
    QString label;
    QString failureMessage;
    bool available = false;

    bool ok() const
    {
        return available && startPosition >= 0 && endPosition > startPosition;
    }
};

struct EditorModuleScopeTarget {
    int startPosition = -1;
    int endPosition = -1;
    int startLine = 0;
    int endLine = 0;
    QString moduleName;
    QString label;
    QString failureMessage;
    bool available = false;

    bool ok() const
    {
        return available && startPosition >= 0 && endPosition > startPosition;
    }
};

struct EditorSymbolPaletteContext {
    QString initialQuery;
    int replacementStart = -1;
    int replacementLength = 0;
    int documentRevision = -1;
    bool memberAccess = false;
    QStringList memberPath;
    QString expectedTypeIdentifier;
};

struct EditorStructuralContextMenuState {
    bool signalDefinitionAvailable = false;
    bool instanceSlotsAvailable = false;
};

struct EditorBlockGeometry {
    qreal top = 0;
    qreal height = 0;
};

struct EditorSynchronousEditState {
    int transactionDepth = 0;
    bool presentationPending = false;
    bool cursorPresentationSuppressed = false;
    std::uint64_t completedTransactionCount = 0;
};

class ZEROSLACK_API MyCodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    class SynchronousEditTransaction
    {
    public:
        SynchronousEditTransaction(const SynchronousEditTransaction&) = delete;
        SynchronousEditTransaction& operator=(
            const SynchronousEditTransaction&) = delete;
        ~SynchronousEditTransaction();

    private:
        friend class MyCodeEditor;
        explicit SynchronousEditTransaction(MyCodeEditor* editor);
        MyCodeEditor* editor = nullptr;
    };

    explicit MyCodeEditor(QWidget *parent = nullptr);
    ~MyCodeEditor();

    void attachSharedDocument(QTextDocument* document,
                              std::uint64_t textRevision = 0);
    void setPlainText(const QString& text);
    void insertPlainText(const QString& text);
    void clear();
    void copy();
    void cut();
    void paste();
    void undo();
    void redo();
    bool find(const QString& expression,
              QTextDocument::FindFlags options = {});
    void showFindDialog();
    bool duplicateLines(QString* failureReason = nullptr);
    bool deleteSelectedContent(QString* failureReason = nullptr);
    bool deleteLines(QString* failureReason = nullptr);
    bool joinLines(QString* failureReason = nullptr);
    bool moveLinesUp(QString* failureReason = nullptr);
    bool moveLinesDown(QString* failureReason = nullptr);
    bool addNextSymbolOccurrence(
        QString* failureReason = nullptr);
    bool selectAllSymbolOccurrences(
        QString* failureReason = nullptr);
    bool expandSmartSelection(
        QString* message = nullptr);
    bool goToNextSelectedSymbolOccurrence(
        QString* message = nullptr);
    bool goToPreviousSelectedSymbolOccurrence(
        QString* message = nullptr);
    void setTextCursor(const QTextCursor& cursor);
    void centerCursor();
    void ensureCursorVisible();
    QTextCursor cursorForPosition(const QPoint& position) const;
    QRect cursorRect(const QTextCursor& cursor) const;
    QRect cursorRect() const;
    QTextBlock firstVisibleBlock() const;
    QTextBlock nextVisibleBlock(const QTextBlock& block) const;
    QRectF blockBoundingGeometry(const QTextBlock& block) const;
    QRectF blockBoundingRect(const QTextBlock& block) const;
    QPointF contentOffset() const;
    void applyLineNavigationTarget(const SourceLineNavigationTarget& target);
    EditorBlockGeometry blockGeometry(int blockNumber) const;
    qreal documentHeightPx() const;
    void refreshScopeAndCurrentLineHighlight();
    void refreshSemanticPresentation();
    std::uint64_t semanticDocumentRevision() const;
    const TSDocument* syntaxDocument() const;
    QString toPlainText() const;
    const QString& cachedDocumentText() const;
    int cachedDocumentLength() const;
    QString cachedDocumentSlice(int position, int length) const;
    [[nodiscard]] SynchronousEditTransaction
    beginSynchronousEditTransaction();
    EditorSynchronousEditState synchronousEditStateForTest() const;
    void acceptLoadedTextAsSemanticBaseline();
    EditorHotPathMetrics hotPathMetricsForTest() const;
    TSTextStorageMetrics cachedTextStorageMetricsForTest() const;
    bool inlineFilterTextOverlayActiveForTest() const;
    EditorOccurrenceIndexStats occurrenceIndexStatsForTest() const;
    QList<int> occurrencePositionsForTest(const QString& word) const;
    void resetHotPathMetricsForTest();
    void setIncludeFileCompletionProvider(
        std::function<QStringList(const QString& currentFile)> provider);
    void setIncludeNewHeaderCreator(
        std::function<IncludeNewHeaderResult(
            const IncludeNewHeaderRequest& request)> creator);
    QStringList includeFileCompletionCandidates() const;
    bool insertPackageImport(const QString& packageName,
                             QString* failureReason = nullptr);
    bool insertHeaderInclude(const QString& includePath,
                             QString* failureReason = nullptr);
    bool createAndInsertHeader(const QString& fileName,
                               QString* failureReason = nullptr);
    void setSemanticContextService(EditorSemanticContextService* service);
    void setHierarchyInstanceContext(
        const HierarchyInstanceContext& context);
    HierarchyInstanceContext hierarchyInstanceContext() const;
    void closeSemanticPopup();
    void setDocumentFileName(QString fileName);
    QString documentFileName() const;
    QString currentModuleName() const;
    EditorAlwaysScopeTarget currentAlwaysScopeTarget() const;
    EditorAlwaysScopeTarget alwaysScopeTargetAt(int cursorPosition) const;
    EditorModuleScopeTarget currentModuleScopeTarget() const;
    bool goToFinalEndmodule(QString* message = nullptr);
    EditorPackageToolAvailability currentPackageToolAvailability() const;
    bool executePackageToolInsert(PackageToolKind kind,
                                  QString* message = nullptr);
    bool selectInsideBeginEnd(QString* message = nullptr);
    void startTemplateSlotMode(int insertionStart,
                               int insertedLength,
                               const CodeTemplateSlotList& slotMetadata);
    bool insertCompletionText(
        const QString& text,
        int selectionStart = -1,
        int selectionLength = 0,
        const CodeTemplateSlotList& slotMetadata = {},
        QString* failureReason = nullptr);
    bool templateSlotModeActive() const;
    int templateSlotModeActiveIndex() const;
    int templateSlotModeSlotCount() const;
    bool templateSlotModeBlinkOnForTest() const;
    bool columnSelectionActive() const;
    EditorColumnModeSnapshot columnModeSnapshotForTest() const;
    EditorMultiCursorSnapshot multiCursorSnapshotForTest() const;
    bool virtualCursorActiveForTest() const;
    int virtualCursorLineForTest() const;
    int virtualCursorColumnForTest() const;
    QStringList columnSelectionTexts() const;
    bool applyColumnSelectionTexts(const QStringList& rows,
                                   bool replaceSelection,
                                   QString* message = nullptr);
    void setDiagnosticHighlights(
        const QList<SemanticDiagnostic>& diagnostics);
    QString diagnosticTooltipForLineForTest(int zeroBasedLine) const;
    QList<int> diagnosticOverviewLinesForTest() const;
    SemanticDiagnostic::Severity diagnosticSeverityForLineForTest(
        int zeroBasedLine,
        bool* available = nullptr) const;
    void setSemanticDecorations(
        const QList<SemanticDecoration>& decorations);
    void setGhostAnnotations(
        const QList<GhostAnnotation>& annotations);
    void setPinloomCodeLinkAnnotations(
        const QList<EditorAnnotation>& annotations);
    void setAnnotationDisplayOptions(
        const EditorAnnotationDisplayOptions& options);
    EditorAnnotationDisplayOptions
    annotationDisplayOptions() const;
    FormatterReport formatDocument();
    bool goToLineNumber(int lineNumber);
    bool replaceNextText(const QString& needle,
                         const QString& replacement,
                         bool caseSensitive = false);
    int replaceAllText(const QString& needle,
                       const QString& replacement,
                       bool caseSensitive = false);
    int showGotoLineDialog();
    void showReplaceDialog();
    bool toggleSelectionCase(QString* failureReason = nullptr);
    bool replaceSelectionWithSpaces(
        QString* failureReason = nullptr);
    bool canOrganizeSignalDeclarationsAt(
        int cursorPosition,
        QString* failureReason = nullptr) const;
    bool organizeSignalDeclarationsAt(
        int cursorPosition,
        QString* failureReason = nullptr);
    void commentSelectionOrLine();
    void uncommentSelectionOrLine();
    void indentSelectionOrLine();
    void unindentSelectionOrLine();
    bool clearSelectedAssignmentRhs(QString* message = nullptr);
    EditorStructuralContextMenuState structuralContextMenuState(
        int cursorPosition) const;
    bool editInstanceSlotsAt(int cursorPosition,
                             QString* message = nullptr);
    bool beginSignalDefinitionEditorAt(
        int cursorPosition,
        QString* failureReason = nullptr);
    bool beginSemanticRename(
        QString* failureReason = nullptr);
    bool editInstanceSlotsAtForTest(int cursorPosition,
                                    QString* message = nullptr);
    QStringList structuralContextMenuActionsForTest(
        int cursorPosition);
    QString signalDefinitionCandidateForTest(
        int cursorPosition,
        QString* failureReason = nullptr) const;
    bool beginSignalDefinitionEditorForTest(
        int cursorPosition,
        QString* failureReason = nullptr);
    bool confirmSignalDefinitionForTest(
        const QString& declaration,
        QString* failureReason = nullptr);
    bool startSignalSelectionMode(QString* message = nullptr);
    bool signalSelectionModeActiveForTest() const;
    // Blinks every target of the requested class in the visible region and
    // reports the one the user picks. The validator is the caller's second
    // stage: rejecting a candidate keeps the mode running with its reason.
    bool startInsightTargetPickMode(
        EditorInsightTargetClass targetClass,
        EditorInsightTargetPickController::Validator validator,
        EditorInsightTargetPickController::PickedHandler handler,
        QString* message = nullptr);
    void cancelInsightTargetPickMode();
    bool insightTargetPickModeActive() const;
    QList<EditorInsightTargetCandidate>
        insightTargetCandidatesForTest() const;
    int insightTargetActiveIndexForTest() const;
    bool insightTargetBlinkOnForTest() const;
    QPair<int, int> insightTargetEnumeratedLineRangeForTest() const;
    QList<QString> insightTargetNameSetForTest(
        EditorInsightTargetClass targetClass) const;
    void publishInsightTargetAnnotationsForTest(int firstVisibleLine,
                                                int lastVisibleLine);
    QStringList selectedSignalNames() const;
    QStringList selectedSignalNamesForTest() const;
    bool toggleSignalSelectionAtForTest(int cursorPosition);
    bool createAssignmentQueueAt(
        int cursorPosition,
        QString* message = nullptr);
    bool createAssignmentQueueAtForTest(
        int cursorPosition,
        QString* message = nullptr);
    EditorModeSnapshot editorModeSnapshot() const;
    bool editorModeActiveForTest(EditorModeId id) const;
    void exitInteractionModes(EditorModeExitReason reason);
    void highlightSearchMatches(const QString& text, bool caseSensitive);
    void clearSearchMatches();
    void flashLine(int lineNumber);
    void flashRange(int startChar, int endChar);
    void applyAppearanceSettings(const EditorAppearanceOptions& options);
    void startFoldRegionMarkMode();
    void cancelFoldRegionMarkMode();
    bool foldRegionMarkModeActive() const;
    void startFoldShelfMode();
    void cancelFoldShelfMode();
    bool foldShelfModeActive() const;
    bool insertCustomFoldMarkersForTest(int startLine,
                                        int endLine,
                                        const QString& alias = QString());
    bool toggleFoldAtLineForTest(int line);
    bool foldCollapsedAtLineForTest(int line) const;
    bool foldLineVisibleForTest(int line) const;
    bool sourceLineVisible(int line) const;
    bool viewProjectionActive() const;
    void invalidateViewProjection();
    EditorViewProjectionMetrics viewProjectionMetricsForTest() const;
    EditorFoldViewState foldingViewState() const;
    void restoreFoldingViewState(const EditorFoldViewState& state);
    QList<GhostAnnotation> ghostAnnotationsForTest() const;
    AnnotationLayerReport annotationLayerReportForTest(
        const AnnotationLayerQuery& query = {}) const;
    QString syntaxTextForTest() const;
    EditorLargeFileSyntaxSnapshot largeFileSyntaxSnapshotForTest() const;
    FoldShelfItem foldShelfItemAtLineForTest(
        int line,
        FoldShelfOriginKind origin = FoldShelfOriginKind::Copied) const;
    bool deleteCustomFoldAtLineForTest(int line);
    bool insertFoldShelfItemAtLineForTest(const FoldShelfItem& item, int line);
    EditorSemanticContext editorSemanticContextForPosition(
        int cursorPosition = -1,
        bool includeDocumentText = false) const;
    EditorSymbolPaletteContext symbolPaletteContext() const;
    bool syntaxCommentAt(int cursorPosition) const;
    GhostNumericLiteralReport numericLiteralAt(
        int cursorPosition) const;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void scrollContentsBy(int dx, int dy) override;
    void doSetTextCursor(const QTextCursor& cursor) override;

private:
    friend class EditorDocumentGeometry;
    friend class EditorCompletionWorkflow;
    friend class EditorGutter;
    friend class EditorFoldingController;
    friend class EditorSourceNavigationUi;
    friend struct MyCodeEditorState;

    std::unique_ptr<MyCodeEditorState> state;
    QList<MyCodeEditor*> sharedDocumentViewsForFormatting() const;

signals:
    void fileNameChanged(const QString& fileName);
    void hierarchyInstanceContextChanged(
        const HierarchyInstanceContext& context);
    void sourceNavigationRequested(const EditorSourceNavigationTarget& target,
                                   const EditorSemanticContext& context);
    void sourceSymbolActionRequested(SourceSymbolAction action,
                                     const EditorSemanticContext& context);
    void registeredActionRequested(
        const QString& actionId,
        const QVariantMap& parameters,
        bool* handled);
    void sourceSymbolContextMenuRequested(QMenu* menu,
                                          const EditorSemanticContext& context);
    void definitionPreviewNavigationRequested(const QString& fileName,
                                              int line,
                                              int column);
    void navigationBackRequested();
    void navigationForwardRequested();
    void editorStatusMessageRequested(const QString& message);
    void editorModeStateChanged(const EditorModeSnapshot& snapshot);
    void foldShelfItemConsumed(const QString& id);
    void fontZoomRequested(int steps);
    void documentChangeApplied(const DocumentChange& change);
    void pinloomCodeLinkActivated(const QString& anchorId);
    void packageToolAvailabilityChanged(
        const EditorPackageToolAvailability& availability);
    void wavePreviewScopeChanged();
};

#endif // MYCODEEDITOR_H
