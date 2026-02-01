#include "mycodeeditor.h"
#include "myhighlighter.h"
#include "mainwindow.h"
#include "completionmodel.h"
#include "completionmanager.h"

#include "tabmanager.h"
#include "workspacemanager.h"
#include "modemanager.h"
#include "symbolanalyzer.h"
#include "navigationmanager.h"
#include "syminfo.h"

#include <QPainter>
#include <QScrollBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>

#include <QDebug>
#include <QKeyEvent>
#include <QTextCursor>
#include <QApplication>
#include <QRect>

#include <QAbstractItemView>
#include <QCompleter>
#include <QToolTip>
#include <QCursor>
#include <QPixmap>
#include <QPen>
#include <QBrush>
#include <QRegularExpression>

MyCodeEditor::MyCodeEditor(QWidget *parent) : QPlainTextEdit(parent)
{
    lineNumberWidget = new LineNumberWidget(this);

    initConnection();
    initFont();
    initHighlighter();
    initAutoComplete();
    initAlternateModeCommands();

    highlighCurrentLine();
    updateLineNumberWidgetWidth();

    setLineWrapMode(QPlainTextEdit::NoWrap);

    isSaved = true;
    mFileName = "";

    ctrlPressed = false;
    hoveredWordStartPos = -1;
    hoveredWordEndPos = -1;

    setMouseTracking(true);
}

MyCodeEditor::~MyCodeEditor()
{
    delete lineNumberWidget;
}

void MyCodeEditor::initConnection()
{
    //cursor
    connect(this,SIGNAL(cursorPositionChanged()),this,SLOT(highlighCurrentLine()));
    connect(this,SIGNAL(cursorPositionChanged()),this,SLOT(onCursorPositionChangedForDebug()));

    //textChanged
    connect(this,SIGNAL(textChanged()),this,SLOT(updateSaveState()));
    scopeRefreshTimer = new QTimer(this);
    scopeRefreshTimer->setSingleShot(true);
    connect(scopeRefreshTimer, &QTimer::timeout, this, &MyCodeEditor::highlighCurrentLine);
    connect(this, &QPlainTextEdit::textChanged, this, [this]() {
        scopeRefreshTimer->stop();
        scopeRefreshTimer->start(0);
    });

    //blockCount：行数变化时立即更新 logic 等作用域背景，避免晚一帧
    connect(this,SIGNAL(blockCountChanged(int)),this,SLOT(updateLineNumberWidgetWidth()));
    connect(this,SIGNAL(blockCountChanged(int)),this,SLOT(highlighCurrentLine()));

    //updateRequest
    connect(this,SIGNAL(updateRequest(QRect,int)),this,SLOT(updateLineNumberWidget(QRect,int)));
}

void MyCodeEditor::initFont()
{
    this->setFont(QFont("Consolas",14));
    // 设置 Tab 宽度为 4 个字符
    int tabWidth = fontMetrics().horizontalAdvance(' ') * 4;
    setTabStopDistance(tabWidth);
}

void MyCodeEditor::initHighlighter()
{
    new MyHighlighter(document());
}

int MyCodeEditor::getLineNumberWidgetWidth()
{
    return 8+QString::number(blockCount()+1).length()*fontMetrics().horizontalAdvance(QChar('0'));
}

void MyCodeEditor::highlighCurrentLine()
{
    QList<QTextEdit::ExtraSelection> existing = extraSelections();
    existing.erase(
        std::remove_if(existing.begin(), existing.end(),
            [](const QTextEdit::ExtraSelection& s) {
                int p = s.format.property(QTextFormat::UserProperty).toInt();
                return p == 997 || p == 998;
            }),
        existing.end());

    QList<QTextEdit::ExtraSelection> list = existing;
    list.append(m_scopeSelections);

    QTextEdit::ExtraSelection currentLine;
    currentLine.format.setBackground(QColor(0,100,100,20));
    currentLine.format.setProperty(QTextFormat::FullWidthSelection, true);
    currentLine.format.setProperty(QTextFormat::UserProperty, 998);
    currentLine.cursor = textCursor();
    list.append(currentLine);

    const int docLen = document()->characterCount();
    const int docEnd = (docLen > 0) ? docLen - 1 : 0;
    for (auto& sel : list) {
        QTextCursor& c = sel.cursor;
        int pos = qBound(0, c.position(), docEnd);
        int anchor = qBound(0, c.anchor(), docEnd);
        c.setPosition(anchor);
        c.setPosition(pos, QTextCursor::KeepAnchor);
    }
    setExtraSelections(list);
}

void MyCodeEditor::updateScopeBackgrounds()
{
    m_scopeSelections.clear();
    QString fileName = getFileName();
    if (fileName.isEmpty()) return;
    sym_list* sym = sym_list::getInstance();
    if (!sym) return;

    QList<sym_list::SymbolInfo> all = sym->findSymbolsByFileName(fileName);
    QList<sym_list::SymbolInfo> modules, logics;
    for (const auto& s : all) {
        if (s.symbolType == sym_list::sym_module) modules.append(s);
        else if (s.symbolType == sym_list::sym_logic) logics.append(s);
    }

    const int blockCnt = document()->blockCount();
    const int maxLine = (blockCnt > 0) ? blockCnt - 1 : 0;
    const int docLen = document()->characterCount();
    const int docEnd = (docLen > 0) ? docLen - 1 : 0;
    auto addRange = [this, docEnd, maxLine](int startLine, int endLine, const QColor& bg, bool fullWidth) {
        int s = qBound(0, startLine, maxLine);
        int e = qBound(0, endLine, maxLine);
        if (e < s) return;
        QTextBlock startBlock = document()->findBlockByNumber(s);
        QTextBlock endBlock = document()->findBlockByNumber(e);
        if (!startBlock.isValid() || !endBlock.isValid()) return;
        int posStart = qBound(0, startBlock.position(), docEnd);
        int posEnd = qBound(0, endBlock.position() + endBlock.length(), docEnd);
        if (posEnd <= posStart) return;
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(bg);
        if (fullWidth) sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.format.setProperty(QTextFormat::UserProperty, 997);
        QTextCursor c(document());
        c.setPosition(posStart);
        c.setPosition(posEnd, QTextCursor::KeepAnchor);
        sel.cursor = c;
        m_scopeSelections.append(sel);
    };

    for (const sym_list::SymbolInfo& mod : modules) {
        if (!sym->isValidModuleName(mod.symbolName)) continue;
        int endLine = sym->findEndModuleLine(fileName, mod);
        if (endLine < 0) continue;
        addRange(mod.startLine, endLine, QColor(0, 0, 0, 40), true);
    }
    for (const sym_list::SymbolInfo& logic : logics) {
        int endLine = logic.endLine >= logic.startLine ? logic.endLine : logic.startLine;
        addRange(logic.startLine, endLine, QColor(0, 80, 0, 45), true);
    }
}

void MyCodeEditor::onCursorPositionChangedForDebug()
{
    updateAndEmitDebugScopeInfo();
}

void MyCodeEditor::updateAndEmitDebugScopeInfo()
{
    QString fileName = getFileName();
    if (fileName.isEmpty()) return;
    int cursorPosition = textCursor().position();
    CompletionManager* manager = CompletionManager::getInstance();
    QString currentModule = manager->getCurrentModule(fileName, cursorPosition);
    qDebug("updateAndEmitDebugScopeInfo: fileName=%s, currentModule=%s", qPrintable(fileName), qPrintable(currentModule));

    int logicCount = 0;
    int structVarCount = 0;
    int structTypeCount = 0;
    if (!currentModule.isEmpty()) {
        // 状态栏只用行范围统计，不用关系引擎 fallback，避免键入 s 后删除等操作导致计数含入全局 struct
        const bool useRelationshipFallback = false;
        logicCount = manager->getModuleInternalSymbolsByType(currentModule, sym_list::sym_logic, "", useRelationshipFallback).size();
        structVarCount = manager->getModuleInternalSymbolsByType(currentModule, sym_list::sym_packed_struct_var, "", useRelationshipFallback).size()
                         + manager->getModuleInternalSymbolsByType(currentModule, sym_list::sym_unpacked_struct_var, "", useRelationshipFallback).size();
        structTypeCount = manager->getModuleInternalSymbolsByType(currentModule, sym_list::sym_packed_struct, "", useRelationshipFallback).size()
                          + manager->getModuleInternalSymbolsByType(currentModule, sym_list::sym_unpacked_struct, "", useRelationshipFallback).size();
    }
    emit debugScopeInfo(currentModule, logicCount, structVarCount, structTypeCount);
}

void MyCodeEditor::refreshDebugScopeInfo()
{
    updateAndEmitDebugScopeInfo();
}

void MyCodeEditor::refreshScopeAndCurrentLineHighlight()
{
    updateScopeBackgrounds();
    highlighCurrentLine();
}

