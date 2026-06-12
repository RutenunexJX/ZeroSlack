#ifndef MYCODEEDITOR_H
#define MYCODEEDITOR_H

#include <QPlainTextEdit>
#include <memory>

class LineNumberWidget;
class AnalysisCoordinator;
class DocumentModel;
class EditorCoordinator;
class NavigationCommandCoordinator;
class QModelIndex;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QPoint;
class QRect;
class QContextMenuEvent;
class QCursor;
class QKeyEvent;
class QResizeEvent;
class ScopeBandWidget;
class QTextBlock;
class QTextCursor;
class QWheelEvent;
class EditorSemanticContextService;
struct CompletionActivationState;
struct CompletionPopupKeyState;
struct EditorAlternateModeCompletionDisplayState;
struct EditorAlternateModeKeyState;
struct EditorCompletionPopupKeyContext;
struct EditorSemanticContext;
struct EditorSourceNavigationTarget;
struct SourceLineNavigationTarget;
enum class SourceSymbolAction;
struct MyCodeEditorState;

class MyCodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit MyCodeEditor(QWidget *parent = nullptr);
    ~MyCodeEditor();

private:
    void highlightCurrentLine();
    void updateLineNumberWidget(QRect rect, int dy);
    void updateLineNumberWidgetWidth();
    void onTextChanged();
    void onAutoCompleteTimer();
    void onCompletionActivated(const QModelIndex &index);
    void onTsContentsChange(int position, int charsRemoved, int charsAdded);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    friend class AnalysisCoordinator;
    friend class DocumentModel;
    friend class EditorCoordinator;
    friend class LineNumberWidget;
    friend class NavigationCommandCoordinator;
    friend class ScopeBandWidget;

    void initConnection();
    void initFont();
    void initHighlighter();
    void initAutoComplete();
    int getLineNumberWidgetWidth();
    EditorSemanticContextService* contextService() const;

    void showAutoComplete();
    void hideAutoComplete();
    void clearAlternateModeBuffer();
    void processAlternateModeInput(const QString &input);

    void setFileName(QString fileName);
    QString getFileName() const;
    QString currentModuleName() const;
    QString currentModuleNameAt(int charPos) const;
    void refreshScopeAndCurrentLineHighlight();
    void setAlternateModeEnabled(bool enabled);
    void setSemanticContextService(EditorSemanticContextService* service);
    void applyLineNavigationTarget(const SourceLineNavigationTarget& target);
    void moveMouseToCursor();
    qreal getBlockTopY(int blockNumber) const;
    qreal getBlockHeight(int blockNumber) const;
    qreal getDocumentHeightPx() const;
    EditorSemanticContext semanticContextForCursor(
        const QTextCursor& cursor,
        bool includeDocumentText = false) const;
    EditorSemanticContext editorSemanticContextForPosition(
        int cursorPosition = -1,
        bool includeDocumentText = false) const;
    QString getWordUnderCursor();
    void updateCompletionTriggerForTextChange(const QTextCursor& cursor);
    bool refreshCommandModeCompletion(const EditorSemanticContext& context);
    void refreshSymbolCompletion(EditorSemanticContext context,
                                 const QTextBlock& currentBlock);
    bool handleSourceSymbolShortcut(QKeyEvent *event);
    bool handleAlternateModeKey(QKeyEvent *event);
    void applyAlternateModeKeyState(const EditorAlternateModeKeyState& state);
    void applyCompletionActivationState(
        const CompletionActivationState& activationState);
    EditorCompletionPopupKeyContext completionPopupKeyContextForEvent(
        QKeyEvent *event) const;
    bool applyCompletionPopupKeyState(
        QKeyEvent *event,
        const CompletionPopupKeyState& popupState);

    std::unique_ptr<MyCodeEditorState> state;

    void executeAlternateModeCommand(const QString &command);
    void applyAlternateModeCompletionDisplayState(
        const EditorAlternateModeCompletionDisplayState& displayState);
    bool handleCompletionPopupKey(QKeyEvent *event);

    EditorSourceNavigationTarget sourceNavigationTargetAtPosition(
        const QPoint& position);
    bool requestSourceNavigationAtPosition(const QPoint& position);
    void refreshSourceNavigationHoverAt(const QPoint& position);
    void applySourceNavigationHover(
        const EditorSourceNavigationTarget& target);
    void clearSourceNavigationHover();

    QCursor createJumpableCursor();
    QCursor createNonJumpableCursor();
signals:
    void fileNameChanged(const QString& fileName);
    void alternateCommandRequested(const QString& command);
    void sourceNavigationRequested(const EditorSourceNavigationTarget& target,
                                   const EditorSemanticContext& context);
    void sourceSymbolActionRequested(SourceSymbolAction action,
                                     const EditorSemanticContext& context);
    void sourceSymbolContextMenuRequested(QMenu* menu,
                                          const EditorSemanticContext& context);
};

#endif // MYCODEEDITOR_H
