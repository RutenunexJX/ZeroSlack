#ifndef MYCODEEDITOR_H
#define MYCODEEDITOR_H

#include "syminfo.h"
#include "completionmodel.h"
#include "tsdocument.h"

#include <QPlainTextEdit>
#include <QCompleter>
#include <QTimer>
#include <QMouseEvent>

class LineNumberWidget;
class MainWindow;
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
    QString getWordUnderCursor();
    QStringList getCompletionSuggestions(const QString &prefix);
    bool isInCommentArea();

    LineNumberWidget *lineNumberWidget;
    QString mFileName;

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

    struct CustomCommand {
        QString prefix;
        sym_list::sym_type_e symbolType;
        QString description;
        QString defaultValue;
    };

    QList<CustomCommand> customCommands;
    bool isInCustomCommandMode = false;
    QString currentCommandPrefix;
    sym_list::sym_type_e currentCommandType;
    QString getCurrentCommandDefaultValue();

    void initCustomCommands();
    bool checkForCustomCommand(const QString &text);
    QString extractCommandInput();

    void highlightCommandText();
    void clearCommandHighlight();
    int commandStartPosition = -1;
    int commandEndPosition = -1;

    bool isInAlternateMode = false;
    QString alternateCommandBuffer;
    QStringList alternateModeCommands;

    void initAlternateModeCommands();
    void executeAlternateModeCommand(const QString &command);
    void showAlternateModeCommands(const QString &filter = QString());

    bool ctrlPressed = false;
    QString hoveredWord;
    int hoveredWordStartPos = -1;
    int hoveredWordEndPos = -1;

    QString getWordAtPosition(const QPoint& position);
    QString getWordAtTextPosition(int position);
    QTextCursor getWordCursorAtPosition(int position);
    bool getPackageNameFromImport(const QPoint& position, QString& packageName, int& startPos, int& endPos);
    void jumpToDefinition(const QString& symbolName, int cursorPosition = -1);
    void highlightHoveredSymbol(const QString& word, int startPos, int endPos);
    void clearHoveredSymbolHighlight();
    bool canJumpToDefinition(const QString& symbolName);
    QCursor createJumpableCursor();
    QCursor createNonJumpableCursor();

    bool getIncludeInfoAtPosition(const QPoint& position, int &startPos, int &endPos, QString &includePath);
    bool tryJumpToIncludeAtPosition(const QPoint& position);
    bool openIncludeFile(const QString& includePath);

    void showSymbolTooltip(const QString& symbolName, const QPoint& position);
    QString getSymbolTypeString(sym_list::sym_type_e symbolType);

    QStringList getCommandModeInternalVariables(const QString &prefix);

    bool commandModeExitedByDoubleSpace = false;
    bool isConsecutiveSpaces();
signals:
    void definitionJumpRequested(const QString& symbolName, const QString& fileName, int line);
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
