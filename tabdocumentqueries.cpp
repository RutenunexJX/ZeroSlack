#include "tabdocumentqueries.h"

#include "documentmodel.h"
#include "tabfileio.h"

TabDocumentQueries::TabDocumentQueries(
    DocumentModel* documentModel,
    const TabFileIo* fileIo)
    : documentModel(documentModel)
    , fileIo(fileIo)
{
}

DocumentSnapshot TabDocumentQueries::currentDocument(
    MyCodeEditor* currentEditor) const
{
    return documentForEditor(currentEditor);
}

DocumentSnapshot TabDocumentQueries::currentDocumentMetadata(
    MyCodeEditor* currentEditor) const
{
    return documentModel
        ? documentModel->documentMetadataForEditor(currentEditor)
        : DocumentSnapshot();
}

DocumentSnapshot TabDocumentQueries::documentForEditor(
    MyCodeEditor* editor) const
{
    return documentModel ? documentModel->documentForEditor(editor)
                         : DocumentSnapshot();
}

MyCodeEditor* TabDocumentQueries::editorForFile(const QString& fileName) const
{
    return documentModel ? documentModel->editorForFile(fileName) : nullptr;
}

QString TabDocumentQueries::plainTextFromCurrent(
    MyCodeEditor* currentEditor) const
{
    if (!documentModel)
        return QString();

    const DocumentSnapshot snapshot = documentForEditor(currentEditor);
    return snapshot.documentId.isEmpty()
        ? QString()
        : documentModel->documentText(snapshot.documentId);
}

QString TabDocumentQueries::plainTextFromFile(const QString& fileName) const
{
    return documentModel ? documentModel->documentTextForFile(fileName)
                         : QString();
}

QStringList TabDocumentQueries::allOpenFileNames() const
{
    QStringList fileNames;
    if (!documentModel)
        return fileNames;

    const QList<DocumentSnapshot> documents = documentModel->openDocuments();
    fileNames.reserve(documents.size());
    for (const DocumentSnapshot& document : documents) {
        if (!document.fileName.isEmpty())
            fileNames.append(document.fileName);
    }
    return fileNames;
}

QStringList TabDocumentQueries::openSystemVerilogFiles() const
{
    QStringList svFiles;
    if (!fileIo)
        return svFiles;

    const QStringList allFiles = allOpenFileNames();
    svFiles.reserve(allFiles.size());
    for (const QString& fileName : allFiles) {
        if (fileIo->isSystemVerilogFile(fileName))
            svFiles.append(fileName);
    }
    return svFiles;
}

bool TabDocumentQueries::hasUnsavedChanges() const
{
    if (!documentModel)
        return false;

    for (const DocumentSnapshot& document : documentModel->openDocuments()) {
        if (document.dirty || !document.saved)
            return true;
    }
    return false;
}
