#include "mycodeeditor.h"
#include "myhighlighter.h"
#include "completionmodel.h"
#include "editorsemanticcontextservice.h"
#include "sourcenavigationservice.h"
#include "tsdocument.h"

#include "syminfo.h"

#include <QPainter>
#include <QScrollBar>
#include <QFileInfo>
#include <QDir>

#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QTextCursor>
#include <QApplication>
#include <QRect>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include <QAbstractItemView>
#include <QCompleter>
#include <QCursor>
#include <QPixmap>
#include <QPen>
#include <QBrush>
#include <memory>

namespace {
constexpr int kPrimarySelectionProperty = QTextFormat::UserProperty;
constexpr int kScopeBackgroundSelectionMarker = 997;
constexpr int kCurrentLineSelectionMarker = 998;
constexpr int kCommandSelectionProperty = kPrimarySelectionProperty;
constexpr int kCommandSelectionMarker = 999;
constexpr int kHoveredSymbolSelectionProperty = QTextFormat::UserProperty + 1;
constexpr int kHoveredSymbolSelectionMarker = 1001;

void removeSelectionsByProperty(
    QList<QTextEdit::ExtraSelection>& selections,
    int property,
    int value)
{
    selections.erase(
        std::remove_if(selections.begin(), selections.end(),
            [property, value](const QTextEdit::ExtraSelection& selection) {
                return selection.format.property(property).toInt() == value;
            }),
        selections.end());
}
}

class LineNumberWidget : public QWidget
{
public:
    explicit LineNumberWidget(MyCodeEditor *editor = nullptr)
        : QWidget(editor)
        , codeEditor(editor)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        codeEditor->lineNumberWidgetPaintEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        codeEditor->lineNumberWidgetMousePressEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        codeEditor->lineNumberWidgetWheelEvent(event);
    }

private:
    MyCodeEditor *codeEditor = nullptr;
};

struct MyCodeEditorState
{
    LineNumberWidget *lineNumberWidget = nullptr;
    QString fileName;
    std::unique_ptr<TSDocument> tsdoc;
    MyHighlighter *highlighter = nullptr;

    struct CompletionUi {
        QCompleter *completer = nullptr;
        CompletionModel *model = nullptr;
        QTimer *timer = nullptr;
        int wordStartPos = 0;

        void init(MyCodeEditor* editor)
        {
            model = new CompletionModel(editor);
            completer = new QCompleter(editor);
            completer->setModel(model);
            completer->setWidget(editor);
            completer->setCompletionMode(QCompleter::PopupCompletion);
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            completer->setMaxVisibleItems(15);
            timer = new QTimer(editor);
            timer->setSingleShot(true);
            timer->setInterval(0);
        }

        QAbstractItemView* popup() const { return completer->popup(); }
        bool popupVisible() const { return popup()->isVisible(); }
        void hidePopup() const { popup()->hide(); }
        void startTimer() const { timer->start(); }
        void stopTimer() const { timer->stop(); }
        int rowCount() const { return model->rowCount(); }
        bool hasRows() const { return rowCount() > 0; }
        QModelIndex currentIndex() const { return popup()->currentIndex(); }
        QModelIndex firstSelectableIndex() const
        {
            return model->firstSelectableIndex();
        }
        void activateIndex(const QModelIndex& index) const
        {
            emit completer->activated(index);
        }
        void complete(const QRect& rect) const { completer->complete(rect); }
    } completion;

    EditorSemanticContextService* semanticContextService = nullptr;

    // Coalesce current-line selection refresh after cursor/text changes.
    QTimer *scopeRefreshTimer = nullptr;

    struct ModeState {
        bool commandModeActive = false;
        bool alternateModeActive = false;
        QString alternateBuffer;
        bool commandModeExitedByDoubleSpace = false;

        void clearAlternateBuffer()
        {
            alternateBuffer.clear();
        }
    } modes;

    struct SourceNavigationHover {
        bool ctrlPressed = false;
        QString word;
        int startPos = -1;
        int endPos = -1;

        bool matches(const EditorSourceNavigationTarget& target) const
        {
            return word == target.text
                && startPos == target.startPos
                && endPos == target.endPos;
        }

        void setTarget(const EditorSourceNavigationTarget& target)
        {
            word = target.text;
            startPos = target.startPos;
            endPos = target.endPos;
        }

        void clearTarget()
        {
            word.clear();
            clearRange();
        }

        void clearRange()
        {
            startPos = -1;
            endPos = -1;
        }
    } sourceHover;

};

