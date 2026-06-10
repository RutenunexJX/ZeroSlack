#include "mycodeeditor.h"
#include "alternatecommandservice.h"
#include "myhighlighter.h"
#include "completionmodel.h"
#include "completionservice.h"
#include "definitionnavigationservice.h"
#include "includenavigationservice.h"

#include "syminfo.h"

#include <QPainter>
#include <QScrollBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>

#include <QKeyEvent>
#include <QMenu>
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
#include <memory>
#include <utility>

MyCodeEditor::MyCodeEditor(QWidget *parent) : QPlainTextEdit(parent)
{
    lineNumberWidget = new LineNumberWidget(this);

    initConnection();
    initFont();
    initHighlighter();
    initAutoComplete();

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
    scopeRefreshTimer = new QTimer(this);
    scopeRefreshTimer->setSingleShot(true);
    connect(scopeRefreshTimer, &QTimer::timeout, this, &MyCodeEditor::highlighCurrentLine);

    // Coalesce cursor/text changes into one selection refresh per event loop.
    auto scheduleHighlightRefresh = [this]() { scopeRefreshTimer->start(0); };
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, scheduleHighlightRefresh);
    connect(this, &QPlainTextEdit::textChanged, this, scheduleHighlightRefresh);

    connect(this, &QPlainTextEdit::textChanged, this, &MyCodeEditor::updateSaveState);
    connect(this, SIGNAL(blockCountChanged(int)), this, SLOT(updateLineNumberWidgetWidth()));
    connect(this, SIGNAL(updateRequest(QRect,int)), this, SLOT(updateLineNumberWidget(QRect,int)));
}

void MyCodeEditor::initFont()
{
    this->setFont(QFont("Consolas",14));
    int tabWidth = fontMetrics().horizontalAdvance(' ') * 4;
    setTabStopDistance(tabWidth);
}

void MyCodeEditor::initHighlighter()
{
    // Seed the live tree with current content, then connect contentsChange BEFORE creating the
    // highlighter so our incremental tree update runs first; the highlighter then reads the fresh
    // tree when QSyntaxHighlighter reformats the changed blocks.
    m_tsdoc.setText(document()->toPlainText());
    connect(document(), &QTextDocument::contentsChange,
            this, &MyCodeEditor::onTsContentsChange);
    m_highlighter = new MyHighlighter(document(), &m_tsdoc);
}

void MyCodeEditor::onTsContentsChange(int position, int charsRemoved, int charsAdded)
{
    // Incrementally update the tree-sitter model (m_tsdoc keeps the pre-edit text, so it can derive
    // the old end point itself). Runs before the highlighter's reformat (connected later).
    m_tsdoc.applyEditChars(position, position + charsRemoved, position + charsAdded,
                           document()->toPlainText());
}

int MyCodeEditor::getLineNumberWidgetWidth()
{
    return 8+QString::number(blockCount()+1).length()*fontMetrics().horizontalAdvance(QChar('0'));
}

