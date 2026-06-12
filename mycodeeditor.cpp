#include "mycodeeditor.h"
#include "completionmodel.h"
#include "editorcursornavigation.h"
#include "editorfileidentity.h"
#include "editorgeometry.h"
#include "editorgutter.h"
#include "editorselection.h"
#include "editorsyntaxstate.h"
#include "editorsemanticcontextservice.h"
#include "sourcenavigationservice.h"

#include "syminfo.h"

#include <QPainter>
#include <QScrollBar>

#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QTextCursor>
#include <QTextBlock>
#include <QApplication>
#include <QRect>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include <QAbstractItemView>
#include <QCompleter>
#include <QPixmap>
#include <QPen>
#include <QBrush>
#include <memory>

struct MyCodeEditorState
{
    struct EditorAppearance {
        void apply(MyCodeEditor* editor) const
        {
            editor->setFont(QFont("Consolas", 14));
            const int tabWidth =
                editor->fontMetrics().horizontalAdvance(' ') * 4;
            editor->setTabStopDistance(tabWidth);
            editor->setLineWrapMode(QPlainTextEdit::NoWrap);
        }
    } appearance;

    EditorGutter gutter;

    EditorDocumentGeometry geometry;
    EditorCursorNavigation cursorNavigation;

    EditorSyntaxState syntax;

    EditorFileIdentity identity;

    struct SemanticRuntime {
        EditorSemanticContextService* service = nullptr;

        void init()
        {
            service = EditorSemanticContextService::getInstance();
        }

        void setService(EditorSemanticContextService* nextService)
        {
            service = nextService
                ? nextService
                : EditorSemanticContextService::getInstance();
        }

        EditorSemanticContextService* contextService() const
        {
            return service
                ? service
                : EditorSemanticContextService::getInstance();
        }

        EditorSemanticContext contextForDocument(
            QTextDocument* document,
            const QString& fileName,
            const QString& moduleName,
            int cursorPosition,
            bool includeDocumentText) const
        {
            EditorSemanticContext context;
            context.fileName = fileName;
            context.moduleName = moduleName;
            if (includeDocumentText)
                context.documentText = document->toPlainText();
            context.cursorPosition = cursorPosition;

            const QTextBlock block = document->findBlock(cursorPosition);
            if (block.isValid()) {
                context.lineText = block.text();
                context.column = cursorPosition - block.position();
                context.cursorLine = block.blockNumber() + 1;
                context.lineUpToCursor = context.lineText.left(context.column);
            }

            return context;
        }
    } semantic;

    struct ModeState {
        bool commandModeActive = false;
        bool alternateModeActive = false;
        QString alternateBuffer;
        bool commandModeExitedByDoubleSpace = false;
        int completionTimerLineNumber = -1;

        void setAlternateModeEnabled(bool enabled)
        {
            alternateModeActive = enabled;
        }

        void setCommandModeActive(bool active)
        {
            commandModeActive = active;
        }

        void noteCompletionTimerLine(int lineNumber)
        {
            if (completionTimerLineNumber == lineNumber)
                return;

            commandModeExitedByDoubleSpace = false;
            completionTimerLineNumber = lineNumber;
        }

        void markCommandModeExitedByDoubleSpace()
        {
            commandModeExitedByDoubleSpace = true;
        }

        void resetCommandModeExit()
        {
            commandModeExitedByDoubleSpace = false;
        }

        void clearCommandMode()
        {
            commandModeActive = false;
        }

        void setAlternateBuffer(const QString& input)
        {
            alternateBuffer = input;
        }

        QString alternateBufferWithoutLastChar() const
        {
            return alternateBuffer.left(alternateBuffer.size() - 1);
        }

        void clearAlternateBuffer()
        {
            alternateBuffer.clear();
        }
    } modes;

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

