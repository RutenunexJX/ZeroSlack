#ifndef DOCUMENTREGISTRY_H
#define DOCUMENTREGISTRY_H

#include "documentsnapshot.h"

#include <QHash>
#include <QList>
#include <QString>

class MyCodeEditor;

struct TrackedDocument {
    DocumentSnapshot snapshot;
    MyCodeEditor* editor = nullptr;
    QString text;
    // Bridges any number of cursor / query refreshes that Qt can emit before
    // DocumentModel receives textChanged for the same content mutation.
    bool contentChangePending = false;
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
    bool markEdited(MyCodeEditor* editor,
                    const DocumentSnapshot& previous,
                    DocumentSnapshot* snapshot);
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
    bool markEdited(MyCodeEditor* editor,
                    const DocumentSnapshot& previous,
                    DocumentSnapshot* snapshot);
    bool markSaved(MyCodeEditor* editor, TrackedDocument* tracked);
    DocumentSnapshot replace(MyCodeEditor* editor,
                             const TrackedDocument& tracked,
                             const DocumentSnapshot& previous);
    QList<DocumentSnapshot> snapshots() const;
    DocumentSnapshot snapshotForEditor(MyCodeEditor* editor) const;
    DocumentSnapshot snapshotForFile(const QString& fileName) const;
    MyCodeEditor* editorForFile(const QString& fileName) const;
    QString textForDocumentId(const QString& documentId) const;
    QString textForFile(const QString& fileName) const;
    QString textForEditor(MyCodeEditor* editor) const;
};

#endif // DOCUMENTREGISTRY_H
