#include "mycodeeditor.h"
#include "myhighlighter.h"
#include "completionmodel.h"
#include "editorsemanticcontextservice.h"

#include "syminfo.h"

#include <QPainter>
#include <QScrollBar>
#include <QFileInfo>
#include <QDir>

#include <QKeyEvent>
#include <QMenu>
#include <QTextCursor>
#include <QApplication>
#include <QRect>

#include <QAbstractItemView>
#include <QCompleter>
#include <QCursor>
#include <QPixmap>
#include <QPen>
#include <QBrush>
#include <memory>

MyCodeEditor::MyCodeEditor(QWidget *parent) : QPlainTextEdit(parent)
{
    semanticContextService = EditorSemanticContextService::getInstance();
    lineNumberWidget = new LineNumberWidget(this);

    initConnection();
    initFont();
    initHighlighter();
    initAutoComplete();

    highlighCurrentLine();
    updateLineNumberWidgetWidth();

    setLineWrapMode(QPlainTextEdit::NoWrap);

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

void MyCodeEditor::setSemanticContextService(EditorSemanticContextService* service)
{
    semanticContextService = service
        ? service
        : EditorSemanticContextService::getInstance();
}

EditorSemanticContextService* MyCodeEditor::contextService() const
{
    return semanticContextService
        ? semanticContextService
        : EditorSemanticContextService::getInstance();
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

void MyCodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    lineNumberWidget->setGeometry(0, 0, getLineNumberWidgetWidth(), contentsRect().height());
}

void MyCodeEditor::contextMenuEvent(QContextMenuEvent *event)
{
    std::unique_ptr<QMenu> menu(createStandardContextMenu(event->pos()));
    const QTextCursor cursorAtPos = cursorForPosition(event->pos());
    emit sourceSymbolContextMenuRequested(
        menu.get(),
        editorSemanticContextForPosition(cursorAtPos.position()));

    menu->exec(event->globalPos());
}

EditorSemanticContext MyCodeEditor::editorSemanticContextForPosition(
    int cursorPosition,
    bool includeDocumentText) const
{
    const int semanticPosition = cursorPosition >= 0
        ? cursorPosition
        : textCursor().position();

    EditorSemanticContext context;
    context.fileName = getFileName();
    context.moduleName = currentModuleNameAt(semanticPosition);
    if (includeDocumentText)
        context.documentText = document()->toPlainText();
    context.cursorPosition = semanticPosition;

    const QTextBlock block = document()->findBlock(semanticPosition);
    if (block.isValid()) {
        context.lineText = block.text();
        context.column = semanticPosition - block.position();
        context.cursorLine = block.blockNumber() + 1;
        context.lineUpToCursor = context.lineText.left(context.column);
    }

    return context;
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

void MyCodeEditor::setFileName(QString fileName)
{
    const QString normalizedFileName = fileName.isEmpty()
        ? QString()
        : QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
    if (mFileName == normalizedFileName)
        return;

    mFileName = normalizedFileName;
    emit fileNameChanged(mFileName);
}

QString MyCodeEditor::getFileName() const
{
    return mFileName;
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
    connect(autoCompleteTimer, &QTimer::timeout, this, &MyCodeEditor::onAutoCompleteTimer);
    connect(completer, QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, &MyCodeEditor::onCompletionActivated);

    connect(this, &QPlainTextEdit::textChanged, this, &MyCodeEditor::onTextChanged);
}

void MyCodeEditor::onTextChanged()
{
    autoCompleteTimer->stop();

    const QTextCursor cursor = textCursor();
    EditorSemanticContext context =
        editorSemanticContextForPosition(cursor.position());
    context.moduleName = currentModuleNameAt(cursor.position() - 1);
    const EditorCompletionTextChangeState completionState =
        contextService()->completionTextChangeState(context);
    isInCustomCommandMode = completionState.commandModeActive;

    if (completionState.startCompletionTimer) {
        autoCompleteTimer->start();
    } else if (completionState.hidePopup) {
        hideAutoComplete();
    }
}

void MyCodeEditor::hideAutoComplete()
{
    completer->popup()->hide();

    if (isInCustomCommandMode) {
        clearCommandHighlight();
    }
}

void MyCodeEditor::onCompletionActivated(const QModelIndex &index)
{
    CompletionModel::CompletionItem item = completionModel->getItem(index);
    EditorCompletionActivationContext activationContext;
    activationContext.selectable = completionModel->isSelectableIndex(index);
    activationContext.alternateModeActive = isInAlternateMode;
    activationContext.commandModeActive = isInCustomCommandMode;
    activationContext.itemText = item.text;
    activationContext.defaultValue = item.defaultValue;
    const CompletionActivationState activationState =
        contextService()->completionActivationState(activationContext);

    if (activationState.action == CompletionActivationAction::None)
        return;

    QTextCursor cursor = textCursor();

    if (activationState.action
        == CompletionActivationAction::ExecuteAlternateCommand) {
        executeAlternateModeCommand(activationState.text);
        return;
    }

    if (activationState.action == CompletionActivationAction::ReplaceLine) {
        cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
        cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        cursor.insertText(activationState.text);

        if (activationState.clearCommandMode) {
            isInCustomCommandMode = false;
            clearCommandHighlight();
        }
    } else if (activationState.action == CompletionActivationAction::ReplaceWord) {
        cursor.setPosition(wordStartPos);
        cursor.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
        cursor.insertText(activationState.text);
    }

    if (activationState.hidePopup)
        hideAutoComplete();
}

void MyCodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control && !ctrlPressed) {
        ctrlPressed = true;

        QPoint mousePos = mapFromGlobal(QCursor::pos());
        if (rect().contains(mousePos)) {
            applySourceNavigationHover(sourceNavigationTargetAtPosition(mousePos));
        }
    }

    EditorSourceSymbolShortcutContext sourceShortcutContext;
    sourceShortcutContext.key = event->key();
    sourceShortcutContext.modifiers = int(event->modifiers());
    sourceShortcutContext.semanticContext =
        editorSemanticContextForPosition(textCursor().position());
    const EditorSourceSymbolShortcutState sourceShortcutState =
        contextService()->sourceSymbolShortcutState(sourceShortcutContext);
    if (sourceShortcutState.matched) {
        emit sourceSymbolActionRequested(
            sourceShortcutState.action,
            sourceShortcutState.semanticContext);
        if (sourceShortcutState.acceptEvent)
            event->accept();
        return;
    }

    if (event->key() == Qt::Key_Shift) {
        event->ignore();
        return;
    }

    if (isInAlternateMode) {
        if (handleCompletionPopupKey(event))
            return;

        EditorAlternateModeKeyContext alternateKeyContext;
        alternateKeyContext.key = event->key();
        alternateKeyContext.text = event->text();
        alternateKeyContext.buffer = alternateCommandBuffer;
        const EditorAlternateModeKeyState alternateKeyState =
            contextService()->alternateModeKeyState(alternateKeyContext);

        switch (alternateKeyState.action) {
        case EditorAlternateModeKeyAction::UpdateInput:
        case EditorAlternateModeKeyAction::RefreshCompletions:
            applyAlternateModeCompletionDisplayState(
                alternateKeyState.completion);
            break;
        case EditorAlternateModeKeyAction::ExecuteCommand:
            executeAlternateModeCommand(alternateKeyState.command);
            break;
        case EditorAlternateModeKeyAction::ClearAndHide:
            if (alternateKeyState.hidePopup)
                hideAutoComplete();
            if (alternateKeyState.clearBuffer)
                clearAlternateModeBuffer();
            break;
        case EditorAlternateModeKeyAction::Consume:
            break;
        }

        return;
    }

    if (handleCompletionPopupKey(event))
        return;

    QPlainTextEdit::keyPressEvent(event);
}