        void attachToEditor(MyCodeEditor* editor, MyCodeEditorState* state)
        {
            init(editor);
            QObject::connect(
                timer,
                &QTimer::timeout,
                editor,
                [state, editor]() {
                    state->handleAutoCompleteTimer(editor);
                });
            QObject::connect(
                completer,
                QOverload<const QModelIndex &>::of(&QCompleter::activated),
                editor,
                [state, editor](const QModelIndex& index) {
                    state->handleCompletionActivated(editor, index);
                });
            QObject::connect(
                editor,
                &QPlainTextEdit::textChanged,
                editor,
                [state, editor]() {
                    state->handleTextChanged(editor);
                });
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

        EditorCompletionActivationContext activationContextForIndex(
            const QModelIndex& index,
            const ModeState& modes) const
        {
            const CompletionModel::CompletionItem item = model->getItem(index);
            EditorCompletionActivationContext context;
            context.selectable = model->isSelectableIndex(index);
            context.alternateModeActive = modes.alternateModeActive;
            context.commandModeActive = modes.commandModeActive;
            context.itemText = item.text;
            context.defaultValue = item.defaultValue;
            return context;
        }

        EditorCompletionPopupKeyContext popupKeyContextForEvent(
            QKeyEvent *event,
            const ModeState& modes) const
        {
            EditorCompletionPopupKeyContext context;
            context.key = event->key();
            context.alternateModeActive = modes.alternateModeActive;
            context.commandModeActive = modes.commandModeActive;
            context.currentIndexValid = currentIndex().isValid();
            context.hasRows = hasRows();
            context.alternateBufferEmpty = modes.alternateBuffer.isEmpty();
            return context;
        }

        void updateCommandModeCompletions(
            const EditorCommandModeCompletionRefreshState& commandState) const
        {
            model->updateSymbolCompletions(
                commandState.completion.symbols,
                commandState.completion.completionPrefix,
                commandState.completion.command.symbolType);
        }

        void updateSymbolCompletions(
            const EditorCompletionState& completionState) const
        {
            model->updateCompletions(
                completionState.completion.names,
                completionState.completion.symbols,
                completionState.prefix,
                CompletionModel::SymbolCompletion);
        }

        void updateAlternateModeCompletions(
            const EditorAlternateModeCompletionDisplayState& displayState) const
        {
            model->updateCommandCompletions(
                displayState.matches,
                displayState.normalizedInput);
        }

        void setReplacementStart(
            int blockPosition,
            int replacementStartColumn)
        {
            wordStartPos = blockPosition + replacementStartColumn;
        }

        QString wordUnderCursor(MyCodeEditor* editor)
        {
            QTextCursor cursor = editor->textCursor();
            const int currentPosition = cursor.position();
            cursor.movePosition(QTextCursor::StartOfWord);
            wordStartPos = cursor.position();
            cursor.setPosition(currentPosition);
            cursor.movePosition(QTextCursor::EndOfWord);
            cursor.setPosition(wordStartPos);
            cursor.setPosition(currentPosition, QTextCursor::KeepAnchor);
            return cursor.selectedText();
        }

        void replaceWordAtCursor(MyCodeEditor* editor, const QString& text) const
        {
            QTextCursor cursor = editor->textCursor();
            cursor.setPosition(wordStartPos);
            cursor.setPosition(
                editor->textCursor().position(),
                QTextCursor::KeepAnchor);
            cursor.insertText(text);
        }

        void showForCursor(const QRect& cursorRectangle,
                           bool selectFirstCompletion) const
        {
            if (!hasRows())
                return;

            QRect popupRectangle = cursorRectangle;
            popupRectangle.setWidth(popup()->sizeHintForColumn(0) + 20);
            if (selectFirstCompletion) {
                const QModelIndex selectableIndex = firstSelectableIndex();
                if (selectableIndex.isValid())
                    popup()->setCurrentIndex(selectableIndex);
            }

            completer->complete(popupRectangle);
        }
    } completion;

    EditorHighlightRefresh highlightRefresh;

    struct SourceNavigationHover {
        bool ctrlPressed = false;
        QString word;
        int startPos = -1;
        int endPos = -1;

        bool setCtrlPressed(bool pressed)
        {
            if (ctrlPressed == pressed)
                return false;

            ctrlPressed = pressed;
            return true;
        }

        bool isCtrlPressed() const
        {
            return ctrlPressed;
        }

        QCursor cursorForTarget(
            const EditorSourceNavigationTarget& target) const
        {
            return target.jumpable ? createJumpableCursor()
                                   : createNonJumpableCursor();
        }

        QCursor nonJumpableCursor() const
        {
            return createNonJumpableCursor();
        }

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

    private:
        QCursor createJumpableCursor() const
        {
            QPixmap pixmap(24, 24);
            pixmap.fill(Qt::transparent);

            QPainter painter(&pixmap);
            if (!painter.isActive())
                return QCursor(Qt::PointingHandCursor);

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

        QCursor createNonJumpableCursor() const
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
    } sourceHover;

