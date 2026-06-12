#ifndef TABTITLECONTROLLER_H
#define TABTITLECONTROLLER_H

class DocumentModel;
class MyCodeEditor;
class QTabWidget;
class TabFileIo;
class QWidget;

class TabTitleController
{
public:
    TabTitleController(
        QTabWidget* tabWidget,
        DocumentModel* documentModel,
        const TabFileIo* fileIo,
        QWidget* parentWidget);

    void updateTitle(MyCodeEditor* editor) const;

private:
    QTabWidget* tabWidget;
    DocumentModel* documentModel;
    const TabFileIo* fileIo;
    QWidget* parentWidget;
};

#endif // TABTITLECONTROLLER_H