bool MyCodeEditor::handleCompletionPopupKey(QKeyEvent *event)
{
    if (!completer->popup()->isVisible())
        return false;

    EditorCompletionPopupKeyContext query;
    query.key = event->key();
    query.alternateModeActive = isInAlternateMode;
    query.commandModeActive = isInCustomCommandMode;
    query.currentIndexValid = completer->popup()->currentIndex().isValid();
    query.hasRows = completionModel->rowCount() > 0;
    query.alternateBufferEmpty = alternateCommandBuffer.isEmpty();

    const CompletionPopupKeyState state =
        contextService()->completionPopupKeyState(query);

    switch (state.action) {
    case CompletionPopupKeyAction::ForwardToPopup:
        QApplication::sendEvent(completer->popup(), event);
        return true;
    case CompletionPopupKeyAction::ActivateCurrent:
        if (completer->popup()->currentIndex().isValid())
            emit completer->activated(completer->popup()->currentIndex());
        return true;
    case CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable:
        {
            QModelIndex currentIndex = completer->popup()->currentIndex();
            if (!currentIndex.isValid() && completionModel->rowCount() > 0)
                currentIndex = completionModel->firstSelectableIndex();
            if (currentIndex.isValid())
                emit completer->activated(currentIndex);
        }
        return true;
    case CompletionPopupKeyAction::HidePopup:
        hideAutoComplete();
        return true;
    case CompletionPopupKeyAction::HidePopupAndClearAlternate:
        hideAutoComplete();
        clearAlternateModeBuffer();
        return true;
    case CompletionPopupKeyAction::BackspaceAlternateInput:
        if (!alternateCommandBuffer.isEmpty()) {
            const EditorAlternateModeCompletionDisplayState completionState =
                contextService()->alternateModeCompletionDisplayState(
                    alternateCommandBuffer.left(
                        alternateCommandBuffer.size() - 1));
            applyAlternateModeCompletionDisplayState(completionState);
        } else {
            hideAutoComplete();
        }
        return true;
    case CompletionPopupKeyAction::Consume:
        return true;
    case CompletionPopupKeyAction::None:
        return false;
    }

    return false;
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
    const QTextCursor cursor = textCursor();
    const QTextBlock currentBlock = cursor.block();

    static int lastLineNumber = -1;
    int currentLineNumber = currentBlock.blockNumber();
    if (currentLineNumber != lastLineNumber) {
        commandModeExitedByDoubleSpace = false;
        lastLineNumber = currentLineNumber;
    }

    EditorSemanticContext context =
        editorSemanticContextForPosition(cursor.position(), true);

    const EditorCommandModeCompletionRefreshState commandState =
        contextService()->commandModeCompletionRefreshState(
            context,
            commandModeExitedByDoubleSpace);
    if (commandState.matched) {
        isInCustomCommandMode = commandState.commandModeActive;
        if (commandState.suppressAfterExit) {
            return;
        }

        if (commandState.exitRequested) {
            if (commandState.clearCommandHighlight)
                clearCommandHighlight();
            if (commandState.markExitedByDoubleSpace)
                commandModeExitedByDoubleSpace = true;
            if (completer->popup()->isVisible()) {
                completer->popup()->hide();
            }
            return;
        }

        if (commandState.highlightCommand)
            highlightCommandText(commandState.completion.prefixPosition);

        if (commandState.hidePopup) {
            if (completer->popup()->isVisible())
                completer->popup()->hide();
            return;
        }

        if (commandState.showCompletions) {
            completionModel->updateSymbolCompletions(
                commandState.completion.symbols,
                commandState.completion.completionPrefix,
                commandState.completion.command.symbolType);
            showAutoComplete();
        }
        return;
    }

    if (commandState.resetExitedByDoubleSpace)
        commandModeExitedByDoubleSpace = false;

    clearCommandHighlight();
    isInCustomCommandMode = false;

    if (isInAlternateMode) {
        processAlternateModeInput(context.lineUpToCursor);
        return;
    }

    context.wordPrefix = getWordUnderCursor();
    const EditorCompletionState completionState =
        contextService()->editorCompletionState(context);
    if (completionState.available) {
        completionModel->updateCompletions(completionState.completion.names,
                                           completionState.completion.symbols,
                                           completionState.prefix,
                                           CompletionModel::SymbolCompletion);
        wordStartPos =
            currentBlock.position() + completionState.replacementStartColumn;
        showAutoComplete();
    }
}

