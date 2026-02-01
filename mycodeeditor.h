#ifndef MYCODEEDITOR_H
#define MYCODEEDITOR_H

#include "syminfo.h"
#include "completionmodel.h"

#include <QPlainTextEdit>
#include <QCompleter>
#include <QTimer>
#include <QMouseEvent>

class LineNumberWidget;
class MainWindow;

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

    void showAutoComplete();
    void hideAutoComplete();
    /** 主动刷新并发射 debugScopeInfo（用于切标签时更新状态栏） */
    void refreshDebugScopeInfo();

    /** 刷新作用域背景与当前行高亮（符号分析完成后由 MainWindow 调用） */
    void refreshScopeAndCurrentLineHighlight();

    /** 作用域条带用：块顶部的 Y 坐标（文档坐标系，与 contentOffset 一致） */
    qreal getBlockTopY(int blockNumber) const;
    /** 作用域条带用：块高度 */
    qreal getBlockHeight(int blockNumber) const;
    /** 作用域条带用：文档总高度（像素） */
    qreal getDocumentHeightPx() const;

    void clearAlternateModeBuffer();
    void processAlternateModeInput(const QString &input);
    bool isSaved = false;

private slots:
    void highlighCurrentLine();
    void onCursorPositionChangedForDebug();
    void updateAndEmitDebugScopeInfo();
    void updateLineNumberWidget(QRect rect, int dy);
    void updateLineNumberWidgetWidth();
    void updateSaveState();
    void onTextChanged();
    void onAutoCompleteTimer();
    void onCompletionActivated(const QModelIndex &index);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

    /** 从数据库刷新并填充作用域背景缓存（仅分析完成时调用） */
    void updateScopeBackgrounds();

private:
    void initConnection();
    void initFont();
    void initHighlighter();
    void initAutoComplete();
    int getLineNumberWidgetWidth();

    QString getWordUnderCursor();
    QStringList getCompletionSuggestions(const QString &prefix);
    bool isInCommentArea();

    LineNumberWidget *lineNumberWidget;
    QString mFileName;

    QCompleter *completer;
    CompletionModel *completionModel;
    QTimer *autoCompleteTimer;
    QString currentWord;
    int wordStartPos;

    // 关系分析去抖：连续输入时重置定时器，停止输入一段时间后再触发单文件关系分析
    QTimer *relationshipAnalysisDebounceTimer;
    static const int RelationshipAnalysisDebounceMs = 2000;

    // 内容变化后延迟刷新作用域背景，避免删除行后灰色消失
    QTimer *scopeRefreshTimer = nullptr;

    int lastKnownBlockCount = -1;

    /** 缓存的作用域背景选区（QTextCursor 随文档自动更新，仅在分析完成时由 updateScopeBackgrounds 刷新） */
    QList<QTextEdit::ExtraSelection> m_scopeSelections;

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
    QStringList getSymbolCompletions(sym_list::sym_type_e symbolType, const QString &prefix);
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
    void jumpToDefinition(const QString& symbolName);
    void highlightHoveredSymbol(const QString& word, int startPos, int endPos);
    void clearHoveredSymbolHighlight();
    bool isSymbolDefinition(const sym_list::SymbolInfo& symbol, const QString& searchWord);
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
    /** Debug：光标所在模块及该模块内 logic/struct var/struct type 数量 */
    void debugScopeInfo(const QString& currentModule, int logicCount, int structVarCount, int structTypeCount);
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