    EditorSelection selections;

    void initializeCore(MyCodeEditor* editor)
    {
        semantic.init();
        syntax.init();
        gutter.init(editor);
        identity.set(QString());
        editor->setMouseTracking(true);
    }

    void shutdown()
    {
        gutter.destroy();
    }

    void attachEditorConnections(MyCodeEditor* editor)
    {
        highlightRefresh.attachToEditor(editor, [this, editor]() {
            refreshScopeAndCurrentLineHighlight(editor);
        });
        QObject::connect(
            editor,
            &QPlainTextEdit::blockCountChanged,
            editor,
            [this, editor]() {
                gutter.updateViewportMargins(editor);
            });
        QObject::connect(
            editor,
            &QPlainTextEdit::updateRequest,
            editor,
            [this, editor](const QRect& rect, int dy) {
                gutter.handleUpdateRequest(editor, rect, dy);
            });
    }

    void attachToEditor(MyCodeEditor* editor)
    {
        initializeCore(editor);
        attachEditorConnections(editor);
        appearance.apply(editor);
        syntax.attachToEditor(editor);
        completion.attachToEditor(editor, this);
        selections.highlightCurrentLine(editor);
        gutter.updateViewportMargins(editor);
    }

    EditorSemanticContextService* semanticService() const
    {
        return semantic.contextService();
    }

    QString currentModuleNameAt(int charPos) const
    {
        return syntax.moduleNameAt(charPos);
    }

    QString currentModuleName(const MyCodeEditor* editor) const
    {
        return currentModuleNameAt(editor->textCursor().position());
    }

    EditorSemanticContext semanticContextForPosition(
        const MyCodeEditor* editor,
        int cursorPosition,
        bool includeDocumentText) const
    {
        const int semanticPosition = cursorPosition >= 0
            ? cursorPosition
            : editor->textCursor().position();

        return semantic.contextForDocument(
            editor->document(),
            identity.current(),
            currentModuleNameAt(semanticPosition),
            semanticPosition,
            includeDocumentText);
    }

    EditorSemanticContext semanticContextForCursor(
        const MyCodeEditor* editor,
        const QTextCursor& cursor,
        bool includeDocumentText) const
    {
        return semanticContextForPosition(
            editor,
            cursor.position(),
            includeDocumentText);
    }

    void hideAutoComplete(MyCodeEditor* editor)
    {
        completion.hidePopup();

        if (modes.commandModeActive)
            selections.clearCommand(editor);
    }

    void showAutoComplete(MyCodeEditor* editor)
    {
        completion.showForCursor(
            editor->cursorRect(editor->textCursor()),
            modes.commandModeActive);
    }

    void applyAlternateModeCompletionDisplayState(
        MyCodeEditor* editor,
        const EditorAlternateModeCompletionDisplayState& displayState)
    {
        if (!displayState.updateCompletions)
            return;

        modes.setAlternateBuffer(displayState.normalizedInput);
        completion.updateAlternateModeCompletions(displayState);

        if (displayState.showPopup)
            showAutoComplete(editor);
    }

    void processAlternateModeInput(MyCodeEditor* editor, const QString& input)
    {
        if (!modes.alternateModeActive)
            return;

        const EditorAlternateModeCompletionDisplayState completionState =
            semanticService()->alternateModeCompletionDisplayState(input);
        applyAlternateModeCompletionDisplayState(editor, completionState);
    }

    void executeAlternateModeCommand(
        MyCodeEditor* editor,
        const QString& command)
    {
        if (!command.trimmed().isEmpty())
            emit editor->alternateCommandRequested(command);
        modes.clearAlternateBuffer();
        hideAutoComplete(editor);
    }

    void updateCompletionTriggerForTextChange(
        MyCodeEditor* editor,
        const QTextCursor& cursor)
    {
        EditorSemanticContext context =
            semanticContextForCursor(editor, cursor, false);
        context.moduleName = currentModuleNameAt(cursor.position() - 1);
        const EditorCompletionTextChangeState completionState =
            semanticService()->completionTextChangeState(context);
        modes.setCommandModeActive(completionState.commandModeActive);

        if (completionState.startCompletionTimer) {
            completion.startTimer();
        } else if (completionState.hidePopup) {
            hideAutoComplete(editor);
        }
    }

