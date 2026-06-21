#include "editorappearancepanel.h"

#include "editorappearance.h"
#include "editorappearancesettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
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

    auto* title = new QLabel(tr("Editor Appearance"), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignTop);

    fontFamilyCombo = new QComboBox(this);
    fontFamilyCombo->setObjectName(QStringLiteral("editorFontFamilyCombo"));
    fontFamilyCombo->setEditable(false);
    populateFonts();
    form->addRow(tr("Font family"), fontFamilyCombo);

    fontSizeSpin = new QSpinBox(this);
    fontSizeSpin->setObjectName(QStringLiteral("editorFontSizeSpin"));
    fontSizeSpin->setRange(8, 32);
    fontSizeSpin->setSuffix(tr(" pt"));
    form->addRow(tr("Font size"), fontSizeSpin);

    lineHeightSpin = new QDoubleSpinBox(this);
    lineHeightSpin->setObjectName(QStringLiteral("editorLineHeightSpin"));
    lineHeightSpin->setRange(1.0, 2.0);
    lineHeightSpin->setDecimals(2);
    lineHeightSpin->setSingleStep(0.05);
    form->addRow(tr("Line height"), lineHeightSpin);

    ligaturesCheck = new QCheckBox(tr("Enable font ligatures"), this);
    ligaturesCheck->setObjectName(QStringLiteral("editorLigaturesCheck"));
    form->addRow(QString(), ligaturesCheck);

    layout->addLayout(form);

    resetButton = new QPushButton(tr("Reset to defaults"), this);
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
        if (family.isEmpty() || added.contains(family, Qt::CaseInsensitive))
            return;
        fontFamilyCombo->addItem(family);
        added.append(family);
    };

    const QFontDatabase database;
    for (const QString& family : EditorAppearance::recommendedFontFamilies()) {
        if (database.families().contains(family, Qt::CaseInsensitive))
            addFamily(family);
    }

    if (fontFamilyCombo->count() > 0)
        fontFamilyCombo->insertSeparator(fontFamilyCombo->count());

    for (const QString& family : database.families()) {
        if (database.isFixedPitch(family))
            addFamily(family);
    }

    const QString fallback = EditorAppearance::fallbackFontFamily();
    addFamily(fallback);
}

void EditorAppearancePanel::syncFromSettings(
    const EditorAppearanceOptions& options)
{
    const QSignalBlocker familyBlocker(fontFamilyCombo);
    const QSignalBlocker sizeBlocker(fontSizeSpin);
    const QSignalBlocker lineBlocker(lineHeightSpin);
    const QSignalBlocker ligatureBlocker(ligaturesCheck);

    if (fontFamilyCombo
        && fontFamilyCombo->findText(options.fontFamily) < 0) {
        fontFamilyCombo->insertItem(0, options.fontFamily);
    }
    if (fontFamilyCombo)
        fontFamilyCombo->setCurrentText(options.fontFamily);
    if (fontSizeSpin)
        fontSizeSpin->setValue(options.fontSizePt);
    if (lineHeightSpin)
        lineHeightSpin->setValue(options.lineHeight);
    if (ligaturesCheck)
        ligaturesCheck->setChecked(options.ligaturesEnabled);
}
