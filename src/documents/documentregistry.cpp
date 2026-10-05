#include "documentregistry.h"

#include "editorfileidentity.h"
#include "mycodeeditor.h"

#include <atomic>
#include <QDir>

namespace {
std::atomic<std::uint64_t> fullTextCopyCount{0};
std::atomic<std::uint64_t> copiedCharacterCount{0};

QString stableIdKey(QString id)
{
#ifdef Q_OS_WIN
    id = id.toCaseFolded();
#endif
    return id;
}

void removeRegisteredView(QHash<QString, QSet<MyCodeEditor*>>& groups,
                          QHash<QString, MyCodeEditor*>& representatives,
                          const QString& key, MyCodeEditor* editor)
{
    auto found = groups.find(key);
    if (found == groups.end()) return;
    found->remove(editor);
    if (found->isEmpty()) {
        groups.erase(found);
        representatives.remove(key);
    } else if (representatives.value(key) == editor) {
        representatives[key] = *found->cbegin();
    }
}

DocumentSnapshot materializedSnapshot(const TrackedDocument& tracked)
{
    DocumentSnapshot snapshot = tracked.snapshot;
    if (tracked.editor)
        snapshot.text = tracked.editor->cachedDocumentText();
    return snapshot;
}
}

void recordDocumentTextCopy(qsizetype characterCount)
{
    fullTextCopyCount.fetch_add(1, std::memory_order_relaxed);
    copiedCharacterCount.fetch_add(
        static_cast<std::uint64_t>(qMax<qsizetype>(0, characterCount)),
        std::memory_order_relaxed);
}

DocumentTextCopyMetrics documentTextCopyMetricsForTest()
{
    DocumentTextCopyMetrics metrics;
    metrics.fullTextCopyCount =
        fullTextCopyCount.load(std::memory_order_relaxed);
    metrics.copiedCharacterCount =
        copiedCharacterCount.load(std::memory_order_relaxed);
    return metrics;
}

void resetDocumentTextCopyMetricsForTest()
{
    fullTextCopyCount.store(0, std::memory_order_relaxed);
    copiedCharacterCount.store(0, std::memory_order_relaxed);
}

void DocumentIndexes::add(
    MyCodeEditor* editor,
    const DocumentSnapshot& snapshot)
{
    if (!editor)
        return;

    const auto registered = registrations.constFind(editor);
    if (registered != registrations.cend()
        && registered->documentId == snapshot.documentId
        && registered->fileName == snapshot.fileName) return;
    remove(editor, snapshot);
    Registration registration;
    registration.documentId = snapshot.documentId;
    registration.fileName = snapshot.fileName;
    registration.documentKey = snapshot.fileName.isEmpty()
        || !editor->property("sharedDocumentId").toString().isEmpty()
        ? stableIdKey(snapshot.documentId)
        : EditorFileIdentity::lookupKey(snapshot.documentId);
    // The file belongs to the bound document. Its display path may already
    // have been rebound before another view of that document is attached.
    if (!snapshot.fileName.isEmpty()) {
        registration.fileKey = QDir::isAbsolutePath(snapshot.documentId)
            ? registration.documentKey : EditorFileIdentity::lookupKey(snapshot.fileName);
    }
    registrations.insert(editor, registration);
    byDocumentId[registration.documentKey] = editor;
    viewsByDocumentId[registration.documentKey].insert(editor);
    if (!registration.fileKey.isEmpty()) {
        byFileName[registration.fileKey] = editor;
        viewsByFileName[registration.fileKey].insert(editor);
    }
}

void DocumentIndexes::remove(
    MyCodeEditor* editor,
    const DocumentSnapshot& snapshot)
{
    if (!editor)
        return;

    Q_UNUSED(snapshot);
    const auto found = registrations.find(editor);
    if (found == registrations.end()) return;
    const auto registration = *found;
    registrations.erase(found);
    removeRegisteredView(viewsByDocumentId, byDocumentId, registration.documentKey, editor);
    removeRegisteredView(viewsByFileName, byFileName, registration.fileKey, editor);
}

MyCodeEditor* DocumentIndexes::editorForDocumentId(
    const QString& documentId) const
{
    return byDocumentId.value(documentKeyForId(documentId), nullptr);
}

