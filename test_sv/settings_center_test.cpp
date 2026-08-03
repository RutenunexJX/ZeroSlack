#include "settingscenterservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QJsonObject workspaceDocument(const QJsonObject& values,
                              int version = 1)
{
    return {
        {QStringLiteral("schema"),
         QStringLiteral("ZeroSlack.SettingsCenter")},
        {QStringLiteral("version"), version},
        {QStringLiteral("futureRoot"),
         QJsonObject{{QStringLiteral("keep"), true}}},
        {QStringLiteral("values"), values},
    };
}

bool hasIssue(const QList<SettingsCenterValidationIssue>& issues,
              SettingsCenterIssueKind kind,
              const QString& fieldId = QString())
{
    for (const SettingsCenterValidationIssue& issue : issues) {
        if (issue.kind == kind
            && (fieldId.isEmpty() || issue.fieldId == fieldId)) {
            return true;
        }
    }
    return false;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const QList<SettingsCenterCategoryDescriptor>& categories =
        SettingsCenterSchema::categories();
    check(categories.size() == 6,
          "schema exposes exactly six settings categories");
    check(SettingsCenterSchema::categoryId(
              SettingsCenterCategory::Font)
              == QStringLiteral("font")
              && SettingsCenterSchema::categoryId(
                     SettingsCenterCategory::Formatter)
                     == QStringLiteral("formatter")
              && SettingsCenterSchema::categoryId(
                     SettingsCenterCategory::Shortcut)
                     == QStringLiteral("shortcut")
              && SettingsCenterSchema::categoryId(
                     SettingsCenterCategory::Annotation)
                     == QStringLiteral("annotation")
              && SettingsCenterSchema::categoryId(
                     SettingsCenterCategory::Analysis)
                     == QStringLiteral("analysis")
              && SettingsCenterSchema::categoryId(
                     SettingsCenterCategory::Layout)
                     == QStringLiteral("layout"),
          "category identifiers are stable for UI binding");
    check(SettingsCenterSchema::field(
              QStringLiteral("font.sizePt"))
              && SettingsCenterSchema::field(
                     QStringLiteral("formatter.profile"))
              && SettingsCenterSchema::field(
                     QStringLiteral("shortcut.overrides"))
              && SettingsCenterSchema::field(
                     QStringLiteral("annotation.maxLanes"))
              && SettingsCenterSchema::field(
                     QStringLiteral("analysis.incremental"))
              && SettingsCenterSchema::field(
                     QStringLiteral("layout.rememberPanelState")),
          "each category provides a field description model");
    check(SettingsCenterSchema::field(
              QStringLiteral("font.sizePt"))->storageKey
              == QStringLiteral("editorAppearance/fontSizePt")
              && SettingsCenterSchema::field(
                     QStringLiteral("formatter.profile"))->storageKey
                     == QStringLiteral("formatter/profile"),
          "legacy appearance and formatter keys are schema-owned");

    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary settings root is valid");
    if (!temporary.isValid())
        return 1;

    const QString globalPath =
        QDir(temporary.path()).absoluteFilePath(
            QStringLiteral("global.ini"));
    const QString workspaceRoot =
        QDir(temporary.path()).absoluteFilePath(
            QStringLiteral("workspace"));
    check(QDir().mkpath(workspaceRoot),
          "workspace fixture is created");

    {
        QSettings settings(globalPath, QSettings::IniFormat);
        settings.setValue(
            QStringLiteral("editorAppearance/fontFamily"),
            QStringLiteral("Iosevka"));
        settings.setValue(
            QStringLiteral("editorAppearance/fontSizePt"),
            18);
        settings.setValue(
            QStringLiteral("formatter/profile"),
            QStringLiteral("indent_only"));
        settings.setValue(
            QStringLiteral("settingsCenter/annotation/maxPerLine"),
            9);
        settings.setValue(
            QStringLiteral("settingsCenter/shortcut/overrides"),
            QVariantMap{
                {QStringLiteral("file.save"),
                 QStringLiteral("Ctrl+S")},
            });
        settings.setValue(
            QStringLiteral("unrelated/futureGlobal"),
            QStringLiteral("preserve"));
        settings.sync();
    }

    SettingsCenterService service(globalPath);
    const QString workspacePath =
        service.workspaceSettingsFilePath(workspaceRoot);
    const QJsonObject initialWorkspaceValues = {
        {QStringLiteral("font.sizePt"), 99},
        {QStringLiteral("formatter.profile"),
         QStringLiteral("STRUCTURED")},
        {QStringLiteral("shortcut.overrides"),
         QJsonObject{
             {QStringLiteral("navigate.open"),
              QStringLiteral("Ctrl+P")},
             {QStringLiteral("navigate.palette"),
              QStringLiteral("Ctrl+P")}}},
        {QStringLiteral("annotation.maxLanes"), 0},
        {QStringLiteral("future.setting"),
         QStringLiteral("preserve")},
    };
    check(writeBytes(
              workspacePath,
              QJsonDocument(
                  workspaceDocument(initialWorkspaceValues))
                  .toJson(QJsonDocument::Indented)),
          "workspace settings fixture is written");

    SettingsCenterSnapshot snapshot =
        service.load(workspaceRoot);
    check(snapshot.globalCompatible
              && snapshot.workspaceCompatible
              && snapshot.workspaceDocumentExists,
          "compatible global and workspace layers load");
    check(snapshot.globalValues.value(
              QStringLiteral("font.family")).toString()
              == QStringLiteral("Iosevka")
              && snapshot.globalValues.value(
                     QStringLiteral("font.sizePt")).toInt()
                     == 18
              && snapshot.globalValues.value(
                     QStringLiteral("formatter.profile")).toString()
                     == QStringLiteral("indent_only"),
          "existing appearance and formatter keys are reused");
    check(snapshot.value(QStringLiteral("font.sizePt")).toInt()
              == 32
              && snapshot.value(
                     QStringLiteral("formatter.profile")).toString()
                     == QStringLiteral("structured")
              && snapshot.value(
                     QStringLiteral("annotation.maxLanes")).toInt()
                     == 1,
          "workspace overrides win and invalid ranges normalize");
    const QVariantMap shortcuts =
        snapshot.value(QStringLiteral("shortcut.overrides")).toMap();
    check(shortcuts.value(QStringLiteral("file.save")).toString()
              == QStringLiteral("Ctrl+S")
              && shortcuts.value(
                     QStringLiteral("navigate.open")).toString()
                     == QStringLiteral("Ctrl+P")
              && shortcuts.value(
                     QStringLiteral("navigate.palette")).toString()
                     == QStringLiteral("Ctrl+P"),
          "map-valued workspace overrides merge by Action Registry ID");
    check(hasIssue(snapshot.issues,
                   SettingsCenterIssueKind::NormalizedValue,
                   QStringLiteral("font.sizePt"))
              && hasIssue(snapshot.issues,
                          SettingsCenterIssueKind::NormalizedValue,
                          QStringLiteral("formatter.profile"))
              && hasIssue(snapshot.issues,
                          SettingsCenterIssueKind::UnknownField,
                          QStringLiteral("future.setting"))
              && hasIssue(snapshot.issues,
                          SettingsCenterIssueKind::InvalidValue,
                          QStringLiteral("shortcut.overrides")),
          "normalization and unknown fields are reported explicitly");
    check(snapshot.value(
              QStringLiteral("analysis.maxDiagnostics")).toInt()
              == 2000
              && snapshot.value(
                     QStringLiteral(
                         "layout.restoreWorkspaceSession")).toBool(),
          "missing fields resolve from schema defaults");

    const SettingsCenterSaveResult globalSave =
        service.saveGlobal(snapshot.globalValues,
                           snapshot.globalRevision);
    check(globalSave.saved && !globalSave.conflict,
          "global layer saves with revision checking");
    {
        QSettings settings(globalPath, QSettings::IniFormat);
        check(settings.value(
                  QStringLiteral("editorAppearance/fontSizePt")).toInt()
                  == 18
                  && settings.value(
                         QStringLiteral("formatter/profile")).toString()
                         == QStringLiteral("indent_only"),
              "global save retains canonical legacy storage keys");
        check(settings.value(
                  QStringLiteral("unrelated/futureGlobal")).toString()
                  == QStringLiteral("preserve"),
              "global save preserves unrelated fields");
        check(settings.value(
                  QStringLiteral("settingsCenter/schema")).toString()
                  == QStringLiteral("ZeroSlack.SettingsCenter")
                  && settings.value(
                         QStringLiteral("settingsCenter/version")).toInt()
                         == 1,
              "global save records the unified schema version");
    }

    QVariantMap workspaceValues = snapshot.workspaceValues;
    workspaceValues.insert(
        QStringLiteral("analysis.maxDiagnostics"),
        0);
    const SettingsCenterSaveResult workspaceSave =
        service.saveWorkspace(workspaceRoot,
                              workspaceValues,
                              snapshot.workspaceRevision);
    check(workspaceSave.saved
              && workspaceSave.normalizedValues.value(
                     QStringLiteral(
                         "analysis.maxDiagnostics")).toInt()
                     == 1,
          "workspace layer validates before atomic save");
    const QByteArray firstSaveBytes = readBytes(workspacePath);
    const QJsonObject firstSaveObject =
        QJsonDocument::fromJson(firstSaveBytes).object();
    const QJsonObject savedValues =
        firstSaveObject.value(QStringLiteral("values")).toObject();
    check(firstSaveObject.value(
              QStringLiteral("futureRoot")).toObject().value(
                  QStringLiteral("keep")).toBool()
              && savedValues.value(
                     QStringLiteral("future.setting")).toString()
                     == QStringLiteral("preserve"),
          "workspace save preserves unknown root and value fields");
    check(savedValues.value(
              QStringLiteral("font.sizePt")).toInt()
              == 32
              && savedValues.value(
                     QStringLiteral(
                         "analysis.maxDiagnostics")).toInt()
                     == 1,
          "workspace save writes only normalized known values");

    const SettingsCenterSaveResult repeatedSave =
        service.saveWorkspace(workspaceRoot,
                              workspaceSave.normalizedValues,
                              workspaceSave.revision);
    check(repeatedSave.saved
              && readBytes(workspacePath) == firstSaveBytes,
          "identical workspace saves produce deterministic bytes");

    const SettingsCenterSnapshot beforeConflict =
        service.load(workspaceRoot);
    QJsonObject externallyChanged =
        QJsonDocument::fromJson(readBytes(workspacePath)).object();
    externallyChanged.insert(QStringLiteral("externalGeneration"), 2);
    check(writeBytes(
              workspacePath,
              QJsonDocument(externallyChanged)
                  .toJson(QJsonDocument::Indented)),
          "external settings change is simulated");
    const QByteArray externalBytes = readBytes(workspacePath);
    const SettingsCenterSaveResult conflict =
        service.saveWorkspace(
            workspaceRoot,
            beforeConflict.workspaceValues,
            beforeConflict.workspaceRevision);
    check(!conflict.saved && conflict.conflict
              && readBytes(workspacePath) == externalBytes,
          "stale workspace revision cannot overwrite external changes");

    check(writeBytes(
              workspacePath,
              QJsonDocument(
                  workspaceDocument(QJsonObject(), 2))
                  .toJson(QJsonDocument::Indented)),
          "future-version fixture is written");
    const QByteArray futureBytes = readBytes(workspacePath);
    const SettingsCenterSnapshot future =
        service.load(workspaceRoot);
    const SettingsCenterSaveResult futureSave =
        service.saveWorkspace(
            workspaceRoot,
            QVariantMap{
                {QStringLiteral("analysis.enabled"), false}},
            future.workspaceRevision);
    check(!future.workspaceCompatible
              && hasIssue(
                  future.issues,
                  SettingsCenterIssueKind::UnsupportedDocument)
              && !futureSave.saved
              && readBytes(workspacePath) == futureBytes,
          "unsupported workspace versions are explicit and never overwritten");

    const QString futureGlobalPath =
        QDir(temporary.path()).absoluteFilePath(
            QStringLiteral("future-global.ini"));
    {
        QSettings settings(futureGlobalPath, QSettings::IniFormat);
        settings.setValue(
            QStringLiteral("settingsCenter/schema"),
            QStringLiteral("ZeroSlack.SettingsCenter"));
        settings.setValue(
            QStringLiteral("settingsCenter/version"),
            2);
        settings.setValue(
            QStringLiteral("editorAppearance/fontSizePt"),
            27);
        settings.setValue(
            QStringLiteral("future/opaque"),
            QStringLiteral("keep"));
        settings.sync();
    }
    const QByteArray futureGlobalBytes =
        readBytes(futureGlobalPath);
    SettingsCenterService futureGlobalService(
        futureGlobalPath);
    const SettingsCenterSnapshot futureGlobal =
        futureGlobalService.load();
    const SettingsCenterSaveResult futureGlobalSave =
        futureGlobalService.saveGlobal(
            QVariantMap{
                {QStringLiteral("font.sizePt"), 12}},
            futureGlobal.globalRevision);
    check(!futureGlobal.globalCompatible
              && futureGlobal.globalValues.isEmpty()
              && !futureGlobalSave.saved
              && readBytes(futureGlobalPath)
                     == futureGlobalBytes,
          "unsupported global versions are read-only");

    std::cout << "settings_center_test: "
              << (checks - failures) << '/' << checks
              << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
