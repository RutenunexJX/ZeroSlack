#include "settingscenterservice.h"

#include "settingscenterkeys.h"
#include "workspaceconfigurationservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSettings>

#include <memory>

namespace {
constexpr const char* kSchema = "ZeroSlack.SettingsCenter";
constexpr const char* kWorkspaceFile = "settings.json";

struct JsonDocumentReadResult {
    bool exists = false;
    bool valid = true;
    bool compatible = true;
    QJsonObject object;
    QString message;
};

std::unique_ptr<QSettings> makeGlobalSettings(
    const QString& settingsFilePath)
{
    std::unique_ptr<QSettings> settings;
    if (settingsFilePath.isEmpty()) {
        settings = std::make_unique<QSettings>(
            QStringLiteral("ZeroSlack"),
            QStringLiteral("ZeroSlack"));
    } else {
        settings = std::make_unique<QSettings>(
            settingsFilePath,
            QSettings::IniFormat);
    }
    settings->setAtomicSyncRequired(true);
    return settings;
}

QString cleanAbsolutePath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
}

QString revisionForFile(const QString& path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return QStringLiteral("missing");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("unreadable");
    return QString::fromLatin1(
        QCryptographicHash::hash(
            file.readAll(),
            QCryptographicHash::Sha256)
            .toHex());
}

void appendIssue(QList<SettingsCenterValidationIssue>* issues,
                 SettingsCenterScope scope,
                 SettingsCenterIssueKind kind,
                 const QString& fieldId,
                 const QString& message)
{
    if (issues)
        issues->append({scope, kind, fieldId, message});
}

bool globalDocumentCompatible(
    const QSettings& settings,
    QList<SettingsCenterValidationIssue>* issues)
{
    const QString schema =
        settings.value(
            QString::fromLatin1(SettingsCenterKeys::Schema)).toString();
    if (schema.isEmpty())
        return true;
    const int version =
        settings.value(
            QString::fromLatin1(SettingsCenterKeys::Version),
            -1).toInt();
    if (schema == QString::fromLatin1(kSchema)
        && version == SettingsCenterService::kVersion) {
        return true;
    }
    appendIssue(
        issues,
        SettingsCenterScope::Global,
        SettingsCenterIssueKind::UnsupportedDocument,
        QString(),
        QStringLiteral(
            "Global settings use an unsupported schema or version and were "
            "left untouched."));
    return false;
}

JsonDocumentReadResult readWorkspaceDocument(const QString& path)
{
    JsonDocumentReadResult result;
    if (path.isEmpty())
        return result;
    QFile file(path);
    if (!file.exists())
        return result;
    result.exists = true;
    if (!file.open(QIODevice::ReadOnly)) {
        result.valid = false;
        result.message =
            QStringLiteral("Workspace settings could not be read.");
        return result;
    }

    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError
        || !document.isObject()) {
        result.valid = false;
        result.message =
            QStringLiteral("Workspace settings contain invalid JSON.");
        return result;
    }

    result.object = document.object();
    const QString schema =
        result.object.value(QStringLiteral("schema")).toString();
    const int version =
        result.object.value(QStringLiteral("version")).toInt(-1);
    if (schema != QString::fromLatin1(kSchema)
        || version != SettingsCenterService::kVersion) {
        result.compatible = false;
        result.message =
            QStringLiteral(
                "Workspace settings use an unsupported schema or version.");
    }
    return result;
}

QJsonObject sortedObject(const QJsonObject& object)
{
    QStringList keys = object.keys();
    keys.sort(Qt::CaseSensitive);
    QJsonObject result;
    for (const QString& key : keys) {
        QJsonValue value = object.value(key);
        if (value.isObject())
            value = sortedObject(value.toObject());
        result.insert(key, value);
    }
    return result;
}

QJsonObject valuesObject(const QVariantMap& values)
{
    QJsonObject object;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        object.insert(it.key(), QJsonValue::fromVariant(it.value()));
    return sortedObject(object);
}

bool revisionMatches(const QString& expected,
                     const QString& current)
{
    return expected.isEmpty() || expected == current;
}
}

QVariant SettingsCenterSnapshot::value(const QString& fieldId) const
{
    return effectiveValues.value(fieldId);
}

bool SettingsCenterSnapshot::hasWorkspaceOverride(
    const QString& fieldId) const
{
    return workspaceValues.contains(fieldId);
}

SettingsCenterService::SettingsCenterService(
    const QString& newGlobalSettingsFilePath,
    const QString& newWorkspaceSettingsFilePathOverride)
    : globalSettingsFilePath(
          cleanAbsolutePath(newGlobalSettingsFilePath))
    , workspaceSettingsFilePathOverride(
          cleanAbsolutePath(newWorkspaceSettingsFilePathOverride))
{
}

