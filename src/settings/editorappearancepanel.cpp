#include "uicontrols.h"
#include "uitypography.h"
#include "editorappearancepanel.h"

#include "editorappearance.h"
#include "editorappearancesettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

EditorAppearancePanel::EditorAppearancePanel(
    EditorAppearanceSettings* newSettings,
    QWidget* parent)
    : QWidget(parent)
    , settings(newSettings)
{
    setObjectName(QStringLiteral("editorAppearancePanel"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto* title = UiControls::label(tr("Editor Appearance"), this);
    UiTypography::apply(title, UiTypography::Role::PanelTitle);
    layout->addWidget(title);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setVerticalSpacing(12);
    form->setHorizontalSpacing(16);
    form->setFormAlignment(Qt::AlignTop);

    fontFamilyCombo = UiControls::comboBox(this);
    fontFamilyCombo->setObjectName(QStringLiteral("editorFontFamilyCombo"));
    fontFamilyCombo->setEditable(false);
    populateFonts();
    UiControls::addFormRow(form, tr("Font family"), fontFamilyCombo);

    fontSizeSpin = UiControls::spinBox(this);
    fontSizeSpin->setObjectName(QStringLiteral("editorFontSizeSpin"));
    fontSizeSpin->setRange(8, 32);
    fontSizeSpin->setSuffix(tr(" pt"));
    UiControls::addFormRow(form, tr("Font size"), fontSizeSpin);

    lineHeightSpin = UiControls::doubleSpinBox(this);
    lineHeightSpin->setObjectName(QStringLiteral("editorLineHeightSpin"));
    lineHeightSpin->setRange(1.0, 2.0);
    lineHeightSpin->setDecimals(2);
    lineHeightSpin->setSingleStep(0.05);
    UiControls::addFormRow(form, tr("Line height"), lineHeightSpin);

    ligaturesCheck = UiControls::checkBox(tr("Enable font ligatures"), this);
    ligaturesCheck->setObjectName(QStringLiteral("editorLigaturesCheck"));
    UiControls::addFormRow(form, QString(), ligaturesCheck);

    layout->addLayout(form);

    resetButton = UiControls::pushButton(tr("Reset to defaults"), this);
    resetButton->setObjectName(QStringLiteral("editorAppearanceResetButton"));
    layout->addWidget(resetButton);
    layout->addStretch(1);

    if (settings) {
        syncFromSettings(settings->options());
        connect(fontFamilyCombo,
                &QComboBox::currentTextChanged,
                settings,
                &EditorAppearanceSettings::setFontFamily);
        connect(fontSizeSpin,
                qOverload<int>(&QSpinBox::valueChanged),
                settings,
                &EditorAppearanceSettings::setFontSizePt);
        connect(lineHeightSpin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                settings,
                &EditorAppearanceSettings::setLineHeight);
        connect(ligaturesCheck,
                &QCheckBox::toggled,
                settings,
                &EditorAppearanceSettings::setLigaturesEnabled);
        connect(resetButton,
                &QPushButton::clicked,
                settings,
                &EditorAppearanceSettings::resetToDefaults);
        connect(settings,
                &EditorAppearanceSettings::settingsChanged,
                this,
                &EditorAppearancePanel::syncFromSettings);
    }
}

void EditorAppearancePanel::populateFonts()
{
    if (!fontFamilyCombo)
        return;

    QStringList added;
    auto addFamily = [this, &added](const QString& family) {
        if (family.isEmpty()
            || EditorAppearance::isCjkFontFamily(family)
            || added.contains(family, Qt::CaseInsensitive)) {
            return;
        }
        fontFamilyCombo->addItem(family);
        added.append(family);
    };

    for (const QString& family : EditorAppearance::recommendedFontFamilies()) {
        addFamily(family);
    }
}

void EditorAppearancePanel::syncFromSettings(
    const EditorAppearanceOptions& options)
{
    const QSignalBlocker familyBlocker(fontFamilyCombo);
    const QSignalBlocker sizeBlocker(fontSizeSpin);
    const QSignalBlocker lineBlocker(lineHeightSpin);
    const QSignalBlocker ligatureBlocker(ligaturesCheck);

    const QString safeFamily =
        EditorAppearance::resolveFontFamily(options.fontFamily);
    if (fontFamilyCombo)
        fontFamilyCombo->setCurrentText(safeFamily);
    if (fontSizeSpin)
        fontSizeSpin->setValue(options.fontSizePt);
    if (lineHeightSpin)
        lineHeightSpin->setValue(options.lineHeight);
    if (ligaturesCheck)
        ligaturesCheck->setChecked(options.ligaturesEnabled);
}
