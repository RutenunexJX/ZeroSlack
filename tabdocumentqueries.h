#ifndef TABDOCUMENTQUERIES_H
#define TABDOCUMENTQUERIES_H

#include "documentsnapshot.h"

#include <QString>
#include <QStringList>

class DocumentModel;
class MyCodeEditor;
class TabFileIo;

class TabDocumentQueries
{
public:
    TabDocumentQueries(
        DocumentModel* documentModel,
        const TabFileIo* fileIo);

    DocumentSnapshot currentDocument(MyCodeEditor* currentEditor) const;
    DocumentSnapshot currentDocumentMetadata(MyCodeEditor* currentEditor) const;
    DocumentSnapshot documentForEditor(MyCodeEditor* editor) const;
    MyCodeEditor* editorForFile(const QString& fileName) const;
    QString plainTextFromCurrent(MyCodeEditor* currentEditor) const;
    QString plainTextFromFile(const QString& fileName) const;
    QStringList allOpenFileNames() const;
    QStringList openSystemVerilogFiles() const;
    bool hasUnsavedChanges() const;

private:
    DocumentModel* documentModel;
    const TabFileIo* fileIo;
};

#endif // TABDOCUMENTQUERIES_H
