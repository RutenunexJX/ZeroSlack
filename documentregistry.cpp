#include "documentregistry.h"

#include "mycodeeditor.h"

#include <atomic>

namespace {
std::atomic<std::uint64_t> fullTextCopyCount{0};
std::atomic<std::uint64_t> copiedCharacterCount{0};

DocumentSnapshot materializedSnapshot(const TrackedDocument& tracked)
{
    DocumentSnapshot snapshot = tracked.snapshot;
    if (tracked.editor) {
        const QString& text = tracked.editor->cachedDocumentText();
        recordDocumentTextCopy(text.size());
        snapshot.text = QString(text.constData(), text.size());
    }
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

    byDocumentId[snapshot.documentId] = editor;
    if (!snapshot.fileName.isEmpty())
        byFileName[snapshot.fileName] = editor;
}

void DocumentIndexes::remove(
    MyCodeEditor* editor,
    const DocumentSnapshot& snapshot)
{
    if (!editor)
        return;

    if (byDocumentId.value(snapshot.documentId, nullptr) == editor)
        byDocumentId.remove(snapshot.documentId);
    if (!snapshot.fileName.isEmpty()
        && byFileName.value(snapshot.fileName, nullptr) == editor) {
        byFileName.remove(snapshot.fileName);
    }
}

MyCodeEditor* DocumentIndexes::editorForDocumentId(
    const QString& documentId) const
{
    return byDocumentId.value(documentId, nullptr);
}

MyCodeEditor* DocumentIndexes::editorForFileName(
    const QString& fileName) const
{
    return byFileName.value(fileName, nullptr);
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
    const QString& text = tracked->editor->cachedDocumentText();
    recordDocumentTextCopy(text.size());
    return QString(text.constData(), text.size());
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

DocumentSnapshot DocumentRegistry::replace(
    MyCodeEditor* editor,
    const TrackedDocument& tracked,
    const DocumentSnapshot& previous)
{
    documents.insert(editor, tracked);
    indexes.remove(editor, previous);
    indexes.add(editor, tracked.snapshot);
    return tracked.snapshot;
}
