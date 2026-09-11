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
    check(categories.size() == 8,
          "schema exposes exactly eight settings categories");
    check(SettingsCenterSchema::categoryId(
              SettingsCenterCategory::Appearance)
              == QStringLiteral("appearance")
              && SettingsCenterSchema::categoryId(
                     SettingsCenterCategory::Font)
              == QStringLiteral("font")
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
                     SettingsCenterCategory::Simulation)
                     == QStringLiteral("simulation")
              && SettingsCenterSchema::categoryId(
                     SettingsCenterCategory::Integration)
                     == QStringLiteral("integration")
              && SettingsCenterSchema::categoryId(
                     SettingsCenterCategory::Layout)
                     == QStringLiteral("layout"),
          "category identifiers are stable for UI binding");
    check(SettingsCenterSchema::field(
              QStringLiteral("appearance.theme"))
              && SettingsCenterSchema::field(
                     QStringLiteral("font.sizePt"))
              && SettingsCenterSchema::field(
                     QStringLiteral("shortcut.overrides"))
              && SettingsCenterSchema::field(
                     QStringLiteral("annotation.maxLanes"))
              && SettingsCenterSchema::field(
                     QStringLiteral("analysis.incremental"))
              && SettingsCenterSchema::field(
                     QStringLiteral("simulation.verilatorPath"))
              && SettingsCenterSchema::field(
                     QStringLiteral("integration.pinloomExecutablePath"))
              && SettingsCenterSchema::field(
                     QStringLiteral("layout.rememberPanelState")),
          "each category provides a field description model");
    const SettingsCenterFieldDescriptor* themeField =
        SettingsCenterSchema::field(
            QStringLiteral("appearance.theme"));
    check(themeField
              && themeField->storageKey
                     == QStringLiteral(
                         "settingsCenter/appearance/theme")
              && themeField->defaultValue.toString()
                     == QStringLiteral("Light")
              && themeField->choices
                     == QStringList{QStringLiteral("Light"),
                                    QStringLiteral("Dark"), QStringLiteral("Catppuccin Latte"),
                                    QStringLiteral("Catppuccin Frappe"), QStringLiteral("Catppuccin Macchiato"),
                                    QStringLiteral("Catppuccin Mocha")}
              && themeField->globalAllowed
              && !themeField->workspaceAllowed
              && themeField->alwaysActive
              && themeField->immediateApply,
          "appearance theme is a global immediate Light/Dark/Catppuccin choice");
    check(SettingsCenterSchema::field(
              QStringLiteral("font.sizePt"))->storageKey
              == QStringLiteral("editorAppearance/fontSizePt"),
          "legacy appearance key is schema-owned");
    const SettingsCenterFieldDescriptor* verilatorField =
        SettingsCenterSchema::field(
            QStringLiteral("simulation.verilatorPath"));
    const SettingsCenterLayerValidation emptyToolPath =
        SettingsCenterSchema::validateLayer(
            {{QStringLiteral("simulation.verilatorPath"), QString()}},
            SettingsCenterScope::Global);
    check(verilatorField
              && verilatorField->valueKind
                     == SettingsCenterValueKind::FilePath
              && verilatorField->defaultValue.toString().isEmpty()
              && verilatorField->globalAllowed
              && !verilatorField->workspaceAllowed
              && verilatorField->allowEmpty
              && emptyToolPath.values.value(
                     QStringLiteral("simulation.verilatorPath"))
                     .toString().isEmpty()
              && !hasIssue(emptyToolPath.issues,
                           SettingsCenterIssueKind::InvalidValue,
                           QStringLiteral("simulation.verilatorPath")),
          "Simulation tool paths are global optional file paths with automatic discovery defaults");

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
        {QStringLiteral("appearance.theme"),
         QStringLiteral("Dark")},
        {QStringLiteral("font.sizePt"), 99},
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
                     == 18,
          "existing appearance keys are reused");
    check(snapshot.value(QStringLiteral("font.sizePt")).toInt()
              == 32
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
                     QStringLiteral("appearance.theme")).toString()
                     == QStringLiteral("Light")
              && snapshot.value(
                     QStringLiteral(
                         "layout.restoreWorkspaceSession")).toBool(),
          "missing fields resolve from schema defaults");
    check(hasIssue(snapshot.issues,
                   SettingsCenterIssueKind::InvalidValue,
                   QStringLiteral("appearance.theme")),
          "workspace theme overrides are rejected");

    const SettingsCenterSaveResult globalSave =
        service.saveGlobal(snapshot.globalValues,
                           snapshot.globalRevision);
    check(globalSave.saved && !globalSave.conflict,
          "global layer saves with revision checking");
    {
        QSettings settings(globalPath, QSettings::IniFormat);
        check(settings.value(
                  QStringLiteral("editorAppearance/fontSizePt")).toInt()
                  == 18,
              "global save retains the canonical appearance storage key");
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

    {
        QSettings settings(globalPath, QSettings::IniFormat);
        settings.setValue(
            QStringLiteral("temporaryEditorDrawer/preferredEdge"),
            QStringLiteral("right"));
        settings.sync();
    }
    const SettingsCenterSnapshot afterUnrelatedGlobalWrite =
        service.load(workspaceRoot);
    check(afterUnrelatedGlobalWrite.globalRevision
              == globalSave.revision,
          "unrelated application preferences do not invalidate the Settings Center revision");

    QVariantMap darkGlobal = globalSave.normalizedValues;
    darkGlobal.insert(
        QStringLiteral("appearance.theme"),
        QStringLiteral("dark"));
    const SettingsCenterSaveResult darkSave =
        service.saveGlobal(
            darkGlobal,
            globalSave.revision);
    const SettingsCenterSnapshot darkSnapshot =
        service.load(workspaceRoot);
    check(darkSave.saved
              && darkSave.revision != globalSave.revision
              && darkSave.normalizedValues.value(
                     QStringLiteral("appearance.theme")).toString()
                     == QStringLiteral("Dark")
              && darkSnapshot.value(
                     QStringLiteral("appearance.theme")).toString()
                     == QStringLiteral("Dark"),
          "Dark theme is normalized, persisted and reloaded");

    QVariantMap invalidTheme = darkSave.normalizedValues;
    invalidTheme.insert(
        QStringLiteral("appearance.theme"),
        QStringLiteral("Solarized"));
    const SettingsCenterSaveResult invalidThemeSave =
        service.saveGlobal(
            invalidTheme,
            darkSave.revision);
    check(invalidThemeSave.saved
              && invalidThemeSave.normalizedValues.value(
                     QStringLiteral("appearance.theme")).toString()
                     == QStringLiteral("Light")
              && hasIssue(
                  invalidThemeSave.issues,
                  SettingsCenterIssueKind::InvalidValue,
                  QStringLiteral("appearance.theme")),
          "unsupported theme values fall back to Light explicitly");

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
