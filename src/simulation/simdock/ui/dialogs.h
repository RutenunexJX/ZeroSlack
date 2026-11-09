#pragma once
#include <ElaDialog.h>
#include <functional>
class ElaLineEdit;

namespace simdock {
class WorkspaceSimulationController;
class SettingsDialog : public ElaDialog {
public:
    explicit SettingsDialog(const QString& executable, QWidget* parent = nullptr, bool simulatorLocked = false,
                            WorkspaceSimulationController* controller = nullptr,
                            std::function<void()> configureWorkspace = {});
    QString executable() const;
private:
    ElaLineEdit* m_path;
};
}
