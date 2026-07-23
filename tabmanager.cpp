#include "tabmanager.h"
#include "documentmodel.h"

#include <QDir>
#include <QFileInfo>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCursor>

namespace {
QString cleanTabWorkspacePath(const QString& path)
{
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

QString normalizedTabWorkspacePath(const QString& path)
{
    if (path.isEmpty())
        return QString();

    QString normalized = cleanTabWorkspacePath(path);
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

QString workspaceRootForFile(const QString& fileName,
                             const QStringList& workspaceRoots)
{
    for (const QString& root : workspaceRoots) {
        if (pathInsideWorkspaceRoot(fileName, root))
            return root;
    }
    return QString();
}

HierarchyInstanceContext unboundTabInstanceContext(
    const QString& workspaceRoot)
{
    HierarchyInstanceContext context;
    context.workspacePath = cleanTabWorkspacePath(workspaceRoot);
    return context;
}

bool semanticRefreshMatchesDocument(const QString& changedFileName,
                                    const QString& documentFileName)
{
    if (changedFileName.isEmpty()
        || changedFileName == QStringLiteral("open_tabs")) {
        return true;
    }
    return normalizedTabWorkspacePath(changedFileName)
        == normalizedTabWorkspacePath(documentFileName);
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
        editor->setHierarchyInstanceContext(
            unboundTabInstanceContext(activeWorkspaceRoot));
        applyWorkspaceScope();
        emit tabCreated(editor);
    }
}

bool TabManager::openFileInTab(const QString& fileName)
{
    if (!fileName.isEmpty() && activateOpenFile(fileName)) {
        if (MyCodeEditor* editor = getCurrentEditor()) {
            editor->setHierarchyInstanceContext(
                unboundTabInstanceContext(activeWorkspaceRoot));
        }
        return true;
    }

    MyCodeEditor* editor = openController.openFile(fileName);
    if (!editor)
        return false;

    editor->setHierarchyInstanceContext(
        unboundTabInstanceContext(activeWorkspaceRoot));
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

    codeEditor->closeSemanticPopup();
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

DocumentSnapshot TabManager::getCurrentDocumentMetadata() const
{
    return documentQueries.currentDocumentMetadata(getCurrentEditor());
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

void TabManager::refreshSemanticPresentations(
    const QString& changedFileName)
{
    if (!tabWidget)
        return;

    MyCodeEditor* editor = getCurrentEditor();
    if (!editor)
        return;
    const DocumentSnapshot document = getDocumentForEditor(editor);
    if (semanticRefreshMatchesDocument(changedFileName, document.fileName))
        editor->refreshSemanticPresentation();
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
    for (int i = 0; tabWidget && i < tabWidget->count(); ++i) {
        MyCodeEditor* editor = getEditorAt(i);
        if (!editor)
            continue;
        const HierarchyInstanceContext current =
            editor->hierarchyInstanceContext();
        const QString currentWorkspace =
            normalizedTabWorkspacePath(current.workspacePath);
        if (!currentWorkspace.isEmpty()
            && scopedWorkspaceRoots.contains(currentWorkspace)) {
            continue;
        }

        const DocumentSnapshot document = getDocumentForEditor(editor);
        QString fallbackWorkspace = workspaceRootForFile(
            document.fileName, scopedWorkspaceRoots);
        if (fallbackWorkspace.isEmpty())
            fallbackWorkspace = activeWorkspaceRoot;
        editor->setHierarchyInstanceContext(
            unboundTabInstanceContext(fallbackWorkspace));
    }
    applyWorkspaceScope();
}

bool TabManager::closeTabsInWorkspace(const QString& workspaceRoot)
{
    if (!tabWidget || workspaceRoot.isEmpty())
        return false;

    struct PendingClose {
        MyCodeEditor* editor = nullptr;
        QString originalFileName;
        QString savedFileName;
    };

    QList<PendingClose> pending;
    for (int i = 0; i < tabWidget->count(); ++i) {
        MyCodeEditor* editor = getEditorAt(i);
        if (!editor)
            continue;

        const DocumentSnapshot snapshot = getDocumentForEditor(editor);
        if (pathInsideWorkspaceRoot(snapshot.fileName, workspaceRoot)) {
            PendingClose close;
            close.editor = editor;
            close.originalFileName = snapshot.fileName;
            pending.append(close);
        }
    }

    for (PendingClose& close : pending) {
        if (!saveController.confirmCloseUnsaved(close.editor,
                                                &close.savedFileName)) {
            return false;
        }
        if (!close.savedFileName.isEmpty()) {
            updateTabTitle(close.editor);
            emit fileSaved(close.savedFileName);
        }
    }

    for (const PendingClose& close : pending) {
        const int index = tabWidget->indexOf(close.editor);
        if (index < 0)
            continue;
        close.editor->closeSemanticPopup();
        documentModel->unregisterEditor(close.editor);
        tabWidget->removeTab(index);
        emit tabClosed(close.originalFileName);
    }

    applyWorkspaceScope();
    return true;
}

bool TabManager::hasUnsavedChanges() const
{
    return documentQueries.hasUnsavedChanges();
}

QList<WorkspaceSessionTabState> TabManager::workspaceSessionTabs(
    const QString& workspaceRoot) const
{
    QList<WorkspaceSessionTabState> states;
    if (!tabWidget || workspaceRoot.isEmpty())
        return states;

    for (int i = 0; i < tabWidget->count(); ++i) {
        MyCodeEditor* editor = getEditorAt(i);
        if (!editor)
            continue;

        const DocumentSnapshot snapshot = getDocumentForEditor(editor);
        const QString filePath = cleanTabWorkspacePath(snapshot.fileName);
        if (!pathInsideWorkspaceRoot(filePath, workspaceRoot)
            || !QFileInfo(filePath).isFile()
            || !fileIo.isSystemVerilogFile(filePath)) {
            continue;
        }

        const QTextCursor cursor = editor->textCursor();
        WorkspaceSessionTabState state;
        state.filePath = filePath;
        state.cursorLine = cursor.blockNumber() + 1;
        state.cursorColumn = cursor.positionInBlock() + 1;
        state.verticalScrollValue = editor->verticalScrollBar()
            ? editor->verticalScrollBar()->value()
            : 0;
        state.active = editor == getCurrentEditor();
        states.append(state);
    }
    return states;
}

QStringList TabManager::restoreWorkspaceSessionTabs(
    const QString& workspaceRoot,
    const QList<WorkspaceSessionTabState>& tabs,
    QStringList* skippedFiles)
{
    QStringList restoredFiles;
    if (!tabWidget || workspaceRoot.isEmpty())
        return restoredFiles;

    QString activeFile;
    for (const WorkspaceSessionTabState& tab : tabs) {
        const QString filePath = cleanTabWorkspacePath(tab.filePath);
        if (!pathInsideWorkspaceRoot(filePath, workspaceRoot)
            || !QFileInfo(filePath).isFile()
            || !fileIo.isSystemVerilogFile(filePath)) {
            if (skippedFiles)
                skippedFiles->append(filePath);
            continue;
        }

        if (!openFileInTab(filePath)) {
            if (skippedFiles)
                skippedFiles->append(filePath);
            continue;
        }

        MyCodeEditor* editor = documentQueries.editorForFile(filePath);
        if (editor) {
            QTextBlock block =
                editor->document()->findBlockByNumber(
                    qMax(0, tab.cursorLine - 1));
            if (!block.isValid())
                block = editor->document()->lastBlock();
            if (block.isValid()) {
                const int column =
                    qBound(0,
                           tab.cursorColumn - 1,
                           qMax(0, block.text().size()));
                QTextCursor cursor(block);
                cursor.setPosition(block.position() + column);
                editor->setTextCursor(cursor);
            }
            if (QScrollBar* bar = editor->verticalScrollBar())
                bar->setValue(qMax(0, tab.verticalScrollValue));
        }

        restoredFiles.append(filePath);
        if (tab.active)
            activeFile = filePath;
    }

    if (!activeFile.isEmpty())
        activateOpenFile(activeFile);
    return restoredFiles;
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
        editor->refreshSemanticPresentation();
        updateTabTitle(editor);
        const DocumentSnapshot snapshot = getDocumentForEditor(editor);
        emit activeTabChanged(editor);
        emit activeDocumentChanged(snapshot);
    }
}