QString DocumentIndexes::documentKeyForEditor(MyCodeEditor* editor) const
{
    return registrations.value(editor).documentKey;
}

QString DocumentIndexes::documentKeyForId(const QString& documentId) const
{
    const auto key = stableIdKey(documentId);
    // Published IDs are fixed identities. Only an unregistered path query
    // needs to resolve its current filesystem target.
    return viewsByDocumentId.contains(key) ? key : EditorFileIdentity::lookupKey(documentId);
}

MyCodeEditor* DocumentIndexes::editorForFileName(
    const QString& fileName) const
{
    return byFileName.value(EditorFileIdentity::lookupKey(fileName), nullptr);
}

bool DocumentStore::contains(MyCodeEditor* editor) const
{
    return byEditor.contains(editor);
}

void DocumentStore::insert(
    MyCodeEditor* editor,
    const TrackedDocument& tracked)
{
    TrackedDocument stored = tracked;
    stored.snapshot.text.clear();
    byEditor.insert(editor, stored);
}

TrackedDocument DocumentStore::take(MyCodeEditor* editor)
{
    return byEditor.take(editor);
}

TrackedDocument DocumentStore::value(MyCodeEditor* editor) const
{
    return byEditor.value(editor);
}

TrackedDocument* DocumentStore::find(MyCodeEditor* editor)
{
    auto it = byEditor.find(editor);
    return it == byEditor.end() ? nullptr : &it.value();
}

const TrackedDocument* DocumentStore::find(MyCodeEditor* editor) const
{
    auto it = byEditor.constFind(editor);
    return it == byEditor.constEnd() ? nullptr : &it.value();
}

bool DocumentStore::updateSnapshot(
    MyCodeEditor* editor,
    const DocumentSnapshot& snapshot)
{
    TrackedDocument* tracked = find(editor);
    if (!tracked)
        return false;

    tracked->snapshot = snapshot;
    return true;
}

QList<DocumentSnapshot> DocumentStore::snapshots() const
{
    QList<DocumentSnapshot> result;
    result.reserve(byEditor.size());
    for (const TrackedDocument& tracked : byEditor)
        result.append(materializedSnapshot(tracked));
    return result;
}

QString DocumentStore::textForEditor(MyCodeEditor* editor) const
{
    const TrackedDocument* tracked = find(editor);
    if (!tracked || !tracked->editor)
        return QString();
    return tracked->editor->cachedDocumentText();
}

bool DocumentRegistry::contains(MyCodeEditor* editor) const
{
    return documents.contains(editor);
}

void DocumentRegistry::add(
    MyCodeEditor* editor,
    const TrackedDocument& tracked)
{
    documents.insert(editor, tracked);
    indexes.add(editor, tracked.snapshot);
}

TrackedDocument DocumentRegistry::take(MyCodeEditor* editor)
{
    const TrackedDocument tracked = documents.take(editor);
    indexes.remove(editor, tracked.snapshot);
    return tracked;
}

TrackedDocument DocumentRegistry::value(MyCodeEditor* editor) const
{
    return documents.value(editor);
}

TrackedDocument* DocumentRegistry::find(MyCodeEditor* editor)
{
    return documents.find(editor);
}

const TrackedDocument* DocumentRegistry::find(MyCodeEditor* editor) const
{
    return documents.find(editor);
}

QList<MyCodeEditor*> DocumentRegistry::editorsForDocumentId(
    const QString& documentId) const
{
    return indexes.viewsByDocumentId.value(indexes.documentKeyForId(documentId)).values();
}

QList<MyCodeEditor*> DocumentRegistry::editorsForEditor(MyCodeEditor* editor) const
{
    return indexes.viewsByDocumentId.value(indexes.documentKeyForEditor(editor)).values();
}

int DocumentRegistry::viewCountForDocumentId(
    const QString& documentId) const
{
    return editorsForDocumentId(documentId).size();
}

DocumentSnapshot DocumentRegistry::replace(
    MyCodeEditor* editor,
    const TrackedDocument& tracked,
    const DocumentSnapshot& previous)
{
    Q_UNUSED(previous);
    documents.insert(editor, tracked);
    // The index owner decides whether a binding changed using its own saved
    // registration, never a re-resolved path or a caller's previous snapshot.
    indexes.add(editor, tracked.snapshot);
    return tracked.snapshot;
}