qreal MyCodeEditor::getBlockTopY(int blockNumber) const
{
    QTextBlock block = document()->findBlockByNumber(blockNumber);
    if (!block.isValid()) return 0;
    return blockBoundingGeometry(block).translated(contentOffset()).top();
}

qreal MyCodeEditor::getBlockHeight(int blockNumber) const
{
    QTextBlock block = document()->findBlockByNumber(blockNumber);
    if (!block.isValid()) return fontMetrics().height();
    return blockBoundingRect(block).height();
}

qreal MyCodeEditor::getDocumentHeightPx() const
{
    QAbstractTextDocumentLayout* layout = document()->documentLayout();
    return layout ? layout->documentSize().height() : 0;
}

void MyCodeEditor::updateLineNumberWidget(QRect rect, int dy)
{
    if(dy)
        lineNumberWidget->scroll(0,dy);
    else
        lineNumberWidget->update(0,rect.y(),getLineNumberWidgetWidth(),rect.height());
}

void MyCodeEditor::updateLineNumberWidgetWidth()
{
    setViewportMargins(getLineNumberWidgetWidth(),0,0,0);
}

void MyCodeEditor::updateSaveState()
{
    isSaved = false;
}

void MyCodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    lineNumberWidget->setGeometry(0, 0, getLineNumberWidgetWidth(), contentsRect().height());
}

void MyCodeEditor::contextMenuEvent(QContextMenuEvent *event)
{
    event->ignore();
}

void MyCodeEditor::lineNumberWidgetPaintEvent(QPaintEvent *event)
{
    QPainter painter(lineNumberWidget);
    painter.fillRect(event->rect(),QColor(100,100,100,20));

    QTextBlock block = firstVisibleBlock();

    int blockNumber = block.blockNumber();

    int cursorTop = blockBoundingGeometry(textCursor().block()).translated(contentOffset()).top();

    int top = blockBoundingGeometry(block).translated(contentOffset()).top();

    int bottom = top + blockBoundingRect(block).height();

    while(block.isValid() && top <= event->rect().bottom()){
        painter.setPen(cursorTop == top ? Qt::black : Qt::gray);
        painter.drawText(0,top,getLineNumberWidgetWidth() - 3,bottom - top,Qt::AlignRight,QString::number(blockNumber+1));

        block = block.next();

        top = bottom;
        bottom = top + blockBoundingRect(block).height();
        blockNumber++;
    }
}

void MyCodeEditor::lineNumberWidgetMousePressEvent(QMouseEvent *event)
{
    QTextBlock block = document()->findBlockByLineNumber(event->y()/fontMetrics().height()+verticalScrollBar()->value());
    setTextCursor(QTextCursor(block));
}

void MyCodeEditor::lineNumberWidgetWheelEvent(QWheelEvent *event)
{
    event->delta();
    if(event->orientation() == Qt::Horizontal){
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - event->delta());
    }
    else{
        verticalScrollBar()->setValue(verticalScrollBar()->value() - event->delta());
    }
    event->accept();
}

bool MyCodeEditor::saveFile()
{
    QString fileName;

    // Check if we have a valid filename and the file exists
    if(mFileName.isEmpty() || !QFile::exists(mFileName)){
        fileName = QFileDialog::getSaveFileName(this, "Save file");

        if(fileName.isEmpty()) {
            return false; // User cancelled the dialog
        }
        mFileName = fileName;
    }
    else{
        fileName = mFileName;
    }

    QFile file(fileName);
    if(!file.open(QIODevice::WriteOnly)){
        QMessageBox::warning(this,"Warning","Cannot save file: "+file.errorString());
        return false;
    }

    QTextStream out(&file);
    out << toPlainText();
    file.close();

    isSaved = true;
    return true;
}

bool MyCodeEditor::saveAsFile()
{
    QString fileName = QFileDialog::getSaveFileName(this,"save file as ");
    QFile file(fileName);
    if(!file.open(QIODevice::WriteOnly)){
        QMessageBox::warning(this,"warning","can not save file:"+file.errorString());
        return false;
    }

    mFileName = fileName;
    QTextStream out(&file);
    QString text = toPlainText();
    out<<text;
    file.close();

    isSaved = true;

    return true;
}

void MyCodeEditor::setFileName(QString fileName)
{
    mFileName = fileName;
}

QString MyCodeEditor::getFileName() const
{
    return mFileName;
}

bool MyCodeEditor::checkSaved()
{
    return isSaved;
}