void MyCodeEditor::highlighCurrentLine()
{
    // Drop previous scope-background (997) and current-line (998) selections, then re-add only the
    // current-line highlight. The scope-background shading was a debug visualization and has been
    // removed (it forced expensive full-viewport repaints via large full-width ExtraSelections).
    QList<QTextEdit::ExtraSelection> list = extraSelections();
    list.erase(
        std::remove_if(list.begin(), list.end(),
            [](const QTextEdit::ExtraSelection& s) {
                int p = s.format.property(QTextFormat::UserProperty).toInt();
                return p == 997 || p == 998;
            }),
        list.end());

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

void MyCodeEditor::refreshScopeAndCurrentLineHighlight()
{
    highlighCurrentLine();
}

void MyCodeEditor::setAlternateModeEnabled(bool enabled)
{
    isInAlternateMode = enabled;
}

void MyCodeEditor::setIncludePathResolver(
    std::function<QString(const QString& includePath, const QString& currentFile)> resolver)
{
    includePathResolver = std::move(resolver);
}

void MyCodeEditor::setFileOpenHandler(std::function<bool(const QString& filePath)> handler)
{
    fileOpenHandler = std::move(handler);
}

QString MyCodeEditor::currentModuleNameAt(int charPos) const
{
    // Live, error-tolerant enclosing module from the tree-sitter tree (A3). The editor keeps
    // m_tsdoc synced on every edit, so this is never stale (unlike the debounced Slang path).
    return m_tsdoc.enclosingModuleName(charPos < 0 ? 0 : charPos);
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
    std::unique_ptr<QMenu> menu(createStandardContextMenu(event->pos()));
    const QTextCursor cursorAtPos = cursorForPosition(event->pos());
    const QString symbolName = getWordAtTextPosition(cursorAtPos.position());

    menu->addSeparator();
    QAction* findReferencesAction = menu->addAction(QStringLiteral("Find References"));
    findReferencesAction->setEnabled(!symbolName.isEmpty() && !getFileName().isEmpty());
    connect(findReferencesAction, &QAction::triggered, this, [this, cursorAtPos]() {
        emitReferenceSearchForCursor(cursorAtPos);
    });

    QAction* showRelationshipsAction = menu->addAction(QStringLiteral("Show Relationships"));
    showRelationshipsAction->setEnabled(!symbolName.isEmpty() && !getFileName().isEmpty());
    connect(showRelationshipsAction, &QAction::triggered, this, [this, cursorAtPos]() {
        emitRelationshipBrowseForCursor(cursorAtPos);
    });

    menu->exec(event->globalPos());
}

bool MyCodeEditor::emitReferenceSearchForCursor(const QTextCursor& cursor)
{
    const QString symbolName = getWordAtTextPosition(cursor.position());
    if (symbolName.isEmpty() || getFileName().isEmpty())
        return false;

    emit referenceSearchRequested(symbolName,
                                  getFileName(),
                                  currentModuleNameAt(cursor.position()));
    return true;
}

bool MyCodeEditor::emitRelationshipBrowseForCursor(const QTextCursor& cursor)
{
    const QString symbolName = getWordAtTextPosition(cursor.position());
    if (symbolName.isEmpty() || getFileName().isEmpty())
        return false;

    emit relationshipBrowseRequested(symbolName,
                                     getFileName(),
                                     currentModuleNameAt(cursor.position()));
    return true;
}

DefinitionNavigationQuery MyCodeEditor::definitionNavigationQuery(
    const QString& symbolName,
    int cursorPosition) const
{
    DefinitionNavigationQuery query;
    query.symbolName = symbolName;
    query.fileName = getFileName();
    const int semanticPosition = cursorPosition >= 0
        ? cursorPosition
        : textCursor().position();
    query.moduleName = currentModuleNameAt(semanticPosition);

    if (cursorPosition >= 0) {
        QTextBlock block = document()->findBlock(cursorPosition);
        const int posInBlock = cursorPosition - block.position();
        query.linePrefixBeforeCursor = block.text().left(posInBlock);
    }

    return query;
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
    QTextBlock block = document()->findBlockByLineNumber(static_cast<int>(event->position().y()) / fontMetrics().height() + verticalScrollBar()->value());
    setTextCursor(QTextCursor(block));
}

void MyCodeEditor::lineNumberWidgetWheelEvent(QWheelEvent *event)
{
    QPoint angle = event->angleDelta();

    // In Qt6, use angleDelta().y() for vertical scrolling and x() for horizontal.
    // Standard mouse wheels usually provide a y component.
    if (!angle.isNull()) {
        int dy = angle.y();
        int dx = angle.x();

        // Handle vertical scrolling (standard wheel)
        if (dy != 0) {
            verticalScrollBar()->setValue(verticalScrollBar()->value() - dy);
        }
        // Handle horizontal scrolling (if available/supported by input device)
        else if (dx != 0) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - dx);
        }
    }

    event->accept();
}

