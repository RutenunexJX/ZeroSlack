#include "temporaryeditordrawercontroller.h"

#include "mycodeeditor.h"
#include "tabmanager.h"
#include "temporaryeditordrawer.h"

#include <QScrollBar>
#include <QSettings>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace {
constexpr auto kSettingsOrganization = "ZeroSlack";
constexpr auto kSettingsApplication = "ZeroSlack";
constexpr auto kEdgeKey = "temporaryEditorDrawer/edge";
constexpr auto kPreferredSizeKey =
    "temporaryEditorDrawer/preferredSize";

TemporaryEditorDrawer::Edge storedEdge(int value)
{
    using Edge = TemporaryEditorDrawer::Edge;
    switch (static_cast<Edge>(value)) {
    case Edge::None:
    case Edge::Left:
    case Edge::Right:
    case Edge::Top:
    case Edge::Bottom:
        return static_cast<Edge>(value);
    }
    return Edge::None;
}

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

TemporaryEditorDrawerController::TemporaryEditorDrawerController(
    TabManager* tabManager,
    QWidget* editorRegion,
    QObject* parent)
    : TemporaryEditorDrawerController(
          tabManager,
          editorRegion,
          std::make_unique<QSettings>(
              QString::fromLatin1(kSettingsOrganization),
              QString::fromLatin1(kSettingsApplication)),
          parent)
{
}

TemporaryEditorDrawerController::TemporaryEditorDrawerController(
    TabManager* tabManager,
    QWidget* editorRegion,
    std::unique_ptr<QSettings> settings,
    QObject* parent)
    : QObject(parent)
    , tabManagerValue(tabManager)
    , settingsValue(std::move(settings))
{
    if (editorRegion)
        drawerValue = new TemporaryEditorDrawer(editorRegion);
    if (tabManagerValue) {
        connect(tabManagerValue,
                &TabManager::auxiliaryViewAboutToClose,
                this,
                &TemporaryEditorDrawerController::
                    handleAuxiliaryViewAboutToClose);
        connect(tabManagerValue,
                &TabManager::documentIdentityChanged,
                this,
                &TemporaryEditorDrawerController::
                    handleDocumentIdentityChanged);
    }
    loadPreferences();
    connectDrawer();
    updateNavigationAvailability();
}

TemporaryEditorDrawerController::~TemporaryEditorDrawerController()
{
    closeDrawer();
}

TemporaryEditorDrawer* TemporaryEditorDrawerController::drawer() const
{
    return drawerValue.data();
}

MyCodeEditor* TemporaryEditorDrawerController::editor() const
{
    return editorValue.data();
}

EditorLocation
TemporaryEditorDrawerController::currentLocation() const
{
    if (currentHistoryIndex < 0
        || currentHistoryIndex >= history.size()) {
        return {};
    }
    return history.at(currentHistoryIndex).location;
}

bool TemporaryEditorDrawerController::isOpen() const
{
    return drawerValue && editorValue
        && currentHistoryIndex >= 0;
}

bool TemporaryEditorDrawerController::canGoBack() const
{
    return currentHistoryIndex > 0;
}

bool TemporaryEditorDrawerController::canGoForward() const
{
    return currentHistoryIndex >= 0
        && currentHistoryIndex + 1 < history.size();
}

int TemporaryEditorDrawerController::historyCount() const
{
    return history.size();
}

int TemporaryEditorDrawerController::historyIndex() const
{
    return currentHistoryIndex;
}

void TemporaryEditorDrawerController::setSearchProvider(
    SearchProvider provider)
{
    searchProvider = std::move(provider);
}

bool TemporaryEditorDrawerController::saveCurrent(bool forceSaveAs)
{
    return tabManagerValue && editorValue
        && tabManagerValue->saveAuxiliaryView(
            editorValue.data(), forceSaveAs);
}

