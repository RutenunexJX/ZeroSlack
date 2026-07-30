#include "editorsignalselectioncontroller.h"

#include "definitionservice.h"
#include "editorgeometry.h"
#include "editormodecontroller.h"
#include "editorruntime.h"
#include "editorselection.h"
#include "effectivevalueservice.h"
#include "mycodeeditor.h"

#include <QMouseEvent>
#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>

#include <algorithm>

void EditorSignalSelectionController::bind(
    EditorModeController* modes,
    EditorSelection* selections,
    MyCodeEditor* editor,
    CandidateResolver resolver)
{
    modeController = modes;
    selectionPresenter = selections;
    candidateResolver = std::move(resolver);
    if (!modeController)
        return;

    const QPointer<MyCodeEditor> target(editor);
    modeController->setExitHandler(
        EditorModeId::SignalSelection,
        [this, target](EditorModeExitReason reason) {
            resetGesture();
            if (reason
                    == EditorModeExitReason::Completed
                && resultPending) {
                return;
            }
            clearSelection(target);
        });
}

void EditorSignalSelectionController::shutdown(
    MyCodeEditor* editor)
{
    resetGesture();
    clearSelection(editor);
    resultPending = false;
    candidateResolver = {};
    modeController = nullptr;
    selectionPresenter = nullptr;
}

void EditorSignalSelectionController::resetGesture()
{
    dragging = false;
    dragSelect = false;
    lastDragIdentity.clear();
    lastDragPoint = QPoint(-1, -1);
}

void EditorSignalSelectionController::clearSelection(
    MyCodeEditor* editor)
{
    selected.clear();
    if (editor && selectionPresenter)
        selectionPresenter->clearSignalSelections(editor);
}

void EditorSignalSelectionController::refreshOverlay(
    MyCodeEditor* editor)
{
    if (!editor || !selectionPresenter)
        return;
    QList<QPair<int, int>> ranges;
    ranges.reserve(selected.size());
    for (const EditorSignalSelectionCandidate& signal :
         std::as_const(selected)) {
        if (signal.start >= 0
            && signal.length > 0) {
            ranges.append(
                qMakePair(signal.start,
                          signal.length));
        }
    }
    std::sort(
        ranges.begin(),
        ranges.end(),
        [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
    selectionPresenter->highlightSignalSelections(
        editor,
        ranges);
}

bool EditorSignalSelectionController::start(
    MyCodeEditor* editor,
    QString* message)
{
    if (!editor || !editor->document()) {
        if (message) {
            *message =
                QStringLiteral("No editor document");
        }
        return false;
    }
    if (!modeController) {
        if (message) {
            *message = QStringLiteral(
                "Editor mode controller unavailable");
        }
        return false;
    }

    clearSelection(editor);
    resetGesture();
    resultPending = false;
    modeController->enter(
        EditorModeId::SignalSelection,
        EditorModeEntryReason::UserAction);
    modeController->updatePresentation(
        EditorModeId::SignalSelection,
        QStringLiteral("Signal selection: 0"),
        QStringLiteral(
            "Left-drag to check; right-click finishes; Esc cancels"));
    if (message)
        *message = QStringLiteral("Select signals");
    emit editor->editorStatusMessageRequested(
        QStringLiteral(
            "Select signals: left-drag to check signals, right-click for actions, Esc to cancel"));
    return true;
}

void EditorSignalSelectionController::cancel(
    MyCodeEditor* editor)
{
    const bool wasActive = active();
    resetGesture();
    clearSelection(editor);
    resultPending = false;
    if (wasActive && modeController) {
        modeController->exit(
            EditorModeId::SignalSelection,
            EditorModeExitReason::Canceled);
    }
}

bool EditorSignalSelectionController::active() const
{
    return modeController
        && modeController->isActive(
            EditorModeId::SignalSelection);
}

bool EditorSignalSelectionController::hasSelection() const
{
    return !selected.isEmpty();
}

QStringList
EditorSignalSelectionController::selectedNames() const
{
    QList<EditorSignalSelectionCandidate> ordered =
        selected.values();
    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const EditorSignalSelectionCandidate& left,
           const EditorSignalSelectionCandidate& right) {
            if (left.start != right.start)
                return left.start < right.start;
            return left.name < right.name;
        });
    QStringList names;
    names.reserve(ordered.size());
    for (const EditorSignalSelectionCandidate& signal :
         ordered) {
        names.append(signal.name);
    }
    return names;
}

bool EditorSignalSelectionController::toggleAt(
    MyCodeEditor* editor,
    int cursorPosition,
    bool toggle,
    bool desiredState)
{
    if (!active()
        || !editor
        || !candidateResolver) {
        return false;
    }
    const std::optional<EditorSignalSelectionCandidate>
        resolved = candidateResolver(cursorPosition);
    if (!resolved || !resolved->isValid())
        return false;

    const bool currentlySelected =
        selected.contains(resolved->identity);
    const bool shouldSelect =
        toggle ? !currentlySelected : desiredState;
    if (shouldSelect) {
        selected.insert(resolved->identity, *resolved);
    } else {
        selected.remove(resolved->identity);
    }
    lastDragIdentity = resolved->identity;
    refreshOverlay(editor);
    modeController->updatePresentation(
        EditorModeId::SignalSelection,
        QStringLiteral("Signal selection: %1")
            .arg(selected.size()),
        QStringLiteral(
            "Left-drag to check; right-click finishes; Esc cancels"));
    return true;
}