MyCodeEditor::MyCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , state(std::make_unique<MyCodeEditorState>())
{
    state->semanticContextService = EditorSemanticContextService::getInstance();
    state->tsdoc = std::make_unique<TSDocument>();
    state->lineNumberWidget = new LineNumberWidget(this);

    initConnection();
    initFont();
    initHighlighter();
    initAutoComplete();

    highlightCurrentLine();
    updateLineNumberWidgetWidth();

    setLineWrapMode(QPlainTextEdit::NoWrap);

    state->fileName = "";

    setMouseTracking(true);
}

MyCodeEditor::~MyCodeEditor()
{
    delete state->lineNumberWidget;
}

void MyCodeEditor::initConnection()
{
    state->scopeRefreshTimer = new QTimer(this);
    state->scopeRefreshTimer->setSingleShot(true);
    connect(state->scopeRefreshTimer, &QTimer::timeout, this, &MyCodeEditor::highlightCurrentLine);

    // Coalesce cursor/text changes into one selection refresh per event loop.
    auto scheduleHighlightRefresh = [this]() { state->scopeRefreshTimer->start(0); };
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
    syncTreeSitterDocumentText();
    connect(document(), &QTextDocument::contentsChange,
            this, &MyCodeEditor::onTsContentsChange);
    state->highlighter = new MyHighlighter(document(), state->tsdoc.get());
}

void MyCodeEditor::onTsContentsChange(int position, int charsRemoved, int charsAdded)
{
    // Incrementally update the tree-sitter model (state->tsdoc keeps the pre-edit text, so it can derive
    // the old end point itself). Runs before the highlighter's reformat (connected later).
    applyTreeSitterEdit(position, charsRemoved, charsAdded);
}

void MyCodeEditor::syncTreeSitterDocumentText()
{
    state->tsdoc->setText(document()->toPlainText());
}

void MyCodeEditor::applyTreeSitterEdit(int position,
                                       int charsRemoved,
                                       int charsAdded)
{
    state->tsdoc->applyEditChars(position,
                            position + charsRemoved,
                            position + charsAdded,
                            document()->toPlainText());
}

QString MyCodeEditor::treeSitterModuleNameAt(int charPos) const
{
    return state->tsdoc->enclosingModuleName(charPos < 0 ? 0 : charPos);
}

int MyCodeEditor::getLineNumberWidgetWidth()
{
    return 8+QString::number(blockCount()+1).length()*fontMetrics().horizontalAdvance(QChar('0'));
}

void MyCodeEditor::highlightCurrentLine()
{
    // Drop previous scope-background (997) and current-line (998) selections, then re-add only the
    // current-line highlight. The scope-background shading was a debug visualization and has been
    // removed (it forced expensive full-viewport repaints via large full-width ExtraSelections).
    QList<QTextEdit::ExtraSelection> list = extraSelections();
    list.erase(
        std::remove_if(list.begin(), list.end(),
            [](const QTextEdit::ExtraSelection& s) {
                int p = s.format.property(kPrimarySelectionProperty).toInt();
                return p == kScopeBackgroundSelectionMarker
                    || p == kCurrentLineSelectionMarker;
            }),
        list.end());

    QTextEdit::ExtraSelection currentLine;
    currentLine.format.setBackground(QColor(0,100,100,20));
    currentLine.format.setProperty(QTextFormat::FullWidthSelection, true);
    currentLine.format.setProperty(
        kPrimarySelectionProperty,
        kCurrentLineSelectionMarker);
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
    highlightCurrentLine();
}

void MyCodeEditor::setAlternateModeEnabled(bool enabled)
{
    state->modes.alternateModeActive = enabled;
}

void MyCodeEditor::setSemanticContextService(EditorSemanticContextService* service)
{
    state->semanticContextService = service
        ? service
        : EditorSemanticContextService::getInstance();
}

EditorSemanticContextService* MyCodeEditor::contextService() const
{
    return state->semanticContextService
        ? state->semanticContextService
        : EditorSemanticContextService::getInstance();
}

QString MyCodeEditor::currentModuleNameAt(int charPos) const
{
    // Live, error-tolerant enclosing module from the tree-sitter tree (A3). The editor keeps
    // state->tsdoc synced on every edit, so this is never stale (unlike the debounced Slang path).
    return treeSitterModuleNameAt(charPos);
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
        state->lineNumberWidget->scroll(0,dy);
    else
        state->lineNumberWidget->update(0,rect.y(),getLineNumberWidgetWidth(),rect.height());
}