void MyCodeEditor::initAutoComplete()
{
    // Create completion model and completer
    completionModel = new CompletionModel(this);
    completer = new QCompleter(this);
    completer->setModel(completionModel);
    completer->setWidget(this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setMaxVisibleItems(15);

    // Timer setup
    autoCompleteTimer = new QTimer(this);
    autoCompleteTimer->setSingleShot(true);
    autoCompleteTimer->setInterval(0);

    relationshipAnalysisDebounceTimer = new QTimer(this);
    relationshipAnalysisDebounceTimer->setSingleShot(true);
    relationshipAnalysisDebounceTimer->setInterval(RelationshipAnalysisDebounceMs);
    connect(relationshipAnalysisDebounceTimer, &QTimer::timeout, this, [this]() {
        MainWindow *mw = qobject_cast<MainWindow*>(window());
        if (mw && mw->relationshipBuilder && !getFileName().isEmpty())
            mw->requestSingleFileRelationshipAnalysis(getFileName(), toPlainText());
    });

    // Connect signals
    connect(autoCompleteTimer, &QTimer::timeout, this, &MyCodeEditor::onAutoCompleteTimer);
    connect(completer, QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, &MyCodeEditor::onCompletionActivated);

    connect(this, &QPlainTextEdit::textChanged, this, &MyCodeEditor::onTextChanged);
    initCustomCommands();
}

void MyCodeEditor::onTextChanged()
{
    updateSaveState();

    MainWindow *mainWindow = qobject_cast<MainWindow*>(window());
    int currentBlockCount = document()->blockCount();

    // 行数变化时调度分析，使新增/删行后作用域背景能更新（保存时也会因 needsAnalysis 行数比较而重分析）
    if (mainWindow && mainWindow->symbolAnalyzer && !getFileName().isEmpty() &&
        lastKnownBlockCount >= 0 && currentBlockCount != lastKnownBlockCount) {
        mainWindow->scheduleOpenFileAnalysis(getFileName(), 500);
    }
    lastKnownBlockCount = currentBlockCount;

    if (mainWindow && mainWindow->symbolAnalyzer &&
        (!mainWindow->workspaceManager || !mainWindow->workspaceManager->isWorkspaceOpen())) {

        QTextCursor cursor = textCursor();
        QTextBlock currentBlock = cursor.block();
        QString currentLineText = currentBlock.text();

        static QStringList significantKeywords = {
            "module", "endmodule", "reg", "wire", "logic",
            "task", "endtask", "function", "endfunction"
        };

        bool hasSignificantKeyword = false;
        for (const QString &keyword : significantKeywords) {
            if (currentLineText.contains(QRegularExpression("\\b" + QRegularExpression::escape(keyword) + "\\b"))) {
                hasSignificantKeyword = true;
                break;
            }
        }

        if (hasSignificantKeyword) {
            if (!getFileName().isEmpty())
                mainWindow->scheduleOpenFileAnalysis(getFileName(), 1000);
        } else {
            if (!getFileName().isEmpty())
                mainWindow->scheduleOpenFileAnalysis(getFileName(), 3000);
        }
    }

    // 关系分析去抖：连续输入时重置定时器；定时器到时再触发单文件关系分析，requestSingleFileRelationshipAnalysis 内部会取消未完成任务
    if (mainWindow && mainWindow->relationshipBuilder && !getFileName().isEmpty()) {
        relationshipAnalysisDebounceTimer->stop();
        relationshipAnalysisDebounceTimer->start();
    }

    // Autocompletion logic
    autoCompleteTimer->stop();

    QTextCursor cursor = textCursor();
    QTextBlock currentBlock = cursor.block();
    QString lineText = currentBlock.text();
    int positionInLine = cursor.position() - currentBlock.position();
    QString lineUpToCursor = lineText.left(positionInLine);

    checkForCustomCommand(lineUpToCursor);

    QChar charAtCursor = document()->characterAt(cursor.position() - 1);
    bool shouldContinueAutoComplete = false;

    if (isInCustomCommandMode) {
        shouldContinueAutoComplete = (charAtCursor.isLetterOrNumber() ||
                                    charAtCursor == '_' ||
                                    charAtCursor == ' ');
    } else {
        shouldContinueAutoComplete = (charAtCursor.isLetterOrNumber() ||
                                    charAtCursor == '_');
        // 结构体变量后输入 . 则开始识别成员
        if (charAtCursor == '.') {
            shouldContinueAutoComplete = true;
        }
        // 输入一半后输入空格：如 test_var.a1 后输入空格，在成员里模糊搜索并识别
        if (charAtCursor == ' ') {
            QString lineBeforeSpace = lineUpToCursor.left(qMax(0, positionInLine - 1)).trimmed();
            QString varName, memberPrefix;
            CompletionManager* mgr = CompletionManager::getInstance();
            if (mgr->tryParseStructMemberContext(lineBeforeSpace, varName, memberPrefix)) {
                QString mod = mgr->getCurrentModule(getFileName(), cursor.position() - 1);
                if (!mgr->getStructTypeForVariable(varName, mod).isEmpty()) {
                    shouldContinueAutoComplete = true;
                }
            }
        }
    }

    if (shouldContinueAutoComplete) {
        autoCompleteTimer->start();
    } else {
        if (!isInCustomCommandMode) {
            hideAutoComplete();
        }
    }
}

void MyCodeEditor::hideAutoComplete()
{
    completer->popup()->hide();

    if (isInCustomCommandMode) {
        clearCommandHighlight();
    }
}

QStringList MyCodeEditor::getCompletionSuggestions(const QString &prefix)
{
    CompletionManager* manager = CompletionManager::getInstance();

    // 🚀 获取当前光标位置和文件名
    QTextCursor cursor = textCursor();
    int cursorPosition = cursor.position();
    QString fileName = getFileName();

    // 🚀 使用严格的模块作用域补全
    QString currentModule = manager->getCurrentModule(fileName, cursorPosition);

    if (!currentModule.isEmpty()) {
        // 在模块内：只返回模块内部变量
        QStringList result = manager->getModuleInternalVariables(currentModule, prefix);

        return result;
    } else {
        // 在模块外：返回模块声明和全局符号
        QStringList result = manager->getGlobalSymbolCompletions(prefix);
        return result;
    }
}

bool MyCodeEditor::isInCommentArea()
{
    QTextCursor cursor = textCursor();
    int position = cursor.position();
    sym_list* symbolList = sym_list::getInstance();
    return symbolList->isPositionInComment(position);
}

void MyCodeEditor::onCompletionActivated(const QModelIndex &index)
{
    CompletionModel::CompletionItem item = completionModel->getItem(index);

    // Skip non-selectable items (headers)
    if (item.text.contains("::") || item.text == "No matching commands" || item.text == "No matching symbols") {
        return;
    }

    QTextCursor cursor = textCursor();

    if (isInAlternateMode) {
        executeAlternateModeCommand(item.text);
        return;
    }

    if (isInCustomCommandMode) {
        QString actualCompletion;

        // Use defaultValue if available, otherwise use text
        if (!item.defaultValue.isEmpty()) {
            actualCompletion = item.defaultValue;
        } else {
            actualCompletion = item.text;
        }

        // Replace entire line with completion
        cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
        cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        cursor.insertText(actualCompletion);

        // Clear command mode
        isInCustomCommandMode = false;
        currentCommandPrefix.clear();
        clearCommandHighlight();
    } else {
        // Standard symbol completion - replace current word
        cursor.setPosition(wordStartPos);
        cursor.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
        cursor.insertText(item.text);
    }

    hideAutoComplete();
}

void MyCodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control && !ctrlPressed) {
        ctrlPressed = true;

        // Ctrl 刚按下时：根据鼠标下符号是否可跳转，设置绿勾/红叉光标
        QPoint mousePos = mapFromGlobal(QCursor::pos());
        if (rect().contains(mousePos)) {
            clearHoveredSymbolHighlight();

            // 优先检测是否在 `include "xxx"` 的路径字符串上
            int incStart = -1;
            int incEnd = -1;
            QString incPath;
            if (getIncludeInfoAtPosition(mousePos, incStart, incEnd, incPath)) {
                hoveredWord = incPath;
                hoveredWordStartPos = incStart;
                hoveredWordEndPos = incEnd;
                highlightHoveredSymbol(incPath, incStart, incEnd);
                viewport()->setCursor(createJumpableCursor());
            } else {
                // 检查是否在 import 语句中的 package 名称上
                QString pkgName;
                int pkgStart = -1;
                int pkgEnd = -1;
                if (getPackageNameFromImport(mousePos, pkgName, pkgStart, pkgEnd)) {
                    hoveredWord = pkgName;
                    hoveredWordStartPos = pkgStart;
                    hoveredWordEndPos = pkgEnd;
                    highlightHoveredSymbol(pkgName, pkgStart, pkgEnd);
                    viewport()->setCursor(createJumpableCursor());
                } else {
                    QString word = getWordAtPosition(mousePos);
                    if (!word.isEmpty()) {
                        QTextCursor cursor = cursorForPosition(mousePos);
                        QTextCursor wordCursor = getWordCursorAtPosition(cursor.position());
                        hoveredWord = word;
                        hoveredWordStartPos = wordCursor.selectionStart();
                        hoveredWordEndPos = wordCursor.selectionEnd();
                        highlightHoveredSymbol(word, hoveredWordStartPos, hoveredWordEndPos);

                        if (canJumpToDefinition(word)) {
                            viewport()->setCursor(createJumpableCursor());
                        } else {
                            viewport()->setCursor(createNonJumpableCursor());
                        }
                    } else {
                        // 没有符号，显示红叉表示无法跳转
                        hoveredWord.clear();
                        viewport()->setCursor(createNonJumpableCursor());
                    }
                }
            }
        }
    }

    MainWindow *mainWindow = qobject_cast<MainWindow*>(window());
    if (mainWindow && mainWindow->modeManager) {
        isInAlternateMode = (mainWindow->modeManager->getCurrentMode() == ModeManager::AlternateMode);
    }

    // Always allow Shift key events to propagate to MainWindow for mode switching
    if (event->key() == Qt::Key_Shift) {
        event->ignore();
        return;
    }

    if (isInAlternateMode) {
        if (completer->popup()->isVisible()) {
            switch (event->key()) {
            case Qt::Key_Down:
            case Qt::Key_Up:
                QApplication::sendEvent(completer->popup(), event);
                return;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (completer->popup()->currentIndex().isValid()) {
                    emit completer->activated(completer->popup()->currentIndex());
                }
                return;
            case Qt::Key_Escape:
                hideAutoComplete();
                clearAlternateModeBuffer();
                return;
            case Qt::Key_Backspace:
                if (!alternateCommandBuffer.isEmpty()) {
                    alternateCommandBuffer.chop(1);
                    processAlternateModeInput(alternateCommandBuffer);
                } else {
                    hideAutoComplete();
                }
                return;
            default:
                break;
            }
        }

        // Handle other alternate mode input
        if (event->key() == Qt::Key_Backspace) {
            if (!alternateCommandBuffer.isEmpty()) {
                alternateCommandBuffer.chop(1);
                processAlternateModeInput(alternateCommandBuffer);
            } else {
                showAlternateModeCommands("");
            }
            return;
        }

        if (event->key() == Qt::Key_Escape) {
            hideAutoComplete();
            clearAlternateModeBuffer();
            return;
        }

        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (!alternateCommandBuffer.isEmpty()) {
                executeAlternateModeCommand(alternateCommandBuffer);
            }
            return;
        }

        QString newChar = event->text();
        if (!newChar.isEmpty() && (newChar.at(0).isPrint())) {
            alternateCommandBuffer += newChar;
            processAlternateModeInput(alternateCommandBuffer);
        }

        return;
    }

    // Normal mode QCompleter handling
    if (completer->popup()->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up:
            QApplication::sendEvent(completer->popup(), event);
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            {
                // Handle case when no item is explicitly selected
                QModelIndex currentIndex = completer->popup()->currentIndex();
                if (!currentIndex.isValid() && completionModel->rowCount() > 0) {
                    // Auto-select first valid item if none selected
                    for (int i = 0; i < completionModel->rowCount(); i++) {
                        QModelIndex index = completionModel->index(i, 0);
                        CompletionModel::CompletionItem item = completionModel->getItem(index);

                        // Skip headers and non-selectable items
                        if (!item.text.contains("::") &&
                            item.text != "No matching commands" &&
                            item.text != "No matching symbols") {
                            currentIndex = index;
                            break;
                        }
                    }
                }

                if (currentIndex.isValid()) {
                    emit completer->activated(currentIndex);
                }
            }
            return;
        case Qt::Key_Escape:
            hideAutoComplete();
            return;
        }
    }

    QPlainTextEdit::keyPressEvent(event);
}

QString MyCodeEditor::getCurrentCommandDefaultValue()
{
    for (const CustomCommand &cmd : qAsConst(customCommands)) {
        if (cmd.symbolType == currentCommandType) {
            return cmd.defaultValue;
        }
    }
    return QString();
}

