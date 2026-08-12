#ifndef DOCUMENTMODEL_H
#define DOCUMENTMODEL_H

#include "zeroslackexport.h"

#include "documentsnapshot.h"
#include "documentchange.h"

#include <QObject>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QString>

#include <memory>

class MyCodeEditor;
class DocumentSessionState;

class ZEROSLACK_API DocumentModel : public QObject
{
    Q_OBJECT

public:
    explicit DocumentModel(QObject* parent = nullptr);
    ~DocumentModel() override;

    void registerEditor(MyCodeEditor* editor,
                        const QString& fileName = QString());
    void unregisterEditor(MyCodeEditor* editor);
    void setDocumentFileName(MyCodeEditor* editor, const QString& fileName);
    void markSaved(MyCodeEditor* editor);
    void refreshEditorState(MyCodeEditor* editor);

    QList<DocumentSnapshot> openDocuments() const;
    DocumentSnapshot documentForEditor(MyCodeEditor* editor) const;
    DocumentSnapshot documentForFile(const QString& fileName) const;
    // Returns identity/version/dirty/cursor metadata only. text is guaranteed
    // empty and the editor's cached text is never read.
    DocumentSnapshot documentMetadataForEditor(MyCodeEditor* editor) const;
    // Event handlers keep metadata current. Text is materialized only by
    // explicit queries from the editor's incremental cache, never by asking
    // QPlainTextEdit to rebuild the whole document.
    QList<DocumentSnapshot> cachedOpenDocuments() const;
    DocumentSnapshot cachedDocumentForFile(const QString& fileName) const;
    MyCodeEditor* editorForFile(const QString& fileName) const;
    QString documentText(const QString& documentId) const;
    QString documentTextForFile(const QString& fileName) const;
    QString documentTextForEditor(MyCodeEditor* editor) const;

signals:
    void documentOpened(const DocumentSnapshot& snapshot);
    void documentEdited(const DocumentSnapshot& snapshot);
    void documentSaved(const DocumentSnapshot& snapshot);
    void documentClosed(const QString& documentId, const QString& fileName);

private:
    std::unique_ptr<DocumentSessionState> state;
    QHash<MyCodeEditor*, QList<QMetaObject::Connection>>
        editorSignalConnections;

    void connectEditorSignals(MyCodeEditor* editor);
    void disconnectEditorSignals(MyCodeEditor* editor);
    void handleEditorDocumentChange(MyCodeEditor* editor,
                                    const DocumentChange& change);
    void handleEditorCursorChanged(MyCodeEditor* editor);
    void handleEditorFileNameChanged(MyCodeEditor* editor);
};

#endif // DOCUMENTMODEL_H