SettingsCenterSnapshot SettingsCenterService::load(
    const QString& workspaceRoot) const
{
    SettingsCenterSnapshot result;
    result.workspaceRoot = cleanAbsolutePath(workspaceRoot);

    std::unique_ptr<QSettings> global =
        makeGlobalSettings(globalSettingsFilePath);
    result.globalStoragePath = global->fileName();
    result.globalRevision = revisionForFile(result.globalStoragePath);
    result.globalCompatible =
        globalDocumentCompatible(*global, &result.issues);
    if (result.globalCompatible) {
        QVariantMap rawGlobal;
        for (const SettingsCenterFieldDescriptor& descriptor :
             SettingsCenterSchema::fields()) {
            if (descriptor.globalAllowed
                && global->contains(descriptor.storageKey)) {
                rawGlobal.insert(
                    descriptor.id,
                    global->value(descriptor.storageKey));
            }
        }
        SettingsCenterLayerValidation validation =
            SettingsCenterSchema::validateLayer(
                rawGlobal,
                SettingsCenterScope::Global);
        result.globalValues = validation.values;
        result.issues.append(validation.issues);
    }

    result.workspaceStoragePath =
        workspaceSettingsFilePath(result.workspaceRoot);
    result.workspaceRevision =
        revisionForFile(result.workspaceStoragePath);
    const JsonDocumentReadResult workspace =
        readWorkspaceDocument(result.workspaceStoragePath);
    result.workspaceDocumentExists = workspace.exists;
    result.workspaceCompatible = workspace.valid && workspace.compatible;
    if (!workspace.valid || !workspace.compatible) {
        appendIssue(
            &result.issues,
            SettingsCenterScope::Workspace,
            workspace.compatible
                ? SettingsCenterIssueKind::StorageError
                : SettingsCenterIssueKind::UnsupportedDocument,
            QString(),
            workspace.message);
    } else if (workspace.exists) {
        const QVariantMap rawWorkspace =
            workspace.object.value(QStringLiteral("values"))
                .toObject()
                .toVariantMap();
        SettingsCenterLayerValidation validation =
            SettingsCenterSchema::validateLayer(
                rawWorkspace,
                SettingsCenterScope::Workspace);
        result.workspaceValues = validation.values;
        result.issues.append(validation.issues);
    }

    result.effectiveValues = SettingsCenterSchema::merge(
        result.globalValues,
        result.workspaceValues);
    return result;
}

SettingsCenterSaveResult SettingsCenterService::saveGlobal(
    const QVariantMap& values,
    const QString& expectedRevision) const
{
    SettingsCenterSaveResult result;
    std::unique_ptr<QSettings> settings =
        makeGlobalSettings(globalSettingsFilePath);
    result.storagePath = settings->fileName();
    const QString currentRevision =
        revisionForFile(result.storagePath);
    if (!revisionMatches(expectedRevision, currentRevision)) {
        result.conflict = true;
        result.message =
            QStringLiteral(
                "Global settings changed after they were loaded.");
        appendIssue(&result.issues,
                    SettingsCenterScope::Global,
                    SettingsCenterIssueKind::Conflict,
                    QString(),
                    result.message);
        return result;
    }
    if (!globalDocumentCompatible(*settings, &result.issues)) {
        result.message =
            QStringLiteral(
                "Unsupported global settings were not overwritten.");
        return result;
    }

    SettingsCenterLayerValidation validation =
        SettingsCenterSchema::validateLayer(
            values,
            SettingsCenterScope::Global);
    result.normalizedValues = validation.values;
    result.issues.append(validation.issues);

    for (const SettingsCenterFieldDescriptor& descriptor :
         SettingsCenterSchema::fields()) {
        if (!descriptor.globalAllowed)
            continue;
        if (result.normalizedValues.contains(descriptor.id)) {
            settings->setValue(
                descriptor.storageKey,
                result.normalizedValues.value(descriptor.id));
        } else {
            settings->remove(descriptor.storageKey);
        }
    }
    settings->setValue(
        QString::fromLatin1(SettingsCenterKeys::Schema),
        QString::fromLatin1(kSchema));
    settings->setValue(
        QString::fromLatin1(SettingsCenterKeys::Version),
        kVersion);
    settings->sync();
    if (settings->status() != QSettings::NoError) {
        result.message =
            QStringLiteral("Global settings could not be written atomically.");
        appendIssue(&result.issues,
                    SettingsCenterScope::Global,
                    SettingsCenterIssueKind::StorageError,
                    QString(),
                    result.message);
        return result;
    }

    result.saved = true;
    result.revision = revisionForFile(result.storagePath);
    result.message = QStringLiteral("Global settings saved.");
    return result;
}