QString MyCodeEditor::textUnderCursor() const
{
    QTextCursor cursor = textCursor();
    cursor.select(QTextCursor::WordUnderCursor);
    return cursor.selectedText();
}

void MyCodeEditor::showAutoComplete()
{
    if (completionModel->rowCount() > 0) {
        QTextCursor cursor = textCursor();
        QRect rect = cursorRect(cursor);
        rect.setWidth(completer->popup()->sizeHintForColumn(0) + 20);

        // Auto-select first valid item in command mode
        if (isInCustomCommandMode) {
            // Find first selectable item (skip headers)
            for (int i = 0; i < completionModel->rowCount(); i++) {
                QModelIndex index = completionModel->index(i, 0);
                CompletionModel::CompletionItem item = completionModel->getItem(index);

                // Skip non-selectable items (headers, "No matching" messages)
                if (!item.text.contains("::") &&
                    item.text != "No matching commands" &&
                    item.text != "No matching symbols") {
                    completer->popup()->setCurrentIndex(index);
                    break;
                }
            }
        }

        completer->complete(rect);
    }
}

void MyCodeEditor::onAutoCompleteTimer()
{
    QTextCursor cursor = textCursor();
    QTextBlock currentBlock = cursor.block();
    QString lineText = currentBlock.text();
    int positionInLine = cursor.position() - currentBlock.position();
    QString lineUpToCursor = lineText.left(positionInLine);

    // 检查是否在同一行内，如果换行了则重置退出标志
    static int lastLineNumber = -1;
    int currentLineNumber = currentBlock.blockNumber();
    if (currentLineNumber != lastLineNumber) {
        commandModeExitedByDoubleSpace = false;
        lastLineNumber = currentLineNumber;
    }

    // 检查命令模式
    if (checkForCustomCommand(lineUpToCursor)) {
        // 如果之前通过双空格退出过，则忽略命令模式
        if (commandModeExitedByDoubleSpace) {
            return;
        }

        // 检测连续空格
        if (isConsecutiveSpaces()) {
            // 第二个连续空格，退出自动补全并设置标志
            clearCommandHighlight();
            isInCustomCommandMode = false;
            commandModeExitedByDoubleSpace = true;
            if (completer->popup()->isVisible()) {
                completer->popup()->hide();
            }
            return;
        }

        // 正常处理命令模式
        highlightCommandText();
        QString commandInput = extractCommandInput().trimmed();

        // 这里是原有的命令模式处理逻辑
        CompletionManager* manager = CompletionManager::getInstance();
        int cursorPosition = cursor.position();
        QString fileName = getFileName();
        QString currentModule = manager->getCurrentModule(fileName, cursorPosition);

        // 对于struct相关的命令，直接获取SymbolInfo列表以保留类型信息
        QList<sym_list::SymbolInfo> filteredSymbols;
        bool useSymbolInfoDirectly = (currentCommandType == sym_list::sym_packed_struct_var ||
                                      currentCommandType == sym_list::sym_unpacked_struct_var ||
                                      currentCommandType == sym_list::sym_packed_struct ||
                                      currentCommandType == sym_list::sym_unpacked_struct);
        
        if (useSymbolInfoDirectly) {
            // struct 相关命令(s/sp/ns/nsp)：严格作用域——仅在模块内补全，模块外不弹出
            if (currentModule.isEmpty()) {
                if (completer->popup()->isVisible()) {
                    completer->popup()->hide();
                }
                return;
            }
            sym_list* symbolList = sym_list::getInstance();
            symbolList->refreshStructTypedefEnumForFile(fileName, document()->toPlainText());
            // 在模块内：聚合模块内 + include 文件 + import 的 package
            filteredSymbols = manager->getModuleContextSymbolsByType(currentModule, fileName, currentCommandType, commandInput);
        } else {
            // 对于其他类型，使用原来的方法
            QStringList symbolNames;
            if (!currentModule.isEmpty()) {
                symbolNames = manager->getModuleInternalVariablesByType(currentModule, currentCommandType, commandInput);
            } else {
                symbolNames = manager->getGlobalSymbolsByType(currentCommandType, commandInput);
            }

            // 转换为 SymbolInfo 列表
            sym_list* symbolList = sym_list::getInstance();

            for (const QString &symbolName : symbolNames) {
                QList<sym_list::SymbolInfo> matchingSymbols = symbolList->findSymbolsByName(symbolName);
                for (const sym_list::SymbolInfo &symbol : matchingSymbols) {
                    if (symbol.symbolType == currentCommandType &&
                        (currentModule.isEmpty() || symbol.moduleScope == currentModule)) {
                        filteredSymbols.append(symbol);
                        break;
                    }
                }
            }
        }

        completionModel->updateSymbolCompletions(filteredSymbols, commandInput, currentCommandType);
        showAutoComplete();
        return;
    }

    // 不在命令模式时，重置退出标志
    commandModeExitedByDoubleSpace = false;

    // 正常模式处理
    clearCommandHighlight();
    isInCustomCommandMode = false;

    if (isInAlternateMode) {
        processAlternateModeInput(lineUpToCursor);
        return;
    }

    // 结构体变量.成员 上下文：输入 . 或 空格 后显示成员补全（含模糊匹配，如 a1 匹配 abc123）
    QString lineForParse = lineUpToCursor.trimmed();
    QString varName, memberPrefix;
    CompletionManager* manager = CompletionManager::getInstance();
    if (manager->tryParseStructMemberContext(lineForParse, varName, memberPrefix)) {
        QString currentModule = manager->getCurrentModule(getFileName(), cursor.position());
        QString structTypeName = manager->getStructTypeForVariable(varName, currentModule);
        if (!structTypeName.isEmpty()) {
            QStringList memberNames = manager->getStructMemberCompletions(memberPrefix, structTypeName);
            QList<sym_list::SymbolInfo> symbolInfoList;
            sym_list* symbolList = sym_list::getInstance();
            for (const QString& name : memberNames) {
                QList<sym_list::SymbolInfo> syms = symbolList->findSymbolsByName(name);
                for (const sym_list::SymbolInfo& s : syms) {
                    if (s.symbolType == sym_list::sym_struct_member && s.moduleScope == structTypeName) {
                        symbolInfoList.append(s);
                        break;
                    }
                }
            }
            completionModel->updateCompletions(memberNames, symbolInfoList, memberPrefix, CompletionModel::SymbolCompletion);
            wordStartPos = currentBlock.position() + lineUpToCursor.lastIndexOf('.') + 1;
            showAutoComplete();
            return;
        }
    }

    QString prefix = getWordUnderCursor();
    if (prefix.length() >= 1) {
        QStringList suggestions = getCompletionSuggestions(prefix);
        QList<sym_list::SymbolInfo> symbolInfoList;
        sym_list* symbolList = sym_list::getInstance();

        for (const QString &suggestion : suggestions) {
            QList<sym_list::SymbolInfo> matchingSymbols = symbolList->findSymbolsByName(suggestion);
            if (!matchingSymbols.isEmpty()) {
                symbolInfoList.append(matchingSymbols.first());
            } else {
                sym_list::SymbolInfo dummySymbol;
                dummySymbol.symbolName = suggestion;
                dummySymbol.symbolType = sym_list::sym_user;
                symbolInfoList.append(dummySymbol);
            }
        }

        completionModel->updateCompletions(suggestions, symbolInfoList, prefix, CompletionModel::SymbolCompletion);
        showAutoComplete();
    }
}


bool MyCodeEditor::isConsecutiveSpaces()
{
    QTextCursor cursor = textCursor();
    QTextBlock currentBlock = cursor.block();
    QString lineText = currentBlock.text();
    int positionInLine = cursor.position() - currentBlock.position();

    // 检查当前位置前两个字符是否都是空格
    if (positionInLine >= 2) {
        QString lastTwoChars = lineText.mid(positionInLine - 2, 2);
        if (lastTwoChars == "  ") { // 两个连续空格
            return true;
        }
    }

    return false;
}

QStringList MyCodeEditor::getCommandModeInternalVariables(const QString &prefix)
{
    CompletionManager* manager = CompletionManager::getInstance();

    // 获取当前光标位置和文件名
    QTextCursor cursor = textCursor();
    int cursorPosition = cursor.position();
    QString fileName = getFileName();

    // 🚀 关键：获取当前模块名
    QString currentModule = manager->getCurrentModule(fileName, cursorPosition);

    if (!currentModule.isEmpty()) {
        // 🚀 在模块内：根据命令类型过滤内部变量
        return manager->getModuleInternalVariablesByType(currentModule, currentCommandType, prefix);
    } else {
        // 🚀 在模块外：根据命令类型返回全局符号
        return manager->getGlobalSymbolsByType(currentCommandType, prefix);
    }
}