void MyCodeEditor::updateLineNumberWidgetWidth()
{
    setViewportMargins(getLineNumberWidgetWidth(),0,0,0);
}

void MyCodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    state->lineNumberWidget->setGeometry(0, 0, getLineNumberWidgetWidth(), contentsRect().height());
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

EditorSemanticContext MyCodeEditor::semanticContextForCursor(
    const QTextCursor& cursor,
    bool includeDocumentText) const
{
    return editorSemanticContextForPosition(cursor.position(), includeDocumentText);
}

void MyCodeEditor::lineNumberWidgetPaintEvent(QPaintEvent *event)
{
    QPainter painter(state->lineNumberWidget);
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
    if (state->fileName == normalizedFileName)
        return;

    state->fileName = normalizedFileName;
    emit fileNameChanged(state->fileName);
}

QString MyCodeEditor::getFileName() const
{
    return state->fileName;
}

QString MyCodeEditor::currentModuleName() const
{
    return currentModuleNameAt(textCursor().position());
}

void MyCodeEditor::initAutoComplete()
{
    state->completion.init(this);
    connect(state->completion.timer, &QTimer::timeout, this, &MyCodeEditor::onAutoCompleteTimer);
    connect(state->completion.completer, QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, &MyCodeEditor::onCompletionActivated);

    connect(this, &QPlainTextEdit::textChanged, this, &MyCodeEditor::onTextChanged);
}

void MyCodeEditor::onTextChanged()
{
    state->completion.stopTimer();
    updateCompletionTriggerForTextChange(textCursor());
}

void MyCodeEditor::updateCompletionTriggerForTextChange(const QTextCursor& cursor)
{
    EditorSemanticContext context =
        semanticContextForCursor(cursor);
    context.moduleName = currentModuleNameAt(cursor.position() - 1);
    const EditorCompletionTextChangeState completionState =
        contextService()->completionTextChangeState(context);
    state->modes.commandModeActive = completionState.commandModeActive;

    if (completionState.startCompletionTimer) {
        state->completion.startTimer();
    } else if (completionState.hidePopup) {
        hideAutoComplete();
    }
}

void MyCodeEditor::hideAutoComplete()
{
    state->completion.hidePopup();

    if (state->modes.commandModeActive) {
        clearCommandHighlight();
    }
}

void MyCodeEditor::onCompletionActivated(const QModelIndex &index)
{
    CompletionModel::CompletionItem item = state->completion.model->getItem(index);
    EditorCompletionActivationContext activationContext;
    activationContext.selectable = state->completion.model->isSelectableIndex(index);
    activationContext.alternateModeActive = state->modes.alternateModeActive;
    activationContext.commandModeActive = state->modes.commandModeActive;
    activationContext.itemText = item.text;
    activationContext.defaultValue = item.defaultValue;
    const CompletionActivationState activationState =
        contextService()->completionActivationState(activationContext);
    applyCompletionActivationState(activationState);
}

void MyCodeEditor::applyCompletionActivationState(
    const CompletionActivationState& activationState)
{
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
            state->modes.commandModeActive = false;
            clearCommandHighlight();
        }
    } else if (activationState.action == CompletionActivationAction::ReplaceWord) {
        cursor.setPosition(state->completion.wordStartPos);
        cursor.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
        cursor.insertText(activationState.text);
    }

    if (activationState.hidePopup)
        hideAutoComplete();
}

void MyCodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control && !state->sourceHover.ctrlPressed) {
        state->sourceHover.ctrlPressed = true;

        QPoint mousePos = mapFromGlobal(QCursor::pos());
        if (rect().contains(mousePos)) {
            refreshSourceNavigationHoverAt(mousePos);
        }
    }

    if (handleSourceSymbolShortcut(event))
        return;

    if (event->key() == Qt::Key_Shift) {
        event->ignore();
        return;
    }

    if (state->modes.alternateModeActive) {
        handleAlternateModeKey(event);
        return;
    }

    if (handleCompletionPopupKey(event))
        return;

    QPlainTextEdit::keyPressEvent(event);
}

