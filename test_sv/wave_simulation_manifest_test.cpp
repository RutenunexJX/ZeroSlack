#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "wavesimulationmanifestservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

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

QString readText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

SemanticSymbolRecord findRecord(
    const QList<SemanticSymbolRecord>& records,
    const QString& name,
    SymbolTaxonomy::CollectorKind kind,
    const QString& owner = QString())
{
    for (const SemanticSymbolRecord& record : records) {
        if (record.name == name && record.collectorKind == kind
            && (owner.isEmpty() || record.owner.name == owner)) {
            return record;
        }
    }
    return {};
}

const WaveSimulationManifestParameter* parameter(
    const WaveSimulationModuleManifest& manifest,
    const QString& name)
{
    for (const WaveSimulationManifestParameter& value : manifest.parameters) {
        if (value.name == name)
            return &value;
    }
    return nullptr;
}

const WaveSimulationManifestPort* port(
    const WaveSimulationModuleManifest& manifest,
    const QString& name)
{
    for (const WaveSimulationManifestPort& value : manifest.ports) {
        if (value.name == name)
            return &value;
    }
    return nullptr;
}

const WaveSimulationManifestObservation* observation(
    const WaveSimulationModuleManifest& manifest,
    const QString& name)
{
    for (const WaveSimulationManifestObservation& value :
         manifest.observations) {
        if (value.name == name)
            return &value;
    }
    return nullptr;
}