void MyCodeEditor::highlightCommandText()
{
    QTextCursor cursor = textCursor();
    QTextBlock currentBlock = cursor.block();
    QString lineText = currentBlock.text();
    int positionInLine = cursor.position() - currentBlock.position();
    QString lineUpToCursor = lineText.left(positionInLine);

    // Find the command prefix position
    int prefixPos = -1;
    for (const CustomCommand &cmd : qAsConst(customCommands)) {
        int pos = lineUpToCursor.lastIndexOf(cmd.prefix);
        if (pos != -1) {
            QString beforePrefix = lineUpToCursor.left(pos).trimmed();
            if (beforePrefix.isEmpty()) {
                prefixPos = pos;
                break;
            }
        }
    }

    if (prefixPos == -1) return;

    // Calculate absolute positions
    commandStartPosition = currentBlock.position() + prefixPos;
    commandEndPosition = cursor.position();

    // Create extra selection for command highlight
    QList<QTextEdit::ExtraSelection> extraSelections = this->extraSelections();

    // Remove any existing command highlight
    extraSelections.erase(
        std::remove_if(extraSelections.begin(), extraSelections.end(),
            [](const QTextEdit::ExtraSelection &selection) {
                return selection.format.property(QTextFormat::UserProperty).toInt() == 999; // Custom marker
            }),
        extraSelections.end()
    );

    // Add new command highlight
    QTextEdit::ExtraSelection commandSelection;
    commandSelection.format.setBackground(QColor(60, 60, 60, 180)); // Dark background for command text
    commandSelection.format.setForeground(QColor(255, 255, 255));   // White text
    commandSelection.format.setProperty(QTextFormat::UserProperty, 999); // Custom marker

    QTextCursor commandCursor = cursor;
    commandCursor.setPosition(commandStartPosition);
    commandCursor.setPosition(commandEndPosition, QTextCursor::KeepAnchor);
    commandSelection.cursor = commandCursor;

    extraSelections.append(commandSelection);
    setExtraSelections(extraSelections);
}

void MyCodeEditor::clearCommandHighlight()
{
    // Remove command highlight from extra selections
    QList<QTextEdit::ExtraSelection> extraSelections = this->extraSelections();

    extraSelections.erase(
        std::remove_if(extraSelections.begin(), extraSelections.end(),
            [](const QTextEdit::ExtraSelection &selection) {
                return selection.format.property(QTextFormat::UserProperty).toInt() == 999; // Custom marker
            }),
        extraSelections.end()
    );

    setExtraSelections(extraSelections);

    commandStartPosition = -1;
    commandEndPosition = -1;
}

QString MyCodeEditor::getWordUnderCursor()
{
    QTextCursor cursor = textCursor();
    int currentPos = cursor.position();

    // Find word start
    cursor.movePosition(QTextCursor::StartOfWord, QTextCursor::MoveAnchor);
    wordStartPos = cursor.position();

    // Find word end
    cursor.setPosition(currentPos);
    cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::MoveAnchor);

    // Extract word
    cursor.setPosition(wordStartPos, QTextCursor::MoveAnchor);
    cursor.setPosition(currentPos, QTextCursor::KeepAnchor);

    return cursor.selectedText();
}

void MyCodeEditor::initCustomCommands()
{
    customCommands.clear();

    // Define custom commands with default values
    CustomCommand regCommand;
    regCommand.prefix = "r ";
    regCommand.symbolType = sym_list::sym_reg;
    regCommand.description = "reg variables";
    regCommand.defaultValue = "reg";
    customCommands.append(regCommand);

    CustomCommand wireCommand;
    wireCommand.prefix = "w ";
    wireCommand.symbolType = sym_list::sym_wire;
    wireCommand.description = "wire variables";
    wireCommand.defaultValue = "wire";
    customCommands.append(wireCommand);

    CustomCommand logicCommand;
    logicCommand.prefix = "l ";
    logicCommand.symbolType = sym_list::sym_logic;
    logicCommand.description = "logic variables";
    logicCommand.defaultValue = "logic";
    customCommands.append(logicCommand);

    CustomCommand moduleCommand;
    moduleCommand.prefix = "m ";
    moduleCommand.symbolType = sym_list::sym_module;
    moduleCommand.description = "modules";
    moduleCommand.defaultValue = "module";
    customCommands.append(moduleCommand);

    CustomCommand taskCommand;
    taskCommand.prefix = "t ";
    taskCommand.symbolType = sym_list::sym_task;
    taskCommand.description = "tasks";
    taskCommand.defaultValue = "task";
    customCommands.append(taskCommand);

    CustomCommand functionCommand;
    functionCommand.prefix = "f ";
    functionCommand.symbolType = sym_list::sym_function;
    functionCommand.description = "functions";
    functionCommand.defaultValue = "function";
    customCommands.append(functionCommand);

    customCommands << CustomCommand{"i ", sym_list::sym_interface, "interfaces", "interface"};
    customCommands << CustomCommand{"e ", sym_list::sym_enum, "enum types", "enum"};
    customCommands << CustomCommand{"d ", sym_list::sym_def_define, "define macros", "`define"};
    // lp 在前，避免 "lp " 被识别成 "p "
    customCommands << CustomCommand{"lp ", sym_list::sym_localparam, "local parameters", "localparam"};
    customCommands << CustomCommand{"p ", sym_list::sym_parameter, "parameters", "parameter"};
    customCommands << CustomCommand{"a ", sym_list::sym_always, "always blocks", "always"};
    customCommands << CustomCommand{"c ", sym_list::sym_assign, "continuous assigns", "assign"};
    customCommands << CustomCommand{"u ", sym_list::sym_typedef, "type definitions", "typedef"};

    customCommands << CustomCommand{"ev ", sym_list::sym_enum_value, "enum values", "enum_value"};
    customCommands << CustomCommand{"en ", sym_list::sym_enum_var, "enum variables", "enum_var"};
    customCommands << CustomCommand{"sm ", sym_list::sym_struct_member, "struct members", "member"};
    
    // 结构体：较长前缀放前面，避免 "nsp " 被识别成 "sp "、"ns " 被识别成 "s "
    customCommands << CustomCommand{"nsp ", sym_list::sym_packed_struct, "packed struct types", "struct"};
    customCommands << CustomCommand{"ns ", sym_list::sym_unpacked_struct, "unpacked struct types", "struct"};
    customCommands << CustomCommand{"sp ", sym_list::sym_packed_struct_var, "packed struct variables", "struct"};
    customCommands << CustomCommand{"s ", sym_list::sym_unpacked_struct_var, "unpacked struct variables", "struct"};

}

bool MyCodeEditor::checkForCustomCommand(const QString &lineUpToCursor)
{
    // Check if we're in a custom command
    for (const CustomCommand &cmd : qAsConst(customCommands)) {
        int prefixPos = lineUpToCursor.lastIndexOf(cmd.prefix);
        if (prefixPos != -1) {
            // Check if there's only whitespace or nothing before the prefix
            QString beforePrefix = lineUpToCursor.left(prefixPos).trimmed();
            if (beforePrefix.isEmpty()) {
                isInCustomCommandMode = true;
                currentCommandPrefix = cmd.prefix;
                currentCommandType = cmd.symbolType;

                return true;
            }
        }
    }

    isInCustomCommandMode = false;
    currentCommandPrefix.clear();
    return false;
}

QString MyCodeEditor::extractCommandInput()
{
    QTextCursor cursor = textCursor();
    QTextBlock currentBlock = cursor.block();
    QString lineText = currentBlock.text();
    int positionInLine = cursor.position() - currentBlock.position();
    QString lineUpToCursor = lineText.left(positionInLine);

    // 使用当前命令前缀位置，保证 ns/nsp 与 s/sp 一致
    int prefixPos = currentCommandPrefix.isEmpty()
        ? -1
        : lineUpToCursor.lastIndexOf(currentCommandPrefix);

    if (prefixPos >= 0) {
        int startPos = prefixPos + currentCommandPrefix.length();
        QString result = lineUpToCursor.mid(startPos);
        return result;
    }

    return QString();
}

QStringList MyCodeEditor::getSymbolCompletions(sym_list::sym_type_e symbolType, const QString &prefix)
{
    return CompletionManager::getInstance()->getSymbolCompletions(symbolType, prefix);
}

void MyCodeEditor::initAlternateModeCommands()
{
    alternateModeCommands.clear();

    // Define alternate mode commands
    alternateModeCommands << "save" << "save_as" << "open" << "new" << "close"
                         << "copy" << "paste" << "cut" << "undo" << "redo"
                         << "find" << "replace" << "goto_line" << "select_all"
                         << "comment" << "uncomment" << "indent" << "unindent";
}

void MyCodeEditor::processAlternateModeInput(const QString &input)
{
    if (!isInAlternateMode) return;

    alternateCommandBuffer = input.trimmed().toLower();

    // Ensure immediate display of command list
    showAlternateModeCommands(alternateCommandBuffer);
}

