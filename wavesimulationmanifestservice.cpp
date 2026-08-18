#include "wavesimulationmanifestservice.h"

#include "semanticindexsnapshot.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <optional>

namespace {
using CollectorKind = SymbolTaxonomy::CollectorKind;
using DeclarationKind = SymbolTaxonomy::DeclarationKind;

QString normalizedAbsolutePath(const QString& workspaceRoot,
                               const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    const QString portable = QDir::fromNativeSeparators(path);
    const QString absolute = QDir::isAbsolutePath(portable)
        ? QFileInfo(portable).absoluteFilePath()
        : QDir(workspaceRoot).absoluteFilePath(portable);
    return QDir::cleanPath(QDir::fromNativeSeparators(absolute));
}

std::optional<QString> relativeProjectPath(const QString& workspaceRoot,
                                           const QString& path)
{
    const QString root = normalizedAbsolutePath(QString(), workspaceRoot);
    const QString absolute = normalizedAbsolutePath(root, path);
    if (root.isEmpty() || absolute.isEmpty())
        return std::nullopt;
    QString relative = QDir::fromNativeSeparators(
        QDir(root).relativeFilePath(absolute));
    relative = QDir::cleanPath(relative);
    if (QDir::isAbsolutePath(relative)
        || relative == QStringLiteral("..")
        || relative.startsWith(QStringLiteral("../"))) {
        return std::nullopt;
    }
    return relative;
}

QString sourceRoleName(SymbolTaxonomy::SourceRole role)
{
    switch (role) {
    case SymbolTaxonomy::SourceRole::DesignSource:
        return QStringLiteral("design");
    case SymbolTaxonomy::SourceRole::Header:
        return QStringLiteral("header");
    case SymbolTaxonomy::SourceRole::ExternalHeader:
        return QStringLiteral("external-header");
    case SymbolTaxonomy::SourceRole::Generated:
        return QStringLiteral("generated");
    case SymbolTaxonomy::SourceRole::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

QString declarationKindName(DeclarationKind kind)
{
    switch (kind) {
    case DeclarationKind::Module:
        return QStringLiteral("module");
    case DeclarationKind::Interface:
        return QStringLiteral("interface");
    case DeclarationKind::Typedef:
        return QStringLiteral("typedef");
    case DeclarationKind::Enum:
        return QStringLiteral("enum");
    case DeclarationKind::Struct:
        return QStringLiteral("struct");
    case DeclarationKind::Parameter:
        return QStringLiteral("parameter");
    case DeclarationKind::Port:
        return QStringLiteral("port");
    default:
        return QStringLiteral("unknown");
    }
}

QString portDirection(CollectorKind kind)
{
    switch (kind) {
    case CollectorKind::PortInput:
        return QStringLiteral("input");
    case CollectorKind::PortOutput:
        return QStringLiteral("output");
    case CollectorKind::PortInout:
        return QStringLiteral("inout");
    case CollectorKind::PortRef:
        return QStringLiteral("ref");
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
        return QStringLiteral("interface");
    default:
        return QStringLiteral("unknown");
    }
}

bool isPort(const SemanticSymbolRecord& record)
{
    return record.declarationKind == DeclarationKind::Port
        || record.collectorKind == CollectorKind::PortInput
        || record.collectorKind == CollectorKind::PortOutput
        || record.collectorKind == CollectorKind::PortInout
        || record.collectorKind == CollectorKind::PortRef
        || record.collectorKind == CollectorKind::PortInterface
        || record.collectorKind == CollectorKind::PortInterfaceModport;
}

bool sameStableKey(const SymbolStableKey& left,
                   const SymbolStableKey& right)
{
    return left.isValid() && right.isValid() && left == right;
}

bool belongsToModule(const SemanticSymbolRecord& record,
                     const SemanticSymbolRecord& module)
{
    if (sameStableKey(record.owner.stableKey, module.stableKey))
        return true;
    if (record.owner.name != module.name)
        return false;
    return QFileInfo(record.location.fileName).absoluteFilePath()
        == QFileInfo(module.location.fileName).absoluteFilePath();
}

const SemanticElaboratedSymbolInfo* elaboratedInfo(
    const SemanticSymbolRecord& record,
    const QString& instancePath)
{
    if (instancePath.isEmpty())
        return &record.presentation.defaultInfo;
    const auto instance = record.presentation.instanceInfoByPath.constFind(
        instancePath);
    if (instance != record.presentation.instanceInfoByPath.constEnd())
        return &instance.value();
    if (record.presentation.effectiveScopeKind
            == SemanticEffectiveScopeKind::Package
        || record.presentation.effectiveScopeKind
            == SemanticEffectiveScopeKind::CompilationUnit) {
        return &record.presentation.defaultInfo;
    }
    return nullptr;
}

const SemanticDeclaredTypeFacts* declaredTypeFacts(
    const SemanticSymbolRecord& record,
    const QString& instancePath)
{
    if (instancePath.isEmpty())
        return &record.presentation.defaultDeclaredTypeFacts;
    const auto instance = record.presentation.declaredTypeFactsByPath.constFind(
        instancePath);
    if (instance != record.presentation.declaredTypeFactsByPath.constEnd())
        return &instance.value();
    if (record.presentation.effectiveScopeKind
            == SemanticEffectiveScopeKind::Package
        || record.presentation.effectiveScopeKind
            == SemanticEffectiveScopeKind::CompilationUnit) {
        return &record.presentation.defaultDeclaredTypeFacts;
    }
    return nullptr;
}

QStringList typeNameCandidates(
    const SemanticSymbolRecord& record,
    const SemanticElaboratedSymbolInfo* info,
    const SemanticDeclaredTypeFacts* facts,
    const QList<SemanticSymbolRecord>& records)
{
    QStringList candidates;
    candidates.append(record.type.resolvedTypeName);
    candidates.append(record.type.rawTypeText);
    if (info)
        candidates.append(info->interfaceName);
    if (facts) {
        candidates.append(facts->declarationBaseText);
        for (const SemanticTypedefResolutionStep& step : facts->typedefChain)
            candidates.append(step.sourceTypeName);
    }

    QStringList knownTypeNames;
    for (const SemanticSymbolRecord& candidate : records) {
        if (candidate.declarationKind == DeclarationKind::Typedef
            || candidate.declarationKind == DeclarationKind::Enum
            || candidate.declarationKind == DeclarationKind::Struct
            || candidate.declarationKind == DeclarationKind::Interface) {
            knownTypeNames.append(candidate.name);
        }
    }
    if (info) {
        for (const QString& known : std::as_const(knownTypeNames)) {
            if (info->resolvedTypeText == known)
                candidates.append(known);
        }
    }
    candidates.removeAll(QString());
    candidates.removeDuplicates();
    return candidates;
}

WaveSimulationManifestTypeShape typeShape(
    const SemanticSymbolRecord& record,
    const SemanticElaboratedSymbolInfo* info,
    const SemanticDeclaredTypeFacts* facts)
{
    WaveSimulationManifestTypeShape shape;
    shape.rawTypeText = record.type.rawTypeText;
    shape.resolvedTypeName = record.type.resolvedTypeName;
    shape.semanticKind = declarationKindName(record.type.resolvedTypeKind);
    shape.modportName = record.type.modportName;
    if (info) {
        shape.semanticAvailable = info->available;
        shape.resolvedTypeText = info->resolvedTypeText;
        shape.fixedSize = info->fixedSize;
        shape.integral = info->integral;
        shape.signedIntegral = info->signedIntegral;
        shape.unpackedArray = info->unpackedArray;
        shape.interfaceType = info->interfaceType;
        shape.bitWidth = info->bitWidth;
        shape.packedDimensions = info->packedDimensionsText;
        shape.unpackedDimensions = info->unpackedDimensionsText;
        shape.unpackedElementCount = info->unpackedElementCountText;
        shape.interfaceName = info->interfaceName;
        if (!info->modportName.isEmpty())
            shape.modportName = info->modportName;
        shape.failureReason = info->failureReason;
    }
    if (facts) {
        shape.semanticAvailable = shape.semanticAvailable || facts->complete;
        shape.canonicalTypeId = facts->canonicalTypeId;
        shape.declarationShapeId = facts->declarationShapeId;
        for (const SemanticTypedefResolutionStep& step : facts->typedefChain) {
            if (!step.sourceTypeName.isEmpty())
                shape.typedefChain.append(step.sourceTypeName);
        }
        shape.typedefChain.removeDuplicates();
        if (shape.failureReason.isEmpty())
            shape.failureReason = facts->failureReason;
    }
    return shape;
}

WaveSimulationManifestType manifestType(
    const SemanticSymbolRecord& record,
    const QString& instancePath,
    const QList<SemanticSymbolRecord>& records)
{
    const SemanticElaboratedSymbolInfo* info = elaboratedInfo(record,
                                                              instancePath);
    const SemanticDeclaredTypeFacts* facts = declaredTypeFacts(record,
                                                                instancePath);
    WaveSimulationManifestType result;
    result.shape = typeShape(record, info, facts);
    const QStringList candidates = typeNameCandidates(record,
                                                       info,
                                                       facts,
                                                       records);
    QSet<QString> candidateSet(candidates.cbegin(), candidates.cend());
    bool packedStruct = false;
    bool unpackedStruct = false;
    for (const SemanticSymbolRecord& related : records) {
        if (!candidateSet.contains(related.owner.name)
            && !candidateSet.contains(related.name)) {
            continue;
        }
        if (related.collectorKind == CollectorKind::EnumValue
            && candidateSet.contains(related.owner.name)) {
            const SemanticElaboratedSymbolInfo* valueInfo = elaboratedInfo(
                related, instancePath);
            WaveSimulationManifestEnumValue value;
            value.name = related.name;
            value.declarationText = related.presentation.declarationText;
            if (valueInfo) {
                value.semanticAvailable = valueInfo->available;
                value.valueText = valueInfo->valueText;
                value.displayValueText = valueInfo->displayValueText;
            }
            result.enumValues.append(value);
        }
        if (related.collectorKind == CollectorKind::StructMember
            && candidateSet.contains(related.owner.name)) {
            WaveSimulationManifestStructMember member;
            member.name = related.name;
            member.declarationText = related.presentation.declarationText;
            member.type = typeShape(related,
                                    elaboratedInfo(related, instancePath),
                                    declaredTypeFacts(related, instancePath));
            result.structMembers.append(member);
        }
        if (candidateSet.contains(related.name)) {
            packedStruct = packedStruct
                || related.collectorKind == CollectorKind::PackedStruct;
            unpackedStruct = unpackedStruct
                || related.collectorKind == CollectorKind::UnpackedStruct;
        }
    }
    if (!result.enumValues.isEmpty())
        result.shape.semanticKind = QStringLiteral("enum");
    else if (packedStruct)
        result.shape.semanticKind = QStringLiteral("packed-struct");
    else if (unpackedStruct || !result.structMembers.isEmpty())
        result.shape.semanticKind = QStringLiteral("unpacked-struct");
    else if (result.shape.interfaceType)
        result.shape.semanticKind = QStringLiteral("interface");
    if (result.shape.resolvedTypeName.isEmpty() && !candidates.isEmpty())
        result.shape.resolvedTypeName = candidates.first();
    return result;
}

QString workspaceIdentity(const WaveSimulationModuleManifest& manifest,
                         const ProjectSnapshot& project)
{
    QJsonObject object;
    QJsonArray sources;
    for (const WaveSimulationManifestSource& source : manifest.sources) {
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), source.path);
        entry.insert(QStringLiteral("role"), source.role);
        sources.append(entry);
    }
    object.insert(QStringLiteral("sources"), sources);
    QJsonArray includeDirs;
    for (const QString& path : manifest.includeDirs)
        includeDirs.append(path);
    object.insert(QStringLiteral("includeDirs"), includeDirs);
    QJsonObject defines;
    for (auto it = manifest.defines.cbegin(); it != manifest.defines.cend(); ++it)
        defines.insert(it.key(), it.value());
    object.insert(QStringLiteral("defines"), defines);
    QJsonArray extensions;
    for (const QString& extension : project.fileExtensions)
        extensions.append(extension);
    object.insert(QStringLiteral("fileExtensions"), extensions);
    object.insert(QStringLiteral("topModule"), project.topModule);
    const QByteArray digest = QCryptographicHash::hash(
        QJsonDocument(object).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex();
    return QStringLiteral("sha256:") + QString::fromLatin1(digest);
}

}

WaveSimulationManifestBuildResult WaveSimulationManifestService::build(
    const WaveSimulationManifestBuildRequest& request) const
{
    WaveSimulationManifestBuildResult result;
    if (!request.semanticSnapshot) {
        result.status = WaveSimulationManifestBuildStatus::MissingSemanticSnapshot;
        result.message = QStringLiteral("No semantic snapshot is available.");
        return result;
    }
    if (!request.project.isOpen()) {
        result.status = WaveSimulationManifestBuildStatus::WorkspaceNotOpen;
        result.message = QStringLiteral("No workspace is open.");
        return result;
    }
    if (!request.moduleStableKey.isValid()) {
        result.status = WaveSimulationManifestBuildStatus::InvalidModuleKey;
        result.message = QStringLiteral("The selected module key is invalid.");
        return result;
    }

    const SemanticSymbolRecord module =
        request.semanticSnapshot->getSymbolRecordByStableKey(
            request.moduleStableKey);
    if (!module.isValid()) {
        result.status = WaveSimulationManifestBuildStatus::ModuleNotFound;
        result.message = QStringLiteral("The selected module is absent from the semantic snapshot.");
        return result;
    }
    if (module.declarationKind != DeclarationKind::Module) {
        result.status = WaveSimulationManifestBuildStatus::UnsupportedTarget;
        result.message = QStringLiteral("The selected symbol is not a module.");
        return result;
    }

    WaveSimulationModuleManifest manifest;
    manifest.target.mode = request.instancePath.isEmpty()
        ? QStringLiteral("module-definition")
        : QStringLiteral("instance");
    manifest.target.module = module.name;
    manifest.target.instancePath = request.instancePath;
    manifest.target.sourceLine = module.location.startLine;
    const std::optional<QString> modulePath = relativeProjectPath(
        request.project.workspaceRoot, module.location.fileName);
    if (!modulePath) {
        result.status =
            WaveSimulationManifestBuildStatus::ProjectPathOutsideWorkspace;
        result.message = QStringLiteral("The module source is outside the workspace root.");
        return result;
    }
    manifest.target.sourceFile = *modulePath;

    QSet<QString> seenSources;
    for (const QString& sourcePath : request.project.systemVerilogFiles) {
        const std::optional<QString> relative = relativeProjectPath(
            request.project.workspaceRoot, sourcePath);
        if (!relative) {
            result.status =
                WaveSimulationManifestBuildStatus::ProjectPathOutsideWorkspace;
            result.message = QStringLiteral("A source dependency is outside the workspace root: %1")
                                 .arg(sourcePath);
            return result;
        }
        if (seenSources.contains(*relative))
            continue;
        seenSources.insert(*relative);
        WaveSimulationManifestSource source;
        source.path = *relative;
        const QString absolute = normalizedAbsolutePath(
            request.project.workspaceRoot, sourcePath);
        source.role = sourceRoleName(request.project.sourceRoles.value(
            absolute,
            SymbolTaxonomy::sourceRoleForFileName(absolute)));
        manifest.sources.append(source);
    }
    for (const QString& includePath : request.project.includeDirs) {
        const std::optional<QString> relative = relativeProjectPath(
            request.project.workspaceRoot, includePath);
        if (!relative) {
            result.status =
                WaveSimulationManifestBuildStatus::ProjectPathOutsideWorkspace;
            result.message = QStringLiteral("An include directory is outside the workspace root: %1")
                                 .arg(includePath);
            return result;
        }
        if (!manifest.includeDirs.contains(*relative))
            manifest.includeDirs.append(*relative);
    }
    for (auto it = request.project.defines.cbegin();
         it != request.project.defines.cend();
         ++it) {
        manifest.defines.insert(it.key(), it.value());
    }

    QList<SemanticSymbolRecord> members;
    for (const SemanticSymbolRecord& record :
         request.semanticSnapshot->symbolRecordsView()) {
        if (belongsToModule(record, module)
            && (record.declarationKind == DeclarationKind::Parameter
                || isPort(record))) {
            members.append(record);
        }
    }
    std::sort(members.begin(), members.end(),
              [](const SemanticSymbolRecord& left,
                 const SemanticSymbolRecord& right) {
                  if (left.location.fileName != right.location.fileName)
                      return left.location.fileName < right.location.fileName;
                  return left.location.position < right.location.position;
              });

    bool instanceContextFound = request.instancePath.isEmpty();
    if (!instanceContextFound) {
        for (const SemanticSymbolRecord& record :
             request.semanticSnapshot->symbolRecordsView()) {
            const bool sameModuleMember = belongsToModule(record, module);
            const bool selectedModuleInstance =
                record.declarationKind == DeclarationKind::Instance
                && record.type.resolvedTypeName == module.name;
            if ((sameModuleMember || selectedModuleInstance)
                && (record.presentation.instanceInfoByPath.contains(
                        request.instancePath)
                    || record.presentation.declaredTypeFactsByPath.contains(
                        request.instancePath))) {
                instanceContextFound = true;
                break;
            }
        }
    }
    for (const SemanticSymbolRecord& member : std::as_const(members)) {
        if (!request.instancePath.isEmpty()
            && member.presentation.instanceInfoByPath.contains(
                request.instancePath)) {
            instanceContextFound = true;
        }
        const SemanticElaboratedSymbolInfo* info = elaboratedInfo(
            member, request.instancePath);
        const std::optional<QString> sourceFile = relativeProjectPath(
            request.project.workspaceRoot, member.location.fileName);
        if (!sourceFile) {
            result.status =
                WaveSimulationManifestBuildStatus::ProjectPathOutsideWorkspace;
            result.message = QStringLiteral("A module member is outside the workspace root: %1")
                                 .arg(member.location.fileName);
            return result;
        }
        if (!info) {
            result.warnings.append(
                QStringLiteral("No Slang instance facts for %1 at %2.")
                    .arg(member.name, request.instancePath));
        } else if (!info->available) {
            result.warnings.append(
                QStringLiteral("Slang could not fully elaborate %1: %2")
                    .arg(member.name, info->failureReason));
        }

        if (member.declarationKind == DeclarationKind::Parameter) {
            WaveSimulationManifestParameter parameter;
            parameter.name = member.name;
            parameter.declarationText = member.presentation.declarationText;
            parameter.expressionText = member.presentation.expressionText;
            parameter.sourceFile = *sourceFile;
            parameter.sourceLine = member.location.startLine;
            parameter.semanticAvailable = info && info->available;
            if (info) {
                parameter.valueText = info->valueText;
                parameter.displayValueText = info->displayValueText;
            }
            parameter.type = manifestType(
                member,
                request.instancePath,
                request.semanticSnapshot->symbolRecordsView());
            manifest.parameters.append(parameter);
        } else if (isPort(member)) {
            WaveSimulationManifestPort port;
            port.name = member.name;
            port.direction = portDirection(member.collectorKind);
            port.declarationText = member.presentation.declarationText;
            port.sourceFile = *sourceFile;
            port.sourceLine = member.location.startLine;
            port.type = manifestType(
                member,
                request.instancePath,
                request.semanticSnapshot->symbolRecordsView());
            manifest.ports.append(port);
        }
    }
    if (!instanceContextFound) {
        result.status =
            WaveSimulationManifestBuildStatus::InstanceContextNotFound;
        result.message = QStringLiteral("The instance path is absent from the current Slang elaboration: %1")
                             .arg(request.instancePath);
        return result;
    }

    const QSet<QString> portKeys = [&]() {
        QSet<QString> keys;
        for (const SemanticSymbolRecord& member : std::as_const(members)) {
            if (isPort(member))
                keys.insert(member.stableKey.toString());
        }
        return keys;
    }();
    QSet<QString> clockPortKeys;
    QSet<QString> resetPortKeys;
    for (const SemanticRelationship& relationship :
         request.semanticSnapshot->relationshipsView()) {
        QSet<QString>* candidates = nullptr;
        if (relationship.type == SymbolRelationshipEngine::CLOCKS)
            candidates = &clockPortKeys;
        else if (relationship.type == SymbolRelationshipEngine::RESETS)
            candidates = &resetPortKeys;
        if (!candidates)
            continue;
        const QString from = relationship.fromStableKey.toString();
        const QString to = relationship.toStableKey.toString();
        if (portKeys.contains(from))
            candidates->insert(from);
        if (portKeys.contains(to))
            candidates->insert(to);
    }
    for (const SemanticSymbolRecord& member : std::as_const(members)) {
        if (!isPort(member))
            continue;
        const QString key = member.stableKey.toString();
        if (clockPortKeys.contains(key))
            manifest.clockCandidates.append(member.name);
        if (resetPortKeys.contains(key))
            manifest.resetCandidates.append(member.name);
    }

    manifest.workspaceId = workspaceIdentity(manifest, request.project);
    result.status = WaveSimulationManifestBuildStatus::Success;
    result.manifest = std::move(manifest);
    return result;
}

QString WaveSimulationManifestService::statusCode(
    WaveSimulationManifestBuildStatus status)
{
    switch (status) {
    case WaveSimulationManifestBuildStatus::Success:
        return QStringLiteral("success");
    case WaveSimulationManifestBuildStatus::MissingSemanticSnapshot:
        return QStringLiteral("missing-semantic-snapshot");
    case WaveSimulationManifestBuildStatus::WorkspaceNotOpen:
        return QStringLiteral("workspace-not-open");
    case WaveSimulationManifestBuildStatus::InvalidModuleKey:
        return QStringLiteral("invalid-module-key");
    case WaveSimulationManifestBuildStatus::ModuleNotFound:
        return QStringLiteral("module-not-found");
    case WaveSimulationManifestBuildStatus::UnsupportedTarget:
        return QStringLiteral("unsupported-target");
    case WaveSimulationManifestBuildStatus::InstanceContextNotFound:
        return QStringLiteral("instance-context-not-found");
    case WaveSimulationManifestBuildStatus::ProjectPathOutsideWorkspace:
        return QStringLiteral("project-path-outside-workspace");
    }
    return QStringLiteral("unknown");
}
