#ifndef EDITORAPPEARANCEPANEL_H
#define EDITORAPPEARANCEPANEL_H

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class EditorAppearanceSettings;
struct EditorAppearanceOptions;

class EditorAppearancePanel : public QWidget
{
    Q_OBJECT

public:
    explicit EditorAppearancePanel(EditorAppearanceSettings* settings,
                                   QWidget* parent = nullptr);

private:
    void populateFonts();
    void syncFromSettings(const EditorAppearanceOptions& options);

    EditorAppearanceSettings* settings = nullptr;
    QComboBox* fontFamilyCombo = nullptr;
    QSpinBox* fontSizeSpin = nullptr;
    QDoubleSpinBox* lineHeightSpin = nullptr;
    QCheckBox* ligaturesCheck = nullptr;
    QPushButton* resetButton = nullptr;
};

#endif // EDITORAPPEARANCEPANEL_H
