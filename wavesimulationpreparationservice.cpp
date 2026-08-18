#include "wavesimulationpreparationservice.h"

#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "wavesimulationmanifestservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <optional>
#include <utility>

namespace {
using DeclarationKind = SymbolTaxonomy::DeclarationKind;

QString normalizedPath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    QString result = QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

bool cancelled(const std::function<bool()>& isCancelled)
{
    return isCancelled && isCancelled();
}

bool readSource(const QString& fileName, QString* content)
{
    if (!content)
        return false;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return false;
    *content = QString::fromUtf8(bytes);
    return true;
}

bool writeArtifact(const QString& fileName,
                   const QByteArray& content,
                   QString* error)
{
    const QFileInfo info(fileName);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("Cannot create artifact directory: %1")
                         .arg(info.absolutePath());
        return false;
    }
    QSaveFile file(fileName);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(content) != content.size()
        || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

std::optional<QString> workspaceRelativePath(
    const QString& workspaceRoot,
    const QString& fileName)
{
    const QString root = normalizedPath(workspaceRoot);
    const QString file = normalizedPath(fileName);
    if (root.isEmpty() || file.isEmpty())
        return std::nullopt;
    const QString relative = QDir(root).relativeFilePath(file);
    if (relative == QStringLiteral("..")
        || relative.startsWith(QStringLiteral("../"))
        || QDir::isAbsolutePath(relative)) {
        return std::nullopt;
    }
    return QDir::cleanPath(relative);
}

QList<SemanticRelationship> semanticRelationships(
    const QVector<RelationshipToAdd>& relationships)
{
    QList<SemanticRelationship> result;
    result.reserve(relationships.size());
    for (const RelationshipToAdd& relationship : relationships) {
        if (relationship.fromId < 0 || relationship.toId < 0)
            continue;
        SemanticRelationship current;
        current.fromId = relationship.fromId;
        current.toId = relationship.toId;
        current.type = relationship.type;
        current.evidenceText = relationship.context;
        current.confidence = relationship.confidence;
        current.evidenceRange = relationship.evidenceRange;
        current.fromAccessPath = relationship.fromAccessPath;
        current.toAccessPath = relationship.toAccessPath;
        current.exactValueForward = relationship.exactValueForward;
        current.provenance = RelationshipProvenance::SlangExtracted;
        result.append(std::move(current));
    }
    return result;
}

const RelationshipExtractionInfo* relationshipInfoForFile(
    const QHash<QString, RelationshipExtractionInfo>& infoByFile,
    const QString& fileName)
{
    const QString target = normalizedPath(fileName);
    for (auto it = infoByFile.constBegin(); it != infoByFile.constEnd(); ++it) {
        if (normalizedPath(it.key()) == target)
            return &it.value();
    }
    return nullptr;
}

QString contentForFile(const QHash<QString, QString>& contents,
                       const QString& fileName)
{
    const QString target = normalizedPath(fileName);
    for (auto it = contents.constBegin(); it != contents.constEnd(); ++it) {
        if (normalizedPath(it.key()) == target)
            return it.value();
    }
    return QString();
}

QList<SemanticSymbolRecord> moduleCandidates(
    const SemanticIndexSnapshot& snapshot,
    const WaveSimulationTargetContext& target)
{
    QList<SemanticSymbolRecord> matchingFile;
    QList<SemanticSymbolRecord> matchingName;
    const QString targetFile = normalizedPath(target.fileName);
    for (const SemanticSymbolRecord& record :
         snapshot.getSymbolRecordsByName(target.moduleName)) {
        if (record.declarationKind != DeclarationKind::Module)
            continue;
        matchingName.append(record);
        if (!targetFile.isEmpty()
            && normalizedPath(record.location.fileName) == targetFile) {
            matchingFile.append(record);
        }
    }
    return matchingFile.isEmpty() ? matchingName : matchingFile;
}

QString unsupportedManifestReason(
    const WaveSimulationModuleManifest& manifest)
{
    for (const WaveSimulationManifestParameter& parameter :
         manifest.parameters) {
        if (!parameter.semanticAvailable) {
            return QStringLiteral(
                "Parameter %1 has no complete Slang elaboration.")
                .arg(parameter.name);
        }
    }
    for (const WaveSimulationManifestPort& port : manifest.ports) {
        const WaveSimulationManifestTypeShape& shape = port.type.shape;
        if (port.direction != QStringLiteral("input")
            && port.direction != QStringLiteral("output")) {
            return QStringLiteral(
                "Port %1 uses unsupported direction %2.")
                .arg(port.name, port.direction);
        }
        if (!shape.semanticAvailable || !shape.fixedSize
            || !shape.integral || shape.unpackedArray
            || shape.interfaceType || shape.bitWidth == 0
            || shape.bitWidth > 64) {
            return QStringLiteral(
                "Port %1 is not a fully elaborated fixed integral port up to 64 bits.")
                .arg(port.name);
        }
    }
    return QString();
}

QString makeRunId()
{
    return QDateTime::currentDateTimeUtc()
               .toString(QStringLiteral("yyyyMMdd-HHmmsszzz"))
        + QLatin1Char('-')
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}

WaveSimulationPreparationResult
WaveSimulationPreparationService::prepare(
    const WaveSimulationPreparationRequest& request,
    const std::function<bool()>& isCancelled)
{
    WaveSimulationPreparationResult result;
    if (!request.project.isOpen()
        || request.target.moduleName.trimmed().isEmpty()
        || !request.cachePaths.isValid()) {
        result.status = WaveSimulationPreparationStatus::InvalidRequest;
        result.message = QStringLiteral(
            "Wave Simulation requires an open workspace, a selected module, and valid cache paths.");
        return result;
    }

    QHash<QString, WaveSimulationSourceOverride> overrides;
    for (const WaveSimulationSourceOverride& source :
         request.sourceOverrides) {
        const QString key = normalizedPath(source.fileName);
        if (!key.isEmpty())
            overrides.insert(key, source);
    }

    QHash<QString, QString> contents;
    QStringList orderedFiles;
    orderedFiles.reserve(request.project.systemVerilogFiles.size());
    for (const QString& sourcePath : request.project.systemVerilogFiles) {
        if (cancelled(isCancelled)) {
            result.status = WaveSimulationPreparationStatus::Cancelled;
            result.message = QStringLiteral("Wave Simulation preparation was cancelled.");
            return result;
        }
        const QString absolutePath = QFileInfo(sourcePath).absoluteFilePath();
        const QString key = normalizedPath(absolutePath);
        QString content;
        if (overrides.contains(key)) {
            content = overrides.value(key).content;
        } else if (!readSource(absolutePath, &content)) {
            result.status = WaveSimulationPreparationStatus::SourceReadFailed;
            result.message = QStringLiteral("Cannot read simulation source: %1")
                                 .arg(absolutePath);
            return result;
        }
        contents.insert(absolutePath, content);
        orderedFiles.append(absolutePath);
    }
    if (contents.isEmpty()) {
        result.status = WaveSimulationPreparationStatus::InvalidRequest;
        result.message = QStringLiteral("The workspace has no SystemVerilog sources.");
        return result;
    }

    SlangManager slang;
    const QList<SemanticSymbolRecord> records =
        slang.extractOverlayWorkspaceSymbolRecords(
            contents,
            request.project.includeDirs,
            request.project.defines,
            isCancelled,
            nullptr,
            orderedFiles);
    if (cancelled(isCancelled)) {
        result.status = WaveSimulationPreparationStatus::Cancelled;
        result.message = QStringLiteral("Wave Simulation preparation was cancelled.");
        return result;
    }
    if (records.isEmpty()) {
        result.status =
            WaveSimulationPreparationStatus::SemanticAnalysisFailed;
        result.message = QStringLiteral(
            "Slang did not produce a semantic model for the workspace snapshot.");
        return result;
    }

    SemanticIndexSnapshot preliminary =
        SemanticIndexSnapshot::fromSymbolRecords(
            records, {}, {}, contents);
    const QList<SemanticSymbolRecord> candidates =
        moduleCandidates(preliminary, request.target);
    if (candidates.isEmpty()) {
        result.status = WaveSimulationPreparationStatus::TargetNotFound;
        result.message = QStringLiteral(
            "The selected module is absent from the current Slang snapshot: %1")
                             .arg(request.target.moduleName);
        return result;
    }
    if (candidates.size() != 1) {
        result.status = WaveSimulationPreparationStatus::TargetAmbiguous;
        result.message = QStringLiteral(
            "The selected module resolves to multiple declarations: %1")
                             .arg(request.target.moduleName);
        return result;
    }

    const QHash<QString, RelationshipExtractionInfo> relationshipInfo =
        slang.extractOverlayWorkspaceRelationshipInfo(
            contents,
            request.project.includeDirs,
            request.project.defines,
            isCancelled,
            orderedFiles);
    if (cancelled(isCancelled)) {
        result.status = WaveSimulationPreparationStatus::Cancelled;
        result.message = QStringLiteral("Wave Simulation preparation was cancelled.");
        return result;
    }
    SmartRelationshipBuilder relationshipBuilder(
        nullptr,
        &slang,
        [&preliminary](const QString& fileName) {
            return preliminary.getSymbolRecords(fileName);
        });
    QList<SemanticRelationship> relationships;
    for (const QString& fileName : orderedFiles) {
        if (cancelled(isCancelled)) {
            relationshipBuilder.cancelAnalysis();
            result.status = WaveSimulationPreparationStatus::Cancelled;
            result.message = QStringLiteral("Wave Simulation preparation was cancelled.");
            return result;
        }
        relationships.append(semanticRelationships(
            relationshipBuilder.computeRelationships(
                fileName,
                contentForFile(contents, fileName),
                preliminary.getSymbolRecords(fileName),
                &preliminary,
                request.project.includeDirs,
                request.project.defines,
                relationshipInfoForFile(relationshipInfo, fileName))));
    }
    const std::shared_ptr<const SemanticIndexSnapshot> snapshot =
        std::make_shared<const SemanticIndexSnapshot>(
            preliminary.withAdditionalRelationships(relationships));

    WaveSimulationManifestBuildRequest manifestRequest;
    manifestRequest.semanticSnapshot = snapshot;
    manifestRequest.project = request.project;
    manifestRequest.moduleStableKey = candidates.constFirst().stableKey;
    manifestRequest.instancePath = request.target.instancePath;
    const WaveSimulationManifestBuildResult manifestResult =
        WaveSimulationManifestService().build(manifestRequest);
    if (!manifestResult.succeeded()) {
        result.status = WaveSimulationPreparationStatus::UnsupportedTarget;
        result.message = manifestResult.message;
        result.warnings = manifestResult.warnings;
        return result;
    }
    const QString unsupported = unsupportedManifestReason(
        manifestResult.manifest);
    if (!unsupported.isEmpty()) {
        result.status = WaveSimulationPreparationStatus::UnsupportedTarget;
        result.message = unsupported;
        result.warnings = manifestResult.warnings;
        return result;
    }

    result.runId = makeRunId();
    result.mirrorWorkspaceRoot = QDir(request.cachePaths.sourceMirrors)
        .absoluteFilePath(result.runId + QStringLiteral("/workspace"));
    result.resultRoot = QDir(request.cachePaths.results)
        .absoluteFilePath(result.runId);
    result.manifestPath = QDir(result.resultRoot)
        .absoluteFilePath(QStringLiteral("module-manifest.json"));
    result.stimulusProjectPath = QDir(result.resultRoot)
        .absoluteFilePath(QStringLiteral("stimulus.wave.json"));
    result.stimulusPath = QDir(result.resultRoot)
        .absoluteFilePath(QStringLiteral("stimulus.json"));
    result.resultProjectPath = QDir(result.resultRoot)
        .absoluteFilePath(QStringLiteral("result.wave.json"));

    for (const QString& sourcePath : orderedFiles) {
        const std::optional<QString> relative = workspaceRelativePath(
            request.project.workspaceRoot, sourcePath);
        if (!relative) {
            result.status = WaveSimulationPreparationStatus::ArtifactWriteFailed;
            result.message = QStringLiteral(
                "A simulation source is outside the workspace root: %1")
                                 .arg(sourcePath);
            return result;
        }
        QString error;
        const QString destination = QDir(result.mirrorWorkspaceRoot)
            .absoluteFilePath(*relative);
        if (!writeArtifact(
                destination,
                contentForFile(contents, sourcePath).toUtf8(),
                &error)) {
            result.status = WaveSimulationPreparationStatus::ArtifactWriteFailed;
            result.message = QStringLiteral("Cannot write source mirror %1: %2")
                                 .arg(destination, error);
            return result;
        }
    }
    for (const QString& includeDir : request.project.includeDirs) {
        const std::optional<QString> relative = workspaceRelativePath(
            request.project.workspaceRoot, includeDir);
        if (relative) {
            QDir().mkpath(QDir(result.mirrorWorkspaceRoot)
                              .absoluteFilePath(*relative));
        }
    }
    QString error;
    if (!writeArtifact(result.manifestPath,
                       manifestResult.manifest.toJsonBytes(),
                       &error)
        || !QDir().mkpath(result.resultRoot)) {
        result.status = WaveSimulationPreparationStatus::ArtifactWriteFailed;
        result.message = QStringLiteral("Cannot write Wave Simulation artifacts: %1")
                             .arg(error);
        return result;
    }

    result.status = WaveSimulationPreparationStatus::Success;
    result.message = QStringLiteral("Wave Simulation source snapshot is ready.");
    result.warnings = manifestResult.warnings;
    result.manifest = manifestResult.manifest;
    return result;
}

QString WaveSimulationPreparationService::statusCode(
    WaveSimulationPreparationStatus status)
{
    switch (status) {
    case WaveSimulationPreparationStatus::Success:
        return QStringLiteral("success");
    case WaveSimulationPreparationStatus::Cancelled:
        return QStringLiteral("cancelled");
    case WaveSimulationPreparationStatus::InvalidRequest:
        return QStringLiteral("invalid-request");
    case WaveSimulationPreparationStatus::SourceReadFailed:
        return QStringLiteral("source-read-failed");
    case WaveSimulationPreparationStatus::SemanticAnalysisFailed:
        return QStringLiteral("semantic-analysis-failed");
    case WaveSimulationPreparationStatus::TargetNotFound:
        return QStringLiteral("target-not-found");
    case WaveSimulationPreparationStatus::TargetAmbiguous:
        return QStringLiteral("target-ambiguous");
    case WaveSimulationPreparationStatus::UnsupportedTarget:
        return QStringLiteral("unsupported-target");
    case WaveSimulationPreparationStatus::ArtifactWriteFailed:
        return QStringLiteral("artifact-write-failed");
    }
    return QStringLiteral("unknown");
}
