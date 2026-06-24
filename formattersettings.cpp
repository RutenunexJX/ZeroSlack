#include "formattersettings.h"

namespace {
constexpr const char* kFormatterProfileKey = "formatter/profile";
constexpr const char* kFormatOnSaveKey = "formatter/formatOnSave";
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

bool FormatterSettings::formatOnSaveEnabled() const
{
    return currentFormatOnSaveEnabled;
}

void FormatterSettings::setProfile(FormatterProfile profile)
{
    if (currentProfile == profile)
        return;

    currentProfile = profile;
    save();
    emit settingsChanged(currentProfile);
}

void FormatterSettings::setFormatOnSaveEnabled(bool enabled)
{
    if (currentFormatOnSaveEnabled == enabled)
        return;

    currentFormatOnSaveEnabled = enabled;
    save();
    emit formatOnSaveChanged(currentFormatOnSaveEnabled);
}

void FormatterSettings::load()
{
    QString key = QString::fromLatin1(kStructuredProfile);
    if (settings) {
        key = settings->value(kFormatterProfileKey, key).toString();
        currentFormatOnSaveEnabled =
            settings->value(kFormatOnSaveKey, false).toBool();
    }
    currentProfile = profileFromKey(key);
}

void FormatterSettings::save() const
{
    if (!settings)
        return;

    settings->setValue(kFormatterProfileKey, profileKey(currentProfile));
    settings->setValue(kFormatOnSaveKey, currentFormatOnSaveEnabled);
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