bool MyCodeEditor::saveFile()
{
    QString fileName;
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
    mFileName = fileName.isEmpty()
        ? QString()
        : QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QString MyCodeEditor::getFileName() const
{
    return mFileName;
}

bool MyCodeEditor::checkSaved()
{
    return isSaved;
}

QString MyCodeEditor::currentModuleName() const
{
    return currentModuleNameAt(textCursor().position());
}

void MyCodeEditor::initAutoComplete()
{
    completionModel = new CompletionModel(this);
    completer = new QCompleter(this);
    completer->setModel(completionModel);
    completer->setWidget(this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setMaxVisibleItems(15);
    autoCompleteTimer = new QTimer(this);
    autoCompleteTimer->setSingleShot(true);
    autoCompleteTimer->setInterval(0);

    relationshipAnalysisDebounceTimer = new QTimer(this);
    relationshipAnalysisDebounceTimer->setSingleShot(true);
    relationshipAnalysisDebounceTimer->setInterval(RelationshipAnalysisDebounceMs);
    connect(relationshipAnalysisDebounceTimer, &QTimer::timeout, this, [this]() {
        if (!getFileName().isEmpty())
            emit relationshipAnalysisRequested(getFileName(), toPlainText());
    });
    connect(autoCompleteTimer, &QTimer::timeout, this, &MyCodeEditor::onAutoCompleteTimer);
    connect(completer, QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, &MyCodeEditor::onCompletionActivated);

    connect(this, &QPlainTextEdit::textChanged, this, &MyCodeEditor::onTextChanged);
}

void MyCodeEditor::onTextChanged()
{
    updateSaveState();

    const bool skipRelationshipAnalysis = m_lastEditWasWhitespaceInsertion;
    m_lastEditWasWhitespaceInsertion = false;

    // Plain whitespace edits (spaces/tabs/newlines) cannot change symbol relationships, but the
    // old path still queued a delayed full-document toPlainText() copy on large files.
    if (!skipRelationshipAnalysis && !getFileName().isEmpty()) {
        relationshipAnalysisDebounceTimer->stop();
        relationshipAnalysisDebounceTimer->start();
    }

    autoCompleteTimer->stop();

    QTextCursor cursor = textCursor();
    QTextBlock currentBlock = cursor.block();
    QString lineText = currentBlock.text();
    int positionInLine = cursor.position() - currentBlock.position();
    QString lineUpToCursor = lineText.left(positionInLine);

    checkForCustomCommand(lineUpToCursor);

    CompletionTriggerQuery triggerQuery;
    triggerQuery.lineUpToCursor = lineUpToCursor;
    triggerQuery.moduleName = currentModuleNameAt(cursor.position() - 1);
    triggerQuery.commandModeActive = isInCustomCommandMode;
    const bool shouldContinueAutoComplete =
        CompletionService::getInstance()->shouldContinueCompletion(triggerQuery);

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
    QTextCursor cursor = textCursor();
    int cursorPosition = cursor.position();
    CompletionQuery query;
    query.prefix = prefix;
    query.fileName = getFileName();
    query.moduleName = currentModuleNameAt(cursorPosition);
    query.cursorLine = cursor.block().blockNumber() + 1;
    query.cursorPosition = cursorPosition;
    return CompletionService::getInstance()->findCompletionResult(query).names;
}

bool MyCodeEditor::isInCommentArea()
{
    QTextCursor cursor = textCursor();
    const int position = cursor.position();
    return m_tsdoc.isCommentAt(position)
        || (position > 0 && m_tsdoc.isCommentAt(position - 1));
}

void MyCodeEditor::onCompletionActivated(const QModelIndex &index)
{
    CompletionModel::CompletionItem item = completionModel->getItem(index);

    if (!completionModel->isSelectableIndex(index)) {
        return;
    }

    QTextCursor cursor = textCursor();

    if (isInAlternateMode) {
        executeAlternateModeCommand(item.text);
        return;
    }

    if (isInCustomCommandMode) {
        QString actualCompletion;

        if (!item.defaultValue.isEmpty()) {
            actualCompletion = item.defaultValue;
        } else {
            actualCompletion = item.text;
        }

        cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
        cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        cursor.insertText(actualCompletion);

        isInCustomCommandMode = false;
        clearCommandHighlight();
    } else {
        cursor.setPosition(wordStartPos);
        cursor.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
        cursor.insertText(item.text);
    }

    hideAutoComplete();
}

void MyCodeEditor::keyPressEvent(QKeyEvent *event)
{
    m_lastEditWasWhitespaceInsertion = false;

    if (event->key() == Qt::Key_Control && !ctrlPressed) {
        ctrlPressed = true;

        QPoint mousePos = mapFromGlobal(QCursor::pos());
        if (rect().contains(mousePos)) {
            clearHoveredSymbolHighlight();

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
                        hoveredWord.clear();
                        viewport()->setCursor(createNonJumpableCursor());
                    }
                }
            }
        }
    }

    if (event->key() == Qt::Key_F12
        && (event->modifiers() & Qt::ShiftModifier)) {
        emitReferenceSearchForCursor(textCursor());
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_R
        && (event->modifiers() & Qt::ControlModifier)
        && (event->modifiers() & Qt::ShiftModifier)) {
        emitRelationshipBrowseForCursor(textCursor());
        event->accept();
        return;
    }

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
                QModelIndex currentIndex = completer->popup()->currentIndex();
                if (!currentIndex.isValid() && completionModel->rowCount() > 0) {
                    currentIndex = completionModel->firstSelectableIndex();
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

    const Qt::KeyboardModifiers semanticNeutralModifiers =
        Qt::ShiftModifier | Qt::KeypadModifier;
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~semanticNeutralModifiers;
    const QString insertedText = event->text();
    m_lastEditWasWhitespaceInsertion =
        modifiers == Qt::NoModifier &&
        !insertedText.isEmpty() &&
        insertedText.trimmed().isEmpty();

    QPlainTextEdit::keyPressEvent(event);
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
        if (isInCustomCommandMode) {
            const QModelIndex selectableIndex = completionModel->firstSelectableIndex();
            if (selectableIndex.isValid())
                completer->popup()->setCurrentIndex(selectableIndex);
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

    static int lastLineNumber = -1;
    int currentLineNumber = currentBlock.blockNumber();
    if (currentLineNumber != lastLineNumber) {
        commandModeExitedByDoubleSpace = false;
        lastLineNumber = currentLineNumber;
    }

    if (checkForCustomCommand(lineUpToCursor)) {
        if (commandModeExitedByDoubleSpace) {
            return;
        }

        if (isConsecutiveSpaces()) {
            clearCommandHighlight();
            isInCustomCommandMode = false;
            commandModeExitedByDoubleSpace = true;
            if (completer->popup()->isVisible()) {
                completer->popup()->hide();
            }
            return;
        }

        highlightCommandText();
        QString commandInput = extractCommandInput().trimmed();

        int cursorPosition = cursor.position();
        CommandCompletionQuery query;
        query.prefix = commandInput;
        query.fileName = getFileName();
        query.moduleName = currentModuleNameAt(cursorPosition);
        query.documentText = document()->toPlainText();
        query.symbolType = currentCommandType;

        const QList<sym_list::SymbolInfo> filteredSymbols =
            CompletionService::getInstance()->findCommandCompletionSymbols(query);
        if (filteredSymbols.isEmpty()
            && (currentCommandType == sym_list::sym_packed_struct_var
                || currentCommandType == sym_list::sym_unpacked_struct_var
                || currentCommandType == sym_list::sym_packed_struct
                || currentCommandType == sym_list::sym_unpacked_struct)
            && query.moduleName.isEmpty()) {
            if (completer->popup()->isVisible())
                completer->popup()->hide();
            return;
        }

        completionModel->updateSymbolCompletions(filteredSymbols, commandInput, currentCommandType);
        showAutoComplete();
        return;
    }

    commandModeExitedByDoubleSpace = false;

    clearCommandHighlight();
    isInCustomCommandMode = false;

    if (isInAlternateMode) {
        processAlternateModeInput(lineUpToCursor);
        return;
    }

    QString lineForParse = lineUpToCursor.trimmed();
    QString varName, memberPrefix;
    CompletionService* completionService = CompletionService::getInstance();
    if (completionService->tryParseStructMemberContext(lineForParse, varName, memberPrefix)) {
        QString currentModule = currentModuleNameAt(cursor.position());
        QString structTypeName = completionService->getStructTypeForVariable(varName, currentModule);
        if (!structTypeName.isEmpty()) {
            CompletionQuery query;
            query.prefix = memberPrefix;
            query.fileName = getFileName();
            query.moduleName = currentModule;
            query.structTypeNameForMember = structTypeName;
            query.cursorLine = cursor.block().blockNumber() + 1;
            query.cursorPosition = cursor.position();
            const CompletionResult completion =
                completionService->findCompletionResult(query);
            completionModel->updateCompletions(completion.names,
                                               completion.symbols,
                                               memberPrefix,
                                               CompletionModel::SymbolCompletion);
            wordStartPos = currentBlock.position() + lineUpToCursor.lastIndexOf('.') + 1;
            showAutoComplete();
            return;
        }
    }

    QString prefix = getWordUnderCursor();
    if (prefix.length() >= 1) {
        CompletionQuery query;
        query.prefix = prefix;
        query.fileName = getFileName();
        query.moduleName = currentModuleNameAt(cursor.position());
        query.cursorLine = cursor.block().blockNumber() + 1;
        query.cursorPosition = cursor.position();

        const CompletionResult completion =
            completionService->findCompletionResult(query);
        completionModel->updateCompletions(completion.names,
                                           completion.symbols,
                                           prefix,
                                           CompletionModel::SymbolCompletion);
        showAutoComplete();
    }
}


bool MyCodeEditor::isConsecutiveSpaces()
{
    QTextCursor cursor = textCursor();
    QTextBlock currentBlock = cursor.block();
    QString lineText = currentBlock.text();
    int positionInLine = cursor.position() - currentBlock.position();

    if (positionInLine >= 2) {
        QString lastTwoChars = lineText.mid(positionInLine - 2, 2);
        if (lastTwoChars == "  ") {
            return true;
        }
    }

    return false;
}

QStringList MyCodeEditor::getCommandModeInternalVariables(const QString &prefix)
{
    QTextCursor cursor = textCursor();
    CommandCompletionQuery query;
    query.prefix = prefix;
    query.fileName = getFileName();
    query.moduleName = currentModuleNameAt(cursor.position());
    query.symbolType = currentCommandType;
    return CompletionService::getInstance()->findCommandCompletions(query);
}

void MyCodeEditor::highlightCommandText()
{
    QTextCursor cursor = textCursor();
    QTextBlock currentBlock = cursor.block();
    QString lineText = currentBlock.text();
    int positionInLine = cursor.position() - currentBlock.position();
    QString lineUpToCursor = lineText.left(positionInLine);

    const CommandModeMatch match =
        CompletionService::getInstance()->matchCommandMode(lineUpToCursor);
    const int prefixPos = match.prefixPosition;

    if (prefixPos == -1) return;

    commandStartPosition = currentBlock.position() + prefixPos;
    commandEndPosition = cursor.position();

    QList<QTextEdit::ExtraSelection> extraSelections = this->extraSelections();

    extraSelections.erase(
        std::remove_if(extraSelections.begin(), extraSelections.end(),
            [](const QTextEdit::ExtraSelection &selection) {
                return selection.format.property(QTextFormat::UserProperty).toInt() == 999; // Custom marker
            }),
        extraSelections.end()
    );

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
    cursor.movePosition(QTextCursor::StartOfWord, QTextCursor::MoveAnchor);
    wordStartPos = cursor.position();
    cursor.setPosition(currentPos);
    cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::MoveAnchor);
    cursor.setPosition(wordStartPos, QTextCursor::MoveAnchor);
    cursor.setPosition(currentPos, QTextCursor::KeepAnchor);

    return cursor.selectedText();
}

