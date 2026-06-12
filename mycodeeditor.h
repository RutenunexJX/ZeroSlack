#ifndef MYCODEEDITOR_H
#define MYCODEEDITOR_H

#include "syminfo.h"
#include "completionmodel.h"
#include "editorsemanticcontextservice.h"
#include "tsdocument.h"

#include <QPlainTextEdit>
#include <QCompleter>
#include <QTimer>
#include <QMouseEvent>

class LineNumberWidget;
class MyHighlighter;
class QMenu;

class MyCodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit MyCodeEditor(QWidget *parent = nullptr);
    ~MyCodeEditor();

    void lineNumberWidgetPaintEvent(QPaintEvent *event);
    void lineNumberWidgetMousePressEvent(QMouseEvent *event);
    void lineNumberWidgetWheelEvent(QWheelEvent *event);

    void setFileName(QString fileName);
    QString getFileName() const;
    QString currentModuleName() const;

    void showAutoComplete();
    void hideAutoComplete();
    void refreshScopeAndCurrentLineHighlight();
    void setAlternateModeEnabled(bool enabled);

    void moveMouseToCursor();

    qreal getBlockTopY(int blockNumber) const;
    qreal getBlockHeight(int blockNumber) const;
    qreal getDocumentHeightPx() const;

    void clearAlternateModeBuffer();
    void processAlternateModeInput(const QString &input);

private slots:
    void highlighCurrentLine();
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
    void initConnection();
    void initFont();
    void initHighlighter();
    void initAutoComplete();
    int getLineNumberWidgetWidth();

    QString currentModuleNameAt(int charPos) const;
    EditorSemanticContext editorSemanticContextForPosition(
        int cursorPosition = -1,
        bool includeDocumentText = false) const;
    QString getWordUnderCursor();

    LineNumberWidget *lineNumberWidget;
    QString mFileName;

    TSDocument m_tsdoc;
    MyHighlighter *m_highlighter = nullptr;

    QCompleter *completer;
    CompletionModel *completionModel;
    QTimer *autoCompleteTimer;
    int wordStartPos;

    // Coalesce current-line selection refresh after cursor/text changes.
    QTimer *scopeRefreshTimer = nullptr;

    bool isInCustomCommandMode = false;

    void highlightCommandText(int prefixPosition);
    void clearCommandHighlight();

    bool isInAlternateMode = false;
    QString alternateCommandBuffer;

    void executeAlternateModeCommand(const QString &command);
    void applyAlternateModeCompletionDisplayState(
        const EditorAlternateModeCompletionDisplayState& state);
    bool handleCompletionPopupKey(QKeyEvent *event);

    bool ctrlPressed = false;
    QString hoveredWord;
    int hoveredWordStartPos = -1;
    int hoveredWordEndPos = -1;

    EditorSourceNavigationTarget sourceNavigationTargetAtPosition(
        const QPoint& position);
    void applySourceNavigationHover(
        const EditorSourceNavigationTarget& target);
    void clearSourceNavigationHover();

    void highlightHoveredSymbol(const QString& word, int startPos, int endPos);
    void clearHoveredSymbolHighlight();
    QCursor createJumpableCursor();
    QCursor createNonJumpableCursor();

    bool commandModeExitedByDoubleSpace = false;
signals:
    void fileNameChanged(const QString& fileName);
    void definitionNavigationRequested(const QString& symbolName,
                                       const EditorSemanticContext& context);
    void alternateCommandRequested(const QString& command);
    void includeOpenRequested(const QString& includePath, const QString& currentFile);
    void sourceSymbolActionRequested(SourceSymbolAction action,
                                     const EditorSemanticContext& context);
    void sourceSymbolContextMenuRequested(QMenu* menu,
                                          const EditorSemanticContext& context);
};

class LineNumberWidget : public QWidget
{
public:
    explicit LineNumberWidget(MyCodeEditor *editor = nullptr) : QWidget(editor) {
        codeEditor = editor;
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        codeEditor->lineNumberWidgetPaintEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override {
        codeEditor->lineNumberWidgetMousePressEvent(event);
    }
    void wheelEvent(QWheelEvent *event) override {
        codeEditor->lineNumberWidgetWheelEvent(event);
    }

private:
    MyCodeEditor *codeEditor;
};

#endif // MYCODEEDITOR_H
