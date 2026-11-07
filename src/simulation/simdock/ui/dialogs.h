#pragma once
#include "../core/wavepalette.h"
#include <ElaDialog.h>
class ElaLineEdit;
class ElaComboBox;

namespace simdock {
class SettingsDialog : public ElaDialog {
public:
    explicit SettingsDialog(const QString& executable, QWidget* parent = nullptr, bool simulatorLocked = false,
                            const QString& waveThemeId = {}, bool hostTheme = false);
    QString executable() const;
    bool darkTheme() const;
    WaveTheme waveTheme() const;
private:
    ElaLineEdit* m_path;
    ElaComboBox* m_theme;
    ElaComboBox* m_waveTheme;
};
QString askProjectName(QWidget* parent, const QString& currentName = {});
}
