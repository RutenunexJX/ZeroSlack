#pragma once
#include "../core/model.h"
#include "../core/wavepalette.h"
#include <ElaDialog.h>
class ElaLineEdit;
class ElaComboBox;
class ElaSpinBox;
class ElaPlainTextEdit;
class ElaText;

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
class TestbenchDialog : public ElaDialog {
public:
    TestbenchDialog(const Module& module, qint64 durationNs, QWidget* parent = nullptr);
    TbOptions options() const;
    QString content() const;
private:
    void updatePreview();
    Module m_module;
    qint64 m_duration;
    ElaLineEdit* m_name;
    ElaComboBox *m_clock, *m_reset, *m_polarity;
    ElaSpinBox *m_period, *m_cycles;
    ElaPlainTextEdit* m_preview;
    ElaText* m_error;
};
QString askProjectName(QWidget* parent);
}
