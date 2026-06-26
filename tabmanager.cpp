#include "tabmanager.h"
#include "documentmodel.h"

#include <QDir>
#include <QFileInfo>
#include <QSignalBlocker>

namespace {
QString normalizedTabWorkspacePath(const QString& path)
{
    if (path.isEmpty())
        return QString();

    QString normalized = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

bool pathInsideWorkspaceRoot(const QString& fileName,
                             const QString& workspaceRoot)
{
    const QString filePath = normalizedTabWorkspacePath(fileName);
    const QString rootPath = normalizedTabWorkspacePath(workspaceRoot);
    if (filePath.isEmpty() || rootPath.isEmpty())
        return false;

    const QString rootPrefix = rootPath.endsWith(QLatin1Char('/'))
        ? rootPath
        : rootPath + QLatin1Char('/');
    return filePath == rootPath || filePath.startsWith(rootPrefix);
}

QStringList normalizedTabWorkspaceRoots(const QStringList& roots)
{
    QStringList normalized;
    normalized.reserve(roots.size());
    for (const QString& root : roots) {
        const QString path = normalizedTabWorkspacePath(root);
        if (!path.isEmpty() && !normalized.contains(path))
            normalized.append(path);
    }
    return normalized;
}
}

TabManager::TabManager(QTabWidget* tabWidget, QObject *parent)
    : QObject(parent)
    , tabWidget(tabWidget)
    , documentModel(std::make_unique<DocumentModel>(this))
    , openController(
          tabWidget,
          documentModel.get(),
          &fileIo,
          qobject_cast<QWidget*>(parent))
    , documentQueries(documentModel.get(), &fileIo)
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
    MyCodeEditor* editor = openController.createNewTab();
    if (editor) {
        applyWorkspaceScope();
        emit tabCreated(editor);
    }
}

bool TabManager::openFileInTab(const QString& fileName)
{
    if (!fileName.isEmpty() && activateOpenFile(fileName))
        return true;

    MyCodeEditor* editor = openController.openFile(fileName);
    if (!editor)
        return false;

    applyWorkspaceScope();
    emit tabCreated(editor);
    return true;
}

bool TabManager::saveCurrentTab()
{
    MyCodeEditor *codeEditor = getCurrentEditor();
    if (!codeEditor) return false;

    QString savedFileName;
    if (saveController.saveEditor(codeEditor, false, &savedFileName)) {
        updateTabTitle(codeEditor);
        applyWorkspaceScope();
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
        applyWorkspaceScope();
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
    applyWorkspaceScope();
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
    return documentQueries.currentDocument(getCurrentEditor());
}

DocumentSnapshot TabManager::getDocumentForEditor(MyCodeEditor* editor) const
{
    return documentQueries.documentForEditor(editor);
}

bool TabManager::activateOpenFile(const QString& fileName)
{
    if (!tabWidget || !documentModel)
        return false;

    MyCodeEditor* editor = documentQueries.editorForFile(fileName);
    const int index = editor ? tabWidget->indexOf(editor) : -1;
    if (index < 0)
        return false;

    tabWidget->setCurrentIndex(index);
    return true;
}

QString TabManager::getPlainTextFromCurrentTab() const
{
    return documentQueries.plainTextFromCurrent(getCurrentEditor());
}

QString TabManager::getPlainTextFromOpenFile(const QString& fileName) const
{
    return documentQueries.plainTextFromFile(fileName);
}

QStringList TabManager::getAllOpenFileNames() const
{
    return documentQueries.allOpenFileNames();
}

QStringList TabManager::getOpenSystemVerilogFiles() const
{
    return documentQueries.openSystemVerilogFiles();
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

void TabManager::setWorkspaceScope(const QStringList& workspaceRoots,
                                   const QString& activeWorkspaceRootPath)
{
    scopedWorkspaceRoots = normalizedTabWorkspaceRoots(workspaceRoots);
    activeWorkspaceRoot = normalizedTabWorkspacePath(activeWorkspaceRootPath);
    applyWorkspaceScope();
}

bool TabManager::hasUnsavedChanges() const
{
    return documentQueries.hasUnsavedChanges();
}

void TabManager::applyWorkspaceScope()
{
    if (!tabWidget)
        return;

    const int previousIndex = tabWidget->currentIndex();
    int firstVisibleIndex = -1;
    bool currentStillVisible = false;
    {
        const QSignalBlocker blocker(tabWidget);
        for (int i = 0; i < tabWidget->count(); ++i) {
            MyCodeEditor* editor = getEditorAt(i);
            const bool visible = editorVisibleInWorkspaceScope(editor);
            tabWidget->setTabVisible(i, visible);
            if (visible && firstVisibleIndex < 0)
                firstVisibleIndex = i;
            if (visible && i == tabWidget->currentIndex())
                currentStillVisible = true;
        }

        if (!currentStillVisible && firstVisibleIndex >= 0)
            tabWidget->setCurrentIndex(firstVisibleIndex);
    }

    if (tabWidget->currentIndex() != previousIndex)
        onCurrentTabChanged(tabWidget->currentIndex());
}

bool TabManager::editorVisibleInWorkspaceScope(MyCodeEditor* editor) const
{
    if (!editor || activeWorkspaceRoot.isEmpty()
        || scopedWorkspaceRoots.isEmpty()) {
        return true;
    }

    const QString fileName =
        documentModel ? documentModel->documentForEditor(editor).fileName
                      : QString();
    if (fileName.isEmpty())
        return true;

    if (pathInsideWorkspaceRoot(fileName, activeWorkspaceRoot))
        return true;

    for (const QString& root : scopedWorkspaceRoots) {
        if (root == activeWorkspaceRoot)
            continue;
        if (pathInsideWorkspaceRoot(fileName, root))
            return false;
    }

    return true;
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
