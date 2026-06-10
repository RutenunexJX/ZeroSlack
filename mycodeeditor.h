#ifndef MYCODEEDITOR_H
#define MYCODEEDITOR_H

#include "syminfo.h"
#include "completionmodel.h"
#include "definitionnavigationservice.h"
#include "tsdocument.h"

#include <QPlainTextEdit>
#include <QCompleter>
#include <QTimer>
#include <QMouseEvent>
#include <functional>

class LineNumberWidget;
class MyHighlighter;

class MyCodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit MyCodeEditor(QWidget *parent = nullptr);
    ~MyCodeEditor();

    void lineNumberWidgetPaintEvent(QPaintEvent *event);
    void lineNumberWidgetMousePressEvent(QMouseEvent *event);
    void lineNumberWidgetWheelEvent(QWheelEvent *event);

    bool saveFile();
    bool saveAsFile();
    void setFileName(QString fileName);
    QString getFileName() const;
    bool checkSaved();
    QString currentModuleName() const;

    void showAutoComplete();
    void hideAutoComplete();
    void refreshScopeAndCurrentLineHighlight();
    void setAlternateModeEnabled(bool enabled);
    void setIncludePathResolver(
        std::function<QString(const QString& includePath, const QString& currentFile)> resolver);
    void setFileOpenHandler(std::function<bool(const QString& filePath)> handler);

    void moveMouseToCursor();

    qreal getBlockTopY(int blockNumber) const;
    qreal getBlockHeight(int blockNumber) const;
    qreal getDocumentHeightPx() const;

    void clearAlternateModeBuffer();
    void processAlternateModeInput(const QString &input);
    bool isSaved = false;

private slots:
    void highlighCurrentLine();
    void updateLineNumberWidget(QRect rect, int dy);
    void updateLineNumberWidgetWidth();
    void updateSaveState();
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
    bool emitReferenceSearchForCursor(const QTextCursor& cursor);
    bool emitRelationshipBrowseForCursor(const QTextCursor& cursor);
    DefinitionNavigationQuery definitionNavigationQuery(
        const QString& symbolName,
        int cursorPosition = -1) const;
    QString getWordUnderCursor();
    QStringList getCompletionSuggestions(const QString &prefix);
    bool isInCommentArea();

    LineNumberWidget *lineNumberWidget;
    QString mFileName;
    std::function<QString(const QString& includePath, const QString& currentFile)> includePathResolver;
    std::function<bool(const QString& filePath)> fileOpenHandler;

    TSDocument m_tsdoc;
    MyHighlighter *m_highlighter = nullptr;

    QCompleter *completer;
    CompletionModel *completionModel;
    QTimer *autoCompleteTimer;
    QString currentWord;
    int wordStartPos;

    QTimer *relationshipAnalysisDebounceTimer;
    static const int RelationshipAnalysisDebounceMs = 2000;

    // Coalesce current-line selection refresh after cursor/text changes.
    QTimer *scopeRefreshTimer = nullptr;

    bool m_lastEditWasWhitespaceInsertion = false;

    QString textUnderCursor() const;

    bool isInCustomCommandMode = false;
    sym_list::sym_type_e currentCommandType = sym_list::sym_user;

    bool checkForCustomCommand(const QString &text);

    void highlightCommandText();
    void clearCommandHighlight();
    int commandStartPosition = -1;
    int commandEndPosition = -1;

    bool isInAlternateMode = false;
    QString alternateCommandBuffer;

    void executeAlternateModeCommand(const QString &command);
    void showAlternateModeCommands(const QString &filter = QString());

    bool ctrlPressed = false;
    QString hoveredWord;
    int hoveredWordStartPos = -1;
    int hoveredWordEndPos = -1;

    struct EditorNavigationTarget {
        bool matched = false;
        bool jumpable = false;
        bool includeTarget = false;
        bool identifierTarget = false;
        QString text;
        int startPos = -1;
        int endPos = -1;
        int cursorPosition = -1;
    };

    EditorNavigationTarget sourceNavigationTargetAtPosition(const QPoint& position);
    void applySourceNavigationHover(const EditorNavigationTarget& target);
    void clearSourceNavigationHover();

    QString getWordAtTextPosition(int position);
    void jumpToDefinition(const QString& symbolName, int cursorPosition = -1);
    void highlightHoveredSymbol(const QString& word, int startPos, int endPos);
    void clearHoveredSymbolHighlight();
    bool canJumpToDefinition(const QString& symbolName);
    QCursor createJumpableCursor();
    QCursor createNonJumpableCursor();

    bool openIncludeFile(const QString& includePath);

    void showSymbolTooltip(const QString& symbolName, const QPoint& position);

    QStringList getCommandModeInternalVariables(const QString &prefix);

    bool commandModeExitedByDoubleSpace = false;
signals:
    void definitionJumpRequested(const QString& symbolName, const QString& fileName, int line);
    void relationshipAnalysisRequested(const QString& fileName, const QString& content);
    void saveFileRequested();
    void saveFileAsRequested();
    void openFileRequested();
    void newFileRequested();
    void referenceSearchRequested(const QString& symbolName,
                                  const QString& fileName,
                                  const QString& moduleName);
    void relationshipBrowseRequested(const QString& symbolName,
                                     const QString& fileName,
                                     const QString& moduleName);
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