    void handleTextChanged(MyCodeEditor* editor)
    {
        completion.stopTimer();
        updateCompletionTriggerForTextChange(editor, editor->textCursor());
    }

    void applyCompletionActivationState(
        MyCodeEditor* editor,
        const CompletionActivationState& activationState)
    {
        if (activationState.action == CompletionActivationAction::None)
            return;

        QTextCursor cursor = editor->textCursor();

        if (activationState.action
            == CompletionActivationAction::ExecuteAlternateCommand) {
            executeAlternateModeCommand(editor, activationState.text);
            return;
        }

        if (activationState.action == CompletionActivationAction::ReplaceLine) {
            cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
            cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
            cursor.insertText(activationState.text);

            if (activationState.clearCommandMode) {
                modes.clearCommandMode();
                selections.clearCommand(editor);
            }
        } else if (activationState.action
                   == CompletionActivationAction::ReplaceWord) {
            completion.replaceWordAtCursor(editor, activationState.text);
        }

        if (activationState.hidePopup)
            hideAutoComplete(editor);
    }

    void handleCompletionActivated(
        MyCodeEditor* editor,
        const QModelIndex& index)
    {
        const EditorCompletionActivationContext activationContext =
            completion.activationContextForIndex(index, modes);
        const CompletionActivationState activationState =
            semanticService()->completionActivationState(activationContext);
        applyCompletionActivationState(editor, activationState);
    }

    bool handleSourceSymbolShortcut(MyCodeEditor* editor, QKeyEvent *event)
    {
        EditorSourceSymbolShortcutContext sourceShortcutContext;
        sourceShortcutContext.key = event->key();
        sourceShortcutContext.modifiers = int(event->modifiers());
        sourceShortcutContext.semanticContext =
            semanticContextForPosition(
                editor,
                editor->textCursor().position(),
                false);
        const EditorSourceSymbolShortcutState sourceShortcutState =
            semanticService()->sourceSymbolShortcutState(sourceShortcutContext);
        if (!sourceShortcutState.matched)
            return false;

        emit editor->sourceSymbolActionRequested(
            sourceShortcutState.action,
            sourceShortcutState.semanticContext);
        if (sourceShortcutState.acceptEvent)
            event->accept();
        return true;
    }

    void applyAlternateModeKeyState(
        MyCodeEditor* editor,
        const EditorAlternateModeKeyState& keyState)
    {
        switch (keyState.action) {
        case EditorAlternateModeKeyAction::UpdateInput:
        case EditorAlternateModeKeyAction::RefreshCompletions:
            applyAlternateModeCompletionDisplayState(
                editor,
                keyState.completion);
            break;
        case EditorAlternateModeKeyAction::ExecuteCommand:
            executeAlternateModeCommand(editor, keyState.command);
            break;
        case EditorAlternateModeKeyAction::ClearAndHide:
            if (keyState.hidePopup)
                hideAutoComplete(editor);
            if (keyState.clearBuffer)
                modes.clearAlternateBuffer();
            break;
        case EditorAlternateModeKeyAction::Consume:
            break;
        }
    }

    bool handleAlternateModeKey(MyCodeEditor* editor, QKeyEvent *event)
    {
        if (handleCompletionPopupKey(editor, event))
            return true;

        EditorAlternateModeKeyContext alternateKeyContext;
        alternateKeyContext.key = event->key();
        alternateKeyContext.text = event->text();
        alternateKeyContext.buffer = modes.alternateBuffer;
        const EditorAlternateModeKeyState alternateKeyState =
            semanticService()->alternateModeKeyState(alternateKeyContext);
        applyAlternateModeKeyState(editor, alternateKeyState);
        return true;
    }

    bool handleCompletionPopupKey(MyCodeEditor* editor, QKeyEvent *event)
    {
        if (!completion.popupVisible())
            return false;

        const CompletionPopupKeyState popupState =
            semanticService()->completionPopupKeyState(
                completion.popupKeyContextForEvent(event, modes));
        return applyCompletionPopupKeyState(editor, event, popupState);
    }

