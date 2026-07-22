#ifndef DOCUMENTMODEL_H
#define DOCUMENTMODEL_H

#include "documentsnapshot.h"

#include <QObject>
#include <QList>
#include <QString>

#include <memory>

class MyCodeEditor;
class DocumentSessionState;

class DocumentModel : public QObject
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
    // Event handlers keep these snapshots current. Semantic scheduling uses
    // the cached views so a save never re-materializes a multi-megabyte
    // QPlainTextEdit buffer on the GUI thread.
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

    void connectEditorSignals(MyCodeEditor* editor);
    void handleEditorTextChanged(MyCodeEditor* editor);
    void handleEditorCursorChanged(MyCodeEditor* editor);
    void handleEditorFileNameChanged(MyCodeEditor* editor);
};

#endif // DOCUMENTMODEL_H