void MyCodeEditor::highlightCommandText(int prefixPosition)
{
    if (prefixPosition < 0)
        return;

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

    QTextCursor commandCursor = textCursor();
    const int commandStartPosition = commandCursor.block().position() + prefixPosition;
    commandCursor.setPosition(commandStartPosition);
    commandCursor.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
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

void MyCodeEditor::processAlternateModeInput(const QString &input)
{
    if (!isInAlternateMode) return;

    const EditorAlternateModeCompletionDisplayState completionState =
        contextService()->alternateModeCompletionDisplayState(input);
    applyAlternateModeCompletionDisplayState(completionState);
}

void MyCodeEditor::applyAlternateModeCompletionDisplayState(
    const EditorAlternateModeCompletionDisplayState& state)
{
    if (!state.updateCompletions)
        return;

    alternateCommandBuffer = state.normalizedInput;
    completionModel->updateCommandCompletions(
        state.matches,
        state.normalizedInput);

    if (state.showPopup) {
        showAutoComplete();
    }
}

void MyCodeEditor::executeAlternateModeCommand(const QString &command)
{
    if (!command.trimmed().isEmpty())
        emit alternateCommandRequested(command);
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
        clearSourceNavigationHover();
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
        const EditorSourceNavigationTarget target =
            sourceNavigationTargetAtPosition(event->pos());
        emit sourceNavigationRequested(
            target,
            editorSemanticContextForPosition(target.cursorPosition));

        if (target.matched && !target.text.isEmpty()) {
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
            applySourceNavigationHover(sourceNavigationTargetAtPosition(event->pos()));
        } else {
            clearSourceNavigationHover();
        }
    } else if (ctrlPressed) {
        applySourceNavigationHover(sourceNavigationTargetAtPosition(event->pos()));
    }

    QPlainTextEdit::mouseMoveEvent(event);
}