    bool applyCompletionPopupKeyState(
        MyCodeEditor* editor,
        QKeyEvent *event,
        const CompletionPopupKeyState& popupState)
    {
        switch (popupState.action) {
        case CompletionPopupKeyAction::ForwardToPopup:
            QApplication::sendEvent(completion.popup(), event);
            return true;
        case CompletionPopupKeyAction::ActivateCurrent:
            if (completion.currentIndex().isValid())
                completion.activateIndex(completion.currentIndex());
            return true;
        case CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable:
            {
                QModelIndex currentIndex = completion.currentIndex();
                if (!currentIndex.isValid() && completion.hasRows())
                    currentIndex = completion.firstSelectableIndex();
                if (currentIndex.isValid())
                    completion.activateIndex(currentIndex);
            }
            return true;
        case CompletionPopupKeyAction::HidePopup:
            hideAutoComplete(editor);
            return true;
        case CompletionPopupKeyAction::HidePopupAndClearAlternate:
            hideAutoComplete(editor);
            modes.clearAlternateBuffer();
            return true;
        case CompletionPopupKeyAction::BackspaceAlternateInput:
            if (!modes.alternateBuffer.isEmpty()) {
                const EditorAlternateModeCompletionDisplayState completionState =
                    semanticService()->alternateModeCompletionDisplayState(
                        modes.alternateBufferWithoutLastChar());
                applyAlternateModeCompletionDisplayState(
                    editor,
                    completionState);
            } else {
                hideAutoComplete(editor);
            }
            return true;
        case CompletionPopupKeyAction::Consume:
            return true;
        case CompletionPopupKeyAction::None:
            return false;
        }

        return false;
    }

    bool refreshCommandModeCompletion(
        MyCodeEditor* editor,
        const EditorSemanticContext& context)
    {
        const EditorCommandModeCompletionRefreshState commandState =
            semanticService()->commandModeCompletionRefreshState(
                context,
                modes.commandModeExitedByDoubleSpace);
        if (commandState.matched) {
            modes.setCommandModeActive(commandState.commandModeActive);
            if (commandState.suppressAfterExit)
                return true;

            if (commandState.exitRequested) {
                if (commandState.clearCommandHighlight)
                    selections.clearCommand(editor);
                if (commandState.markExitedByDoubleSpace)
                    modes.markCommandModeExitedByDoubleSpace();
                if (completion.popupVisible())
                    completion.hidePopup();
                return true;
            }

            if (commandState.highlightCommand)
                selections.highlightCommand(
                    editor,
                    commandState.completion.prefixPosition);

            if (commandState.hidePopup) {
                if (completion.popupVisible())
                    completion.hidePopup();
                return true;
            }

            if (commandState.showCompletions) {
                completion.updateCommandModeCompletions(commandState);
                showAutoComplete(editor);
            }
            return true;
        }

        if (commandState.resetExitedByDoubleSpace)
            modes.resetCommandModeExit();

        selections.clearCommand(editor);
        modes.clearCommandMode();

        return false;
    }

    void refreshSymbolCompletion(
        MyCodeEditor* editor,
        EditorSemanticContext context,
        const QTextBlock& currentBlock)
    {
        context.wordPrefix = completion.wordUnderCursor(editor);
        const EditorCompletionState completionState =
            semanticService()->editorCompletionState(context);
        if (completionState.available) {
            completion.updateSymbolCompletions(completionState);
            completion.setReplacementStart(
                currentBlock.position(),
                completionState.replacementStartColumn);
            showAutoComplete(editor);
        }
    }

    void handleAutoCompleteTimer(MyCodeEditor* editor)
    {
        const QTextCursor cursor = editor->textCursor();
        const QTextBlock currentBlock = cursor.block();

        modes.noteCompletionTimerLine(currentBlock.blockNumber());

        EditorSemanticContext context =
            semanticContextForCursor(editor, cursor, true);

        if (refreshCommandModeCompletion(editor, context))
            return;

        if (modes.alternateModeActive) {
            processAlternateModeInput(editor, context.lineUpToCursor);
            return;
        }

        refreshSymbolCompletion(editor, context, currentBlock);
    }

    EditorSourceNavigationTarget sourceNavigationTargetAtPosition(
        MyCodeEditor* editor,
        const QPoint& position) const
    {
        QTextCursor cursor = editor->cursorForPosition(position);
        QTextBlock block = cursor.block();
        if (!block.isValid())
            return {};

        return semanticService()->editorSourceNavigationTarget(
            semanticContextForPosition(editor, cursor.position(), false),
            block.position());
    }

