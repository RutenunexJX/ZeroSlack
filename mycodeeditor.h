#ifndef MYCODEEDITOR_H
#define MYCODEEDITOR_H

#include "semanticindex.h"
#include "semanticdecorationservice.h"
#include "ghostannotationservice.h"
#include "foldblockshelfmodel.h"
#include "includeheaderworkflowtypes.h"
#include "completiontypes.h"
#include "documentchange.h"
#include "packagetoolservice.h"
#include "symbolpresentationservice.h"

#include <QList>
#include <QPlainTextEdit>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <memory>

class QMenu;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMouseEvent;
class QPaintEvent;
class QRect;
class QContextMenuEvent;
class QKeyEvent;
class QInputMethodEvent;
class QResizeEvent;
class QWheelEvent;
class EditorDocumentGeometry;
class EditorSemanticContextService;
class EditorGutter;
class EditorFoldingController;
struct EditorLargeFileSyntaxScopeSnapshot;
class EditorCompletionWorkflow;
class EditorSourceNavigationUi;
struct EditorAppearanceOptions;
struct EditorOccurrenceIndexStats;
struct EditorSemanticContext;
struct EditorSourceNavigationTarget;
struct SourceLineNavigationTarget;
enum class FormatterProfile;
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

class MyCodeEditor : public QPlainTextEdit
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

    void setPlainText(const QString& text);
    void insertPlainText(const QString& text);
    void clear();
    void undo();
    void redo();
    void setTextCursor(const QTextCursor& cursor);
    void applyLineNavigationTarget(const SourceLineNavigationTarget& target);
    EditorBlockGeometry blockGeometry(int blockNumber) const;
    qreal documentHeightPx() const;
    void refreshScopeAndCurrentLineHighlight();
    void refreshSemanticPresentation();
    std::uint64_t semanticDocumentRevision() const;
    QString toPlainText() const;
    const QString& cachedDocumentText() const;
    QString cachedDocumentSlice(int position, int length) const;
    [[nodiscard]] SynchronousEditTransaction
    beginSynchronousEditTransaction();
    EditorSynchronousEditState synchronousEditStateForTest() const;
    void acceptLoadedTextAsSemanticBaseline();
    EditorHotPathMetrics hotPathMetricsForTest() const;
    bool inlineFilterTextOverlayActiveForTest() const;
    EditorOccurrenceIndexStats occurrenceIndexStatsForTest() const;
    QList<int> occurrencePositionsForTest(const QString& word) const;
    void resetHotPathMetricsForTest();
    void setIncludeFileCompletionProvider(
        std::function<QStringList(const QString& currentFile)> provider);
    void setIncludeNewHeaderCreator(
        std::function<IncludeNewHeaderResult(
            const IncludeNewHeaderRequest& request)> creator);
    void setSemanticContextService(EditorSemanticContextService* service);
    void setHierarchyInstanceContext(
        const HierarchyInstanceContext& context);
    HierarchyInstanceContext hierarchyInstanceContext() const;
    void closeSemanticPopup();
    void setDocumentFileName(QString fileName);
    QString documentFileName() const;
    QString currentModuleName() const;
    EditorAlwaysScopeTarget currentAlwaysScopeTarget() const;
    EditorModuleScopeTarget currentModuleScopeTarget() const;
    bool addPortRow(QString* message = nullptr);
    bool addSignalRow(QString* message = nullptr);
    bool addParameterRow(QString* message = nullptr);
    bool goToFinalEndmodule(QString* message = nullptr);
    EditorPackageToolAvailability currentPackageToolAvailability() const;
    bool executePackageToolInsert(PackageToolKind kind,
                                  QString* message = nullptr);
    bool selectInsideBeginEnd(QString* message = nullptr);
    void startTemplateSlotMode(int insertionStart,
                               int insertedLength,
                               const CodeTemplateSlotList& slotMetadata);
    bool templateSlotModeActive() const;
    int templateSlotModeActiveIndex() const;
    int templateSlotModeSlotCount() const;
    bool templateSlotModeBlinkOnForTest() const;
    bool columnSelectionActive() const;
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
    void setFormatterProfile(FormatterProfile profile);
    FormatterProfile formatterProfile() const;
    void setFormatOnSaveEnabled(bool enabled);
    bool formatOnSaveEnabled() const;
    bool formatDocumentForSave();
    void formatDocument();
    void formatSelection();
    bool goToLineNumber(int lineNumber);
    bool replaceNextText(const QString& needle,
                         const QString& replacement,
                         bool caseSensitive = false);
    int replaceAllText(const QString& needle,
                       const QString& replacement,
                       bool caseSensitive = false);
    void showGotoLineDialog();
    void showReplaceDialog();
    void commentSelectionOrLine();
    void uncommentSelectionOrLine();
    void indentSelectionOrLine();
    void unindentSelectionOrLine();
    bool clearSelectedAssignmentRhs(QString* message = nullptr);
    void addStructuralContextMenuActions(QMenu* menu,
                                         int cursorPosition);
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
    QStringList selectedSignalNamesForTest() const;
    bool toggleSignalSelectionAtForTest(int cursorPosition);
    bool createAssignmentQueueAtForTest(
        int cursorPosition,
        QString* message = nullptr);
    void highlightSearchMatches(const QString& text, bool caseSensitive);
    void clearSearchMatches();
    void flashLine(int lineNumber);
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
    QList<GhostAnnotation> ghostAnnotationsForTest() const;
    QString syntaxTextForTest() const;
    EditorLargeFileSyntaxScopeSnapshot largeFileSyntaxScopeForTest() const;
    FoldShelfItem foldShelfItemAtLineForTest(
        int line,
        FoldShelfOriginKind origin = FoldShelfOriginKind::Copied) const;
    bool deleteCustomFoldAtLineForTest(int line);
    bool insertFoldShelfItemAtLineForTest(const FoldShelfItem& item, int line);
    EditorSemanticContext editorSemanticContextForPosition(
        int cursorPosition = -1,
        bool includeDocumentText = false) const;
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
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent *event) override;

private:
    friend class EditorDocumentGeometry;
    friend class EditorCompletionWorkflow;
    friend class EditorGutter;
    friend class EditorFoldingController;
    friend class EditorSourceNavigationUi;
    friend struct MyCodeEditorState;

    std::unique_ptr<MyCodeEditorState> state;

signals:
    void fileNameChanged(const QString& fileName);
    void sourceNavigationRequested(const EditorSourceNavigationTarget& target,
                                   const EditorSemanticContext& context);
    void sourceSymbolActionRequested(SourceSymbolAction action,
                                     const EditorSemanticContext& context);
    void safeRenameRequested(const QString& symbolName,
                             const EditorSemanticContext& context,
                             bool* handled);
    void sourceSymbolContextMenuRequested(QMenu* menu,
                                          const EditorSemanticContext& context);
    void definitionPreviewNavigationRequested(const QString& fileName,
                                              int line,
                                              int column);
    void navigationBackRequested();
    void navigationForwardRequested();
    void editorStatusMessageRequested(const QString& message);
    void formatterProfileChanged(FormatterProfile profile);
    void formatOnSaveChanged(bool enabled);
    void foldShelfItemConsumed(const QString& id);
    void fontZoomRequested(int steps);
    void documentChangeApplied(const DocumentChange& change);
    void packageToolAvailabilityChanged(
        const EditorPackageToolAvailability& availability);
    void wavePreviewScopeChanged();
};

#endif // MYCODEEDITOR_H
