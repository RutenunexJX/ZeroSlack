#include "tabmanager.h"
#include "documentmodel.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QTextStream>

TabManager::TabManager(QTabWidget* tabWidget, QObject *parent)
    : QObject(parent), tabWidget(tabWidget), documentModel(std::make_unique<DocumentModel>(this))
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
        fileToOpen = QFileDialog::getOpenFileName(
            qobject_cast<QWidget*>(parent()), "open file");
        if (fileToOpen.isEmpty()) return false; // User cancelled
    }

    QFile file(fileToOpen);
    if (!file.open(QIODevice::ReadOnly | QFile::Text)) {
        QMessageBox::warning(qobject_cast<QWidget*>(parent()),
                            "warning",
                            "can not open file:" + file.errorString());
        return false;
    }

    QTextStream in(&file);
    const QString text = in.readAll();
    file.close();

    std::unique_ptr<MyCodeEditor> codeEditor = createEditor();
    MyCodeEditor* editorPtr = codeEditor.get();
    editorPtr->setPlainText(text);
    editorPtr->setFileName(fileToOpen);
    editorPtr->markDocumentSaved();
    tabWidget->addTab(codeEditor.release(), getDisplayName(fileToOpen));
    documentModel->registerEditor(editorPtr);
    tabWidget->setCurrentIndex(tabWidget->count() - 1);
    emit tabCreated(editorPtr);
    return true;
}

bool TabManager::saveCurrentTab()
{
    MyCodeEditor *codeEditor = getCurrentEditor();
    if (!codeEditor) return false;

    if (saveEditorToFile(codeEditor, false)) {
        documentModel->markSaved(codeEditor);
        updateTabTitle(codeEditor);
        emit fileSaved(getDocumentForEditor(codeEditor).fileName);
        return true;
    }
    return false;
}

bool TabManager::saveAsCurrentTab()
{
    MyCodeEditor *codeEditor = getCurrentEditor();
    if (!codeEditor) return false;

    if (saveEditorToFile(codeEditor, true)) {
        documentModel->markSaved(codeEditor);
        updateTabTitle(codeEditor);
        emit fileSaved(getDocumentForEditor(codeEditor).fileName);
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
    if (!confirmCloseUnsaved(codeEditor)) {
        return; // User cancelled or save failed
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
    if (documentModel) {
        const QList<DocumentSnapshot> documents = documentModel->openDocuments();
        fileNames.reserve(documents.size());
        for (const DocumentSnapshot& document : documents) {
            if (!document.fileName.isEmpty())
                fileNames.append(document.fileName);
        }
        return fileNames;
    }

    fileNames.reserve(tabWidget->count());
    for (int i = 0; i < tabWidget->count(); ++i) {
        MyCodeEditor *codeEditor = getEditorAt(i);
        if (codeEditor && !codeEditor->getFileName().isEmpty()) {
            fileNames.append(codeEditor->getFileName());
        }
    }
    return fileNames;
}

QStringList TabManager::getOpenSystemVerilogFiles() const
{
    QStringList svFiles;
    const QStringList allFiles = getAllOpenFileNames();

    svFiles.reserve(allFiles.size());

    for (const QString& fileName : allFiles) {
        if (isSystemVerilogFile(fileName)) {
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
    if (!editor) return;
    for (int i = 0; i < tabWidget->count(); ++i) {
        if (tabWidget->widget(i) == editor) {
            QString fileName = getDocumentForEditor(editor).fileName;
            QString displayName = fileName.isEmpty() ? "untitled" : getDisplayName(fileName);
            tabWidget->setTabText(i, displayName);
            if (tabWidget->currentIndex() == i) {
                QWidget* parentWidget = qobject_cast<QWidget*>(parent());
                if (parentWidget)
                    parentWidget->setWindowTitle(fileName.isEmpty() ? "untitled" : fileName);
            }
            break;
        }
    }
}

bool TabManager::hasUnsavedChanges() const
{
    if (documentModel) {
        for (const DocumentSnapshot& document : documentModel->openDocuments()) {
            if (document.dirty || !document.saved)
                return true;
        }
        return false;
    }

    for (int i = 0; i < tabWidget->count(); ++i) {
        MyCodeEditor *codeEditor = getEditorAt(i);
        if (codeEditor && !codeEditor->checkSaved()) {
            return true;
        }
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
        emit activeTabChanged(editor);
    }
}

std::unique_ptr<MyCodeEditor> TabManager::createEditor()
{
    return std::unique_ptr<MyCodeEditor>(new MyCodeEditor(tabWidget));
}

bool TabManager::saveEditorToFile(MyCodeEditor* editor, bool forceSaveAs)
{
    if (!editor)
        return false;

    QString fileName = editor->getFileName();
    if (forceSaveAs || fileName.isEmpty() || !QFile::exists(fileName)) {
        fileName = QFileDialog::getSaveFileName(
            qobject_cast<QWidget*>(parent()),
            forceSaveAs ? QStringLiteral("save file as ") : QStringLiteral("Save file"));
        if (fileName.isEmpty())
            return false;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QFile::Text)) {
        QMessageBox::warning(
            qobject_cast<QWidget*>(parent()),
            "Warning",
            "Cannot save file: " + file.errorString());
        return false;
    }

    QTextStream out(&file);
    out << editor->toPlainText();
    file.close();

    editor->setFileName(fileName);
    return true;
}

bool TabManager::confirmCloseUnsaved(MyCodeEditor* editor)
{
    const DocumentSnapshot snapshot = getDocumentForEditor(editor);
    if (!editor || (!snapshot.dirty && snapshot.saved)) {
        return true; // No unsaved changes
    }

    auto result = QMessageBox::question(
        qobject_cast<QWidget*>(parent()),
        "Unsaved Changes",
        "Save changes before closing?",
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (result == QMessageBox::Yes) {
        if (!saveEditorToFile(editor, false))
            return false;
        documentModel->markSaved(editor);
        updateTabTitle(editor);
        emit fileSaved(getDocumentForEditor(editor).fileName);
        return true;
    } else if (result == QMessageBox::No) {
        return true; // Don't save, but allow close
    }

    return false; // Cancel - don't close
}

QString TabManager::getDisplayName(const QString& fullPath) const
{
    return fullPath.isEmpty() ? "untitled" : QFileInfo(fullPath).fileName();
}

bool TabManager::isSystemVerilogFile(const QString& fileName) const
{
    if (fileName.isEmpty()) return false;

    static const QStringList svExtensions = {"sv", "v", "vh", "svh", "vp", "svp"};
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return svExtensions.contains(suffix);
}
