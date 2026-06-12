#include "editorsourcenavigation.h"

#include "editorselection.h"
#include "mycodeeditor.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QWidget>

bool EditorSourceNavigationUi::handleSourceSymbolShortcut(
    MyCodeEditor* editor,
    QKeyEvent* event,
    EditorSemanticContextService* service,
    const EditorSourceContextProvider& contextProvider)
{
    EditorSourceSymbolShortcutContext sourceShortcutContext;
    sourceShortcutContext.key = event->key();
    sourceShortcutContext.modifiers = int(event->modifiers());
    sourceShortcutContext.semanticContext =
        contextProvider(editor->textCursor().position(), false);
    const EditorSourceSymbolShortcutState sourceShortcutState =
        service->sourceSymbolShortcutState(sourceShortcutContext);
    if (!sourceShortcutState.matched)
        return false;

    emit editor->sourceSymbolActionRequested(
        sourceShortcutState.action,
        sourceShortcutState.semanticContext);
    if (sourceShortcutState.acceptEvent)
        event->accept();
    return true;
}

void EditorSourceNavigationUi::handleControlKeyPress(
    MyCodeEditor* editor,
    QKeyEvent* event,
    EditorSemanticContextService* service,
    const EditorSourceContextProvider& contextProvider,
    EditorSelection& selections)
{
    if (event->key() != Qt::Key_Control
        || !sourceHover.setCtrlPressed(true)) {
        return;
    }

    const QPoint mousePos = editor->mapFromGlobal(QCursor::pos());
    if (editor->rect().contains(mousePos)) {
        refreshHoverAt(
            editor,
            mousePos,
            service,
            contextProvider,
            selections);
    }
}

void EditorSourceNavigationUi::handleControlKeyRelease(
    MyCodeEditor* editor,
    QKeyEvent* event,
    EditorSelection& selections)
{
    if (event->key() == Qt::Key_Control
        && sourceHover.setCtrlPressed(false)) {
        clearHover(editor, selections);
    }
}

bool EditorSourceNavigationUi::handleMousePress(
    MyCodeEditor* editor,
    QMouseEvent* event,
    EditorSemanticContextService* service,
    const EditorSourceContextProvider& contextProvider)
{
    if (event->button() != Qt::LeftButton
        || !(event->modifiers() & Qt::ControlModifier)) {
        return false;
    }

    if (!requestNavigationAtPosition(
            editor,
            event->pos(),
            service,
            contextProvider)) {
        return false;
    }

    event->accept();
    return true;
}

void EditorSourceNavigationUi::handleMouseMove(
    MyCodeEditor* editor,
    QMouseEvent* event,
    EditorSemanticContextService* service,
    const EditorSourceContextProvider& contextProvider,
    EditorSelection& selections)
{
    const bool isCtrlPressed =
        (event->modifiers() & Qt::ControlModifier);

    if (sourceHover.setCtrlPressed(isCtrlPressed)) {
        if (sourceHover.isCtrlPressed()) {
            refreshHoverAt(
                editor,
                event->pos(),
                service,
                contextProvider,
                selections);
        } else {
            clearHover(editor, selections);
        }
    } else if (sourceHover.isCtrlPressed()) {
        refreshHoverAt(
            editor,
            event->pos(),
            service,
            contextProvider,
            selections);
    }
}

void EditorSourceNavigationUi::handleLeave(
    MyCodeEditor* editor,
    EditorSelection& selections)
{
    sourceHover.setCtrlPressed(false);
    clearHover(editor, selections);
}

void EditorSourceNavigationUi::handleContextMenu(
    MyCodeEditor* editor,
    QContextMenuEvent* event,
    const EditorSourceContextProvider& contextProvider)
{
    std::unique_ptr<QMenu> menu(
        editor->createStandardContextMenu(event->pos()));
    const QTextCursor cursorAtPos = editor->cursorForPosition(event->pos());
    emit editor->sourceSymbolContextMenuRequested(
        menu.get(),
        contextProvider(cursorAtPos.position(), false));

    menu->exec(event->globalPos());
}

EditorSourceNavigationTarget EditorSourceNavigationUi::targetAtPosition(
    MyCodeEditor* editor,
    const QPoint& position,
    EditorSemanticContextService* service,
    const EditorSourceContextProvider& contextProvider) const
{
    QTextCursor cursor = editor->cursorForPosition(position);
    QTextBlock block = cursor.block();
    if (!block.isValid())
        return {};

    return service->editorSourceNavigationTarget(
        contextProvider(cursor.position(), false),
        block.position());
}

bool EditorSourceNavigationUi::requestNavigationAtPosition(
    MyCodeEditor* editor,
    const QPoint& position,
    EditorSemanticContextService* service,
    const EditorSourceContextProvider& contextProvider) const
{
    const EditorSourceNavigationTarget target =
        targetAtPosition(editor, position, service, contextProvider);
    emit editor->sourceNavigationRequested(
        target,
        contextProvider(target.cursorPosition, false));
    return target.matched && !target.text.isEmpty();
}

void EditorSourceNavigationUi::refreshHoverAt(
    MyCodeEditor* editor,
    const QPoint& position,
    EditorSemanticContextService* service,
    const EditorSourceContextProvider& contextProvider,
    EditorSelection& selections)
{
    applyHover(
        editor,
        targetAtPosition(editor, position, service, contextProvider),
        selections);
}

void EditorSourceNavigationUi::applyHover(
    MyCodeEditor* editor,
    const EditorSourceNavigationTarget& target,
    EditorSelection& selections)
{
    if (!target.matched) {
        clearHover(editor, selections);
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

void EditorSourceNavigationUi::clearHover(
    MyCodeEditor* editor,
    EditorSelection& selections)
{
    editor->viewport()->setCursor(Qt::IBeamCursor);
    selections.clearHoveredSymbol(editor);
    sourceHover.clearTarget();
}