void EditorSignalSelectionController::
    completeForContextMenu()
{
    if (!active() || !modeController)
        return;
    resultPending = true;
    modeController->exit(
        EditorModeId::SignalSelection,
        EditorModeExitReason::Completed);
    resultPending = false;
}

bool EditorSignalSelectionController::handleMousePress(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (!active()
        || !editor
        || !event
        || event->button() != Qt::LeftButton) {
        return false;
    }
    const QTextCursor target =
        editor->cursorForPosition(
            event->position().toPoint());
    lastDragPoint = event->position().toPoint();
    lastDragIdentity.clear();
    const bool resolved =
        toggleAt(editor, target.position(), true);
    dragging = true;
    dragSelect =
        !resolved
        || selected.contains(lastDragIdentity);
    event->accept();
    return true;
}

bool EditorSignalSelectionController::handleMouseMove(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (!active()
        || !dragging
        || !editor
        || !event
        || !event->buttons().testFlag(
            Qt::LeftButton)) {
        return false;
    }

    const QPoint currentPoint =
        event->position().toPoint();
    const QPoint previousPoint =
        lastDragPoint.x() >= 0
        ? lastDragPoint
        : currentPoint;
    const int previousLine =
        editor->cursorForPosition(
            QPoint(0, previousPoint.y()))
            .block()
            .blockNumber();
    const int currentLine =
        editor->cursorForPosition(
            QPoint(0, currentPoint.y()))
            .block()
            .blockNumber();
    const int lineStep =
        currentLine >= previousLine ? 1 : -1;
    for (int line = previousLine;;
         line += lineStep) {
        const EditorBlockGeometry geometry =
            editor->blockGeometry(line);
        const int y = qRound(
            geometry.top + geometry.height / 2.0);
        qreal progress = 1.0;
        if (currentPoint.y() != previousPoint.y()) {
            progress =
                static_cast<qreal>(
                    y - previousPoint.y())
                / static_cast<qreal>(
                    currentPoint.y()
                    - previousPoint.y());
        }
        progress = qBound<qreal>(
            0.0,
            progress,
            1.0);
        const int x = qRound(
            previousPoint.x()
            + (currentPoint.x()
               - previousPoint.x())
                  * progress);
        const QTextCursor target =
            editor->cursorForPosition(
                QPoint(x, y));
        toggleAt(
            editor,
            target.position(),
            false,
            dragSelect);
        if (line == currentLine)
            break;
    }
    lastDragPoint = currentPoint;
    event->accept();
    return true;
}

bool EditorSignalSelectionController::handleMouseRelease(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    Q_UNUSED(editor)
    if (!dragging
        || !event
        || event->button() != Qt::LeftButton) {
        return false;
    }
    resetGesture();
    event->accept();
    return true;
}
std::optional<EditorSignalSelectionCandidate>
MyCodeEditorState::resolveSignalSelectionCandidate(
    const MyCodeEditor* editor,
    int cursorPosition) const
{
    if (!editor)
        return std::nullopt;

    const TSIdentifierTarget identifier =
        syntax.identifierAt(cursorPosition);
    if (!identifier.ok())
        return std::nullopt;

    const EditorSemanticContext context =
        semanticContextForPosition(
            editor,
            identifier.startChar,
            false);
    DefinitionQuery query;
    query.symbolName = identifier.text;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.linePrefixBeforeCursor =
        context.lineUpToCursor;
    query.cursorLine = context.cursorLine;
    query.cursorColumn = context.column;
    const DefinitionResult definition =
        DefinitionService::getInstance()
            ->resolveDefinition(query);
    if (!definition.found)
        return std::nullopt;

    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(
            definition.symbolRecord);
    if (!SymbolTaxonomy::isSignalDeclaration(metadata)
        && !SymbolTaxonomy::isPortDeclaration(metadata)) {
        return std::nullopt;
    }
    if (EditorFileIdentity::same(
            definition.symbolRecord.location.fileName,
            identity.current())) {
        const std::uint64_t recordRevision =
            definition.symbolRecord.presentation
                .documentRevision;
        if (recordRevision != 0
            && recordRevision
                   != semanticDocumentRevision()) {
            return std::nullopt;
        }
        if (recordRevision == 0
            && editor->document()->isModified()) {
            return std::nullopt;
        }
    }

    const QString stableIdentity =
        definition.symbolRecord.stableKey.isValid()
        ? definition.symbolRecord.stableKey.toString()
        : EffectiveValueService::stableSourceIdentity(
              definition.symbolRecord);
    if (stableIdentity.isEmpty())
        return std::nullopt;

    EditorSignalSelectionCandidate candidate;
    candidate.identity = stableIdentity;
    candidate.name = identifier.text;
    candidate.start = identifier.startChar;
    candidate.length =
        identifier.endChar - identifier.startChar;
    return candidate;
}
