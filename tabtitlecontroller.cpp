#include "tabtitlecontroller.h"

#include "documentmodel.h"
#include "mycodeeditor.h"
#include "tabfileio.h"

#include <QTabWidget>
#include <QWidget>

TabTitleController::TabTitleController(
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

void TabTitleController::updateTitle(MyCodeEditor* editor) const
{
    if (!editor || !tabWidget || !documentModel || !fileIo)
        return;

    for (int i = 0; i < tabWidget->count(); ++i) {
        if (tabWidget->widget(i) != editor)
            continue;

        const QString fileName = documentModel->documentForEditor(editor).fileName;
        tabWidget->setTabText(i, fileIo->displayName(fileName));
        if (tabWidget->currentIndex() == i && parentWidget)
            parentWidget->setWindowTitle(fileName.isEmpty() ? "untitled" : fileName);
        break;
    }
}