    bool requestSourceNavigationAtPosition(
        MyCodeEditor* editor,
        const QPoint& position) const
    {
        const EditorSourceNavigationTarget target =
            sourceNavigationTargetAtPosition(editor, position);
        emit editor->sourceNavigationRequested(
            target,
            semanticContextForPosition(editor, target.cursorPosition, false));
        return target.matched && !target.text.isEmpty();
    }

    void refreshSourceNavigationHoverAt(
        MyCodeEditor* editor,
        const QPoint& position)
    {
        applySourceNavigationHover(
            editor,
            sourceNavigationTargetAtPosition(editor, position));
    }

    void applySourceNavigationHover(
        MyCodeEditor* editor,
        const EditorSourceNavigationTarget& target)
    {
        if (!target.matched) {
            clearSourceNavigationHover(editor);
            editor->viewport()->setCursor(sourceHover.nonJumpableCursor());
            return;
        }

        if (!sourceHover.matches(target)) {
            selections.clearHoveredSymbol(editor);
            sourceHover.setTarget(target);
            selections.highlightHoveredSymbol(editor, target);
        }

        editor->viewport()->setCursor(sourceHover.cursorForTarget(target));
    }

    void clearSourceNavigationHover(MyCodeEditor* editor)
    {
        editor->viewport()->setCursor(Qt::IBeamCursor);
        selections.clearHoveredSymbol(editor);
        sourceHover.clearTarget();
    }

    void handleControlKeyPress(MyCodeEditor* editor, QKeyEvent *event)
    {
        if (event->key() != Qt::Key_Control
            || !sourceHover.setCtrlPressed(true)) {
            return;
        }

        const QPoint mousePos = editor->mapFromGlobal(QCursor::pos());
        if (editor->rect().contains(mousePos))
            refreshSourceNavigationHoverAt(editor, mousePos);
    }

    void handleControlKeyRelease(MyCodeEditor* editor, QKeyEvent *event)
    {
        if (event->key() == Qt::Key_Control
            && sourceHover.setCtrlPressed(false)) {
            clearSourceNavigationHover(editor);
        }
    }

    bool handleKeyPress(MyCodeEditor* editor, QKeyEvent *event)
    {
        handleControlKeyPress(editor, event);

        if (handleSourceSymbolShortcut(editor, event))
            return true;

        if (event->key() == Qt::Key_Shift) {
            event->ignore();
            return true;
        }

        if (modes.alternateModeActive) {
            handleAlternateModeKey(editor, event);
            return true;
        }

        return handleCompletionPopupKey(editor, event);
    }

    bool handleKeyRelease(MyCodeEditor* editor, QKeyEvent *event)
    {
        handleControlKeyRelease(editor, event);
        return event->key() != Qt::Key_Shift && modes.alternateModeActive;
    }

    bool handleSourceNavigationMousePress(
        MyCodeEditor* editor,
        QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton
            || !(event->modifiers() & Qt::ControlModifier)) {
            return false;
        }

        if (!requestSourceNavigationAtPosition(editor, event->pos()))
            return false;

        event->accept();
        return true;
    }

    void handleSourceNavigationMouseMove(
        MyCodeEditor* editor,
        QMouseEvent *event)
    {
        const bool isCtrlPressed =
            (event->modifiers() & Qt::ControlModifier);

        if (sourceHover.setCtrlPressed(isCtrlPressed)) {
            if (sourceHover.isCtrlPressed())
                refreshSourceNavigationHoverAt(editor, event->pos());
            else
                clearSourceNavigationHover(editor);
        } else if (sourceHover.isCtrlPressed()) {
            refreshSourceNavigationHoverAt(editor, event->pos());
        }
    }

    void handleLeave(MyCodeEditor* editor)
    {
        sourceHover.setCtrlPressed(false);
        clearSourceNavigationHover(editor);
    }

    void handleSourceSymbolContextMenu(
        MyCodeEditor* editor,
        QContextMenuEvent *event)
    {
        std::unique_ptr<QMenu> menu(
            editor->createStandardContextMenu(event->pos()));
        const QTextCursor cursorAtPos = editor->cursorForPosition(event->pos());
        emit editor->sourceSymbolContextMenuRequested(
            menu.get(),
            semanticContextForPosition(
                editor,
                cursorAtPos.position(),
                false));

        menu->exec(event->globalPos());
    }

    void handleResize(MyCodeEditor* editor) const
    {
        gutter.resizeTo(editor, editor->contentsRect());
    }

