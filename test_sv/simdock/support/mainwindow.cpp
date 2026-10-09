#include "mainwindow.h"
#include "../../../src/simulation/simdock/ui/workbench.h"
#include <QCloseEvent>
#include <QSettings>
#include <QVBoxLayout>

namespace simdock {
MainWindow::MainWindow(QWidget* parent) : ElaWidget(parent)
{
    setObjectName(QStringLiteral("SimDockWindow"));
    setWindowTitle(QStringLiteral("SimDock"));
    setWindowIcon(QIcon(QStringLiteral(":/simdock.ico")));
    setIsDefaultClosed(true);
    setWindowButtonFlags(ElaAppBarType::MinimizeButtonHint | ElaAppBarType::MaximizeButtonHint | ElaAppBarType::CloseButtonHint);
    setAppBarHeight(36);
    setIsStayTop(false);
    resize(1200, 780);
    setMinimumSize(1000, 700);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_workbench = new Workbench(this, true);
    layout->addWidget(m_workbench);
    connect(m_workbench, &Workbench::scanFinished, this, &MainWindow::scanFinished);
    QSettings settings;
    if (settings.contains(QStringLiteral("window/geometry")))
        restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
}
void MainWindow::initialize() { m_workbench->initialize(); }
void MainWindow::openWorkspace(const QString& path) { m_workbench->openWorkspace(path); }
bool MainWindow::openSuiteTarget(const QString& root, const QString& projectId, QString* error)
{
    if (!m_workbench->openSuiteTarget(root, projectId, error)) return false;
    showNormal(); raise(); activateWindow();
    return true;
}
bool MainWindow::createProject(const QString& name) { return m_workbench->createProject(name); }
bool MainWindow::createDemo(const TbOptions& options, QString* error) { return m_workbench->createDemo(options, error); }
QString MainWindow::workspace() const { return m_workbench->workspace(); }
void MainWindow::setSimulator(const QString& executable) { m_workbench->setSimulator(executable); }
void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings().setValue(QStringLiteral("window/geometry"), saveGeometry());
    m_workbench->savePreferences();
    m_workbench->shutdown();
    ElaWidget::closeEvent(event);
}
}
