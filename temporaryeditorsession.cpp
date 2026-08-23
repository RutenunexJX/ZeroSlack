#include "temporaryeditorsession.h"

#include "mycodeeditor.h"
#include "tabmanager.h"

#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QWidget>

#include <algorithm>

namespace {
int positionForLineColumn(QTextDocument* document,
                          int line,
                          int column)
{
    if (!document)
        return 0;
    const int boundedLine =
        std::clamp(line, 1, std::max(1, document->blockCount()));
    const QTextBlock block =
        document->findBlockByNumber(boundedLine - 1);
    if (!block.isValid())
        return 0;
    const int lastColumnOffset = std::max(0, block.length() - 1);
    return block.position()
        + std::clamp(column - 1, 0, lastColumnOffset);
}
}

TemporaryEditorSession::TemporaryEditorSession(
    TabManager* tabManager,
    QObject* parent)
    : QObject(parent)
    , tabManagerValue(tabManager)
{
    if (!tabManagerValue)
        return;
    connect(tabManagerValue,
            &TabManager::auxiliaryViewAboutToClose,
            this,
            &TemporaryEditorSession::handleAuxiliaryViewAboutToClose);
    connect(tabManagerValue,
            &TabManager::documentIdentityChanged,
            this,
            &TemporaryEditorSession::handleDocumentIdentityChanged);
}

TemporaryEditorSession::~TemporaryEditorSession()
{
    close();
}

MyCodeEditor* TemporaryEditorSession::editor() const
{
    return editorValue;
}

EditorLocation TemporaryEditorSession::currentLocation() const
{
    if (currentHistoryIndex < 0
        || currentHistoryIndex >= history.size()) {
        return {};
    }
    return history.at(currentHistoryIndex).location;
}

bool TemporaryEditorSession::isOpen() const
{
    return editorValue && currentHistoryIndex >= 0;
}

bool TemporaryEditorSession::canGoBack() const
{
    return currentHistoryIndex > 0;
}

bool TemporaryEditorSession::canGoForward() const
{
    return currentHistoryIndex >= 0
        && currentHistoryIndex + 1 < history.size();
}

int TemporaryEditorSession::historyCount() const
{
    return history.size();
}

int TemporaryEditorSession::historyIndex() const
{
    return currentHistoryIndex;
}

void TemporaryEditorSession::setViewParent(QWidget* parent)
{
    viewParentValue = parent;
}

bool TemporaryEditorSession::saveCurrent(bool forceSaveAs)
{
    return tabManagerValue && editorValue
        && tabManagerValue->saveAuxiliaryView(
            editorValue, forceSaveAs);
}

bool TemporaryEditorSession::openLocation(
    const EditorLocation& location)
{
    if (!location.isValid() || !tabManagerValue || !viewParentValue) {
        emit openFailed(location);
        return false;
    }

    if (isOpen() && currentLocation().equivalentTo(location)) {
        applyInitialLocation(location);
        editorValue->setFocus();
        return true;
    }

    captureCurrentViewState();
    HistoryEntry next;
    next.location = location;
    if (!activateEntry(&next)) {
        emit openFailed(location);
        return false;
    }

    while (history.size() > currentHistoryIndex + 1)
        history.removeLast();
    history.append(next);
    currentHistoryIndex = history.size() - 1;
    updateNavigationAvailability();
    emit currentLocationChanged(next.location);
    return true;
}

bool TemporaryEditorSession::goBack()
{
    if (!canGoBack())
        return false;
    captureCurrentViewState();
    const int targetIndex = currentHistoryIndex - 1;
    if (!activateEntry(&history[targetIndex]))
        return false;
    currentHistoryIndex = targetIndex;
    updateNavigationAvailability();
    emit currentLocationChanged(history.at(targetIndex).location);
    return true;
}

bool TemporaryEditorSession::goForward()
{
    if (!canGoForward())
        return false;
    captureCurrentViewState();
    const int targetIndex = currentHistoryIndex + 1;
    if (!activateEntry(&history[targetIndex]))
        return false;
    currentHistoryIndex = targetIndex;
    updateNavigationAvailability();
    emit currentLocationChanged(history.at(targetIndex).location);
    return true;
}

void TemporaryEditorSession::close()
{
    if (closing)
        return;
    closing = true;
    const bool hadContent = editorValue || currentHistoryIndex >= 0;
    if (tabManagerValue && editorValue)
        tabManagerValue->closeAuxiliaryView(editorValue);
    editorValue.clear();
    history.clear();
    currentHistoryIndex = -1;
    updateNavigationAvailability();
    closing = false;
    if (hadContent)
        emit closed();
}

void TemporaryEditorSession::captureCurrentViewState()
{
    if (!tabManagerValue || !editorValue
        || currentHistoryIndex < 0
        || currentHistoryIndex >= history.size()) {
        return;
    }
    SharedDocument* document =
        tabManagerValue->sharedDocumentForEditor(editorValue);
    if (!document)
        return;
    HistoryEntry& entry = history[currentHistoryIndex];
    entry.viewState = document->viewState(editorValue);
    entry.trackedCursor = editorValue->textCursor();
    entry.hasViewState = true;
}