bool MyCodeEditor::handleSourceSymbolShortcut(QKeyEvent *event)
{
    EditorSourceSymbolShortcutContext sourceShortcutContext;
    sourceShortcutContext.key = event->key();
    sourceShortcutContext.modifiers = int(event->modifiers());
    sourceShortcutContext.semanticContext =
        editorSemanticContextForPosition(textCursor().position());
    const EditorSourceSymbolShortcutState sourceShortcutState =
        contextService()->sourceSymbolShortcutState(sourceShortcutContext);
    if (!sourceShortcutState.matched)
        return false;

    emit sourceSymbolActionRequested(
        sourceShortcutState.action,
        sourceShortcutState.semanticContext);
    if (sourceShortcutState.acceptEvent)
        event->accept();
    return true;
}

bool MyCodeEditor::handleAlternateModeKey(QKeyEvent *event)
{
    if (handleCompletionPopupKey(event))
        return true;

    EditorAlternateModeKeyContext alternateKeyContext;
    alternateKeyContext.key = event->key();
    alternateKeyContext.text = event->text();
    alternateKeyContext.buffer = state->modes.alternateBuffer;
    const EditorAlternateModeKeyState alternateKeyState =
        contextService()->alternateModeKeyState(alternateKeyContext);
    applyAlternateModeKeyState(alternateKeyState);
    return true;
}

void MyCodeEditor::applyAlternateModeKeyState(
    const EditorAlternateModeKeyState& state)
{
    switch (state.action) {
    case EditorAlternateModeKeyAction::UpdateInput:
    case EditorAlternateModeKeyAction::RefreshCompletions:
        applyAlternateModeCompletionDisplayState(state.completion);
        break;
    case EditorAlternateModeKeyAction::ExecuteCommand:
        executeAlternateModeCommand(state.command);
        break;
    case EditorAlternateModeKeyAction::ClearAndHide:
        if (state.hidePopup)
            hideAutoComplete();
        if (state.clearBuffer)
            clearAlternateModeBuffer();
        break;
    case EditorAlternateModeKeyAction::Consume:
        break;
    }
}

bool MyCodeEditor::handleCompletionPopupKey(QKeyEvent *event)
{
    if (!state->completion.popupVisible())
        return false;

    const CompletionPopupKeyState popupState =
        contextService()->completionPopupKeyState(
            completionPopupKeyContextForEvent(event));
    return applyCompletionPopupKeyState(event, popupState);
}

EditorCompletionPopupKeyContext
MyCodeEditor::completionPopupKeyContextForEvent(QKeyEvent *event) const
{
    EditorCompletionPopupKeyContext query;
    query.key = event->key();
    query.alternateModeActive = state->modes.alternateModeActive;
    query.commandModeActive = state->modes.commandModeActive;
    query.currentIndexValid = state->completion.currentIndex().isValid();
    query.hasRows = state->completion.hasRows();
    query.alternateBufferEmpty = state->modes.alternateBuffer.isEmpty();
    return query;
}

