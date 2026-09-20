#include "editorappearancesettings.h"

#include "settingscenterkeys.h"

#include <QtGlobal>

namespace {
constexpr const char* kFontFamilyKey = SettingsCenterKeys::FontFamily;
constexpr const char* kFontSizeKey = SettingsCenterKeys::FontSizePt;
constexpr const char* kLineHeightKey = SettingsCenterKeys::FontLineHeight;
constexpr const char* kLigaturesKey =
    SettingsCenterKeys::FontLigaturesEnabled;

bool sameOptions(const EditorAppearanceOptions& lhs,
                 const EditorAppearanceOptions& rhs)
{
    return lhs.fontFamily == rhs.fontFamily
        && lhs.fontSizePt == rhs.fontSizePt
        && qFuzzyCompare(lhs.lineHeight, rhs.lineHeight)
        && lhs.ligaturesEnabled == rhs.ligaturesEnabled;
}
}

EditorAppearanceSettings::EditorAppearanceSettings(QObject* parent)
    : EditorAppearanceSettings(
          std::make_unique<QSettings>(
              QSettings::defaultFormat(), QSettings::UserScope,
              QStringLiteral("ZeroSlack"),
              QStringLiteral("ZeroSlack")),
          parent)
{
}

EditorAppearanceSettings::EditorAppearanceSettings(
    std::unique_ptr<QSettings> newSettings,
    QObject* parent)
    : QObject(parent)
    , settings(std::move(newSettings))
{
    load();
}

EditorAppearanceOptions EditorAppearanceSettings::options() const
{
    return currentOptions;
}

void EditorAppearanceSettings::setOptions(
    const EditorAppearanceOptions& options)
{
    const EditorAppearanceOptions next = normalized(options);
    if (sameOptions(currentOptions, next))
        return;

    currentOptions = next;
    save();
    emit settingsChanged(currentOptions);
}

void EditorAppearanceSettings::setFontFamily(const QString& family)
{
    EditorAppearanceOptions next = currentOptions;
    next.fontFamily = family;
    setOptions(next);
}

void EditorAppearanceSettings::setFontSizePt(int sizePt)
{
    EditorAppearanceOptions next = currentOptions;
    next.fontSizePt = sizePt;
    setOptions(next);
}

void EditorAppearanceSettings::setLineHeight(double lineHeight)
{
    EditorAppearanceOptions next = currentOptions;
    next.lineHeight = lineHeight;
    setOptions(next);
}

void EditorAppearanceSettings::setLigaturesEnabled(bool enabled)
{
    EditorAppearanceOptions next = currentOptions;
    next.ligaturesEnabled = enabled;
    setOptions(next);
}

void EditorAppearanceSettings::resetToDefaults()
{
    setOptions(EditorAppearance::defaultOptions());
}

void EditorAppearanceSettings::load()
{
    EditorAppearanceOptions defaults = EditorAppearance::defaultOptions();
    if (settings) {
        defaults.fontFamily =
            settings->value(kFontFamilyKey, defaults.fontFamily).toString();
        defaults.fontSizePt =
            settings->value(kFontSizeKey, defaults.fontSizePt).toInt();
        defaults.lineHeight =
            settings->value(kLineHeightKey, defaults.lineHeight).toDouble();
        defaults.ligaturesEnabled =
            settings->value(kLigaturesKey, defaults.ligaturesEnabled).toBool();
    }
    currentOptions = normalized(defaults);
}

void EditorAppearanceSettings::save() const
{
    if (!settings)
        return;

    settings->setValue(kFontFamilyKey, currentOptions.fontFamily);
    settings->setValue(kFontSizeKey, currentOptions.fontSizePt);
    settings->setValue(kLineHeightKey, currentOptions.lineHeight);
    settings->setValue(kLigaturesKey, currentOptions.ligaturesEnabled);
    settings->sync();
}

EditorAppearanceOptions EditorAppearanceSettings::normalized(
    const EditorAppearanceOptions& options)
{
    EditorAppearanceOptions normalizedOptions = options;
    normalizedOptions.fontFamily =
        EditorAppearance::resolveFontFamily(options.fontFamily);
    normalizedOptions.fontSizePt = qBound(8, options.fontSizePt, 32);
    normalizedOptions.lineHeight = qBound(1.0, options.lineHeight, 2.0);
    return normalizedOptions;
}
