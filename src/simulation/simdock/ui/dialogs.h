#pragma once
#include <ElaDialog.h>
#include <functional>
#include "../core/wavepalette.h"
class ElaLineEdit;
class QComboBox;

namespace simdock {
class WorkspaceSimulationController;
class SettingsDialog : public ElaDialog {
public:
    explicit SettingsDialog(const QString& executable, QWidget* parent = nullptr, bool simulatorLocked = false,
                            WorkspaceSimulationController* controller = nullptr,
                            std::function<void()> configureWorkspace = {},
                            WaveTheme waveTheme = WaveTheme::QuestaDefault);
    QString executable() const;
    WaveTheme waveTheme() const;
private:
    ElaLineEdit* m_path;
    QComboBox* m_waveTheme;
};
}