void MyCodeEditor::showAlternateModeCommands(const QString &filter)
{
    // Update command completion list
    completionModel->updateCommandCompletions(alternateModeCommands, filter);

    // Show if there's content
    if (completionModel->rowCount() > 0) {
        showAutoComplete();
    } else {
    }
}

void MyCodeEditor::executeAlternateModeCommand(const QString &command)
{
    QString cmd = command.trimmed().toLower();

    MainWindow *mainWindow = qobject_cast<MainWindow*>(window());
    if (!mainWindow) {
        clearAlternateModeBuffer();
        hideAutoComplete();
        return;
    }

    if (cmd == "save") {
        // Use TabManager instead of direct MainWindow calls
        if (mainWindow->tabManager) {
            mainWindow->tabManager->saveCurrentTab();
        }
    } else if (cmd == "save_as") {
        if (mainWindow->tabManager) {
            mainWindow->tabManager->saveAsCurrentTab();
        }
    } else if (cmd == "open") {
        if (mainWindow->tabManager) {
            mainWindow->tabManager->openFileInTab(QString());
        }
    } else if (cmd == "new") {
        if (mainWindow->tabManager) {
            mainWindow->tabManager->createNewTab();
        }
    } else if (cmd == "copy") {
        copy();
    } else if (cmd == "paste") {
        paste();
    } else if (cmd == "cut") {
        cut();
    } else if (cmd == "undo") {
        undo();
    } else if (cmd == "redo") {
        redo();
    } else if (cmd == "select_all") {
        selectAll();
    } else if (cmd == "comment") {
        insertPlainText("// ");
    } else if (cmd == "goto_line") {
        // TODO: Implement goto line functionality
    } else {
    }

    clearAlternateModeBuffer();
    hideAutoComplete();
}

void MyCodeEditor::clearAlternateModeBuffer()
{
    alternateCommandBuffer.clear();
}

void MyCodeEditor::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control && ctrlPressed) {
        ctrlPressed = false;
        // 松开 Ctrl：恢复为普通 I 型光标
        viewport()->setCursor(Qt::IBeamCursor);
        clearHoveredSymbolHighlight();
        hoveredWord.clear();
    }

    // Check if we're in alternate mode
    MainWindow *mainWindow = qobject_cast<MainWindow*>(window());
    if (mainWindow && mainWindow->modeManager) {
        isInAlternateMode = (mainWindow->modeManager->getCurrentMode() == ModeManager::AlternateMode);
    }

    // Always allow Shift key release events to propagate to MainWindow for mode switching
    if (event->key() == Qt::Key_Shift) {
        QPlainTextEdit::keyReleaseEvent(event);
        return;
    }

    // For other keys in alternate mode, don't propagate
    if (isInAlternateMode) {
        return;
    }

    // Normal mode - pass through to base class
    QPlainTextEdit::keyReleaseEvent(event);
}

void MyCodeEditor::mousePressEvent(QMouseEvent *event)
{
    // 检查是否是 Ctrl+左键点击
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        // 1) 优先判断是否点击在 `include` 的文件名上
        if (tryJumpToIncludeAtPosition(event->pos())) {
            event->accept();
            return;
        }

        // 2) 检查是否点击在 import 语句中的 package 名称上
        QString pkgName;
        int pkgStart = -1;
        int pkgEnd = -1;
        if (getPackageNameFromImport(event->pos(), pkgName, pkgStart, pkgEnd)) {
            jumpToDefinition(pkgName);
            event->accept();
            return;
        }

        // 3) 否则走原有的符号跳转逻辑
        QString wordUnderCursor = getWordAtPosition(event->pos());
        if (!wordUnderCursor.isEmpty()) {
            jumpToDefinition(wordUnderCursor);
            event->accept();
            return;
        }
    }

    // 调用基类处理其他鼠标事件
    QPlainTextEdit::mousePressEvent(event);
}

void MyCodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    // 检查是否按下 Ctrl 键
    bool isCtrlPressed = (event->modifiers() & Qt::ControlModifier);

    if (isCtrlPressed != ctrlPressed) {
        ctrlPressed = isCtrlPressed;

        if (ctrlPressed) {
            // Ctrl 刚按下：根据当前鼠标位置更新高亮和光标
            clearHoveredSymbolHighlight();

            int incStart = -1;
            int incEnd = -1;
            QString incPath;
            if (getIncludeInfoAtPosition(event->pos(), incStart, incEnd, incPath)) {
                // 在 `include "..."` 的路径上
                hoveredWord = incPath;
                hoveredWordStartPos = incStart;
                hoveredWordEndPos = incEnd;
                highlightHoveredSymbol(incPath, incStart, incEnd);
                viewport()->setCursor(createJumpableCursor());
            } else {
                // 检查是否在 import 语句中的 package 名称上
                QString pkgName;
                int pkgStart = -1;
                int pkgEnd = -1;
                if (getPackageNameFromImport(event->pos(), pkgName, pkgStart, pkgEnd)) {
                    hoveredWord = pkgName;
                    hoveredWordStartPos = pkgStart;
                    hoveredWordEndPos = pkgEnd;
                    highlightHoveredSymbol(pkgName, pkgStart, pkgEnd);
                    viewport()->setCursor(createJumpableCursor());
                } else {
                    QString word = getWordAtPosition(event->pos());
                    if (!word.isEmpty()) {
                        QTextCursor cursor = cursorForPosition(event->pos());
                        QTextCursor wordCursor = getWordCursorAtPosition(cursor.position());
                        hoveredWord = word;
                        hoveredWordStartPos = wordCursor.selectionStart();
                        hoveredWordEndPos = wordCursor.selectionEnd();
                        highlightHoveredSymbol(word, hoveredWordStartPos, hoveredWordEndPos);

                        if (canJumpToDefinition(word)) {
                            viewport()->setCursor(createJumpableCursor());
                        } else {
                            viewport()->setCursor(createNonJumpableCursor());
                        }
                    } else {
                        hoveredWord.clear();
                        viewport()->setCursor(createNonJumpableCursor());
                    }
                }
            }
        } else {
            // 刚从按下 Ctrl 切换为未按：恢复普通 I 型光标并清除高亮
            viewport()->setCursor(Qt::IBeamCursor);
            clearHoveredSymbolHighlight();
            hoveredWord.clear();
        }
    } else if (ctrlPressed) {
        // Ctrl 持续按下时，随鼠标移动更新高亮和光标
        int incStart = -1;
        int incEnd = -1;
        QString incPath;
        bool onInclude = getIncludeInfoAtPosition(event->pos(), incStart, incEnd, incPath);

        if (onInclude) {
            // 鼠标在 include 路径上
            if (hoveredWord != incPath || hoveredWordStartPos != incStart || hoveredWordEndPos != incEnd) {
                clearHoveredSymbolHighlight();
                hoveredWord = incPath;
                hoveredWordStartPos = incStart;
                hoveredWordEndPos = incEnd;
                highlightHoveredSymbol(incPath, incStart, incEnd);
            }
            viewport()->setCursor(createJumpableCursor());
        } else {
            // 检查是否在 import 语句中的 package 名称上
            QString pkgName;
            int pkgStart = -1;
            int pkgEnd = -1;
            bool onImport = getPackageNameFromImport(event->pos(), pkgName, pkgStart, pkgEnd);

            if (onImport) {
                // 鼠标在 import 语句的 package 名称上
                if (hoveredWord != pkgName || hoveredWordStartPos != pkgStart || hoveredWordEndPos != pkgEnd) {
                    clearHoveredSymbolHighlight();
                    hoveredWord = pkgName;
                    hoveredWordStartPos = pkgStart;
                    hoveredWordEndPos = pkgEnd;
                    highlightHoveredSymbol(pkgName, pkgStart, pkgEnd);
                }
                viewport()->setCursor(createJumpableCursor());
            } else {
                QString word = getWordAtPosition(event->pos());
                if (word != hoveredWord) {
                    clearHoveredSymbolHighlight();
                    if (!word.isEmpty()) {
                        QTextCursor cursor = cursorForPosition(event->pos());
                        QTextCursor wordCursor = getWordCursorAtPosition(cursor.position());
                        hoveredWord = word;
                        hoveredWordStartPos = wordCursor.selectionStart();
                        hoveredWordEndPos = wordCursor.selectionEnd();
                        highlightHoveredSymbol(word, hoveredWordStartPos, hoveredWordEndPos);
                    } else {
                        hoveredWord.clear();
                    }
                }

                if (!hoveredWord.isEmpty() && canJumpToDefinition(hoveredWord)) {
                    viewport()->setCursor(createJumpableCursor());
                } else {
                    viewport()->setCursor(createNonJumpableCursor());
                }
            }
        }
    }

    QPlainTextEdit::mouseMoveEvent(event);
}

