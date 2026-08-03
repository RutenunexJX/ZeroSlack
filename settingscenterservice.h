#ifndef SETTINGSCENTERSERVICE_H
#define SETTINGSCENTERSERVICE_H

#include "settingscenterschema.h"

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVariantMap>

struct SettingsCenterSnapshot {
    QString workspaceRoot;
    QString globalStoragePath;
    QString workspaceStoragePath;
    QString globalRevision;
    QString workspaceRevision;
    QVariantMap globalValues;
    QVariantMap workspaceValues;
    QVariantMap effectiveValues;
    QList<SettingsCenterValidationIssue> issues;
    bool globalCompatible = true;
    bool workspaceCompatible = true;
    bool workspaceDocumentExists = false;

    QVariant value(const QString& fieldId) const;
    bool hasWorkspaceOverride(const QString& fieldId) const;
};

struct SettingsCenterSaveResult {
    bool saved = false;
    bool conflict = false;
    QString storagePath;
    QString revision;
    QString message;
    QVariantMap normalizedValues;
    QList<SettingsCenterValidationIssue> issues;
};

class SettingsCenterService
{
public:
    static constexpr int kVersion = 1;

    explicit SettingsCenterService(
        const QString& globalSettingsFilePath = QString(),
        const QString& workspaceSettingsFilePathOverride = QString());

    SettingsCenterSnapshot load(
        const QString& workspaceRoot = QString()) const;

    SettingsCenterSaveResult saveGlobal(
        const QVariantMap& values,
        const QString& expectedRevision = QString()) const;
    SettingsCenterSaveResult saveWorkspace(
        const QString& workspaceRoot,
        const QVariantMap& values,
        const QString& expectedRevision = QString()) const;

    QString workspaceSettingsFilePath(
        const QString& workspaceRoot) const;

private:
    QString globalSettingsFilePath;
    QString workspaceSettingsFilePathOverride;
};

#endif // SETTINGSCENTERSERVICE_H
