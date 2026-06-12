#include "mycodeeditor.h"
#include "editorappearance.h"
#include "editorcompletionui.h"
#include "editorcursornavigation.h"
#include "editorfileidentity.h"
#include "editorgeometry.h"
#include "editorgutter.h"
#include "editorselection.h"
#include "editorsourcenavigation.h"
#include "editorsemanticruntime.h"
#include "editorsyntaxstate.h"
#include "editorsemanticcontextservice.h"
#include "editormodestate.h"
#include "sourcenavigationservice.h"

#include "syminfo.h"

#include <QScrollBar>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QTextCursor>
#include <QTextBlock>
#include <QApplication>
#include <QRect>
#include <QWheelEvent>
#include <QWidget>

#include <QAbstractItemView>
#include <memory>

struct MyCodeEditorState
{
    EditorAppearance appearance;

    EditorGutter gutter;

    EditorDocumentGeometry geometry;
    EditorCursorNavigation cursorNavigation;

    EditorSyntaxState syntax;

    EditorFileIdentity identity;

    EditorSemanticRuntime semantic;
    EditorModeState modes;

    EditorCompletionUi completion;

    EditorHighlightRefresh highlightRefresh;

    EditorSourceNavigationUi sourceNavigation;

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
        completion.attachToEditor(
            editor,
            [this, editor]() {
                handleAutoCompleteTimer(editor);
            },
            [this, editor](const QModelIndex& index) {
                handleCompletionActivated(editor, index);
            },
            [this, editor]() {
                handleTextChanged(editor);
            });
        selections.highlightCurrentLine(editor);
        gutter.updateViewportMargins(editor);
    }

    EditorSemanticContextService* semanticService() const
    {
        return semantic.contextService();
    }

    EditorSourceContextProvider sourceContextProvider(
        const MyCodeEditor* editor) const
    {
        return [this, editor](int cursorPosition, bool includeDocumentText) {
            return semanticContextForPosition(
                editor,
                cursorPosition,
                includeDocumentText);
        };
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

    void handleControlKeyPress(MyCodeEditor* editor, QKeyEvent *event)
    {
        sourceNavigation.handleControlKeyPress(
            editor,
            event,
            semanticService(),
            sourceContextProvider(editor),
            selections);
    }

    void handleControlKeyRelease(MyCodeEditor* editor, QKeyEvent *event)
    {
        sourceNavigation.handleControlKeyRelease(editor, event, selections);
    }

    bool handleKeyPress(MyCodeEditor* editor, QKeyEvent *event)
    {
        handleControlKeyPress(editor, event);

        if (sourceNavigation.handleSourceSymbolShortcut(
                editor,
                event,
                semanticService(),
                sourceContextProvider(editor))) {
            return true;
        }

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

    void handleResize(MyCodeEditor* editor) const
    {
        gutter.resizeTo(editor, editor->contentsRect());
    }

    void handleContextMenu(MyCodeEditor* editor, QContextMenuEvent *event)
    {
        sourceNavigation.handleContextMenu(
            editor,
            event,
            sourceContextProvider(editor));
    }

    bool handleMousePress(MyCodeEditor* editor, QMouseEvent *event)
    {
        return sourceNavigation.handleMousePress(
            editor,
            event,
            semanticService(),
            sourceContextProvider(editor));
    }

    void handleMouseMove(MyCodeEditor* editor, QMouseEvent *event)
    {
        sourceNavigation.handleMouseMove(
            editor,
            event,
            semanticService(),
            sourceContextProvider(editor),
            selections);
    }

    void handleLeaveEvent(MyCodeEditor* editor)
    {
        sourceNavigation.handleLeave(editor, selections);
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