void MyCodeEditor::leaveEvent(QEvent *event)
{
    // 鼠标离开编辑器时清除高亮并恢复普通光标
    ctrlPressed = false;
    viewport()->setCursor(Qt::IBeamCursor);
    clearHoveredSymbolHighlight();
    hoveredWord.clear();

    QPlainTextEdit::leaveEvent(event);
}

QString MyCodeEditor::getWordAtPosition(const QPoint& position)
{
    QTextCursor cursor = cursorForPosition(position);
    return getWordAtTextPosition(cursor.position());
}

QString MyCodeEditor::getWordAtTextPosition(int position)
{
    QTextCursor cursor = textCursor();
    cursor.setPosition(position);

    // 检查是否在单词中
    if (!cursor.atBlockEnd() && !cursor.atBlockStart()) {
        QChar currentChar = document()->characterAt(position);
        if (!currentChar.isLetterOrNumber() && currentChar != '_') {
            return QString();
        }
    }

    // 选择当前单词
    cursor.select(QTextCursor::WordUnderCursor);
    QString word = cursor.selectedText();

    // 验证是否是有效的 sv 标识符
    if (word.isEmpty() || (!word[0].isLetter() && word[0] != '_')) {
        return QString();
    }

    for (int i = 1; i < word.length(); ++i) {
        if (!word[i].isLetterOrNumber() && word[i] != '_') {
            return QString();
        }
    }

    return word;
}

QTextCursor MyCodeEditor::getWordCursorAtPosition(int position)
{
    QTextCursor cursor = textCursor();
    cursor.setPosition(position);
    cursor.select(QTextCursor::WordUnderCursor);
    return cursor;
}

bool MyCodeEditor::getPackageNameFromImport(const QPoint& position, QString& packageName, int& startPos, int& endPos)
{
    QTextCursor cursor = cursorForPosition(position);
    QTextBlock block = cursor.block();
    QString lineText = block.text();
    if (lineText.isEmpty()) {
        return false;
    }

    int posInLine = cursor.position() - block.position();

    // 查找 import 关键字
    int importPos = lineText.indexOf("import");
    if (importPos == -1) {
        return false;
    }

    static const QRegularExpression importPattern("import\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*::");
    QRegularExpressionMatch m = importPattern.match(lineText);
    if (!m.hasMatch()) {
        return false;
    }

    QString matchedPackageName = m.captured(1);
    int packageStartInLine = m.capturedStart(1);
    int packageEndInLine = packageStartInLine + matchedPackageName.length();

    // 检查鼠标位置是否在 package 名称范围内
    if (posInLine < packageStartInLine || posInLine >= packageEndInLine) {
        return false;
    }

    packageName = matchedPackageName;
    startPos = block.position() + packageStartInLine;
    endPos = block.position() + packageEndInLine;

    return true;
}


// 定义类型优先级（用于跨文件跳转时同名符号排序，数值越小优先级越高）
static int definitionTypePriority(sym_list::sym_type_e t)
{
    switch (t) {
    case sym_list::sym_module:   return 0;
    case sym_list::sym_interface: return 1;
    case sym_list::sym_package:  return 2;
    case sym_list::sym_port_input:
    case sym_list::sym_port_output:
    case sym_list::sym_port_inout:
    case sym_list::sym_port_ref:
    case sym_list::sym_port_interface:
    case sym_list::sym_port_interface_modport: return 3;  // 端口优先于 reg/wire/logic
    case sym_list::sym_task:
    case sym_list::sym_function: return 4;
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:   return 5;
    case sym_list::sym_parameter:
    case sym_list::sym_localparam: return 6;
    default:                     return 10;
    }
}

void MyCodeEditor::jumpToDefinition(const QString& symbolName)
{
    if (symbolName.isEmpty()) {
        return;
    }

    sym_list* symbolList = sym_list::getInstance();
    if (!symbolList) {
        return;
    }

    const QString currentFile = getFileName();
    int cursorLine = textCursor().blockNumber();
    QString currentModuleName = symbolList->getCurrentModuleScope(currentFile, cursorLine);

    // 作用域限定：在模块内时只考虑当前模块的符号，避免跨模块跳转（如两个模块都有 clk_main 时只跳本模块）
    auto inScope = [&currentModuleName](const sym_list::SymbolInfo& s) {
        if (currentModuleName.isEmpty()) return true;
        return s.moduleScope == currentModuleName;
    };

    // ---------- Step 1: 本地搜索（仅当前文件），在模块内时仅当前模块符号 ----------
    QList<sym_list::SymbolInfo> localSymbols = symbolList->findSymbolsByFileName(currentFile);
    sym_list::SymbolInfo localBest;
    bool foundLocal = false;
    int localBestPriority = 999;
    for (const sym_list::SymbolInfo& symbol : qAsConst(localSymbols)) {
        if (symbol.symbolName != symbolName || !isSymbolDefinition(symbol, symbolName)) {
            continue;
        }
        if (!inScope(symbol)) continue;  // 不在当前作用域则跳过
        int p = definitionTypePriority(symbol.symbolType);
        if (!currentModuleName.isEmpty() && symbol.moduleScope == currentModuleName) {
            p -= 100;  // 同模块符号优先（端口/变量等）
        }
        if (p < localBestPriority) {
            localBest = symbol;
            localBestPriority = p;
            foundLocal = true;
        }
    }
    if (foundLocal) {
        // 当前文件内跳转
        QTextCursor cursor = textCursor();
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, localBest.startLine);
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, localBest.startColumn);
        setTextCursor(cursor);
        centerCursor();
        return;
    }

    // ---------- Step 2: 全局搜索（跨文件，排除当前文件）；在模块内时仅考虑当前模块符号 ----------
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();
    sym_list::SymbolInfo globalBest;
    bool foundGlobal = false;
    int globalBestPriority = 999;
    for (const sym_list::SymbolInfo& symbol : qAsConst(allSymbols)) {
        if (symbol.symbolName != symbolName || !isSymbolDefinition(symbol, symbolName)) {
            continue;
        }
        if (symbol.fileName == currentFile) {
            continue; // Step 1 已覆盖，忽略当前文件
        }
        if (!inScope(symbol)) continue;  // 不在当前作用域则跳过（避免跳到其他模块的同名端口）
        int p = definitionTypePriority(symbol.symbolType);
        if (!currentModuleName.isEmpty() && symbol.moduleScope == currentModuleName) {
            p -= 100;
        }
        if (p < globalBestPriority) {
            globalBest = symbol;
            globalBestPriority = p;
            foundGlobal = true;
        }
    }

    // ---------- Step 3: 跨文件时通过信号由 MainWindow 打开文件并跳转 ----------
    if (foundGlobal) {
        emit definitionJumpRequested(globalBest.symbolName, globalBest.fileName, globalBest.startLine + 1);
    }
}

bool MyCodeEditor::isSymbolDefinition(const sym_list::SymbolInfo& symbol, const QString& searchWord)
{
    // 检查符号名称是否匹配
    if (symbol.symbolName != searchWord) {
        return false;
    }

    // 所有这些类型都被认为是定义（含端口、跨文件跳转的 module/interface/package/task/function、struct 类型）
    switch (symbol.symbolType) {
        case sym_list::sym_module:
        case sym_list::sym_interface:
        case sym_list::sym_package:
        case sym_list::sym_task:
        case sym_list::sym_function:
        case sym_list::sym_port_input:
        case sym_list::sym_port_output:
        case sym_list::sym_port_inout:
        case sym_list::sym_port_ref:
        case sym_list::sym_port_interface:
        case sym_list::sym_port_interface_modport:
        case sym_list::sym_reg:
        case sym_list::sym_wire:
        case sym_list::sym_logic:
        case sym_list::sym_parameter:
        case sym_list::sym_localparam:
        case sym_list::sym_packed_struct:
        case sym_list::sym_unpacked_struct:
            return true;
        default:
            return false;
    }
}

void MyCodeEditor::highlightHoveredSymbol(const QString& word, int startPos, int endPos)
{
    if (word.isEmpty() || startPos < 0 || endPos <= startPos) {
        return;
    }

    // 创建高亮选择
    QTextEdit::ExtraSelection highlight;
    highlight.cursor = textCursor();
    highlight.cursor.setPosition(startPos);
    highlight.cursor.setPosition(endPos, QTextCursor::KeepAnchor);

    // 设置高亮样式 - 蓝色下划线
    highlight.format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    highlight.format.setUnderlineColor(QColor(0, 100, 200));
    highlight.format.setForeground(QColor(0, 100, 200));

    // 标记这是定义跳转高亮 (使用唯一标识符)
    highlight.format.setProperty(QTextFormat::UserProperty + 1, 1001);

    // 添加到额外选择中
    QList<QTextEdit::ExtraSelection> extraSelections = this->extraSelections();

    // 移除之前的定义跳转高亮
    extraSelections.erase(
        std::remove_if(extraSelections.begin(), extraSelections.end(),
            [](const QTextEdit::ExtraSelection &selection) {
                return selection.format.property(QTextFormat::UserProperty + 1).toInt() == 1001;
            }),
        extraSelections.end()
    );

    extraSelections.append(highlight);
    setExtraSelections(extraSelections);
}