bool MyCodeEditor::applyCompletionPopupKeyState(
    QKeyEvent *event,
    const CompletionPopupKeyState& popupState)
{
    switch (popupState.action) {
    case CompletionPopupKeyAction::ForwardToPopup:
        QApplication::sendEvent(state->completion.popup(), event);
        return true;
    case CompletionPopupKeyAction::ActivateCurrent:
        if (state->completion.currentIndex().isValid())
            state->completion.activateIndex(state->completion.currentIndex());
        return true;
    case CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable:
        {
            QModelIndex currentIndex = state->completion.currentIndex();
            if (!currentIndex.isValid() && state->completion.hasRows())
                currentIndex = state->completion.firstSelectableIndex();
            if (currentIndex.isValid())
                state->completion.activateIndex(currentIndex);
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
        if (!state->modes.alternateBuffer.isEmpty()) {
            const EditorAlternateModeCompletionDisplayState completionState =
                contextService()->alternateModeCompletionDisplayState(
                    state->modes.alternateBuffer.left(
                        state->modes.alternateBuffer.size() - 1));
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
    if (state->completion.hasRows()) {
        QTextCursor cursor = textCursor();
        QRect rect = cursorRect(cursor);
        rect.setWidth(state->completion.popup()->sizeHintForColumn(0) + 20);
        if (state->modes.commandModeActive) {
            const QModelIndex selectableIndex =
                state->completion.firstSelectableIndex();
            if (selectableIndex.isValid())
                state->completion.popup()->setCurrentIndex(selectableIndex);
        }

        state->completion.complete(rect);
    }
}

void MyCodeEditor::onAutoCompleteTimer()
{
    const QTextCursor cursor = textCursor();
    const QTextBlock currentBlock = cursor.block();

    static int lastLineNumber = -1;
    int currentLineNumber = currentBlock.blockNumber();
    if (currentLineNumber != lastLineNumber) {
        state->modes.commandModeExitedByDoubleSpace = false;
        lastLineNumber = currentLineNumber;
    }

    EditorSemanticContext context =
        semanticContextForCursor(cursor, true);

    if (refreshCommandModeCompletion(context))
        return;

    if (state->modes.alternateModeActive) {
        processAlternateModeInput(context.lineUpToCursor);
        return;
    }

    refreshSymbolCompletion(context, currentBlock);
}

bool MyCodeEditor::refreshCommandModeCompletion(
    const EditorSemanticContext& context)
{
    const EditorCommandModeCompletionRefreshState commandState =
        contextService()->commandModeCompletionRefreshState(
            context,
            state->modes.commandModeExitedByDoubleSpace);
    if (commandState.matched) {
        state->modes.commandModeActive = commandState.commandModeActive;
        if (commandState.suppressAfterExit) {
            return true;
        }

        if (commandState.exitRequested) {
            if (commandState.clearCommandHighlight)
                clearCommandHighlight();
            if (commandState.markExitedByDoubleSpace)
                state->modes.commandModeExitedByDoubleSpace = true;
            if (state->completion.popupVisible()) {
                state->completion.hidePopup();
            }
            return true;
        }

        if (commandState.highlightCommand)
            highlightCommandText(commandState.completion.prefixPosition);

        if (commandState.hidePopup) {
            if (state->completion.popupVisible())
                state->completion.hidePopup();
            return true;
        }

        if (commandState.showCompletions) {
            state->completion.model->updateSymbolCompletions(
                commandState.completion.symbols,
                commandState.completion.completionPrefix,
                commandState.completion.command.symbolType);
            showAutoComplete();
        }
        return true;
    }

    if (commandState.resetExitedByDoubleSpace)
        state->modes.commandModeExitedByDoubleSpace = false;

    clearCommandHighlight();
    state->modes.commandModeActive = false;

    return false;
}

void MyCodeEditor::refreshSymbolCompletion(
    EditorSemanticContext context,
    const QTextBlock& currentBlock)
{
    context.wordPrefix = getWordUnderCursor();
    const EditorCompletionState completionState =
        contextService()->editorCompletionState(context);
    if (completionState.available) {
        state->completion.model->updateCompletions(completionState.completion.names,
                                           completionState.completion.symbols,
                                           completionState.prefix,
                                           CompletionModel::SymbolCompletion);
        state->completion.wordStartPos =
            currentBlock.position() + completionState.replacementStartColumn;
        showAutoComplete();
    }
}

void MyCodeEditor::highlightCommandText(int prefixPosition)
{
    if (prefixPosition < 0)
        return;

    QList<QTextEdit::ExtraSelection> extraSelections = this->extraSelections();
    removeSelectionsByProperty(
        extraSelections,
        kCommandSelectionProperty,
        kCommandSelectionMarker);

    QTextEdit::ExtraSelection commandSelection;
    commandSelection.format.setBackground(QColor(60, 60, 60, 180));
    commandSelection.format.setForeground(QColor(255, 255, 255));
    commandSelection.format.setProperty(
        kCommandSelectionProperty,
        kCommandSelectionMarker);

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
    removeExtraSelectionsByProperty(
        kCommandSelectionProperty,
        kCommandSelectionMarker);
}

QString MyCodeEditor::getWordUnderCursor()
{
    QTextCursor cursor = textCursor();
    int currentPos = cursor.position();
    cursor.movePosition(QTextCursor::StartOfWord, QTextCursor::MoveAnchor);
    state->completion.wordStartPos = cursor.position();
    cursor.setPosition(currentPos);
    cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::MoveAnchor);
    cursor.setPosition(state->completion.wordStartPos, QTextCursor::MoveAnchor);
    cursor.setPosition(currentPos, QTextCursor::KeepAnchor);

    return cursor.selectedText();
}

void MyCodeEditor::processAlternateModeInput(const QString &input)
{
    if (!state->modes.alternateModeActive) return;

    const EditorAlternateModeCompletionDisplayState completionState =
        contextService()->alternateModeCompletionDisplayState(input);
    applyAlternateModeCompletionDisplayState(completionState);
}

void MyCodeEditor::applyAlternateModeCompletionDisplayState(
    const EditorAlternateModeCompletionDisplayState& displayState)
{
    if (!displayState.updateCompletions)
        return;

    state->modes.alternateBuffer = displayState.normalizedInput;
    state->completion.model->updateCommandCompletions(
        displayState.matches,
        displayState.normalizedInput);

    if (displayState.showPopup) {
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
    state->modes.clearAlternateBuffer();
}

void MyCodeEditor::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control && state->sourceHover.ctrlPressed) {
        state->sourceHover.ctrlPressed = false;
        clearSourceNavigationHover();
    }

    if (event->key() == Qt::Key_Shift) {
        QPlainTextEdit::keyReleaseEvent(event);
        return;
    }

    if (state->modes.alternateModeActive) {
        return;
    }

    QPlainTextEdit::keyReleaseEvent(event);
}

void MyCodeEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        if (requestSourceNavigationAtPosition(event->pos())) {
            event->accept();
            return;
        }
    }

    QPlainTextEdit::mousePressEvent(event);
}

void MyCodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    bool isCtrlPressed = (event->modifiers() & Qt::ControlModifier);

    if (isCtrlPressed != state->sourceHover.ctrlPressed) {
        state->sourceHover.ctrlPressed = isCtrlPressed;

        if (state->sourceHover.ctrlPressed) {
            refreshSourceNavigationHoverAt(event->pos());
        } else {
            clearSourceNavigationHover();
        }
    } else if (state->sourceHover.ctrlPressed) {
        refreshSourceNavigationHoverAt(event->pos());
    }

    QPlainTextEdit::mouseMoveEvent(event);
}

void MyCodeEditor::leaveEvent(QEvent *event)
{
    state->sourceHover.ctrlPressed = false;
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

bool MyCodeEditor::requestSourceNavigationAtPosition(const QPoint& position)
{
    const EditorSourceNavigationTarget target =
        sourceNavigationTargetAtPosition(position);
    emit sourceNavigationRequested(
        target,
        editorSemanticContextForPosition(target.cursorPosition));
    return target.matched && !target.text.isEmpty();
}

void MyCodeEditor::refreshSourceNavigationHoverAt(const QPoint& position)
{
    applySourceNavigationHover(sourceNavigationTargetAtPosition(position));
}

void MyCodeEditor::applySourceNavigationHover(
    const EditorSourceNavigationTarget& target)
{
    if (!target.matched) {
        clearSourceNavigationHover();
        viewport()->setCursor(createNonJumpableCursor());
        return;
    }

    if (!state->sourceHover.matches(target)) {
        clearHoveredSymbolHighlight();
        state->sourceHover.setTarget(target);
        highlightHoveredSymbol(target.text, target.startPos, target.endPos);
    }

    viewport()->setCursor(
        target.jumpable ? createJumpableCursor() : createNonJumpableCursor());
}

void MyCodeEditor::clearSourceNavigationHover()
{
    viewport()->setCursor(Qt::IBeamCursor);
    clearHoveredSymbolHighlight();
    state->sourceHover.clearTarget();
}

void MyCodeEditor::moveMouseToCursor()
{
    if (viewport() && viewport()->isVisible()) {
        QCursor::setPos(viewport()->mapToGlobal(cursorRect().center()));
    }
}

void MyCodeEditor::applyLineNavigationTarget(
    const SourceLineNavigationTarget& target)
{
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::Start);
    for (int i = 0; i < target.lineMoves; ++i)
        cursor.movePosition(QTextCursor::Down);
    if (target.columnMoves > 0) {
        cursor.movePosition(QTextCursor::Right,
                            QTextCursor::MoveAnchor,
                            target.columnMoves);
    }
    setTextCursor(cursor);
    centerCursor();
    setFocus();
    moveMouseToCursor();
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

    highlight.format.setProperty(
        kHoveredSymbolSelectionProperty,
        kHoveredSymbolSelectionMarker);

    QList<QTextEdit::ExtraSelection> extraSelections = this->extraSelections();
    removeSelectionsByProperty(
        extraSelections,
        kHoveredSymbolSelectionProperty,
        kHoveredSymbolSelectionMarker);

    extraSelections.append(highlight);
    setExtraSelections(extraSelections);
}

void MyCodeEditor::clearHoveredSymbolHighlight()
{
    removeExtraSelectionsByProperty(
        kHoveredSymbolSelectionProperty,
        kHoveredSymbolSelectionMarker);
    state->sourceHover.clearRange();
}

void MyCodeEditor::removeExtraSelectionsByProperty(int property, int value)
{
    QList<QTextEdit::ExtraSelection> extraSelections = this->extraSelections();
    removeSelectionsByProperty(extraSelections, property, value);
    setExtraSelections(extraSelections);
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