    void handleContextMenu(MyCodeEditor* editor, QContextMenuEvent *event)
    {
        handleSourceSymbolContextMenu(editor, event);
    }

    bool handleMousePress(MyCodeEditor* editor, QMouseEvent *event)
    {
        return handleSourceNavigationMousePress(editor, event);
    }

    void handleMouseMove(MyCodeEditor* editor, QMouseEvent *event)
    {
        handleSourceNavigationMouseMove(editor, event);
    }

    void handleLeaveEvent(MyCodeEditor* editor)
    {
        handleLeave(editor);
    }

    void refreshScopeAndCurrentLineHighlight(MyCodeEditor* editor)
    {
        selections.highlightCurrentLine(editor);
    }

    void setAlternateModeEnabled(bool enabled)
    {
        modes.setAlternateModeEnabled(enabled);
    }

    void setSemanticContextService(EditorSemanticContextService* service)
    {
        semantic.setService(service);
    }

    EditorBlockGeometry blockGeometry(
        const MyCodeEditor* editor,
        int blockNumber) const
    {
        return geometry.blockGeometry(editor, blockNumber);
    }

    qreal documentHeightPx(const MyCodeEditor* editor) const
    {
        return geometry.documentHeightPx(editor);
    }

    void setDocumentFileName(MyCodeEditor* editor, QString fileName)
    {
        if (!identity.set(fileName))
            return;

        emit editor->fileNameChanged(identity.current());
    }

    QString documentFileName() const
    {
        return identity.current();
    }

    void applyLineNavigationTarget(
        MyCodeEditor* editor,
        const SourceLineNavigationTarget& target) const
    {
        cursorNavigation.applyLineTarget(editor, target);
    }

};

MyCodeEditor::MyCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , state(std::make_unique<MyCodeEditorState>())
{
    state->attachToEditor(this);
}

MyCodeEditor::~MyCodeEditor()
{
    state->shutdown();
}

void MyCodeEditor::refreshScopeAndCurrentLineHighlight()
{
    state->refreshScopeAndCurrentLineHighlight(this);
}

void MyCodeEditor::setAlternateModeEnabled(bool enabled)
{
    state->setAlternateModeEnabled(enabled);
}

void MyCodeEditor::setSemanticContextService(EditorSemanticContextService* service)
{
    state->setSemanticContextService(service);
}

EditorBlockGeometry MyCodeEditor::blockGeometry(int blockNumber) const
{
    return state->blockGeometry(this, blockNumber);
}

qreal MyCodeEditor::documentHeightPx() const
{
    return state->documentHeightPx(this);
}

void MyCodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    state->handleResize(this);
}

void MyCodeEditor::contextMenuEvent(QContextMenuEvent *event)
{
    state->handleContextMenu(this, event);
}

EditorSemanticContext MyCodeEditor::editorSemanticContextForPosition(
    int cursorPosition,
    bool includeDocumentText) const
{
    return state->semanticContextForPosition(
        this,
        cursorPosition,
        includeDocumentText);
}

void MyCodeEditor::setDocumentFileName(QString fileName)
{
    state->setDocumentFileName(this, fileName);
}

QString MyCodeEditor::documentFileName() const
{
    return state->documentFileName();
}

QString MyCodeEditor::currentModuleName() const
{
    return state->currentModuleName(this);
}

void MyCodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (state->handleKeyPress(this, event))
        return;

    QPlainTextEdit::keyPressEvent(event);
}

void MyCodeEditor::executeAlternateModeCommand(const QString& command)
{
    state->executeAlternateModeCommand(this, command);
}

void MyCodeEditor::keyReleaseEvent(QKeyEvent *event)
{
    if (state->handleKeyRelease(this, event))
        return;

    QPlainTextEdit::keyReleaseEvent(event);
}

void MyCodeEditor::mousePressEvent(QMouseEvent *event)
{
    if (state->handleMousePress(this, event))
        return;

    QPlainTextEdit::mousePressEvent(event);
}

void MyCodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    state->handleMouseMove(this, event);

    QPlainTextEdit::mouseMoveEvent(event);
}

void MyCodeEditor::leaveEvent(QEvent *event)
{
    state->handleLeaveEvent(this);

    QPlainTextEdit::leaveEvent(event);
}

void MyCodeEditor::applyLineNavigationTarget(
    const SourceLineNavigationTarget& target)
{
    state->applyLineNavigationTarget(this, target);
}
