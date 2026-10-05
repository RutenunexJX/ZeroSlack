#pragma once

#include <zeroslack/ui/uiapi.h>
#include <QMainWindow>
#include <QString>
#include <memory>

namespace ZeroSlack {
// The application shell owns all editor and workspace coordinators. Embedders
// need neither MainWindow's implementation headers nor its child ownership.
ZEROSLACK_API std::unique_ptr<QMainWindow> createApplicationWindow();
ZEROSLACK_API bool openApplicationDocument(QMainWindow& window, const QString& fileName);
}
