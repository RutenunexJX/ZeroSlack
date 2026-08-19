#include "wavesimulationmanifestservice.h"

#include "semanticdependencygraph.h"
#include "semanticindexsnapshot.h"
#include "tsdocument.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMultiHash>
#include <QQueue>
#include <QSet>

#include <algorithm>
#include <optional>

namespace {
using CollectorKind = SymbolTaxonomy::CollectorKind;
using DeclarationKind = SymbolTaxonomy::DeclarationKind;
using DriverRelationshipIndex =
    QMultiHash<QString, const SemanticRelationship*>;

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

bool isObservable(const SemanticSymbolRecord& record)
{
    if (isPort(record))
        return true;
    switch (record.collectorKind) {
    case CollectorKind::Reg:
    case CollectorKind::Wire:
    case CollectorKind::Logic:
    case CollectorKind::EnumVariable:
    case CollectorKind::PackedStructVariable:
    case CollectorKind::UnpackedStructVariable:
        return true;
    default:
        return false;
    }
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

QString portableSemanticFactId(const QString& semanticId,
                               const QString& workspaceRoot)
{
    if (semanticId.isEmpty())
        return QString();

    QString normalized = QDir::fromNativeSeparators(semanticId);
    const QString root = normalizedAbsolutePath(QString(), workspaceRoot);
    if (!root.isEmpty()) {
        normalized.replace(
            QDir::fromNativeSeparators(root),
            QStringLiteral("${workspace}"),
            Qt::CaseInsensitive);
    }
    const QByteArray digest = QCryptographicHash::hash(
        normalized.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("sha256:") + QString::fromLatin1(digest);
}

void makeTypePortable(WaveSimulationManifestTypeShape& shape,
                      const QString& workspaceRoot)
{
    shape.canonicalTypeId = portableSemanticFactId(
        shape.canonicalTypeId, workspaceRoot);
    shape.declarationShapeId = portableSemanticFactId(
        shape.declarationShapeId, workspaceRoot);
}

WaveSimulationManifestType manifestType(
    const SemanticSymbolRecord& record,
    const QString& instancePath,
    const QList<SemanticSymbolRecord>& records,
    const QString& workspaceRoot)
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
    makeTypePortable(result.shape, workspaceRoot);
    for (WaveSimulationManifestStructMember& member : result.structMembers)
        makeTypePortable(member.type, workspaceRoot);
    return result;
}

QString structuredSelectorKindName(
    SemanticStructuredSelectorKind kind)
{
    switch (kind) {
    case SemanticStructuredSelectorKind::StructMember:
        return QStringLiteral("struct-member");
    case SemanticStructuredSelectorKind::PackedIndex:
        return QStringLiteral("packed-index");
    case SemanticStructuredSelectorKind::UnpackedIndex:
        return QStringLiteral("unpacked-index");
    case SemanticStructuredSelectorKind::InterfaceMember:
        return QStringLiteral("interface-member");
    }
    return QStringLiteral("unknown");
}

QList<WaveSimulationManifestEditableLeaf> editableLeaves(
    const SemanticElaboratedSymbolInfo* info,
    const QString& workspaceRoot)
{
    QList<WaveSimulationManifestEditableLeaf> result;
    if (!info || !info->structuredLeavesAvailable)
        return result;
    result.reserve(info->structuredLeaves.size());
    for (const SemanticStructuredLeafFact& fact :
         info->structuredLeaves) {
        WaveSimulationManifestEditableLeaf leaf;
        leaf.relativePath = fact.relativePath;
        leaf.direction = fact.direction;
        leaf.type.semanticAvailable = true;
        leaf.type.resolvedTypeText = fact.resolvedTypeText;
        leaf.type.canonicalTypeId = fact.canonicalTypeId;
        leaf.type.declarationShapeId = fact.canonicalTypeId;
        leaf.type.fixedSize = fact.fixedSize;
        leaf.type.integral = fact.integral;
        leaf.type.signedIntegral = fact.signedIntegral;
        leaf.type.bitWidth = fact.bitWidth;
        leaf.packedBitOffsetValid = fact.packedBitOffsetValid;
        leaf.packedBitOffset = fact.packedBitOffset;
        makeTypePortable(leaf.type, workspaceRoot);
        for (const SemanticStructuredSelectorFact& selector :
             fact.selectors) {
            WaveSimulationManifestStructuredSelector manifestSelector;
            manifestSelector.kind = structuredSelectorKindName(selector.kind);
            manifestSelector.name = selector.name;
            manifestSelector.sourceIndex = selector.sourceIndex;
            manifestSelector.storageIndex = selector.storageIndex;
            leaf.selectors.append(std::move(manifestSelector));
        }
        for (const SemanticStructuredEnumValueFact& value :
             fact.enumValues) {
            WaveSimulationManifestEnumValue manifestValue;
            manifestValue.name = value.name;
            manifestValue.valueText = value.valueText;
            manifestValue.displayValueText = value.displayValueText;
            manifestValue.semanticAvailable = value.available;
            leaf.enumValues.append(std::move(manifestValue));
        }
        result.append(std::move(leaf));
    }
    return result;
}

bool supportedObservationType(const WaveSimulationManifestType& type)
{
    const WaveSimulationManifestTypeShape& shape = type.shape;
    return shape.semanticAvailable
        && shape.fixedSize
        && shape.integral
        && !shape.unpackedArray
        && !shape.interfaceType
        && shape.bitWidth > 0;
}

int lineStartChar(const QString& text, int oneBasedLine)
{
    if (oneBasedLine <= 1)
        return 0;
    int line = 1;
    int position = 0;
    while (line < oneBasedLine && position < text.size()) {
        const int newline = text.indexOf(QLatin1Char('\n'), position);
        if (newline < 0)
            return text.size();
        position = newline + 1;
        ++line;
    }
    return position;
}

QString observationRootName(const QString& accessPath,
                            const QString& fallback)
{
    const QString trimmed = accessPath.trimmed();
    if (trimmed.isEmpty())
        return fallback.trimmed();
    const qsizetype separator = trimmed.indexOf(QLatin1Char('.'));
    return separator < 0 ? trimmed : trimmed.left(separator);
}

const SemanticSymbolRecord* observableRecord(
    const QList<SemanticSymbolRecord>& records,
    const SemanticSymbolRecord& module,
    const QString& name)
{
    const SemanticSymbolRecord* best = nullptr;
    for (const SemanticSymbolRecord& record : records) {
        if (record.name != name
            || !belongsToModule(record, module)
            || !isObservable(record)) {
            continue;
        }
        if (!best
            || record.location.position < best->location.position) {
            best = &record;
        }
    }
    return best;
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

QString portableSemanticIdentityForRecord(
    const SemanticSymbolRecord& record,
    const QString& relativeSourceFile)
{
    QJsonObject object;
    object.insert(QStringLiteral("sourceFile"), relativeSourceFile);
    object.insert(
        QStringLiteral("declarationKind"),
        static_cast<int>(record.declarationKind));
    object.insert(QStringLiteral("owner"), record.owner.name);
    object.insert(QStringLiteral("name"), record.name);
    object.insert(QStringLiteral("line"), record.location.startLine);
    object.insert(QStringLiteral("column"), record.location.startColumn);
    const QByteArray digest = QCryptographicHash::hash(
        QJsonDocument(object).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex();
    return QStringLiteral("sha256:") + QString::fromLatin1(digest);
}

QList<WaveSimulationManifestSourceLink> sourceLinksForRecord(
    const SemanticSymbolRecord& record,
    const QString& relativeSourceFile,
    const DriverRelationshipIndex& driverRelationships,
    const QString& workspaceRoot,
    QStringList* warnings)
{
    QList<WaveSimulationManifestSourceLink> links;
    WaveSimulationManifestSourceLink declaration;
    declaration.kind = QStringLiteral("declaration");
    declaration.sourceFile = relativeSourceFile;
    declaration.sourceLine = record.location.startLine;
    declaration.sourceColumn = std::max(1, record.location.startColumn);
    declaration.label = QStringLiteral("Declaration");
    links.append(declaration);

    QSet<QString> seenLocations;
    seenLocations.insert(QStringLiteral("%1:%2:%3")
                             .arg(declaration.sourceFile)
                             .arg(declaration.sourceLine)
                             .arg(declaration.sourceColumn));
    const QString stableKey = record.stableKey.toString();
    for (const SemanticRelationship* relationshipPointer :
         driverRelationships.values(stableKey)) {
        const SemanticRelationship& relationship =
            *relationshipPointer;
        const std::optional<QString> sourceFile = relativeProjectPath(
            workspaceRoot, relationship.evidenceRange.fileName);
        if (!sourceFile) {
            if (warnings) {
                warnings->append(
                    QStringLiteral(
                        "A driver for %1 is outside the workspace and was not exported: %2")
                        .arg(record.name,
                             relationship.evidenceRange.fileName));
            }
            continue;
        }
        const int sourceColumn = std::max(
            1, relationship.evidenceRange.column);
        const QString identity = QStringLiteral("%1:%2:%3")
                                     .arg(*sourceFile)
                                     .arg(relationship.evidenceRange.line)
                                     .arg(sourceColumn);
        if (seenLocations.contains(identity))
            continue;
        seenLocations.insert(identity);

        WaveSimulationManifestSourceLink driver;
        driver.kind = QStringLiteral("driver");
        driver.sourceFile = *sourceFile;
        driver.sourceLine = relationship.evidenceRange.line;
        driver.sourceColumn = sourceColumn;
        driver.label = relationship.evidenceText.trimmed().isEmpty()
            ? QStringLiteral("Assignment")
            : relationship.evidenceText.trimmed();
        links.append(std::move(driver));
    }
    return links;
}

enum class AssociationStyle {
    None,
    Named,
    Positional,
    Mixed
};

AssociationStyle associationStyle(
    const QList<WaveSimulationManifestAssociation>& associations)
{
    if (associations.isEmpty())
        return AssociationStyle::None;
    bool named = false;
    bool positional = false;
    for (const WaveSimulationManifestAssociation& association : associations) {
        named = named || !association.name.isEmpty();
        positional = positional || association.name.isEmpty();
    }
    if (named && positional)
        return AssociationStyle::Mixed;
    return named ? AssociationStyle::Named : AssociationStyle::Positional;
}

bool mergeStyle(AssociationStyle candidate,
                AssociationStyle* merged)
{
    if (!merged || candidate == AssociationStyle::Mixed)
        return false;
    if (candidate == AssociationStyle::None)
        return true;
    if (*merged == AssociationStyle::None) {
        *merged = candidate;
        return true;
    }
    return *merged == candidate;
}

QList<WaveSimulationManifestAssociation> manifestAssociations(
    const QList<SemanticModuleAssociationFact>& facts)
{
    QList<WaveSimulationManifestAssociation> result;
    result.reserve(facts.size());
    for (const SemanticModuleAssociationFact& fact : facts) {
        WaveSimulationManifestAssociation association;
        association.name = fact.name;
        association.position = fact.position;
        result.append(std::move(association));
    }
    return result;
}

struct LocatedInstantiationFact {
    QString fileName;
    SemanticModuleInstantiationFact fact;
};

bool appendUnresolvedDependencies(
    const WaveSimulationManifestBuildRequest& request,
    WaveSimulationModuleManifest* manifest,
    QString* error)
{
    if (!manifest)
        return false;

    QSet<QString> declarations;
    QHash<QString, QList<LocatedInstantiationFact>> instantiationsByOwner;
    if (!request.dependencyGraph)
        return true;
    const QHash<QString, SemanticFileDependencyFacts> dependencyFacts =
        request.dependencyGraph->fileFacts();
    for (auto file = dependencyFacts.cbegin();
         file != dependencyFacts.cend(); ++file) {
        declarations.unite(file->moduleDeclarations);
        for (const SemanticModuleInstantiationFact& fact :
             file->moduleInstantiations) {
            if (fact.ownerName.isEmpty())
                continue;
            instantiationsByOwner[fact.ownerName].append(
                LocatedInstantiationFact{file->fileName, fact});
        }
    }

    QMap<QString, WaveSimulationManifestUnresolvedDependency> unresolved;
    QQueue<QString> pending;
    QSet<QString> visited;
    pending.enqueue(manifest->target.module);
    while (!pending.isEmpty()) {
        const QString owner = pending.dequeue();
        if (visited.contains(owner))
            continue;
        visited.insert(owner);
        for (const LocatedInstantiationFact& located :
             std::as_const(instantiationsByOwner[owner])) {
            const QString target = located.fact.targetName.trimmed();
            if (target.isEmpty())
                continue;
            if (declarations.contains(target)) {
                if (!visited.contains(target))
                    pending.enqueue(target);
                continue;
            }

            const std::optional<QString> sourceFile = relativeProjectPath(
                request.project.workspaceRoot, located.fileName);
            if (!sourceFile) {
                if (error) {
                    *error = QStringLiteral(
                        "An unresolved dependency location is outside the workspace root: %1")
                                 .arg(located.fileName);
                }
                return false;
            }
            WaveSimulationManifestUnresolvedDependency& dependency =
                unresolved[target];
            dependency.moduleName = target;
            WaveSimulationManifestUnresolvedInstance instance;
            instance.instanceName = located.fact.instanceName;
            instance.constructKind = located.fact.constructKind;
            instance.sourceFile = *sourceFile;
            instance.sourceLine = located.fact.sourceLine;
            instance.sourceColumn = located.fact.sourceColumn;
            instance.parameterAssociations = manifestAssociations(
                located.fact.parameterAssociations);
            instance.portAssociations = manifestAssociations(
                located.fact.portAssociations);
            instance.syntaxComplete = located.fact.syntaxComplete;
            instance.failureReason = located.fact.failureReason;
            dependency.instances.append(std::move(instance));
        }
    }

    for (auto dependency = unresolved.begin();
         dependency != unresolved.end(); ++dependency) {
        AssociationStyle parameterStyle = AssociationStyle::None;
        AssociationStyle portStyle = AssociationStyle::None;
        for (const WaveSimulationManifestUnresolvedInstance& instance :
             std::as_const(dependency->instances)) {
            if (instance.constructKind != QStringLiteral("module")) {
                dependency->stubUnsupportedReason = QStringLiteral(
                    "Only unresolved module instances can be represented by passive stubs.");
                break;
            }
            if (!instance.syntaxComplete) {
                dependency->stubUnsupportedReason = instance.failureReason.isEmpty()
                    ? QStringLiteral("At least one instance has incomplete syntax.")
                    : instance.failureReason;
                break;
            }
            if (!mergeStyle(associationStyle(instance.parameterAssociations),
                            &parameterStyle)) {
                dependency->stubUnsupportedReason = QStringLiteral(
                    "Instances use incompatible parameter association styles.");
                break;
            }
            if (!mergeStyle(associationStyle(instance.portAssociations),
                            &portStyle)) {
                dependency->stubUnsupportedReason = QStringLiteral(
                    "Instances use incompatible port association styles.");
                break;
            }
        }
        dependency->stubSupported =
            dependency->stubUnsupportedReason.isEmpty();
        if (dependency->stubSupported)
            dependency->stubUnsupportedReason.clear();

        std::sort(
            dependency->instances.begin(),
            dependency->instances.end(),
            [](const WaveSimulationManifestUnresolvedInstance& left,
               const WaveSimulationManifestUnresolvedInstance& right) {
                if (left.sourceFile != right.sourceFile)
                    return left.sourceFile < right.sourceFile;
                if (left.sourceLine != right.sourceLine)
                    return left.sourceLine < right.sourceLine;
                if (left.sourceColumn != right.sourceColumn)
                    return left.sourceColumn < right.sourceColumn;
                return left.instanceName < right.instanceName;
            });
        manifest->unresolvedDependencies.append(*dependency);
    }
    return true;
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
    manifest.observationScope.mode = QStringLiteral("module");
    manifest.observationScope.sourceFile = *modulePath;
    manifest.observationScope.startLine = module.location.startLine;
    manifest.observationScope.endLine = module.location.startLine;

    if (!request.observationScope.mode.trimmed().isEmpty()) {
        if (request.observationScope.mode != QStringLiteral("always")
            || request.observationScope.fileName.trimmed().isEmpty()
            || request.observationScope.startLine <= 0
            || request.observationScope.endLine
                   < request.observationScope.startLine) {
            result.status =
                WaveSimulationManifestBuildStatus::InvalidObservationScope;
            result.message = QStringLiteral(
                "The selected Wave Simulation observation scope is invalid.");
            return result;
        }
        const std::optional<QString> scopePath = relativeProjectPath(
            request.project.workspaceRoot,
            request.observationScope.fileName);
        if (!scopePath) {
            result.status =
                WaveSimulationManifestBuildStatus::ProjectPathOutsideWorkspace;
            result.message = QStringLiteral(
                "The observation scope is outside the workspace root.");
            return result;
        }
        if (*scopePath != *modulePath) {
            result.status =
                WaveSimulationManifestBuildStatus::InvalidObservationScope;
            result.message = QStringLiteral(
                "The selected always block does not belong to the target module source file.");
            return result;
        }
        manifest.observationScope.mode = QStringLiteral("always");
        manifest.observationScope.label = request.observationScope.label;
        manifest.observationScope.sourceFile = *scopePath;
        manifest.observationScope.startLine =
            request.observationScope.startLine;
        manifest.observationScope.endLine =
            request.observationScope.endLine;
    }

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
    QString unresolvedError;
    if (!appendUnresolvedDependencies(
            request, &manifest, &unresolvedError)) {
        result.status =
            WaveSimulationManifestBuildStatus::ProjectPathOutsideWorkspace;
        result.message = unresolvedError;
        return result;
    }

    DriverRelationshipIndex driverRelationships;
    for (const SemanticRelationship& relationship :
         request.semanticSnapshot->relationshipsView()) {
        if (relationship.type == SymbolRelationshipEngine::ASSIGNS_TO
            && relationship.toStableKey.isValid()
            && relationship.evidenceRange.isValid()) {
            driverRelationships.insert(
                relationship.toStableKey.toString(),
                &relationship);
        }
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
                request.semanticSnapshot->symbolRecordsView(),
                request.project.workspaceRoot);
            manifest.parameters.append(parameter);
        } else if (isPort(member)) {
            WaveSimulationManifestPort port;
            port.name = member.name;
            port.semanticId = portableSemanticIdentityForRecord(
                member, *sourceFile);
            port.direction = portDirection(member.collectorKind);
            port.declarationText = member.presentation.declarationText;
            port.sourceFile = *sourceFile;
            port.sourceLine = member.location.startLine;
            port.sourceColumn = std::max(1, member.location.startColumn);
            port.sourceLinks = sourceLinksForRecord(
                member,
                *sourceFile,
                driverRelationships,
                request.project.workspaceRoot,
                &result.warnings);
            port.type = manifestType(
                member,
                request.instancePath,
                request.semanticSnapshot->symbolRecordsView(),
                request.project.workspaceRoot);
            port.structuredLeavesAvailable =
                info && info->structuredLeavesAvailable;
            port.editableLeaves = editableLeaves(
                info, request.project.workspaceRoot);
            if (info)
                port.structuredFailureReason =
                    info->structuredFailureReason;
            manifest.ports.append(port);
        }
    }

    QList<QPair<QString, QString>> requestedObservations;
    if (manifest.observationScope.mode == QStringLiteral("always")) {
        const QString scopeSource =
            request.semanticSnapshot->getCachedFileContent(
                request.observationScope.fileName);
        if (scopeSource.isEmpty()) {
            result.status =
                WaveSimulationManifestBuildStatus::InvalidObservationScope;
            result.message = QStringLiteral(
                "The selected always block is absent from the current semantic snapshot.");
            return result;
        }
        TSDocument syntax;
        syntax.setText(scopeSource);
        const int startChar = lineStartChar(
            scopeSource, request.observationScope.startLine);
        const int endChar = lineStartChar(
            scopeSource, request.observationScope.endLine + 1);
        for (const TSIdentifierTarget& identifier :
             syntax.identifiersInRange(startChar, endChar)) {
            requestedObservations.append({identifier.text, identifier.text});
        }
    }
    for (const WaveSimulationObservationRequest& observation :
         request.explicitObservations) {
        const QString accessPath = observation.accessPath.trimmed().isEmpty()
            ? observation.name
            : observation.accessPath;
        requestedObservations.append(
            {observationRootName(accessPath, observation.name), accessPath});
    }

    QSet<QString> seenObservationPaths;
    for (const auto& requested : std::as_const(requestedObservations)) {
        const QString accessPath = requested.second.trimmed();
        if (accessPath.isEmpty() || seenObservationPaths.contains(accessPath))
            continue;
        const SemanticSymbolRecord* record = observableRecord(
            request.semanticSnapshot->symbolRecordsView(),
            module,
            requested.first);
        if (!record)
            continue;
        const WaveSimulationManifestType type = manifestType(
            *record,
            request.instancePath,
            request.semanticSnapshot->symbolRecordsView(),
            request.project.workspaceRoot);
        if (!supportedObservationType(type)) {
            result.warnings.append(
                QStringLiteral("Observation %1 has no supported fixed integral representation.")
                    .arg(accessPath));
            continue;
        }
        const std::optional<QString> sourceFile = relativeProjectPath(
            request.project.workspaceRoot, record->location.fileName);
        if (!sourceFile) {
            result.status =
                WaveSimulationManifestBuildStatus::ProjectPathOutsideWorkspace;
            result.message = QStringLiteral(
                "An observed signal is outside the workspace root: %1")
                                 .arg(record->location.fileName);
            return result;
        }
        WaveSimulationManifestObservation observation;
        observation.name = record->name;
        observation.accessPath = accessPath;
        observation.semanticId = portableSemanticIdentityForRecord(
            *record, *sourceFile);
        observation.declarationText =
            record->presentation.declarationText;
        observation.type = type;
        observation.sourceFile = *sourceFile;
        observation.sourceLine = record->location.startLine;
        observation.sourceColumn = std::max(
            1, record->location.startColumn);
        observation.sourceLinks = sourceLinksForRecord(
            *record,
            *sourceFile,
            driverRelationships,
            request.project.workspaceRoot,
            &result.warnings);
        observation.port = isPort(*record);
        manifest.observations.append(std::move(observation));
        seenObservationPaths.insert(accessPath);
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
    case WaveSimulationManifestBuildStatus::InvalidObservationScope:
        return QStringLiteral("invalid-observation-scope");
    case WaveSimulationManifestBuildStatus::ProjectPathOutsideWorkspace:
        return QStringLiteral("project-path-outside-workspace");
    }
    return QStringLiteral("unknown");
}

QString WaveSimulationManifestService::portableSemanticIdentity(
    const SemanticSymbolRecord& record,
    const QString& workspaceRoot)
{
    const std::optional<QString> sourceFile = relativeProjectPath(
        workspaceRoot, record.location.fileName);
    return sourceFile
        ? portableSemanticIdentityForRecord(record, *sourceFile)
        : QString();
}
