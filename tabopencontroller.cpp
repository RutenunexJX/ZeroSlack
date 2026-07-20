#include "tabopencontroller.h"

#include "documentmodel.h"
#include "mycodeeditor.h"
#include "tabfileio.h"

#include <QTabWidget>

#include <memory>

TabOpenController::TabOpenController(
    QTabWidget* tabWidget,
    DocumentModel* documentModel,
    const TabFileIo* fileIo,
    QWidget* parentWidget)
    : tabWidget(tabWidget)
    , documentModel(documentModel)
    , fileIo(fileIo)
    , parentWidget(parentWidget)
{
}

MyCodeEditor* TabOpenController::createNewTab() const
{
    if (!tabWidget || !documentModel)
        return nullptr;

    std::unique_ptr<MyCodeEditor> editor(new MyCodeEditor(tabWidget));
    MyCodeEditor* editorPtr = editor.get();
    tabWidget->addTab(editor.release(), "untitled");
    documentModel->registerEditor(editorPtr);
    tabWidget->setCurrentIndex(tabWidget->count() - 1);
    return editorPtr;
}

MyCodeEditor* TabOpenController::openFile(const QString& fileName) const
{
    if (!tabWidget || !documentModel || !fileIo)
        return nullptr;

    QString fileToOpen = fileName;
    if (fileToOpen.isEmpty()) {
        fileToOpen = fileIo->promptOpenFile(parentWidget);
        if (fileToOpen.isEmpty())
            return nullptr;
    }

    QString text;
    if (!fileIo->readTextFile(parentWidget, fileToOpen, &text))
        return nullptr;

    std::unique_ptr<MyCodeEditor> editor(new MyCodeEditor(tabWidget));
    MyCodeEditor* editorPtr = editor.get();
    editorPtr->setPlainText(text);
    // Loading persisted text establishes revision zero. Treating setPlainText
    // as an edit made clean workspace symbols look stale and unnecessarily
    // launched a full open-document Slang overlay on every navigation.
    editorPtr->acceptLoadedTextAsSemanticBaseline();
    tabWidget->addTab(editor.release(), fileIo->displayName(fileToOpen));
    documentModel->registerEditor(editorPtr, fileToOpen);
    tabWidget->setCurrentIndex(tabWidget->count() - 1);
    return editorPtr;
}
