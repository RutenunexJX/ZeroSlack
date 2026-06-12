#ifndef DOCUMENTSESSIONSTATE_H
#define DOCUMENTSESSIONSTATE_H

#include "documentregistry.h"
#include "documentsnapshot.h"

#include <QList>
#include <QString>

class MyCodeEditor;

struct DocumentCloseResult {
    bool closed = false;
    QString documentId;
    QString fileName;
};

struct DocumentSaveResult {
    bool opened = false;
    bool saved = false;
    DocumentSnapshot snapshot;
};

struct DocumentFileNameResult {
    bool opened = false;
    DocumentSnapshot snapshot;
};

class DocumentSessionState
{
public:
    bool contains(MyCodeEditor* editor) const;

    bool registerEditor(
        MyCodeEditor* editor,
        const QString& fileName,
        DocumentSnapshot* openedSnapshot);
    DocumentCloseResult unregisterEditor(MyCodeEditor* editor);
    DocumentFileNameResult setDocumentFileName(
        MyCodeEditor* editor,
        const QString& fileName);
    DocumentSaveResult markSaved(MyCodeEditor* editor);
    void refreshEditorState(MyCodeEditor* editor);

    bool markEdited(MyCodeEditor* editor, DocumentSnapshot* editedSnapshot);
    bool refreshCursor(MyCodeEditor* editor, DocumentSnapshot* snapshot);
    void refreshFileName(MyCodeEditor* editor);

    QList<DocumentSnapshot> openDocuments() const;
    DocumentSnapshot documentForEditor(MyCodeEditor* editor) const;
    DocumentSnapshot documentForFile(const QString& fileName) const;
    MyCodeEditor* editorForFile(const QString& fileName) const;
    QString documentText(const QString& documentId) const;
    QString documentTextForFile(const QString& fileName) const;
    QString documentTextForEditor(MyCodeEditor* editor) const;

private:
    DocumentRegistry registry;
    DocumentSnapshotReader snapshotReader;

    DocumentSnapshot refreshTrackedDocument(MyCodeEditor* editor);
};

#endif // DOCUMENTSESSIONSTATE_H
