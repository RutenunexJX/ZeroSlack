#ifndef DOCUMENTREGISTRY_H
#define DOCUMENTREGISTRY_H

#include "documentsnapshot.h"

#include <QHash>
#include <QList>
#include <QString>
#include <cstdint>

class MyCodeEditor;

struct DocumentTextCopyMetrics {
    std::uint64_t fullTextCopyCount = 0;
    std::uint64_t copiedCharacterCount = 0;
};

void recordDocumentTextCopy(qsizetype characterCount);
DocumentTextCopyMetrics documentTextCopyMetricsForTest();
void resetDocumentTextCopyMetricsForTest();

struct TrackedDocument {
    DocumentSnapshot snapshot;
    MyCodeEditor* editor = nullptr;
};

struct DocumentIndexes {
    QHash<QString, MyCodeEditor*> byDocumentId;
    QHash<QString, MyCodeEditor*> byFileName;

    void add(MyCodeEditor* editor, const DocumentSnapshot& snapshot);
    void remove(MyCodeEditor* editor, const DocumentSnapshot& snapshot);
    MyCodeEditor* editorForDocumentId(const QString& documentId) const;
    MyCodeEditor* editorForFileName(const QString& fileName) const;
};

struct DocumentStore {
    QHash<MyCodeEditor*, TrackedDocument> byEditor;

    bool contains(MyCodeEditor* editor) const;
    void insert(MyCodeEditor* editor, const TrackedDocument& tracked);
    TrackedDocument take(MyCodeEditor* editor);
    TrackedDocument value(MyCodeEditor* editor) const;
    TrackedDocument* find(MyCodeEditor* editor);
    const TrackedDocument* find(MyCodeEditor* editor) const;
    bool updateSnapshot(MyCodeEditor* editor, const DocumentSnapshot& snapshot);
    bool markSaved(MyCodeEditor* editor, TrackedDocument* tracked);
    QList<DocumentSnapshot> snapshots() const;
    QString textForEditor(MyCodeEditor* editor) const;
};

struct DocumentSnapshotReader {
    QString normalizedFileName(const QString& fileName) const;
    QString documentIdForEditor(MyCodeEditor* editor) const;
    void captureFileIdentity(MyCodeEditor* editor, DocumentSnapshot* snapshot) const;
    void captureCursorState(MyCodeEditor* editor, DocumentSnapshot* snapshot) const;
    TrackedDocument capture(
        MyCodeEditor* editor,
        const TrackedDocument* previous = nullptr) const;
};

struct DocumentRegistry {
    DocumentStore documents;
    DocumentIndexes indexes;

    bool contains(MyCodeEditor* editor) const;
    void add(MyCodeEditor* editor, const TrackedDocument& tracked);
    TrackedDocument take(MyCodeEditor* editor);
    TrackedDocument value(MyCodeEditor* editor) const;
    TrackedDocument* find(MyCodeEditor* editor);
    const TrackedDocument* find(MyCodeEditor* editor) const;
    bool markSaved(MyCodeEditor* editor, TrackedDocument* tracked);
    DocumentSnapshot replace(MyCodeEditor* editor,
                             const TrackedDocument& tracked,
                             const DocumentSnapshot& previous);
    QList<DocumentSnapshot> snapshots() const;
    DocumentSnapshot snapshotForEditor(MyCodeEditor* editor) const;
    DocumentSnapshot snapshotForFile(const QString& fileName) const;
    // Metadata-only queries never read editor text. The returned snapshot's
    // text field is always empty.
    DocumentSnapshot metadataForEditor(MyCodeEditor* editor) const;
    MyCodeEditor* editorForFile(const QString& fileName) const;
    QString textForDocumentId(const QString& documentId) const;
    QString textForFile(const QString& fileName) const;
    QString textForEditor(MyCodeEditor* editor) const;
};

#endif // DOCUMENTREGISTRY_H
