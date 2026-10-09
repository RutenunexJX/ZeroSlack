#pragma once
#include "../../../src/simulation/simdock/core/model.h"
#include <ElaWidget.h>

namespace simdock {
class Workbench;
// Standalone chrome only. The same child workbench is exported to native hosts.
class MainWindow : public ElaWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void initialize();
    void openWorkspace(const QString& path);
    bool openSuiteTarget(const QString& root, const QString& projectId, QString* error);
    bool createProject(const QString& name);
    bool createDemo(const TbOptions& options, QString* error);
    QString workspace() const;
    void setSimulator(const QString& executable);
signals:
    void scanFinished();
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    Workbench* m_workbench;
};
}