bool isRelativeProjectPath(const QString& path)
{
    return !path.isEmpty()
        && !QDir::isAbsolutePath(path)
        && path != QStringLiteral("..")
        && !path.startsWith(QStringLiteral("../"));
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const QString sourceRoot = qEnvironmentVariable("ZEROSLACK_SOURCE_DIR");
    const QString fixtureRoot = QDir(sourceRoot).absoluteFilePath(
        QStringLiteral("test_sv/wave_simulation_manifest_fixture"));
    const QString headerFile = QDir(fixtureRoot).absoluteFilePath(
        QStringLiteral("manifest_defs.svh"));
    const QString childFile = QDir(fixtureRoot).absoluteFilePath(
        QStringLiteral("manifest_child.sv"));
    const QString topFile = QDir(fixtureRoot).absoluteFilePath(
        QStringLiteral("manifest_top.sv"));
    const QString schemaFile = QDir(sourceRoot).absoluteFilePath(
        QStringLiteral("schemas/wave-simulation-module-manifest-v2.schema.json"));

    check(QFileInfo(fixtureRoot).isDir(), "real manifest fixture exists");
    const QHash<QString, QString> contents{
        {childFile, readText(childFile)},
        {topFile, readText(topFile)},
        {headerFile, readText(headerFile)},
    };
    check(!contents.value(childFile).isEmpty()
              && !contents.value(topFile).isEmpty()
              && !contents.value(headerFile).isEmpty(),
          "all fixture sources are readable");

    const QHash<QString, QString> defines{
        {QStringLiteral("MANIFEST_DEFAULT_WIDTH"), QStringLiteral("5")},
    };
    const QStringList orderedFiles{childFile, topFile, headerFile};
    SlangManager slang;
    const QList<SemanticSymbolRecord> records =
        slang.extractOverlayWorkspaceSymbolRecords(
            contents,
            {fixtureRoot},
            defines,
            nullptr,
            nullptr,
            orderedFiles);
    check(!records.isEmpty(), "Slang elaborates the real manifest fixture");

    const SemanticSymbolRecord module = findRecord(
        records,
        QStringLiteral("manifest_child"),
        SymbolTaxonomy::CollectorKind::Module);
    const SemanticSymbolRecord widthRecord = findRecord(
        records,
        QStringLiteral("WIDTH"),
        SymbolTaxonomy::CollectorKind::Parameter,
        QStringLiteral("manifest_child"));
    const SemanticSymbolRecord dataPortRecord = findRecord(
        records,
        QStringLiteral("data_i"),
        SymbolTaxonomy::CollectorKind::PortInput,
        QStringLiteral("manifest_child"));
    check(module.isValid() && widthRecord.isValid() && dataPortRecord.isValid(),
          "fixture exposes module, parameter, and port semantic records");

    ProjectSnapshot project;
    project.revision = 7;
    project.workspaceRoot = fixtureRoot;
    project.allFiles = orderedFiles;
    project.systemVerilogFiles = orderedFiles;
    project.includeDirs = {fixtureRoot};
    project.defines = defines;
    project.fileExtensions = {
        QStringLiteral(".sv"), QStringLiteral(".svh")};
    project.sourceRoles.insert(childFile,
                               SymbolTaxonomy::SourceRole::DesignSource);
    project.sourceRoles.insert(topFile,
                               SymbolTaxonomy::SourceRole::DesignSource);
    project.sourceRoles.insert(headerFile,
                               SymbolTaxonomy::SourceRole::Header);
    project.topModule = QStringLiteral("manifest_top");

    const auto snapshot = std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, contents));
    WaveSimulationManifestService service;
    WaveSimulationManifestBuildRequest definitionRequest;
    definitionRequest.semanticSnapshot = snapshot;
    definitionRequest.project = project;
    definitionRequest.moduleStableKey = module.stableKey;
    definitionRequest.observationScope.mode =
        QStringLiteral("always");
    definitionRequest.observationScope.fileName = childFile;
    definitionRequest.observationScope.startLine = 16;
    definitionRequest.observationScope.endLine = 19;
    definitionRequest.observationScope.label =
        QStringLiteral("always_comb at manifest_child.sv:16");
    const WaveSimulationManifestBuildResult definition =
        service.build(definitionRequest);
    check(definition.succeeded() && definition.manifest.isValid(),
          "module-definition manifest builds successfully");
    check(definition.manifest.target.mode == QStringLiteral("module-definition")
              && definition.manifest.target.instancePath.isEmpty(),
          "definition and instance target modes are distinct");
    check(definition.manifest.schemaVersion == 2
              && definition.manifest.observationScope.mode
                     == QStringLiteral("always")
              && definition.manifest.observationScope.sourceFile
                     == QStringLiteral("manifest_child.sv")
              && definition.manifest.observationScope.startLine == 16
              && definition.manifest.observationScope.endLine == 19,
          "selected always scope is preserved as portable manifest metadata");
    const WaveSimulationManifestObservation* nextData = observation(
        definition.manifest, QStringLiteral("next_data"));
    check(nextData
              && nextData->accessPath == QStringLiteral("next_data")
              && nextData->type.shape.semanticAvailable
              && nextData->type.shape.bitWidth == 6
              && observation(definition.manifest,
                             QStringLiteral("payload_i"))
              && observation(definition.manifest,
                             QStringLiteral("data_i"))
              && observation(definition.manifest,
                             QStringLiteral("data_o")),
          "selected always scope resolves internal and port observations from Slang relationships");
    check(definition.manifest.sources.size() == 3
              && definition.manifest.sources.at(0).path
                  == QStringLiteral("manifest_child.sv")
              && definition.manifest.sources.at(2).role
                  == QStringLiteral("header")
              && definition.manifest.includeDirs
                  == QStringList{QStringLiteral(".")}
              && definition.manifest.defines.value(
                     QStringLiteral("MANIFEST_DEFAULT_WIDTH"))
                  == QStringLiteral("5"),
          "dependencies, include directories, and defines preserve project configuration");

    const WaveSimulationManifestParameter* defaultWidth = parameter(
        definition.manifest, QStringLiteral("WIDTH"));
    const WaveSimulationManifestPort* defaultData = port(
        definition.manifest, QStringLiteral("data_i"));
    check(defaultWidth && defaultData
              && defaultWidth->valueText
                  == widthRecord.presentation.defaultInfo.valueText
              && defaultData->type.shape.bitWidth
                  == dataPortRecord.presentation.defaultInfo.bitWidth
              && defaultData->type.shape.packedDimensions
                  == dataPortRecord.presentation.defaultInfo
                         .packedDimensionsText,
          "definition manifest copies Slang default parameter and port facts");

    WaveSimulationManifestBuildRequest instanceRequest = definitionRequest;
    instanceRequest.instancePath = QStringLiteral("manifest_top.u_fast");
    const WaveSimulationManifestBuildResult instance =
        service.build(instanceRequest);
    check(instance.succeeded() && instance.manifest.isValid()
              && instance.manifest.target.mode == QStringLiteral("instance"),
          "instance manifest builds successfully");
    const auto widthInfo = widthRecord.presentation.instanceInfoByPath.constFind(
        instanceRequest.instancePath);
    const auto dataInfo = dataPortRecord.presentation.instanceInfoByPath.constFind(
        instanceRequest.instancePath);
    const WaveSimulationManifestParameter* boundWidth = parameter(
        instance.manifest, QStringLiteral("WIDTH"));
    const WaveSimulationManifestPort* boundData = port(
        instance.manifest, QStringLiteral("data_i"));
    check(widthInfo != widthRecord.presentation.instanceInfoByPath.constEnd()
              && dataInfo
                  != dataPortRecord.presentation.instanceInfoByPath.constEnd()
              && boundWidth && boundData
              && boundWidth->valueText == widthInfo->valueText
              && boundData->type.shape.bitWidth == dataInfo->bitWidth
              && boundData->type.shape.packedDimensions
                  == dataInfo->packedDimensionsText
              && boundWidth->valueText != defaultWidth->valueText,
          "instance manifest uses exact Slang instance values without default fallback");

    const WaveSimulationManifestPort* modePort = port(
        instance.manifest, QStringLiteral("mode_i"));
    const WaveSimulationManifestPort* payloadPort = port(
        instance.manifest, QStringLiteral("payload_i"));
    check(modePort && modePort->type.shape.semanticKind == QStringLiteral("enum")
              && modePort->type.enumValues.size() == 2
              && modePort->type.enumValues.at(0).name
                  == QStringLiteral("MANIFEST_IDLE")
              && modePort->type.enumValues.at(1).name
                  == QStringLiteral("MANIFEST_RUN"),
          "enum port metadata preserves semantic source order");
    check(payloadPort
              && payloadPort->type.shape.semanticKind
                  == QStringLiteral("packed-struct")
              && payloadPort->type.structMembers.size() == 2
              && payloadPort->type.structMembers.at(0).name
                  == QStringLiteral("valid")
              && payloadPort->type.structMembers.at(1).name
                  == QStringLiteral("payload"),
          "packed struct port metadata preserves semantic source order");

    WaveSimulationManifestBuildRequest explicitRequest = definitionRequest;
    explicitRequest.observationScope = {};
    WaveSimulationObservationRequest explicitObservation;
    explicitObservation.name = QStringLiteral("next_data");
    explicitObservation.accessPath = QStringLiteral("next_data");
    explicitObservation.fileName = childFile;
    explicitObservation.line = 14;
    explicitRequest.explicitObservations = {explicitObservation};
    const WaveSimulationManifestBuildResult explicitResult =
        service.build(explicitRequest);
    check(explicitResult.succeeded()
              && explicitResult.manifest.observationScope.mode
                     == QStringLiteral("module")
              && explicitResult.manifest.observations.size() == 1
              && explicitResult.manifest.observations.constFirst().name
                     == QStringLiteral("next_data"),
          "an editor-selected internal signal can be added explicitly without narrowing compilation");

    WaveSimulationManifestBuildRequest foreignScopeRequest =
        definitionRequest;
    foreignScopeRequest.observationScope.fileName = topFile;
    const WaveSimulationManifestBuildResult foreignScopeResult =
        service.build(foreignScopeRequest);
    check(foreignScopeResult.status
              == WaveSimulationManifestBuildStatus::InvalidObservationScope,
          "an always observation scope from another source file is rejected");

    for (const WaveSimulationManifestSource& source : instance.manifest.sources)
        check(isRelativeProjectPath(source.path), "source paths are workspace-relative");
    for (const QString& includeDir : instance.manifest.includeDirs)
        check(isRelativeProjectPath(includeDir), "include paths are workspace-relative");
    check(isRelativeProjectPath(instance.manifest.target.sourceFile),
          "target source path is workspace-relative");
    check(!instance.manifest.toJsonBytes().contains(
              fixtureRoot.toUtf8()),
          "serialized manifest does not leak the absolute workspace path");

    QFile schema(schemaFile);
    check(schema.open(QIODevice::ReadOnly), "versioned JSON Schema is readable");
    QJsonParseError schemaError;
    const QJsonDocument schemaDocument = QJsonDocument::fromJson(
        schema.readAll(), &schemaError);
    check(schemaError.error == QJsonParseError::NoError
              && schemaDocument.isObject()
              && schemaDocument.object().value(QStringLiteral("properties"))
                     .toObject().value(QStringLiteral("schemaVersion"))
                     .toObject().value(QStringLiteral("const")).toInt()
                  == WaveSimulationModuleManifest::kSchemaVersion,
          "JSON Schema parses and pins the contract version");
    QJsonParseError manifestError;
    const QJsonDocument serialized = QJsonDocument::fromJson(
        instance.manifest.toJsonBytes(), &manifestError);
    check(manifestError.error == QJsonParseError::NoError
              && serialized.object().value(QStringLiteral("schemaVersion")).toInt()
                  == WaveSimulationModuleManifest::kSchemaVersion,
          "manifest serialization is valid versioned JSON");

    WaveSimulationManifestBuildRequest missingInstance = definitionRequest;
    missingInstance.instancePath = QStringLiteral("manifest_top.missing");
    const WaveSimulationManifestBuildResult missing =
        service.build(missingInstance);
    check(missing.status
              == WaveSimulationManifestBuildStatus::InstanceContextNotFound,
          "unknown instance paths fail instead of falling back to definition defaults");

    ProjectSnapshot externalProject = project;
    externalProject.includeDirs.append(
        QDir(fixtureRoot).absoluteFilePath(QStringLiteral("../external")));
    WaveSimulationManifestBuildRequest externalRequest = definitionRequest;
    externalRequest.project = externalProject;
    check(service.build(externalRequest).status
              == WaveSimulationManifestBuildStatus::ProjectPathOutsideWorkspace,
          "v1 rejects project paths that cannot satisfy the relative-path contract");

    std::cout << "wave simulation manifest checks: " << checks
              << ", failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
