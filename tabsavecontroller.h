#ifndef TABSAVECONTROLLER_H
#define TABSAVECONTROLLER_H

#include <QString>

class DocumentModel;
class MyCodeEditor;
class TabFileIo;
class QWidget;

class TabSaveController
{
public:
    TabSaveController(
        DocumentModel* documentModel,
        const TabFileIo* fileIo,
        QWidget* parentWidget);

    bool saveEditor(
        MyCodeEditor* editor,
        bool forceSaveAs,
        QString* savedFileName = nullptr) const;
    bool confirmCloseUnsaved(
        MyCodeEditor* editor,
        QString* savedFileName = nullptr) const;

private:
    DocumentModel* documentModel;
    const TabFileIo* fileIo;
    QWidget* parentWidget;
};

#endif // TABSAVECONTROLLER_H
