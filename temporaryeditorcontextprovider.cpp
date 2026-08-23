#include "temporaryeditorcontextprovider.h"

#include "mycodeeditor.h"
#include "shareddocument.h"
#include "tabmanager.h"
#include "temporaryeditorcontextview.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QTextBlock>
#include <QTextCursor>
#include <QUrl>

#include <utility>

namespace {
const QString kPrimaryResourceId = QStringLiteral("primary");
const QString kWorkspaceRelativePath =
    QStringLiteral("workspaceRelativePath");

QString normalizedPath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return {};
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
}

bool isInsideRoot(const QString& root, const QString& path)
{
    const QString cleanRoot = normalizedPath(root);
    const QString cleanPath = normalizedPath(path);
    if (cleanRoot.isEmpty() || cleanPath.isEmpty())
        return false;
    const QString prefix = cleanRoot.endsWith(QLatin1Char('/'))
        ? cleanRoot
        : cleanRoot + QLatin1Char('/');
#ifdef Q_OS_WIN
    return cleanPath.compare(cleanRoot, Qt::CaseInsensitive) == 0
        || cleanPath.startsWith(prefix, Qt::CaseInsensitive);
#else
    return cleanPath == cleanRoot || cleanPath.startsWith(prefix);
#endif
}
}

TemporaryEditorContextProvider::TemporaryEditorContextProvider(
    TabManager* tabManager)
    : tabManagerValue(tabManager)
{
}

QString TemporaryEditorContextProvider::staticProviderId()
{
    return QStringLiteral("temporaryEditor");
}

ContextResource TemporaryEditorContextProvider::resourceForLocation(
    const EditorLocation& location,
    const QString& workspaceId)
{
    ContextResource resource;
    if (!location.isValid())
        return resource;
    resource.providerId = staticProviderId();
    resource.resourceId = kPrimaryResourceId;
    resource.uri = QUrl(QStringLiteral(
        "zeroslack://temporary-editor/primary"));
    resource.title = location.displayText();
    resource.iconKey = QStringLiteral("document-edit");
    resource.workspaceId = workspaceId;
    resource.state.insert(
        QStringLiteral("location"),
        editorLocationActionParameters(location));
    return resource;
}

ContextResource TemporaryEditorContextProvider::resourceForCurrentEditor(
    TabManager* tabManager,
    const QString& workspaceId)
{
    MyCodeEditor* editor = tabManager
        ? tabManager->getCurrentEditor()
        : nullptr;
    SharedDocument* document = editor && tabManager
        ? tabManager->sharedDocumentForEditor(editor)
        : nullptr;
    if (!editor || !document)
        return {};

    EditorLocation location;
    location.documentId = document->documentId();
    location.filePath = document->fileName();
    const QTextCursor cursor = editor->textCursor();
    const QTextBlock block = cursor.block();
    location.line = block.isValid()
        ? block.blockNumber() + 1
        : 1;
    location.column = block.isValid()
        ? cursor.position() - block.position() + 1
        : 1;
    return resourceForLocation(location, workspaceId);
}

EditorLocation TemporaryEditorContextProvider::locationFromResource(
    const ContextResource& resource)
{
    return editorLocationFromActionParameters(
        resource.state.value(QStringLiteral("location")).toMap());
}

QString TemporaryEditorContextProvider::providerId() const
{
    return staticProviderId();
}

QString TemporaryEditorContextProvider::displayName() const
{
    return QStringLiteral("Temporary Editor");
}

QString TemporaryEditorContextProvider::iconKey() const
{
    return QStringLiteral("document-edit");
}

bool TemporaryEditorContextProvider::canOpen(
    const ContextResource& resource) const
{
    return tabManagerValue
        && resource.providerId == providerId()
        && resource.resourceId == kPrimaryResourceId
        && locationFromResource(resource).isValid();
}

QWidget* TemporaryEditorContextProvider::createView(
    const ContextResource&,
    QWidget* parent)
{
    if (!tabManagerValue)
        return nullptr;
    auto* view = new TemporaryEditorContextView(
        tabManagerValue, parent);
    view->setSearchProvider(searchProvider);
    return view;
}

