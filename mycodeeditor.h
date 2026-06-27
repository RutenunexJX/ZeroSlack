#ifndef MYCODEEDITOR_H
#define MYCODEEDITOR_H

#include "semanticindex.h"
#include "semanticdecorationservice.h"
#include "ghostannotationservice.h"
#include "foldblockshelfmodel.h"
#include "includeheaderworkflowtypes.h"
#include "completiontypes.h"

#include <QList>
#include <QPlainTextEdit>
#include <QStringList>
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
class QResizeEvent;
class QWheelEvent;
class EditorDocumentGeometry;
class EditorSemanticContextService;
class EditorGutter;
class EditorFoldingController;
class EditorCompletionWorkflow;
class EditorSourceNavigationUi;
struct EditorAppearanceOptions;
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

struct EditorBlockGeometry {
    qreal top = 0;
    qreal height = 0;
};

class MyCodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit MyCodeEditor(QWidget *parent = nullptr);
    ~MyCodeEditor();

    void applyLineNavigationTarget(const SourceLineNavigationTarget& target);
    EditorBlockGeometry blockGeometry(int blockNumber) const;
    qreal documentHeightPx() const;
    void refreshScopeAndCurrentLineHighlight();
    void setAlternateModeEnabled(bool enabled);
    void setIncludeFileCompletionProvider(
        std::function<QStringList(const QString& currentFile)> provider);
    void setIncludeNewHeaderCreator(
        std::function<IncludeNewHeaderResult(
            const IncludeNewHeaderRequest& request)> creator);
    void setSemanticContextService(EditorSemanticContextService* service);
    void setDocumentFileName(QString fileName);
    QString documentFileName() const;
    QString currentModuleName() const;
    EditorAlwaysScopeTarget currentAlwaysScopeTarget() const;
    bool executeComPortAppend(QString* message = nullptr);
    bool executeComSignalInsert(QString* message = nullptr);
    bool executeComInstanceInsert(QString* message = nullptr);
    bool executeComAssignInsert(QString* message = nullptr);
    bool executeComParameterInsert(QString* message = nullptr);
    bool executeComModuleEndInsert(QString* message = nullptr);
    bool comModeActive() const;
    QString comModeBuffer() const;
    void enterComMode(const QString& message = QString());
    void exitComMode();
    void showComModeMessage(const QString& message);
    void executeAlternateModeCommand(const QString& command);
    void startTemplateSlotMode(int insertionStart,
                               int insertedLength,
                               const CodeTemplateSlotList& slotMetadata);
    bool templateSlotModeActive() const;
    int templateSlotModeActiveIndex() const;
    void setDiagnosticHighlights(
        const QList<SemanticDiagnostic>& diagnostics);
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
    bool clearSelectedAssignmentRhs();
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
    FoldShelfItem foldShelfItemAtLineForTest(
        int line,
        FoldShelfOriginKind origin = FoldShelfOriginKind::Copied) const;
    bool deleteCustomFoldAtLineForTest(int line);
    bool insertFoldShelfItemAtLineForTest(const FoldShelfItem& item, int line);
    EditorSemanticContext editorSemanticContextForPosition(
        int cursorPosition = -1,
        bool includeDocumentText = false) const;

protected:
    void keyPressEvent(QKeyEvent *event) override;
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
    void alternateCommandRequested(const QString& command);
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
    void foldShelfRequested();
    void foldShelfItemConsumed(const QString& id);
    void fontZoomRequested(int steps);
    void comModeStateChanged(bool active,
                             const QString& buffer,
                             const QString& message);
    void comCommandRequested(const QString& command);
    void comRelativeLineRequested(int moduleLine);
};

#endif // MYCODEEDITOR_H
