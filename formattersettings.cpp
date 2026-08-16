#include "formattersettings.h"

#include "settingscenterkeys.h"

namespace {
constexpr const char* kFormatterProfileKey =
    SettingsCenterKeys::FormatterProfile;
constexpr const char* kStructuredProfile = "structured";
constexpr const char* kIndentOnlyProfile = "indent_only";
}

FormatterSettings::FormatterSettings(QObject* parent)
    : FormatterSettings(
          std::make_unique<QSettings>(
              QStringLiteral("ZeroSlack"),
              QStringLiteral("ZeroSlack")),
          parent)
{
}

FormatterSettings::FormatterSettings(
    std::unique_ptr<QSettings> newSettings,
    QObject* parent)
    : QObject(parent)
    , settings(std::move(newSettings))
{
    load();
}

FormatterProfile FormatterSettings::profile() const
{
    return currentProfile;
}

void FormatterSettings::setProfile(FormatterProfile profile)
{
    if (currentProfile == profile)
        return;

    currentProfile = profile;
    save();
    emit settingsChanged(currentProfile);
}

void FormatterSettings::load()
{
    QString key = QString::fromLatin1(kStructuredProfile);
    if (settings) {
        key = settings->value(kFormatterProfileKey, key).toString();
    }
    currentProfile = profileFromKey(key);
}

void FormatterSettings::save() const
{
    if (!settings)
        return;

    settings->setValue(kFormatterProfileKey, profileKey(currentProfile));
    settings->sync();
}

QString FormatterSettings::profileKey(FormatterProfile profile)
{
    switch (profile) {
    case FormatterProfile::IndentOnly:
        return QString::fromLatin1(kIndentOnlyProfile);
    case FormatterProfile::Structured:
        return QString::fromLatin1(kStructuredProfile);
    }
    return QString::fromLatin1(kStructuredProfile);
}

FormatterProfile FormatterSettings::profileFromKey(const QString& key)
{
    if (key == QString::fromLatin1(kIndentOnlyProfile))
        return FormatterProfile::IndentOnly;
    return FormatterProfile::Structured;
}
