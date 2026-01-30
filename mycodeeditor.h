// ============ mycodeeditor.h 更新内容 ============

#ifndef MYCODEEDITOR_H
#define MYCODEEDITOR_H

#include "syminfo.h"
#include "completionmodel.h"

#include <QPlainTextEdit>
#include <QCompleter>
#include <QTimer>
#include <QMouseEvent>  // 新增：鼠标事件支持

class LineNumberWidget;
class MainWindow;  // 新增：前向声明

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
    QString getFileName();
    bool checkSaved();

    void showAutoComplete();
    void hideAutoComplete();

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

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

    // 新增：鼠标事件处理
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

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

    // Completion system
    QCompleter *completer;
    CompletionModel *completionModel;
    QTimer *autoCompleteTimer;
    QString currentWord;
    int wordStartPos;

    // 关系分析去抖：连续输入时重置定时器，停止输入一段时间后再触发单文件关系分析
    QTimer *relationshipAnalysisDebounceTimer;
    static const int RelationshipAnalysisDebounceMs = 2000;

    QString textUnderCursor() const;

    // Custom command system
    struct CustomCommand {
        QString prefix;              // e.g., "r "
        sym_list::sym_type_e symbolType;  // e.g., sym_reg
        QString description;         // e.g., "reg variables"
        QString defaultValue;        // e.g., "reg"
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

    // Alternate mode system
    bool isInAlternateMode = false;
    QString alternateCommandBuffer;
    QStringList alternateModeCommands;

    void initAlternateModeCommands();
    void executeAlternateModeCommand(const QString &command);
    void showAlternateModeCommands(const QString &filter = QString());

    // 新增：定义跳转功能
    bool ctrlPressed = false;
    QString hoveredWord;
    int hoveredWordStartPos = -1;
    int hoveredWordEndPos = -1;

    // Definition jump methods
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

    // include 跳转相关
    bool getIncludeInfoAtPosition(const QPoint& position, int &startPos, int &endPos, QString &includePath);
    bool tryJumpToIncludeAtPosition(const QPoint& position);
    bool openIncludeFile(const QString& includePath);

    // 增强功能
    void showSymbolTooltip(const QString& symbolName, const QPoint& position);
    QString getSymbolTypeString(sym_list::sym_type_e symbolType);

    QStringList getCommandModeInternalVariables(const QString &prefix);

    bool commandModeExitedByDoubleSpace = false;
    bool isConsecutiveSpaces();
signals:
    // 可以添加一个信号来通知定义跳转事件
    void definitionJumpRequested(const QString& symbolName, const QString& fileName, int line);
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
