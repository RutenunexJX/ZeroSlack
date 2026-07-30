#include "editorsourcenavigation.h"

#include "editorhoverpopup.h"
#include "editormodecontroller.h"
#include "editorselection.h"
#include "ghostannotationservice.h"
#include "mycodeeditor.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>
#include <QWidget>

namespace {
QString normalizedSourceNavigationFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool definitionPreviewTargetIsCurrentLocation(
    const EditorSemanticContext& context,
    const DefinitionPreviewReport& report)
{
    if (!report.targetResolved
        || context.fileName.isEmpty()
        || context.cursorLine <= 0
        || report.targetLine <= 0
        || context.cursorLine != report.targetLine) {
        return false;
    }

    const QString targetFile = report.targetFile.isEmpty()
        ? context.fileName
        : report.targetFile;
    if (normalizedSourceNavigationFileName(context.fileName)
        != normalizedSourceNavigationFileName(targetFile)) {
        return false;
    }

    if (report.targetColumn <= 0 || context.column < 0)
        return true;

    const int oneBasedColumn = context.column + 1;
    const int symbolWidth = qMax(1, report.symbolName.size());
    return oneBasedColumn >= report.targetColumn
        && oneBasedColumn <= report.targetColumn + symbolWidth;
}

int numericPopupSelectionStart(const QString& literal,
                               int startPosition)
{
    if (startPosition < 0 || literal.isEmpty()) {
        return startPosition;
    }

    const int quote = literal.indexOf(QLatin1Char('\''));
    if (quote < 0 || quote + 1 >= literal.size())
        return startPosition;

    const QChar baseChar = literal.at(quote + 1).toLower();
    if (baseChar == QLatin1Char('b')
        || baseChar == QLatin1Char('d')
        || baseChar == QLatin1Char('h')) {
        return startPosition + quote + 2;
    }
    if (baseChar.isDigit())
        return startPosition + quote + 1;

    return startPosition;
}

bool isSourceIdentifierText(const QString& text)
{
    if (text.isEmpty())
        return false;
    const QChar first = text.at(0);
    if (!first.isLetter() && first != QLatin1Char('_'))
        return false;
    for (int i = 1; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_'))
            return false;
    }
    return true;
}

int sourceSymbolContextPositionForMenu(MyCodeEditor* editor,
                                       const QTextCursor& cursorAtPos)
{
    if (!editor)
        return cursorAtPos.position();

    const QTextCursor activeCursor = editor->textCursor();
    if (!activeCursor.hasSelection())
        return cursorAtPos.position();

    const int hitPosition = cursorAtPos.position();
    if (hitPosition < activeCursor.selectionStart()
        || hitPosition > activeCursor.selectionEnd()) {
        return cursorAtPos.position();
    }

    QString selectedText = activeCursor.selectedText().trimmed();
    selectedText.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    if (!isSourceIdentifierText(selectedText))
        return cursorAtPos.position();

    return activeCursor.selectionStart();
}
}

EditorSourceNavigationUi::~EditorSourceNavigationUi() = default;

void EditorSourceNavigationUi::bindModeController(
    EditorModeController* modes,
    MyCodeEditor* editor,
    EditorSelection* selections)
{
    modeController = modes;
    boundEditor = editor;
    boundSelections = selections;
    if (!modeController)
        return;

    const QPointer<MyCodeEditor> target(editor);
    modeController->setExitHandler(
        EditorModeId::SourceNavigation,
        [this, target](EditorModeExitReason) {
            if (target && boundSelections) {
                closeForEditor(
                    target,
                    *boundSelections);
            }
        });
}

void EditorSourceNavigationUi::syncMode()
{
    if (!modeController)
        return;
    if (active()) {
        if (modeController->hasActiveMode()
            && !modeController->isActive(
                EditorModeId::SourceNavigation)) {
            return;
        }
        if (!modeController->isActive(
                EditorModeId::SourceNavigation)) {
            modeController->enter(
                EditorModeId::SourceNavigation,
                EditorModeEntryReason::MouseGesture);
        }
        modeController->updatePresentation(
            EditorModeId::SourceNavigation,
            QStringLiteral("Source navigation"),
            QStringLiteral(
                "Choose a source target; Esc closes"));
    } else if (modeController->isActive(
                   EditorModeId::SourceNavigation)) {
        modeController->exit(
            EditorModeId::SourceNavigation,
            EditorModeExitReason::Completed);
    }
}

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

    const QPoint mousePos = hasLastMousePosition
        ? lastMousePosition
        : editor->mapFromGlobal(QCursor::pos());
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
    Q_UNUSED(service)
    Q_UNUSED(contextProvider)
    if (event->key() == Qt::Key_Control
        && sourceHover.setCtrlPressed(false)) {
        clearHover(editor, selections);
    }
}

bool EditorSourceNavigationUi::handleMousePress(
    MyCodeEditor* editor,
    QMouseEvent* event,
    EditorSemanticContextService* service,
    const EditorSourceContextProvider& contextProvider,
    EditorSelection& selections)
{
    if (event->button() == Qt::LeftButton
        && !(event->modifiers() & Qt::ControlModifier)
        && popupPinnedBySelection) {
        clearHover(editor, selections);
        return false;
    }

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

    consumeNextNavigationRelease = true;
    clearHover(editor, selections);
    event->accept();
    return true;
}

bool EditorSourceNavigationUi::handleMouseRelease(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    Q_UNUSED(editor)
    if (!consumeNextNavigationRelease)
        return false;
    if (!event || event->button() != Qt::LeftButton)
        return false;

    consumeNextNavigationRelease = false;
    event->accept();
    return true;
}