bool TemporaryEditorDrawerController::openLocation(
    const EditorLocation& location)
{
    if (!location.isValid() || !drawerValue || !tabManagerValue) {
        emit openFailed(location);
        return false;
    }

    if (isOpen()
        && currentLocation().equivalentTo(location)) {
        applyInitialLocation(location);
        drawerValue->open(currentLocation());
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
    drawerValue->setLocation(next.location);
    updateNavigationAvailability();
    emit currentLocationChanged(next.location);
    return true;
}

void TemporaryEditorDrawerController::goBack()
{
    if (!canGoBack())
        return;
    captureCurrentViewState();
    const int targetIndex = currentHistoryIndex - 1;
    if (!activateEntry(&history[targetIndex]))
        return;
    currentHistoryIndex = targetIndex;
    drawerValue->setLocation(history.at(targetIndex).location);
    updateNavigationAvailability();
    emit currentLocationChanged(history.at(targetIndex).location);
}

void TemporaryEditorDrawerController::goForward()
{
    if (!canGoForward())
        return;
    captureCurrentViewState();
    const int targetIndex = currentHistoryIndex + 1;
    if (!activateEntry(&history[targetIndex]))
        return;
    currentHistoryIndex = targetIndex;
    drawerValue->setLocation(history.at(targetIndex).location);
    updateNavigationAvailability();
    emit currentLocationChanged(history.at(targetIndex).location);
}

void TemporaryEditorDrawerController::closeDrawer()
{
    if (closing)
        return;
    closing = true;
    const bool hadContent = editorValue
        || currentHistoryIndex >= 0;

    if (drawerValue) {
        drawerValue->takeEditorWidget();
        drawerValue->setState(
            TemporaryEditorDrawer::State::Hidden);
    }
    if (tabManagerValue && editorValue) {
        tabManagerValue->closeAuxiliaryView(
            editorValue.data());
    }
    editorValue.clear();
    history.clear();
    currentHistoryIndex = -1;
    updateNavigationAvailability();
    closing = false;
    if (hadContent)
        emit closed();
}

void TemporaryEditorDrawerController::connectDrawer()
{
    if (!drawerValue)
        return;
    connect(drawerValue,
            &TemporaryEditorDrawer::openRequested,
            this,
            [this](const EditorLocation& location) {
                openLocation(location);
            });
    connect(drawerValue,
            &TemporaryEditorDrawer::backRequested,
            this,
            &TemporaryEditorDrawerController::goBack);
    connect(drawerValue,
            &TemporaryEditorDrawer::forwardRequested,
            this,
            &TemporaryEditorDrawerController::goForward);
    connect(drawerValue,
            &TemporaryEditorDrawer::closeRequested,
            this,
            &TemporaryEditorDrawerController::closeDrawer);
    connect(drawerValue,
            &TemporaryEditorDrawer::pinChanged,
            this,
            &TemporaryEditorDrawerController::pinnedChanged);
    connect(drawerValue,
            &TemporaryEditorDrawer::geometryPreferenceChanged,
            this,
            [this](const QSize&,
                   TemporaryEditorDrawer::Edge) {
                persistGeometryPreference();
            });
    connect(drawerValue,
            &TemporaryEditorDrawer::searchTextChanged,
            this,
            [this](const QString& query) {
                emit searchRequested(query);
                const EditorSearchCandidates candidates =
                    searchProvider && !query.trimmed().isEmpty()
                        ? searchProvider(query)
                        : EditorSearchCandidates{};
                if (drawerValue) {
                    drawerValue->setSearchCandidates(
                        candidates, query);
                }
            });
    connect(drawerValue,
            &TemporaryEditorDrawer::searchCandidateActivated,
            this,
            [this](const EditorSearchCandidate& candidate) {
                openLocation(candidate.location);
            });
}

void TemporaryEditorDrawerController::loadPreferences()
{
    if (!drawerValue || !settingsValue)
        return;
    const QSize size = settingsValue
                           ->value(
                               QString::fromLatin1(kPreferredSizeKey),
                               drawerValue->preferredSize())
                           .toSize();
    const int edge = settingsValue
                         ->value(
                             QString::fromLatin1(kEdgeKey),
                             static_cast<int>(
                                 TemporaryEditorDrawer::Edge::None))
                         .toInt();
    drawerValue->setPreferredSize(size);
    drawerValue->setEdge(storedEdge(edge));
}

void TemporaryEditorDrawerController::persistGeometryPreference()
{
    if (!drawerValue || !settingsValue)
        return;
    settingsValue->setValue(
        QString::fromLatin1(kEdgeKey),
        static_cast<int>(drawerValue->edge()));
    settingsValue->setValue(
        QString::fromLatin1(kPreferredSizeKey),
        drawerValue->preferredSize());
    settingsValue->sync();
}

void TemporaryEditorDrawerController::captureCurrentViewState()
{
    if (!tabManagerValue || !editorValue
        || currentHistoryIndex < 0
        || currentHistoryIndex >= history.size()) {
        return;
    }
    SharedDocument* document =
        tabManagerValue->sharedDocumentForEditor(editorValue.data());
    if (!document)
        return;
    HistoryEntry& entry = history[currentHistoryIndex];
    entry.viewState = document->viewState(editorValue.data());
    entry.trackedCursor = editorValue->textCursor();
    entry.hasViewState = true;
}

bool TemporaryEditorDrawerController::activateEntry(
    HistoryEntry* entry)
{
    if (!entry || !ensureEditorFor(entry))
        return false;

    if (entry->hasViewState)
        applyViewState(entry->viewState, entry->trackedCursor);
    else
        applyInitialLocation(entry->location);
    normalizeLocation(entry);
    drawerValue->setEditorWidget(editorValue.data());
    drawerValue->open(entry->location);
    editorValue->setFocus();
    return true;
}

bool TemporaryEditorDrawerController::ensureEditorFor(
    HistoryEntry* entry)
{
    if (!entry || !tabManagerValue || !drawerValue)
        return false;

    if (!editorValue) {
        editorValue = tabManagerValue->createAuxiliaryView(
            entry->location.documentId,
            entry->location.filePath,
            drawerValue.data(),
            entry->hasViewState
                ? entry->viewState
                : SharedDocumentViewState());
        return !editorValue.isNull();
    }

    SharedDocument* current =
        tabManagerValue->sharedDocumentForEditor(editorValue.data());
    const bool sameDocument = current
        && ((!entry->location.documentId.isEmpty()
             && current->documentId()
                    == entry->location.documentId)
            || (!entry->location.filePath.isEmpty()
                && EditorFileIdentity::same(
                    current->fileName(),
                    entry->location.filePath)));
    if (sameDocument)
        return true;

    return tabManagerValue->rebindAuxiliaryView(
        editorValue.data(),
        entry->location.documentId,
        entry->location.filePath,
        entry->hasViewState
            ? entry->viewState
            : SharedDocumentViewState());
}

void TemporaryEditorDrawerController::applyInitialLocation(
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

void TemporaryEditorDrawerController::applyViewState(
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

void TemporaryEditorDrawerController::
handleAuxiliaryViewAboutToClose(MyCodeEditor* editor)
{
    if (closing || !editor || editorValue.data() != editor)
        return;

    closing = true;
    if (drawerValue) {
        drawerValue->takeEditorWidget();
        drawerValue->setState(
            TemporaryEditorDrawer::State::Hidden);
    }
    editorValue.clear();
    history.clear();
    currentHistoryIndex = -1;
    updateNavigationAvailability();
    closing = false;
    emit closed();
}

void TemporaryEditorDrawerController::
handleDocumentIdentityChanged(
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

    if (!currentEntryChanged || !drawerValue)
        return;
    const EditorLocation location = currentLocation();
    drawerValue->setLocation(location);
    emit currentLocationChanged(location);
}

void TemporaryEditorDrawerController::normalizeLocation(
    HistoryEntry* entry) const
{
    if (!entry || !tabManagerValue || !editorValue)
        return;
    SharedDocument* document =
        tabManagerValue->sharedDocumentForEditor(editorValue.data());
    if (!document)
        return;
    entry->location.documentId = document->documentId();
    entry->location.filePath = document->fileName();
}

void TemporaryEditorDrawerController::updateNavigationAvailability()
{
    const bool back = canGoBack();
    const bool forward = canGoForward();
    if (drawerValue)
        drawerValue->setNavigationAvailability(back, forward);
    emit navigationAvailabilityChanged(back, forward);
}