void MyCodeEditor::leaveEvent(QEvent *event)
{
    ctrlPressed = false;
    clearSourceNavigationHover();

    QPlainTextEdit::leaveEvent(event);
}

EditorSourceNavigationTarget
MyCodeEditor::sourceNavigationTargetAtPosition(const QPoint& position)
{
    QTextCursor cursor = cursorForPosition(position);
    QTextBlock block = cursor.block();
    if (!block.isValid())
        return {};

    return contextService()->editorSourceNavigationTarget(
            editorSemanticContextForPosition(cursor.position()),
            block.position());
}

void MyCodeEditor::applySourceNavigationHover(
    const EditorSourceNavigationTarget& target)
{
    if (!target.matched) {
        clearSourceNavigationHover();
        viewport()->setCursor(createNonJumpableCursor());
        return;
    }

    if (hoveredWord != target.text
        || hoveredWordStartPos != target.startPos
        || hoveredWordEndPos != target.endPos) {
        clearHoveredSymbolHighlight();
        hoveredWord = target.text;
        hoveredWordStartPos = target.startPos;
        hoveredWordEndPos = target.endPos;
        highlightHoveredSymbol(target.text, target.startPos, target.endPos);
    }

    viewport()->setCursor(
        target.jumpable ? createJumpableCursor() : createNonJumpableCursor());
}

void MyCodeEditor::clearSourceNavigationHover()
{
    viewport()->setCursor(Qt::IBeamCursor);
    clearHoveredSymbolHighlight();
    hoveredWord.clear();
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