bool EditorSourceNavigationUi::handleMouseDoubleClick(
    MyCodeEditor* editor,
    QMouseEvent* event,
    EditorSemanticContextService* service,
    const EditorSourceContextProvider& contextProvider,
    EditorSelection& selections)
{
    if (event->button() != Qt::LeftButton
        || (event->modifiers() & Qt::ControlModifier)) {
        return false;
    }

    sourceHover.setCtrlPressed(false);
    selections.clearHoveredSymbol(editor);
    sourceHover.clearTarget();

    const EditorSourceNavigationTarget target =
        targetAtPosition(editor, event->pos(), service, contextProvider);
    refreshPopupAt(
        editor,
        event->pos(),
        service,
        contextProvider,
        target);
    popupPinnedBySelection = popup && popup->isVisible();

    return false;
}

bool EditorSourceNavigationUi::handleMouseMove(
    MyCodeEditor* editor,
    QMouseEvent* event,
    EditorSemanticContextService* service,
    const EditorSourceContextProvider& contextProvider,
    EditorSelection& selections)
{
    lastMousePosition = event->pos();
    hasLastMousePosition = true;

    if (consumeNextNavigationRelease
        && event->buttons().testFlag(Qt::LeftButton)) {
        event->accept();
        return true;
    }

    const bool isCtrlPressed =
        (event->modifiers() & Qt::ControlModifier);

    sourceHover.setCtrlPressed(isCtrlPressed);
    if (!isCtrlPressed) {
        if (popupPinnedBySelection && popupSelectionStillActive(editor))
            return false;
        if (hasActiveHover())
            clearHover(editor, selections);
        return false;
    }

    refreshHoverAt(
        editor,
        event->pos(),
        service,
        contextProvider,
        selections);
    return false;
}

void EditorSourceNavigationUi::handleLeave(
    MyCodeEditor* editor,
    EditorSelection& selections)
{
    hasLastMousePosition = false;
    sourceHover.setCtrlPressed(false);
    if (popupPinnedBySelection && popupSelectionStillActive(editor))
        return;
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

bool EditorSourceNavigationUi::active() const
{
    return hasActiveHover();
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
    if (hasActiveHover())
        clearHover(editor, selections);
}

void EditorSourceNavigationUi::closeForEditor(
    MyCodeEditor* editor,
    EditorSelection& selections)
{
    clearHover(editor, selections);
}

void EditorSourceNavigationUi::shutdown()
{
    closePopup();
    popup.reset();
    modeController = nullptr;
    boundEditor = nullptr;
    boundSelections = nullptr;
}

void EditorSourceNavigationUi::handleContextMenu(
    MyCodeEditor* editor,
    QContextMenuEvent* event,
    const EditorSourceContextProvider& contextProvider)
{
    std::unique_ptr<QMenu> menu =
        std::make_unique<QMenu>(editor);
    const QTextCursor cursorAtPos = editor->cursorForPosition(event->pos());
    emit editor->sourceSymbolContextMenuRequested(
        menu.get(),
        contextProvider(sourceSymbolContextPositionForMenu(editor, cursorAtPos),
                        true));
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
    if (editor->syntaxCommentAt(cursor.position()))
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
    if (!target.matched || target.text.isEmpty())
        return false;

    QTextCursor clickCursor = editor->cursorForPosition(position);
    clickCursor.clearSelection();
    editor->setTextCursor(clickCursor);

    emit editor->sourceNavigationRequested(
        target,
        contextProvider(target.cursorPosition, false));
    return true;
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
        editor->numericLiteralAt(cursor.position());
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
            hoverPopup->setTransientPreview(sourceHover.isCtrlPressed());
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
        contextProvider(target.cursorPosition, !previewMode);
    EditorHoverPopup* hoverPopup = ensurePopup(editor);
    hoverPopup->setTransientPreview(previewMode);
    const QPoint globalPosition = editor->viewport()->mapToGlobal(position);
    if (previewMode) {
        const DefinitionPreviewReport report =
            service->definitionPreviewReport(context);
        if (definitionPreviewTargetIsCurrentLocation(context, report)) {
            closePopup();
            return;
        }
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
    if (editor)
        editor->viewport()->setCursor(Qt::IBeamCursor);
    if (sourceHover.hasTarget()) {
        selections.clearHoveredSymbol(editor);
        sourceHover.clearTarget();
    }
    if (popup && popup->isVisible())
        closePopup();
    else {
        popupStartPos = -1;
        popupEndPos = -1;
        popupNumericMode = false;
        popupPreviewMode = false;
        popupPinnedBySelection = false;
    }
}

void EditorSourceNavigationUi::closePopup()
{
    if (popup)
        popup->closePopup();
    popupStartPos = -1;
    popupEndPos = -1;
    popupNumericMode = false;
    popupPreviewMode = false;
    popupPinnedBySelection = false;
}

bool EditorSourceNavigationUi::hasActiveHover() const
{
    return sourceHover.hasTarget() || (popup && popup->isVisible());
}

EditorHoverPopup* EditorSourceNavigationUi::ensurePopup(MyCodeEditor* editor)
{
    if (!popup)
        popup = std::make_unique<EditorHoverPopup>(
            editor,
            EditorHoverPopup::PlacementMode::EmbeddedChild);
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

bool EditorSourceNavigationUi::popupSelectionStillActive(
    MyCodeEditor* editor) const
{
    if (!editor || !popup || !popup->isVisible()
        || popupStartPos < 0 || popupEndPos <= popupStartPos) {
        return false;
    }

    const QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection())
        return false;

    const int selectionStart = popupNumericMode
        ? numericPopupSelectionStart(
              editor->cachedDocumentSlice(popupStartPos,
                                          popupEndPos - popupStartPos),
              popupStartPos)
        : popupStartPos;

    return cursor.selectionStart() <= selectionStart
        && cursor.selectionEnd() >= popupEndPos;
}
