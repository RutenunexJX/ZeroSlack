#include <zeroslack/ui/applicationwindow.h>
#include "mainwindow.h"
#include "tabmanager.h"

namespace ZeroSlack {
std::unique_ptr<QMainWindow> createApplicationWindow()
{
    return std::make_unique<MainWindow>();
}

bool openApplicationDocument(QMainWindow& window, const QString& fileName)
{
    auto* application = qobject_cast<MainWindow*>(&window);
    if (!application || !application->tabManager || fileName.isEmpty())
        return false;
    return application->tabManager->openFileInTab(fileName);
}
}