bool MyCodeEditor::checkForCustomCommand(const QString &lineUpToCursor)
{
    const CommandModeMatch match =
        CompletionService::getInstance()->matchCommandMode(lineUpToCursor);
    if (match.matched) {
        isInCustomCommandMode = true;
        currentCommandType = match.command.symbolType;
        return true;
    }

    isInCustomCommandMode = false;
    currentCommandType = sym_list::sym_user;
    return false;
}

QString MyCodeEditor::extractCommandInput()
{
    QTextCursor cursor = textCursor();
    QTextBlock currentBlock = cursor.block();
    QString lineText = currentBlock.text();
    int positionInLine = cursor.position() - currentBlock.position();
    QString lineUpToCursor = lineText.left(positionInLine);

    const CommandModeMatch match =
        CompletionService::getInstance()->matchCommandMode(lineUpToCursor);
    return match.matched ? match.input : QString();
}

void MyCodeEditor::processAlternateModeInput(const QString &input)
{
    if (!isInAlternateMode) return;

    AlternateCommandService* alternateCommandService =
        AlternateCommandService::getInstance();
    alternateCommandBuffer = alternateCommandService->normalizeCommandInput(input);

    showAlternateModeCommands(alternateCommandBuffer);
}

void MyCodeEditor::showAlternateModeCommands(const QString &filter)
{
    AlternateCommandService* alternateCommandService =
        AlternateCommandService::getInstance();
    completionModel->updateCommandCompletions(
        alternateCommandService->matchingCommands(filter),
        alternateCommandService->normalizeCommandInput(filter));

    if (completionModel->rowCount() > 0) {
        showAutoComplete();
    } else {
    }
}

