#ifndef DOCUMENTMODEL_H
#define DOCUMENTMODEL_H

#include <QObject>
#include <QHash>
#include <QList>
#include <QString>

class MyCodeEditor;

struct DocumentSnapshot {
    QString documentId;
    QString fileName;
    int textVersion = 0;
    int savedTextVersion = 0;
    bool dirty = false;
    bool saved = true;
    int cursorPosition = 0;
    int cursorLine = 1;
    int cursorColumn = 1;
    QString currentModuleName;
};

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
    MyCodeEditor* editorForFile(const QString& fileName) const;
    QString documentText(const QString& documentId) const;
    QString documentTextForFile(const QString& fileName) const;
    QString documentTextForEditor(MyCodeEditor* editor) const;

signals:
    void documentOpened(const DocumentSnapshot& snapshot);
    void documentEdited(const DocumentSnapshot& snapshot);
    void documentSaved(const DocumentSnapshot& snapshot);
    void documentClosed(const QString& documentId, const QString& fileName);
    void cursorChanged(const DocumentSnapshot& snapshot);

private:
    struct TrackedDocument {
        DocumentSnapshot snapshot;
        MyCodeEditor* editor = nullptr;
        QString text;
    };

    QHash<MyCodeEditor*, TrackedDocument> documentsByEditor;
    QHash<QString, MyCodeEditor*> editorByDocumentId;
    QHash<QString, MyCodeEditor*> editorByFileName;

    QString documentIdForEditor(MyCodeEditor* editor) const;
    QString normalizedFileName(const QString& fileName) const;
    TrackedDocument makeTrackedDocument(MyCodeEditor* editor,
                                        const DocumentSnapshot* previous = nullptr) const;
    DocumentSnapshot refreshTrackedDocument(MyCodeEditor* editor);
    void indexDocument(MyCodeEditor* editor, const DocumentSnapshot& snapshot);
    void removeIndexes(MyCodeEditor* editor, const DocumentSnapshot& snapshot);
};

Q_DECLARE_METATYPE(DocumentSnapshot)

#endif // DOCUMENTMODEL_H
