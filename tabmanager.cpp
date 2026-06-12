#include "tabmanager.h"
#include "documentmodel.h"

TabManager::TabManager(QTabWidget* tabWidget, QObject *parent)
    : QObject(parent)
    , tabWidget(tabWidget)
    , documentModel(std::make_unique<DocumentModel>(this))
    , saveController(
          documentModel.get(),
          &fileIo,
          qobject_cast<QWidget*>(parent))
    , titleController(
          tabWidget,
          documentModel.get(),
          &fileIo,
          qobject_cast<QWidget*>(parent))
{
    if (!tabWidget) {
        return;
    }

    connect(tabWidget, &QTabWidget::tabCloseRequested,
            this, &TabManager::onTabCloseRequested);
    connect(tabWidget, &QTabWidget::currentChanged,
            this, &TabManager::onCurrentTabChanged);

}

TabManager::~TabManager()
{
}

void TabManager::createNewTab()
{
    std::unique_ptr<MyCodeEditor> newEditor = createEditor();
    MyCodeEditor* editorPtr = newEditor.get();
    tabWidget->addTab(newEditor.release(), "untitled");
    documentModel->registerEditor(editorPtr);
    tabWidget->setCurrentIndex(tabWidget->count() - 1);
    emit tabCreated(editorPtr);
}

bool TabManager::openFileInTab(const QString& fileName)
{
    QString fileToOpen = fileName;
    if (fileToOpen.isEmpty()) {
        fileToOpen = fileIo.promptOpenFile(qobject_cast<QWidget*>(parent()));
        if (fileToOpen.isEmpty()) return false; // User cancelled
    }

    QString text;
    if (!fileIo.readTextFile(qobject_cast<QWidget*>(parent()), fileToOpen, &text))
        return false;

    std::unique_ptr<MyCodeEditor> codeEditor = createEditor();
    MyCodeEditor* editorPtr = codeEditor.get();
    editorPtr->setPlainText(text);
    tabWidget->addTab(codeEditor.release(), fileIo.displayName(fileToOpen));
    documentModel->registerEditor(editorPtr, fileToOpen);
    tabWidget->setCurrentIndex(tabWidget->count() - 1);
    emit tabCreated(editorPtr);
    return true;
}

bool TabManager::saveCurrentTab()
{
    MyCodeEditor *codeEditor = getCurrentEditor();
    if (!codeEditor) return false;

    QString savedFileName;
    if (saveController.saveEditor(codeEditor, false, &savedFileName)) {
        updateTabTitle(codeEditor);
        emit fileSaved(savedFileName);
        return true;
    }
    return false;
}

bool TabManager::saveAsCurrentTab()
{
    MyCodeEditor *codeEditor = getCurrentEditor();
    if (!codeEditor) return false;

    QString savedFileName;
    if (saveController.saveEditor(codeEditor, true, &savedFileName)) {
        updateTabTitle(codeEditor);
        emit fileSaved(savedFileName);
        return true;
    }
    return false;
}

void TabManager::closeTab(int index)
{
    if (index < 0 || index >= tabWidget->count()) return;

    MyCodeEditor *codeEditor = getEditorAt(index);
    if (!codeEditor) return;

    QString fileName = getDocumentForEditor(codeEditor).fileName;
    QString savedFileName;
    if (!saveController.confirmCloseUnsaved(codeEditor, &savedFileName)) {
        return; // User cancelled or save failed
    }
    if (!savedFileName.isEmpty()) {
        updateTabTitle(codeEditor);
        emit fileSaved(savedFileName);
    }

    documentModel->unregisterEditor(codeEditor);
    tabWidget->removeTab(index);
    emit tabClosed(fileName);
}

MyCodeEditor* TabManager::getCurrentEditor() const
{
    return qobject_cast<MyCodeEditor*>(tabWidget->currentWidget());
}

MyCodeEditor* TabManager::getEditorAt(int index) const
{
    if (index < 0 || index >= tabWidget->count()) return nullptr;
    return qobject_cast<MyCodeEditor*>(tabWidget->widget(index));
}

DocumentSnapshot TabManager::getCurrentDocument() const
{
    return getDocumentForEditor(getCurrentEditor());
}

DocumentSnapshot TabManager::getDocumentForEditor(MyCodeEditor* editor) const
{
    return documentModel ? documentModel->documentForEditor(editor) : DocumentSnapshot();
}

bool TabManager::activateOpenFile(const QString& fileName)
{
    if (!tabWidget || !documentModel)
        return false;

    MyCodeEditor* editor = documentModel->editorForFile(fileName);
    const int index = editor ? tabWidget->indexOf(editor) : -1;
    if (index < 0)
        return false;

    tabWidget->setCurrentIndex(index);
    return true;
}

QString TabManager::getPlainTextFromCurrentTab() const
{
    if (!documentModel)
        return QString();

    const DocumentSnapshot snapshot = getCurrentDocument();
    return snapshot.documentId.isEmpty()
        ? QString()
        : documentModel->documentText(snapshot.documentId);
}

QString TabManager::getPlainTextFromOpenFile(const QString& fileName) const
{
    return documentModel ? documentModel->documentTextForFile(fileName) : QString();
}

QStringList TabManager::getAllOpenFileNames() const
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

QStringList TabManager::getOpenSystemVerilogFiles() const
{
    QStringList svFiles;
    const QStringList allFiles = getAllOpenFileNames();

    svFiles.reserve(allFiles.size());

    for (const QString& fileName : allFiles) {
        if (fileIo.isSystemVerilogFile(fileName)) {
            svFiles.append(fileName);
        }
    }
    return svFiles;
}

int TabManager::editorCount() const
{
    return tabWidget ? tabWidget->count() : 0;
}

DocumentModel* TabManager::getDocumentModel() const
{
    return documentModel.get();
}

void TabManager::updateTabTitle(MyCodeEditor* editor)
{
    titleController.updateTitle(editor);
}

bool TabManager::hasUnsavedChanges() const
{
    if (!documentModel)
        return false;

    for (const DocumentSnapshot& document : documentModel->openDocuments()) {
        if (document.dirty || !document.saved)
            return true;
    }
    return false;
}

void TabManager::onTabCloseRequested(int index)
{
    closeTab(index);
}

void TabManager::onCurrentTabChanged(int index)
{
    MyCodeEditor* editor = getEditorAt(index);
    if (editor) {
        updateTabTitle(editor);
        const DocumentSnapshot snapshot = getDocumentForEditor(editor);
        emit activeTabChanged(editor);
        emit activeDocumentChanged(snapshot);
    }
}

std::unique_ptr<MyCodeEditor> TabManager::createEditor()
{
    return std::unique_ptr<MyCodeEditor>(new MyCodeEditor(tabWidget));
}
