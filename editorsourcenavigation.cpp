#include "editorsourcenavigation.h"

#include "editorhoverpopup.h"
#include "editorselection.h"
#include "ghostannotationservice.h"
#include "mycodeeditor.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QWidget>

EditorSourceNavigationUi::~EditorSourceNavigationUi() = default;

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
    EditorSemanticContextService* service,
    const EditorSourceContextProvider& contextProvider,
    EditorSelection& selections)
{
    if (event->key() == Qt::Key_Control
        && sourceHover.setCtrlPressed(false)) {
        const QPoint mousePos = editor->mapFromGlobal(QCursor::pos());
        if (editor->rect().contains(mousePos)) {
            refreshHoverAt(
                editor,
                mousePos,
                service,
                contextProvider,
                selections);
        } else {
            clearHover(editor, selections);
        }
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
        refreshHoverAt(
            editor,
            event->pos(),
            service,
            contextProvider,
            selections);
    } else {
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

bool EditorSourceNavigationUi::handleEscape(
    MyCodeEditor* editor,
    EditorSelection& selections)
{
    if (popup && popup->isVisible()) {
        clearHover(editor, selections);
        return true;
    }
    return false;
}

void EditorSourceNavigationUi::handleEditorContentChanged(
    MyCodeEditor* editor,
    EditorSelection& selections)
{
    clearHover(editor, selections);
}

void EditorSourceNavigationUi::handleEditorScrolled(
    MyCodeEditor* editor,
    EditorSelection& selections)
{
    clearHover(editor, selections);
}

void EditorSourceNavigationUi::shutdown()
{
    closePopup();
    popup.reset();
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
    const EditorSourceNavigationTarget target =
        targetAtPosition(editor, position, service, contextProvider);
    applyHover(
        editor,
        target,
        selections);
    refreshPopupAt(
        editor,
        position,
        service,
        contextProvider,
        target);
}

void EditorSourceNavigationUi::refreshPopupAt(
    MyCodeEditor* editor,
    const QPoint& position,
    EditorSemanticContextService* service,
    const EditorSourceContextProvider& contextProvider,
    const EditorSourceNavigationTarget& target)
{
    const QTextCursor cursor = editor->cursorForPosition(position);
    const GhostNumericLiteralReport numericReport =
        GhostAnnotationService::getInstance()->numericLiteralAt(
            GhostNumericLiteralQuery{
                editor->toPlainText(),
                cursor.position()
            });
    if (numericReport.available) {
        if (!numericPopupMatches(numericReport.startPosition,
                                 numericReport.endPosition)) {
            EditorHoverPopup* hoverPopup = ensurePopup(editor);
            const QPoint globalPosition =
                editor->viewport()->mapToGlobal(position);
            hoverPopup->showNumericLiteral(
                numericReport.displayText,
                globalPosition,
                editor->font());
            popupNumericMode = true;
            popupPreviewMode = false;
            popupStartPos = numericReport.startPosition;
            popupEndPos = numericReport.endPosition;
        }
        return;
    }

    if (!target.matched || !target.identifierTarget) {
        closePopup();
        return;
    }

    const bool previewMode = sourceHover.isCtrlPressed();
    if (popupMatches(target, previewMode))
        return;

    const EditorSemanticContext context =
        contextProvider(target.cursorPosition, false);
    EditorHoverPopup* hoverPopup = ensurePopup(editor);
    const QPoint globalPosition = editor->viewport()->mapToGlobal(position);
    if (previewMode) {
        const DefinitionPreviewReport report =
            service->definitionPreviewReport(context);
        if (report.symbolName.isEmpty()
            && report.unavailableReason.isEmpty()) {
            closePopup();
            return;
        }
        hoverPopup->showPreview(report, globalPosition, editor->font());
    } else {
        const SymbolHoverReport report = service->symbolHoverReport(context);
        if (!report.available) {
            closePopup();
            return;
        }
        hoverPopup->showHover(report, globalPosition, editor->font());
    }
    popupNumericMode = false;
    popupPreviewMode = previewMode;
    popupStartPos = target.startPos;
    popupEndPos = target.endPos;
}

void EditorSourceNavigationUi::applyHover(
    MyCodeEditor* editor,
    const EditorSourceNavigationTarget& target,
    EditorSelection& selections)
{
    if (!target.matched) {
        clearHover(editor, selections);
        editor->viewport()->setCursor(sourceHover.isCtrlPressed()
                                          ? sourceHover.nonJumpableCursor()
                                          : Qt::IBeamCursor);
        return;
    }

    if (!sourceHover.matches(target)) {
        selections.clearHoveredSymbol(editor);
        sourceHover.setTarget(target);
        selections.highlightHoveredSymbol(editor, target);
    }

    editor->viewport()->setCursor(sourceHover.isCtrlPressed()
                                      ? sourceHover.cursorForTarget(target)
                                      : Qt::IBeamCursor);
}

void EditorSourceNavigationUi::clearHover(
    MyCodeEditor* editor,
    EditorSelection& selections)
{
    editor->viewport()->setCursor(Qt::IBeamCursor);
    selections.clearHoveredSymbol(editor);
    sourceHover.clearTarget();
    closePopup();
}

void EditorSourceNavigationUi::closePopup()
{
    if (popup)
        popup->closePopup();
    popupStartPos = -1;
    popupEndPos = -1;
    popupNumericMode = false;
    popupPreviewMode = false;
}

EditorHoverPopup* EditorSourceNavigationUi::ensurePopup(MyCodeEditor* editor)
{
    if (!popup)
        popup = std::make_unique<EditorHoverPopup>();
    popup->setNavigationHandler([editor](const QString& fileName,
                                         int line,
                                         int column) {
        if (editor)
            emit editor->definitionPreviewNavigationRequested(
                fileName,
                line,
                column);
    });
    return popup.get();
}

bool EditorSourceNavigationUi::popupMatches(
    const EditorSourceNavigationTarget& target,
    bool previewMode) const
{
    return popup
        && popup->isVisible()
        && !popupNumericMode
        && popupPreviewMode == previewMode
        && popupStartPos == target.startPos
        && popupEndPos == target.endPos;
}

bool EditorSourceNavigationUi::numericPopupMatches(
    int startPosition,
    int endPosition) const
{
    return popup
        && popup->isVisible()
        && popupNumericMode
        && popupStartPos == startPosition
        && popupEndPos == endPosition;
}
