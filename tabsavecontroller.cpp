#include "tabsavecontroller.h"

#include "documentmodel.h"
#include "mycodeeditor.h"
#include "tabfileio.h"

#include <QMessageBox>

TabSaveController::TabSaveController(
    DocumentModel* documentModel,
    const TabFileIo* fileIo,
    QWidget* parentWidget)
    : documentModel(documentModel)
    , fileIo(fileIo)
    , parentWidget(parentWidget)
{
}

bool TabSaveController::saveEditor(
    MyCodeEditor* editor,
    bool forceSaveAs,
    QString* savedFileName) const
{
    if (savedFileName)
        savedFileName->clear();

    if (!editor || !documentModel || !fileIo)
        return false;

    const DocumentSnapshot snapshot = documentModel->documentForEditor(editor);
    const QString documentText = documentModel->documentTextForEditor(editor);
    const QString fileName = fileIo->resolveSaveFileName(
        parentWidget,
        snapshot.fileName,
        forceSaveAs);
    if (fileName.isEmpty())
        return false;
    if (!fileIo->writeTextFile(parentWidget, fileName, documentText))
        return false;

    documentModel->setDocumentFileName(editor, fileName);
    documentModel->markSaved(editor);
    if (savedFileName)
        *savedFileName = fileName;
    return true;
}

bool TabSaveController::confirmCloseUnsaved(
    MyCodeEditor* editor,
    QString* savedFileName) const
{
    if (savedFileName)
        savedFileName->clear();

    if (!editor || !documentModel)
        return true;

    const DocumentSnapshot snapshot = documentModel->documentForEditor(editor);
    if (!snapshot.dirty && snapshot.saved)
        return true;

    auto result = QMessageBox::question(
        parentWidget,
        "Unsaved Changes",
        "Save changes before closing?",
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (result == QMessageBox::Yes)
        return saveEditor(editor, false, savedFileName);
    if (result == QMessageBox::No)
        return true;

    return false;
}