bool TemporaryEditorContextProvider::activateView(
    QWidget* view,
    const ContextResource& resource)
{
    auto* editorView =
        qobject_cast<TemporaryEditorContextView*>(view);
    const EditorLocation location = locationFromResource(resource);
    if (!editorView || !location.isValid())
        return false;
    editorView->setProperty(
        "contextWorkspaceId", resource.workspaceId);
    editorView->setSearchProvider(searchProvider);
    return editorView->openLocation(location);
}

ContextViewCapabilities
TemporaryEditorContextProvider::capabilities(
    const ContextResource&) const
{
    ContextViewCapabilities result;
    result.presentations = ContextPresentation::Peek
        | ContextPresentation::Pinned;
    result.minimumWidth = 360;
    result.preferredWidth = 580;
    result.maximumWidth = 900;
    return result;
}

void TemporaryEditorContextProvider::observeViewResourceChanges(
    QWidget* view,
    QObject* context,
    ResourceUpdateHandler handler)
{
    auto* editorView =
        qobject_cast<TemporaryEditorContextView*>(view);
    if (!editorView || !context || !handler)
        return;
    QObject::connect(
        editorView,
        &TemporaryEditorContextView::currentLocationChanged,
        context,
        [editorView, handler = std::move(handler)](
            const EditorLocation& location) {
            handler(resourceForLocation(
                location,
                editorView->property("contextWorkspaceId")
                    .toString()));
        });
}

QVariantMap TemporaryEditorContextProvider::saveViewState(
    QWidget* view) const
{
    auto* editorView =
        qobject_cast<TemporaryEditorContextView*>(view);
    return editorView ? editorView->saveState() : QVariantMap{};
}

void TemporaryEditorContextProvider::restoreViewState(
    QWidget* view,
    const QVariantMap& state)
{
    auto* editorView =
        qobject_cast<TemporaryEditorContextView*>(view);
    if (!editorView)
        return;
    const EditorLocation location = editorLocationFromActionParameters(
        state.value(QStringLiteral("location")).toMap());
    if (location.isValid())
        editorView->openLocation(location);
}

ContextResource
TemporaryEditorContextProvider::resourceForPersistence(
    const ContextResource& resource,
    QWidget* view,
    const QString& workspaceRoot) const
{
    auto* editorView =
        qobject_cast<TemporaryEditorContextView*>(view);
    if (!editorView)
        return {};
    EditorLocation location = editorView->currentLocation();
    if (!location.isValid()
        || !isInsideRoot(workspaceRoot, location.filePath)) {
        return {};
    }

    QVariantMap persistedLocation =
        editorLocationActionParameters(location);
    persistedLocation.remove(QStringLiteral("documentId"));
    persistedLocation.remove(QStringLiteral("path"));
    persistedLocation.insert(
        kWorkspaceRelativePath,
        QDir(normalizedPath(workspaceRoot)).relativeFilePath(
            normalizedPath(location.filePath)));

    ContextResource persisted = resource;
    persisted.workspaceId.clear();
    persisted.state.clear();
    persisted.state.insert(
        QStringLiteral("location"), persistedLocation);
    return persisted;
}

ContextResource
TemporaryEditorContextProvider::resourceFromPersistence(
    const ContextResource& resource,
    const QString& workspaceRoot) const
{
    QVariantMap locationData =
        resource.state.value(QStringLiteral("location")).toMap();
    const QString relativePath =
        locationData.take(kWorkspaceRelativePath).toString();
    if (relativePath.trimmed().isEmpty()
        || workspaceRoot.trimmed().isEmpty()) {
        return {};
    }

    const QString absolutePath = normalizedPath(
        QDir(normalizedPath(workspaceRoot)).absoluteFilePath(relativePath));
    if (!isInsideRoot(workspaceRoot, absolutePath)
        || !QFileInfo(absolutePath).isFile()) {
        return {};
    }
    locationData.remove(QStringLiteral("documentId"));
    locationData.insert(QStringLiteral("path"), absolutePath);
    const EditorLocation location =
        editorLocationFromActionParameters(locationData);
    if (!location.isValid())
        return {};

    ContextResource restored = resourceForLocation(
        location, normalizedPath(workspaceRoot));
    restored.state.insert(
        QStringLiteral("location"), locationData);
    return restored;
}

void TemporaryEditorContextProvider::setSearchProvider(
    SearchProvider provider)
{
    searchProvider = std::move(provider);
}