void MyCodeEditor::clearHoveredSymbolHighlight()
{
    // 移除定义跳转高亮
    QList<QTextEdit::ExtraSelection> extraSelections = this->extraSelections();

    int removedCount = 0;
    auto it = std::remove_if(extraSelections.begin(), extraSelections.end(),
        [&removedCount](const QTextEdit::ExtraSelection &selection) {
            bool shouldRemove = selection.format.property(QTextFormat::UserProperty + 1).toInt() == 1001;
            if (shouldRemove) removedCount++;
            return shouldRemove;
        });

    extraSelections.erase(it, extraSelections.end());
    setExtraSelections(extraSelections);

    hoveredWordStartPos = -1;
    hoveredWordEndPos = -1;
}


void MyCodeEditor::showSymbolTooltip(const QString& symbolName, const QPoint& position)
{
    if (symbolName.isEmpty()) return;

    sym_list* symbolList = sym_list::getInstance();
    if (!symbolList) return;

    QList<sym_list::SymbolInfo> symbols = symbolList->findSymbolsByName(symbolName);
    if (symbols.isEmpty()) return;

    // 构建工具提示文本
    QString tooltipText;
    int definitionCount = 0;

    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (isSymbolDefinition(symbol, symbolName)) {
            definitionCount++;
            if (definitionCount == 1) {
                tooltipText = QString("定义: %1 (%2)\n位置: %3:%4")
                             .arg(symbol.symbolName)
                             .arg(getSymbolTypeString(symbol.symbolType))
                             .arg(QFileInfo(symbol.fileName).fileName())
                             .arg(symbol.startLine + 1);
            }
        }
    }

    if (definitionCount > 1) {
        tooltipText += QString("\n(+%1 个其他定义)").arg(definitionCount - 1);
    }

    if (!tooltipText.isEmpty()) {
        QToolTip::showText(mapToGlobal(position), tooltipText, this);
    }
}

QString MyCodeEditor::getSymbolTypeString(sym_list::sym_type_e symbolType)
{
    switch (symbolType) {
    case sym_list::sym_reg:      return "reg";
    case sym_list::sym_wire:     return "wire";
    case sym_list::sym_logic:    return "logic";
    case sym_list::sym_module:   return "module";
    case sym_list::sym_task:     return "task";
    case sym_list::sym_function: return "function";
    default:                     return QString("unknown_%1").arg(static_cast<int>(symbolType));
    }
}

bool MyCodeEditor::canJumpToDefinition(const QString& symbolName)
{
    if (symbolName.isEmpty()) {
        return false;
    }

    sym_list* symbolList = sym_list::getInstance();
    if (!symbolList) {
        return false;
    }

    QString currentFile = getFileName();
    int cursorLine = textCursor().blockNumber();
    QString currentModuleName = symbolList->getCurrentModuleScope(currentFile, cursorLine);

    QList<sym_list::SymbolInfo> symbols = symbolList->findSymbolsByName(symbolName);
    if (symbols.isEmpty()) {
        return false;
    }

    // 在模块内时：仅当当前模块中存在该符号的定义才允许跳转（作用域限定）
    if (!currentModuleName.isEmpty()) {
        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (symbol.moduleScope == currentModuleName && isSymbolDefinition(symbol, symbolName)) {
                return true;
            }
        }
        return false;  // 当前模块无此符号，不显示可跳转
    }

    // 不在模块内：任意可跳转定义即可
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (isSymbolDefinition(symbol, symbolName)) {
            return true;
        }
    }
    return !symbols.isEmpty();
}

QCursor MyCodeEditor::createJumpableCursor()
{
    // 创建绿色对勾图标 - 使用更大的尺寸以便更清晰
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    if (!painter.isActive()) {
        // 如果绘制器未激活，返回默认光标
        return QCursor(Qt::PointingHandCursor);
    }
    
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制绿色对勾 - 使用更粗的线条和更亮的绿色
    QPen pen(QColor(0, 255, 0), 4); // 亮绿色，4像素宽
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    
    // 绘制对勾形状（两条线段组成V形对勾）
    // 第一段：从左上到中间
    painter.drawLine(7, 12, 11, 16);
    // 第二段：从中间到右下
    painter.drawLine(11, 16, 18, 6);
    
    painter.end();
    
    return QCursor(pixmap, 12, 12); // 热点在中心
}

QCursor MyCodeEditor::createNonJumpableCursor()
{
    // 创建红色叉图标
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制红色叉
    QPen pen(QColor(255, 0, 0), 3); // 红色，3像素宽
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    
    // 绘制X形状（两条对角线）
    painter.drawLine(5, 5, 15, 15);
    painter.drawLine(15, 5, 5, 15);
    
    return QCursor(pixmap, 10, 10); // 热点在中心
}

bool MyCodeEditor::getIncludeInfoAtPosition(const QPoint& position, int &startPos, int &endPos, QString &includePath)
{
    QTextCursor cursor = cursorForPosition(position);
    QTextBlock block = cursor.block();
    QString lineText = block.text();
    if (lineText.isEmpty()) {
        return false;
    }

    int posInLine = cursor.position() - block.position();

    // 严格遵守 SystemVerilog 标准语法：只支持 `` `include "file" `` 形式
    int keywordPos = lineText.indexOf("`include");
    if (keywordPos == -1) {
        return false;
    }

    // 找到 include 之后的第一个双引号
    int firstQuote = lineText.indexOf('"', keywordPos);
    if (firstQuote == -1) {
        return false;
    }
    int secondQuote = lineText.indexOf('"', firstQuote + 1);
    if (secondQuote == -1) {
        return false;
    }

    // 判断鼠标是否落在引号之间
    if (posInLine <= firstQuote || posInLine >= secondQuote) {
        return false;
    }

    includePath = lineText.mid(firstQuote + 1, secondQuote - firstQuote - 1).trimmed();
    if (includePath.isEmpty()) {
        return false;
    }

    // 计算文档中的绝对位置（不包含引号，只选中内容本身）
    startPos = block.position() + firstQuote + 1;
    endPos = block.position() + secondQuote;

    return true;
}

bool MyCodeEditor::tryJumpToIncludeAtPosition(const QPoint& position)
{
    int startPos = -1;
    int endPos = -1;
    QString includePath;
    if (!getIncludeInfoAtPosition(position, startPos, endPos, includePath)) {
        return false;
    }

    return openIncludeFile(includePath);
}

bool MyCodeEditor::openIncludeFile(const QString& includePath)
{
    if (includePath.isEmpty()) {
        return false;
    }

    QString targetPath;

    // 1) 先按当前文件所在目录的相对路径解析
    QString currentFile = getFileName();
    if (!currentFile.isEmpty()) {
        QFileInfo currentInfo(currentFile);
        QString candidate = currentInfo.dir().absoluteFilePath(includePath);
        if (QFileInfo::exists(candidate)) {
            targetPath = candidate;
        }
    }

    // 2) 如果还没找到，并且打开了 workspace，则在 workspace 里搜索
    if (targetPath.isEmpty()) {
        MainWindow *mainWindow = qobject_cast<MainWindow*>(window());
        if (mainWindow && mainWindow->workspaceManager && mainWindow->workspaceManager->isWorkspaceOpen()) {
            QString workspaceRoot = mainWindow->workspaceManager->getWorkspacePath();
            // 先尝试直接拼接
            QString candidate = QDir(workspaceRoot).absoluteFilePath(includePath);
            if (QFileInfo::exists(candidate)) {
                targetPath = candidate;
            } else {
                // 再在 workspace 所有文件中按文件名匹配一次
                const QStringList allFiles = mainWindow->workspaceManager->getAllFiles();
                QFileInfo incInfo(includePath);
                QString incFileName = incInfo.fileName();
                for (const QString& f : allFiles) {
                    if (QFileInfo(f).fileName() == incFileName) {
                        targetPath = f;
                        break;
                    }
                }
            }
        }
    }

    if (targetPath.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Include not found"),
                             tr("Can not locate include file:\n%1").arg(includePath));
        return false;
    }

    MainWindow *mainWindow = qobject_cast<MainWindow*>(window());
    if (!mainWindow || !mainWindow->tabManager) {
        return false;
    }

    return mainWindow->tabManager->openFileInTab(targetPath);
}
