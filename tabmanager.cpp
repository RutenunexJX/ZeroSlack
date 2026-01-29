#include "tabmanager.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QTextStream>

TabManager::TabManager(QTabWidget* tabWidget, QObject *parent)
    : QObject(parent), tabWidget(tabWidget)
{
    if (!tabWidget) {
        return;
    }

    // Connect tab widget signals
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
    auto newEditor = createEditor();
    auto* editorPtr = newEditor.get();

    // Transfer ownership to Qt's parent-child system
    tabWidget->addTab(newEditor.release(), "untitled");
    tabWidget->setCurrentIndex(tabWidget->count() - 1);

    emit tabCreated(editorPtr);
}

bool TabManager::openFileInTab(const QString& fileName)
{
    QString fileToOpen = fileName;

    // If no filename provided, show file dialog
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

    // Create editor
    auto codeEditor = createEditor();
    auto* editorPtr = codeEditor.get();

    editorPtr->setPlainText(text);
    editorPtr->setFileName(fileToOpen);
    editorPtr->isSaved = true;

    // Transfer ownership to Qt
    tabWidget->addTab(codeEditor.release(), getDisplayName(fileToOpen));
    tabWidget->setCurrentIndex(tabWidget->count() - 1);

    emit tabCreated(editorPtr);
    return true;
}

bool TabManager::saveCurrentTab()
{
    MyCodeEditor *codeEditor = getCurrentEditor();
    if (!codeEditor) return false;

    if (codeEditor->saveFile()) {
        updateTabTitle(codeEditor);
        emit fileSaved(codeEditor->getFileName());
        return true;
    }
    return false;
}

bool TabManager::saveAsCurrentTab()
{
    MyCodeEditor *codeEditor = getCurrentEditor();
    if (!codeEditor) return false;

    if (codeEditor->saveAsFile()) {
        updateTabTitle(codeEditor);
        emit fileSaved(codeEditor->getFileName());
        return true;
    }
    return false;
}

void TabManager::closeTab(int index)
{
    if (index < 0 || index >= tabWidget->count()) return;

    MyCodeEditor *codeEditor = getEditorAt(index);
    if (!codeEditor) return;

    QString fileName = codeEditor->getFileName();

    // Check if file needs saving
    if (!confirmCloseUnsaved(codeEditor)) {
        return; // User cancelled or save failed
    }

    tabWidget->removeTab(index);
    // Widget automatically deleted by Qt parent-child relationship

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

QString TabManager::getPlainTextFromCurrentTab() const
{
    MyCodeEditor *codeEditor = getCurrentEditor();
    return codeEditor ? codeEditor->toPlainText() : QString();
}

QString TabManager::getPlainTextFromOpenFile(const QString& fileName) const
{
    for (int i = 0; i < tabWidget->count(); ++i) {
        MyCodeEditor *codeEditor = getEditorAt(i);
        if (codeEditor && codeEditor->getFileName().endsWith(fileName)) {
            return codeEditor->toPlainText();
        }
    }
    return QString();
}

QStringList TabManager::getAllOpenFileNames() const
{
    QStringList fileNames;
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

void TabManager::updateTabTitle(MyCodeEditor* editor)
{
    if (!editor) return;

    // Find the tab containing this editor
    for (int i = 0; i < tabWidget->count(); ++i) {
        if (tabWidget->widget(i) == editor) {
            QString fileName = editor->getFileName();
            QString displayName = fileName.isEmpty() ? "untitled" : getDisplayName(fileName);
            tabWidget->setTabText(i, displayName);

            // Update window title if this is the current tab
            if (tabWidget->currentIndex() == i) {
                QWidget* parentWidget = qobject_cast<QWidget*>(parent());
                if (parentWidget) {
                    parentWidget->setWindowTitle(fileName.isEmpty() ? "untitled" : fileName);
                }
            }
            break;
        }
    }
}

bool TabManager::hasUnsavedChanges() const
{
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

bool TabManager::confirmCloseUnsaved(MyCodeEditor* editor)
{
    if (!editor || editor->checkSaved()) {
        return true; // No unsaved changes
    }

    auto result = QMessageBox::question(
        qobject_cast<QWidget*>(parent()),
        "Unsaved Changes",
        "Save changes before closing?",
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (result == QMessageBox::Yes) {
        return editor->saveFile(); // Return save result
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