void MyCodeEditor::executeAlternateModeCommand(const QString &command)
{
    AlternateCommandService* alternateCommandService =
        AlternateCommandService::getInstance();
    QString cmd = alternateCommandService->normalizeCommandInput(command);

    if (cmd == "save") {
        emit saveFileRequested();
    } else if (cmd == "save_as") {
        emit saveFileAsRequested();
    } else if (cmd == "open") {
        emit openFileRequested();
    } else if (cmd == "new") {
        emit newFileRequested();
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
        viewport()->setCursor(Qt::IBeamCursor);
        clearHoveredSymbolHighlight();
        hoveredWord.clear();
    }

    if (event->key() == Qt::Key_Shift) {
        QPlainTextEdit::keyReleaseEvent(event);
        return;
    }

    if (isInAlternateMode) {
        return;
    }

    QPlainTextEdit::keyReleaseEvent(event);
}

void MyCodeEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        if (tryJumpToIncludeAtPosition(event->pos())) {
            event->accept();
            return;
        }

        QString pkgName;
        int pkgStart = -1;
        int pkgEnd = -1;
        if (getPackageNameFromImport(event->pos(), pkgName, pkgStart, pkgEnd)) {
            jumpToDefinition(pkgName);
            event->accept();
            return;
        }

        QString wordUnderCursor = getWordAtPosition(event->pos());
        if (!wordUnderCursor.isEmpty()) {
            QTextCursor cur = cursorForPosition(event->pos());
            jumpToDefinition(wordUnderCursor, cur.position());
            event->accept();
            return;
        }
    }

    QPlainTextEdit::mousePressEvent(event);
}

void MyCodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    bool isCtrlPressed = (event->modifiers() & Qt::ControlModifier);

    if (isCtrlPressed != ctrlPressed) {
        ctrlPressed = isCtrlPressed;

        if (ctrlPressed) {
            clearHoveredSymbolHighlight();

            int incStart = -1;
            int incEnd = -1;
            QString incPath;
            if (getIncludeInfoAtPosition(event->pos(), incStart, incEnd, incPath)) {
                hoveredWord = incPath;
                hoveredWordStartPos = incStart;
                hoveredWordEndPos = incEnd;
                highlightHoveredSymbol(incPath, incStart, incEnd);
                viewport()->setCursor(createJumpableCursor());
            } else {
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
            viewport()->setCursor(Qt::IBeamCursor);
            clearHoveredSymbolHighlight();
            hoveredWord.clear();
        }
    } else if (ctrlPressed) {
        int incStart = -1;
        int incEnd = -1;
        QString incPath;
        bool onInclude = getIncludeInfoAtPosition(event->pos(), incStart, incEnd, incPath);

        if (onInclude) {
            if (hoveredWord != incPath || hoveredWordStartPos != incStart || hoveredWordEndPos != incEnd) {
                clearHoveredSymbolHighlight();
                hoveredWord = incPath;
                hoveredWordStartPos = incStart;
                hoveredWordEndPos = incEnd;
                highlightHoveredSymbol(incPath, incStart, incEnd);
            }
            viewport()->setCursor(createJumpableCursor());
        } else {
            QString pkgName;
            int pkgStart = -1;
            int pkgEnd = -1;
            bool onImport = getPackageNameFromImport(event->pos(), pkgName, pkgStart, pkgEnd);

            if (onImport) {
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

    if (!cursor.atBlockEnd() && !cursor.atBlockStart()) {
        QChar currentChar = document()->characterAt(position);
        if (!currentChar.isLetterOrNumber() && currentChar != '_') {
            return QString();
        }
    }

    cursor.select(QTextCursor::WordUnderCursor);
    QString word = cursor.selectedText();

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

    if (posInLine < packageStartInLine || posInLine >= packageEndInLine) {
        return false;
    }

    packageName = matchedPackageName;
    startPos = block.position() + packageStartInLine;
    endPos = block.position() + packageEndInLine;

    return true;
}


void MyCodeEditor::jumpToDefinition(const QString& symbolName, int cursorPosition)
{
    if (symbolName.isEmpty())
        return;

    const DefinitionNavigationTarget target =
        DefinitionNavigationService::getInstance()->resolveTarget(
            definitionNavigationQuery(symbolName, cursorPosition));
    if (!target.found)
        return;

    if (target.localFile) {
        QTextCursor cursor = textCursor();
        cursor.movePosition(QTextCursor::Start);
        const int downLines = (target.line > 0) ? target.line - 1 : 0;
        const int rightCols = (target.column > 0) ? target.column - 1 : 0;
        cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, downLines);
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, rightCols);
        setTextCursor(cursor);
        centerCursor();
        moveMouseToCursor();
        return;
    }

    emit definitionJumpRequested(target.symbolName, target.fileName, target.line);
}

void MyCodeEditor::moveMouseToCursor()
{
    if (viewport() && viewport()->isVisible()) {
        QCursor::setPos(viewport()->mapToGlobal(cursorRect().center()));
    }
}

void MyCodeEditor::highlightHoveredSymbol(const QString& word, int startPos, int endPos)
{
    if (word.isEmpty() || startPos < 0 || endPos <= startPos) {
        return;
    }

    QTextEdit::ExtraSelection highlight;
    highlight.cursor = textCursor();
    highlight.cursor.setPosition(startPos);
    highlight.cursor.setPosition(endPos, QTextCursor::KeepAnchor);

    highlight.format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    highlight.format.setUnderlineColor(QColor(0, 100, 200));
    highlight.format.setForeground(QColor(0, 100, 200));

    highlight.format.setProperty(QTextFormat::UserProperty + 1, 1001);

    QList<QTextEdit::ExtraSelection> extraSelections = this->extraSelections();

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

    const QString tooltipText =
        DefinitionNavigationService::getInstance()->tooltipText(
            definitionNavigationQuery(symbolName));
    if (tooltipText.isEmpty())
        return;

    QToolTip::showText(mapToGlobal(position), tooltipText, this);
}

bool MyCodeEditor::canJumpToDefinition(const QString& symbolName)
{
    if (symbolName.isEmpty())
        return false;

    return DefinitionNavigationService::getInstance()->canResolveTarget(
        definitionNavigationQuery(symbolName));
}

QCursor MyCodeEditor::createJumpableCursor()
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    if (!painter.isActive()) {
        return QCursor(Qt::PointingHandCursor);
    }

    painter.setRenderHint(QPainter::Antialiasing);

    QPen pen(QColor(0, 255, 0), 4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    painter.drawLine(7, 12, 11, 16);
    painter.drawLine(11, 16, 18, 6);

    painter.end();

    return QCursor(pixmap, 12, 12);
}

QCursor MyCodeEditor::createNonJumpableCursor()
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QPen pen(QColor(255, 0, 0), 3);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);

    painter.drawLine(5, 5, 15, 15);
    painter.drawLine(15, 5, 5, 15);

    return QCursor(pixmap, 10, 10);
}

bool MyCodeEditor::getIncludeInfoAtPosition(const QPoint& position, int &startPos, int &endPos, QString &includePath)
{
    QTextCursor cursor = cursorForPosition(position);
    QTextBlock block = cursor.block();
    const int posInLine = cursor.position() - block.position();
    const IncludeDirectiveTarget includeTarget =
        IncludeNavigationService::getInstance()->includeAtColumn(block.text(), posInLine);
    if (!includeTarget.matched)
        return false;

    includePath = includeTarget.includePath;
    startPos = block.position() + includeTarget.startColumn;
    endPos = block.position() + includeTarget.endColumn;

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

    const QString currentFile = getFileName();
    QString targetPath;
    if (includePathResolver)
        targetPath = includePathResolver(includePath, currentFile);

    if (targetPath.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Include not found"),
                             tr("Can not locate include file:\n%1").arg(includePath));
        return false;
    }

    if (!fileOpenHandler) {
        return false;
    }

    return fileOpenHandler(targetPath);
}