SettingsCenterSaveResult SettingsCenterService::saveWorkspace(
    const QString& workspaceRoot,
    const QVariantMap& values,
    const QString& expectedRevision) const
{
    SettingsCenterSaveResult result;
    result.storagePath =
        workspaceSettingsFilePath(workspaceRoot);
    if (result.storagePath.isEmpty()) {
        result.message =
            QStringLiteral("A workspace root is required.");
        appendIssue(&result.issues,
                    SettingsCenterScope::Workspace,
                    SettingsCenterIssueKind::StorageError,
                    QString(),
                    result.message);
        return result;
    }

    const QString currentRevision =
        revisionForFile(result.storagePath);
    if (!revisionMatches(expectedRevision, currentRevision)) {
        result.conflict = true;
        result.message =
            QStringLiteral(
                "Workspace settings changed after they were loaded.");
        appendIssue(&result.issues,
                    SettingsCenterScope::Workspace,
                    SettingsCenterIssueKind::Conflict,
                    QString(),
                    result.message);
        return result;
    }

    const JsonDocumentReadResult existing =
        readWorkspaceDocument(result.storagePath);
    if (!existing.valid || !existing.compatible) {
        result.message = existing.message;
        appendIssue(
            &result.issues,
            SettingsCenterScope::Workspace,
            existing.compatible
                ? SettingsCenterIssueKind::StorageError
                : SettingsCenterIssueKind::UnsupportedDocument,
            QString(),
            existing.message);
        return result;
    }

    SettingsCenterLayerValidation validation =
        SettingsCenterSchema::validateLayer(
            values,
            SettingsCenterScope::Workspace);
    result.normalizedValues = validation.values;
    result.issues.append(validation.issues);

    QJsonObject root = existing.object;
    QJsonObject storedValues =
        root.value(QStringLiteral("values")).toObject();
    for (const SettingsCenterFieldDescriptor& descriptor :
         SettingsCenterSchema::fields()) {
        if (descriptor.workspaceAllowed)
            storedValues.remove(descriptor.id);
    }
    const QJsonObject normalized =
        valuesObject(result.normalizedValues);
    for (auto it = normalized.begin(); it != normalized.end(); ++it)
        storedValues.insert(it.key(), it.value());

    root.insert(QStringLiteral("schema"),
                QString::fromLatin1(kSchema));
    root.insert(QStringLiteral("version"), kVersion);
    root.insert(QStringLiteral("values"), sortedObject(storedValues));
    root = sortedObject(root);

    if (!QDir().mkpath(
            QFileInfo(result.storagePath).absolutePath())) {
        result.message =
            QStringLiteral(
                "Workspace settings directory could not be created.");
        appendIssue(&result.issues,
                    SettingsCenterScope::Workspace,
                    SettingsCenterIssueKind::StorageError,
                    QString(),
                    result.message);
        return result;
    }

    QSaveFile file(result.storagePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.message =
            QStringLiteral("Workspace settings could not be opened.");
        appendIssue(&result.issues,
                    SettingsCenterScope::Workspace,
                    SettingsCenterIssueKind::StorageError,
                    QString(),
                    result.message);
        return result;
    }
    const QByteArray bytes =
        QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        result.message =
            QStringLiteral("Workspace settings could not be written.");
        appendIssue(&result.issues,
                    SettingsCenterScope::Workspace,
                    SettingsCenterIssueKind::StorageError,
                    QString(),
                    result.message);
        return result;
    }
    if (!file.commit()) {
        result.message =
            QStringLiteral(
                "Workspace settings could not be committed atomically.");
        appendIssue(&result.issues,
                    SettingsCenterScope::Workspace,
                    SettingsCenterIssueKind::StorageError,
                    QString(),
                    result.message);
        return result;
    }

    result.saved = true;
    result.revision = revisionForFile(result.storagePath);
    result.message = QStringLiteral("Workspace settings saved.");
    return result;
}

QString SettingsCenterService::workspaceSettingsFilePath(
    const QString& workspaceRoot) const
{
    if (!workspaceSettingsFilePathOverride.isEmpty())
        return workspaceSettingsFilePathOverride;
    const QString directory =
        WorkspaceConfigurationService::projectDirectoryPath(
            workspaceRoot);
    if (directory.isEmpty())
        return QString();
    return QDir(directory).absoluteFilePath(
        QString::fromLatin1(kWorkspaceFile));
}
