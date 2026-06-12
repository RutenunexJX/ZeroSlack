#ifndef TABOPENCONTROLLER_H
#define TABOPENCONTROLLER_H

#include <QString>

class DocumentModel;
class MyCodeEditor;
class QTabWidget;
class TabFileIo;
class QWidget;

class TabOpenController
{
public:
    TabOpenController(
        QTabWidget* tabWidget,
        DocumentModel* documentModel,
        const TabFileIo* fileIo,
        QWidget* parentWidget);

    MyCodeEditor* createNewTab() const;
    MyCodeEditor* openFile(const QString& fileName) const;

private:
    QTabWidget* tabWidget;
    DocumentModel* documentModel;
    const TabFileIo* fileIo;
    QWidget* parentWidget;
};

#endif // TABOPENCONTROLLER_H