bool TemporaryEditorSession::activateEntry(HistoryEntry* entry)
{
    if (!entry || !ensureEditorFor(entry))
        return false;

    if (entry->hasViewState)
        applyViewState(entry->viewState, entry->trackedCursor);
    else
        applyInitialLocation(entry->location);
    normalizeLocation(entry);
    editorValue->setFocus();
    return true;
}

bool TemporaryEditorSession::ensureEditorFor(HistoryEntry* entry)
{
    if (!entry || !tabManagerValue || !viewParentValue)
        return false;

    if (!editorValue) {
        editorValue = tabManagerValue->createAuxiliaryView(
            entry->location.documentId,
            entry->location.filePath,
            viewParentValue,
            entry->hasViewState
                ? entry->viewState
                : SharedDocumentViewState());
        if (editorValue)
            emit editorChanged(editorValue);
        return !editorValue.isNull();
    }

    SharedDocument* current =
        tabManagerValue->sharedDocumentForEditor(editorValue);
    const bool sameDocument = current
        && ((!entry->location.documentId.isEmpty()
             && current->documentId() == entry->location.documentId)
            || (!entry->location.filePath.isEmpty()
                && EditorFileIdentity::same(
                    current->fileName(),
                    entry->location.filePath)));
    if (sameDocument)
        return true;

    return tabManagerValue->rebindAuxiliaryView(
        editorValue,
        entry->location.documentId,
        entry->location.filePath,
        entry->hasViewState
            ? entry->viewState
            : SharedDocumentViewState());
}

void TemporaryEditorSession::applyInitialLocation(
    const EditorLocation& location)
{
    if (!editorValue || !editorValue->document())
        return;
    QTextDocument* document = editorValue->document();
    QTextCursor cursor(document);
    if (location.selection && location.selection->isValid()) {
        const EditorSelectionRange& range = *location.selection;
        cursor.setPosition(positionForLineColumn(
            document, range.startLine, range.startColumn));
        cursor.setPosition(positionForLineColumn(
            document, range.endLine, range.endColumn),
                           QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(positionForLineColumn(
            document, location.line, location.column));
    }
    editorValue->setTextCursor(cursor);
    editorValue->centerCursor();
}

void TemporaryEditorSession::applyViewState(
    const SharedDocumentViewState& state,
    const QTextCursor& trackedCursor)
{
    if (!editorValue || !editorValue->document())
        return;
    QTextDocument* document = editorValue->document();
    const int maxPosition =
        std::max(0, document->characterCount() - 1);
    QTextCursor cursor;
    if (!trackedCursor.isNull()
        && trackedCursor.document() == document) {
        cursor = trackedCursor;
    } else {
        cursor = QTextCursor(document);
        cursor.setPosition(
            std::clamp(state.anchorPosition, 0, maxPosition));
        cursor.setPosition(
            std::clamp(state.cursorPosition, 0, maxPosition),
            QTextCursor::KeepAnchor);
    }
    editorValue->setTextCursor(cursor);
    editorValue->restoreFoldingViewState(state.folding);
    if (QScrollBar* bar = editorValue->verticalScrollBar())
        bar->setValue(std::max(0, state.verticalScrollValue));
    if (QScrollBar* bar = editorValue->horizontalScrollBar())
        bar->setValue(std::max(0, state.horizontalScrollValue));
}

void TemporaryEditorSession::handleAuxiliaryViewAboutToClose(
    MyCodeEditor* editor)
{
    if (closing || !editor || editorValue != editor)
        return;
    closing = true;
    editorValue.clear();
    history.clear();
    currentHistoryIndex = -1;
    updateNavigationAvailability();
    closing = false;
    emit editorChanged(nullptr);
    emit closed();
}

void TemporaryEditorSession::handleDocumentIdentityChanged(
    const QString& previousDocumentId,
    const QString& previousFileName,
    const QString& documentId,
    const QString& fileName)
{
    bool currentEntryChanged = false;
    for (int index = 0; index < history.size(); ++index) {
        EditorLocation& location = history[index].location;
        const bool stableIdentityMatches =
            !previousDocumentId.isEmpty()
            && !location.documentId.isEmpty()
            && location.documentId == previousDocumentId;
        const bool pathIdentityMatches =
            (previousDocumentId.isEmpty()
             || location.documentId.isEmpty())
            && !previousFileName.isEmpty()
            && EditorFileIdentity::same(
                location.filePath,
                previousFileName);
        if (!stableIdentityMatches && !pathIdentityMatches)
            continue;
        location.documentId = documentId;
        location.filePath = fileName;
        currentEntryChanged =
            currentEntryChanged || index == currentHistoryIndex;
    }

    if (currentEntryChanged)
        emit currentLocationChanged(currentLocation());
}

void TemporaryEditorSession::normalizeLocation(
    HistoryEntry* entry) const
{
    if (!entry || !tabManagerValue || !editorValue)
        return;
    SharedDocument* document =
        tabManagerValue->sharedDocumentForEditor(editorValue);
    if (!document)
        return;
    entry->location.documentId = document->documentId();
    entry->location.filePath = document->fileName();
}

void TemporaryEditorSession::updateNavigationAvailability()
{
    emit navigationAvailabilityChanged(
        canGoBack(), canGoForward());
}
