#include "semanticdiffservice.h"

#include <QDir>
#include <QHash>
#include <QSet>
#include <algorithm>

std::unique_ptr<SemanticDiffService> SemanticDiffService::instance = nullptr;

namespace {
bool hasDisplaySymbol(const sym_list::SymbolInfo& symbol)
{
    return !symbol.symbolName.isEmpty() || !symbol.fileName.isEmpty();
}

SymbolTaxonomy::SemanticMetadata metadataForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(fallback);
    if (!record.isValid())
        return metadata;

    metadata.declarationKind = record.declarationKind;
    metadata.usageRole = record.usageRole;
    metadata.visibility = record.visibility;
    metadata.sourceRole = record.sourceRole;
    metadata.rawCollectorKind = record.rawCollectorKind;
    metadata.interfaceLikeOwner = record.owner.interfaceLike;
    return metadata;
}

QString symbolTypeDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    return SymbolTaxonomy::symbolTypeLabel(
        metadataForRecord(record, fallback));
}

QString symbolDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.name.isEmpty())
        return record.name;
    if (!fallback.symbolName.isEmpty())
        return fallback.symbolName;
    return QStringLiteral("<unknown>");
}

QString sourceRoleDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    return SymbolTaxonomy::sourceRoleDisplayName(
        metadataForRecord(record, fallback).sourceRole);
}

QString dataTypeDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.type.rawTypeText.isEmpty())
        return record.type.rawTypeText;
    const SemanticSymbolRecord fallbackRecord =
        semanticSymbolRecordForSymbol(fallback);
    return fallbackRecord.type.rawTypeText;
}

QString symbolScopeDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (record.owner.kind == SymbolTaxonomy::SymbolOwnerScope::Global)
        return QStringLiteral("global");
    if (!record.owner.name.isEmpty())
        return QStringLiteral("scope %1").arg(record.owner.name);
    const SemanticSymbolRecord fallbackRecord =
        semanticSymbolRecordForSymbol(fallback);
    if (!fallbackRecord.owner.name.isEmpty())
        return QStringLiteral("scope %1").arg(fallbackRecord.owner.name);
    if (SymbolTaxonomy::isGlobalDefinition(
            SymbolTaxonomy::semanticMetadata(fallback))) {
        return QStringLiteral("global");
    }
    return QStringLiteral("scope unknown");
}

RtlInsightCodeLink codeLinkForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (record.isValid()) {
        const QString fileName = fallback.fileName.isEmpty()
            ? record.location.fileName
            : fallback.fileName;
        return RtlInsightLink::fromFileLine(fileName,
                                            record.location.startLine,
                                            record.location.startColumn);
    }
    return RtlInsightLink::fromSymbol(fallback);
}
}

SemanticDiffService* SemanticDiffService::getInstance()
{
    if (!instance)
        instance = std::make_unique<SemanticDiffService>();
    return instance.get();
}

SemanticDiffService::SemanticDiffService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

SemanticDiffService::~SemanticDiffService() = default;

void SemanticDiffService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

SemanticDiffReport SemanticDiffService::buildSemanticDiff(
    const SemanticDiffQuery& query) const
{
    SemanticDiffReport report;
    report.symbolGroupDisplayName = QStringLiteral("Semantic Diff Symbols");
    report.relationshipGroupDisplayName =
        QStringLiteral("Semantic Diff Relationships");
    report.diagnosticGroupDisplayName =
        QStringLiteral("Semantic Diff Diagnostics");
    if (!query.beforeSnapshot || !query.afterSnapshot) {
        report.notFoundReason = SemanticDiffNotFoundReason::MissingSnapshot;
        report.notFoundReasonDisplayName =
            notFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    report.symbolChanges = symbolChanges(query);
    report.relationshipChanges = relationshipChanges(query);
    report.diagnosticChanges = diagnosticChanges(query);
    report.symbolChangeCount = report.symbolChanges.size();
    report.relationshipChangeCount = report.relationshipChanges.size();
    report.diagnosticChangeCount = report.diagnosticChanges.size();
    report.found = !report.symbolChanges.isEmpty()
        || !report.relationshipChanges.isEmpty()
        || !report.diagnosticChanges.isEmpty();
    if (!report.found) {
        report.notFoundReason = SemanticDiffNotFoundReason::NoChanges;
        report.notFoundReasonDisplayName =
            notFoundReasonDisplayName(report.notFoundReason);
    } else {
        report.notFoundReason = SemanticDiffNotFoundReason::None;
    }
    return report;
}

SemanticIndex* SemanticDiffService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

QList<SemanticDiffSymbolChange> SemanticDiffService::symbolChanges(
    const SemanticDiffQuery& query)
{
    QHash<QString, sym_list::SymbolInfo> beforeSymbols;
    QHash<QString, sym_list::SymbolInfo> afterSymbols;
    QHash<QString, SemanticDiffSymbolCategory> categories;

    auto collect = [&](const SemanticIndexSnapshot& snapshot,
                       const QString& fileName,
                       QHash<QString, sym_list::SymbolInfo>* target) {
        const QList<sym_list::SymbolInfo> symbols = snapshot.getSymbols(fileName);
        for (const sym_list::SymbolInfo& symbol : symbols) {
            SemanticDiffSymbolCategory category;
            if (!symbolCategory(SymbolTaxonomy::semanticMetadata(symbol),
                                &category)) {
                continue;
            }
            if (!symbolInScope(symbol, query.moduleName, fileName))
                continue;
            const QString key = symbolKey(symbol, category);
            target->insert(key, symbol);
            categories.insert(key, category);
        }
    };

    collect(*query.beforeSnapshot, query.beforeFileName, &beforeSymbols);
    collect(*query.afterSnapshot, query.afterFileName, &afterSymbols);

    QList<SemanticDiffSymbolChange> changes;
    QSet<QString> allKeys;
    for (auto it = beforeSymbols.constBegin(); it != beforeSymbols.constEnd(); ++it)
        allKeys.insert(it.key());
    for (auto it = afterSymbols.constBegin(); it != afterSymbols.constEnd(); ++it)
        allKeys.insert(it.key());

    for (const QString& key : std::as_const(allKeys)) {
        const bool hasBefore = beforeSymbols.contains(key);
        const bool hasAfter = afterSymbols.contains(key);
        SemanticDiffSymbolChange change;
        change.key = key;
        change.category = categories.value(key, SemanticDiffSymbolCategory::Signal);
        sym_list::SymbolInfo beforeSymbol;
        sym_list::SymbolInfo afterSymbol;
        if (hasBefore)
            beforeSymbol = beforeSymbols.value(key);
        if (hasAfter)
            afterSymbol = afterSymbols.value(key);

        if (!hasBefore) {
            change.kind = SemanticDiffChangeKind::Added;
            fillDisplayMetadata(change, beforeSymbol, afterSymbol);
            changes.append(change);
            continue;
        }
        if (!hasAfter) {
            change.kind = SemanticDiffChangeKind::Removed;
            fillDisplayMetadata(change, beforeSymbol, afterSymbol);
            changes.append(change);
            continue;
        }
        if (symbolSignature(beforeSymbol) != symbolSignature(afterSymbol)) {
            change.kind = SemanticDiffChangeKind::Modified;
            fillDisplayMetadata(change, beforeSymbol, afterSymbol);
            changes.append(change);
        }
    }

    sortSymbolChanges(changes);
    return changes;
}

QList<SemanticDiffRelationshipChange> SemanticDiffService::relationshipChanges(
    const SemanticDiffQuery& query)
{
    QHash<QString, SemanticRelationship> beforeRelationships;
    QHash<QString, SemanticRelationship> afterRelationships;

    auto collect = [&](const SemanticIndexSnapshot& snapshot,
                       bool afterSide,
                       QHash<QString, SemanticRelationship>* target) {
        const QList<SemanticRelationship> relationships = snapshot.relationships();
        for (const SemanticRelationship& relationship : relationships) {
            if (!relationshipInScope(relationship, snapshot, query, afterSide))
                continue;
            target->insert(relationshipKey(relationship, snapshot), relationship);
        }
    };

    collect(*query.beforeSnapshot, false, &beforeRelationships);
    collect(*query.afterSnapshot, true, &afterRelationships);

    QList<SemanticDiffRelationshipChange> changes;
    QSet<QString> allKeys;
    for (auto it = beforeRelationships.constBegin();
         it != beforeRelationships.constEnd(); ++it) {
        allKeys.insert(it.key());
    }
    for (auto it = afterRelationships.constBegin();
         it != afterRelationships.constEnd(); ++it) {
        allKeys.insert(it.key());
    }

    for (const QString& key : std::as_const(allKeys)) {
        const bool hasBefore = beforeRelationships.contains(key);
        const bool hasAfter = afterRelationships.contains(key);
        if (hasBefore && hasAfter)
            continue;

        SemanticDiffRelationshipChange change;
        change.key = key;
        sym_list::SymbolInfo beforeFromSymbol;
        sym_list::SymbolInfo beforeToSymbol;
        sym_list::SymbolInfo afterFromSymbol;
        sym_list::SymbolInfo afterToSymbol;
        if (hasBefore) {
            change.kind = SemanticDiffChangeKind::Removed;
            change.beforeRelationship = beforeRelationships.value(key);
            beforeFromSymbol =
                relationshipEndpointSymbol(change.beforeRelationship,
                                           *query.beforeSnapshot,
                                           true);
            beforeToSymbol =
                relationshipEndpointSymbol(change.beforeRelationship,
                                           *query.beforeSnapshot,
                                           false);
        } else {
            change.kind = SemanticDiffChangeKind::Added;
            change.afterRelationship = afterRelationships.value(key);
            afterFromSymbol =
                relationshipEndpointSymbol(change.afterRelationship,
                                           *query.afterSnapshot,
                                           true);
            afterToSymbol =
                relationshipEndpointSymbol(change.afterRelationship,
                                           *query.afterSnapshot,
                                           false);
        }
        fillDisplayMetadata(change,
                            beforeFromSymbol,
                            beforeToSymbol,
                            afterFromSymbol,
                            afterToSymbol);
        changes.append(change);
    }

    sortRelationshipChanges(changes);
    return changes;
}

QList<SemanticDiffDiagnosticChange> SemanticDiffService::diagnosticChanges(
    const SemanticDiffQuery& query)
{
    QHash<QString, SemanticDiagnostic> beforeDiagnostics;
    QHash<QString, SemanticDiagnostic> afterDiagnostics;

    auto collect = [&](const SemanticIndexSnapshot& snapshot,
                       const QString& fileName,
                       QHash<QString, SemanticDiagnostic>* target) {
        const QList<SemanticDiagnostic> diagnostics = snapshot.getDiagnostics(fileName);
        for (const SemanticDiagnostic& diagnostic : diagnostics) {
            if (!diagnosticInScope(diagnostic, fileName))
                continue;
            target->insert(diagnosticKey(diagnostic), diagnostic);
        }
    };

    collect(*query.beforeSnapshot, query.beforeFileName, &beforeDiagnostics);
    collect(*query.afterSnapshot, query.afterFileName, &afterDiagnostics);

    QList<SemanticDiffDiagnosticChange> changes;
    QSet<QString> allKeys;
    for (auto it = beforeDiagnostics.constBegin();
         it != beforeDiagnostics.constEnd(); ++it) {
        allKeys.insert(it.key());
    }
    for (auto it = afterDiagnostics.constBegin();
         it != afterDiagnostics.constEnd(); ++it) {
        allKeys.insert(it.key());
    }

    for (const QString& key : std::as_const(allKeys)) {
        const bool hasBefore = beforeDiagnostics.contains(key);
        const bool hasAfter = afterDiagnostics.contains(key);
        if (hasBefore && hasAfter)
            continue;

        SemanticDiffDiagnosticChange change;
        change.key = key;
        if (hasBefore) {
            change.kind = SemanticDiffChangeKind::Removed;
            change.beforeDiagnostic = beforeDiagnostics.value(key);
        } else {
            change.kind = SemanticDiffChangeKind::Added;
            change.afterDiagnostic = afterDiagnostics.value(key);
        }
        fillDisplayMetadata(change);
        changes.append(change);
    }

    sortDiagnosticChanges(changes);
    return changes;
}

bool SemanticDiffService::symbolCategory(
    const SymbolTaxonomy::SemanticMetadata& metadata,
    SemanticDiffSymbolCategory* category)
{
    const SymbolTaxonomy::DeclarationKind kind = metadata.declarationKind;
    if (kind == SymbolTaxonomy::DeclarationKind::Package) {
        if (category)
            *category = SemanticDiffSymbolCategory::Package;
        return true;
    }
    if (kind == SymbolTaxonomy::DeclarationKind::Interface
        || kind == SymbolTaxonomy::DeclarationKind::Modport) {
        if (category)
            *category = SemanticDiffSymbolCategory::Interface;
        return true;
    }
    if (kind == SymbolTaxonomy::DeclarationKind::Typedef
        || kind == SymbolTaxonomy::DeclarationKind::Enum
        || kind == SymbolTaxonomy::DeclarationKind::Struct
        || kind == SymbolTaxonomy::DeclarationKind::StructVariable
        || kind == SymbolTaxonomy::DeclarationKind::StructMember) {
        if (category)
            *category = SemanticDiffSymbolCategory::Type;
        return true;
    }

    switch (SymbolTaxonomy::declarationGroup(metadata)) {
    case SymbolTaxonomy::DeclarationGroup::Port:
        if (category)
            *category = SemanticDiffSymbolCategory::Port;
        return true;
    case SymbolTaxonomy::DeclarationGroup::Parameter:
        if (category)
            *category = SemanticDiffSymbolCategory::Parameter;
        return true;
    case SymbolTaxonomy::DeclarationGroup::Instance:
        if (category)
            *category = SemanticDiffSymbolCategory::Instance;
        return true;
    case SymbolTaxonomy::DeclarationGroup::Signal:
        if (category)
            *category = SemanticDiffSymbolCategory::Signal;
        return true;
    case SymbolTaxonomy::DeclarationGroup::Unknown:
        break;
    }
    return false;
}

bool SemanticDiffService::symbolInScope(
    const sym_list::SymbolInfo& symbol,
    const QString& moduleName,
    const QString& fileName)
{
    if (!fileName.isEmpty()
        && normalizedFileName(symbol.fileName) != normalizedFileName(fileName)) {
        return false;
    }
    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(symbol);
    if (SymbolTaxonomy::isGlobalDefinition(metadata)
        || SymbolTaxonomy::isPackageVisibleDefinition(metadata)) {
        return true;
    }
    return SymbolTaxonomy::isSymbolInModuleScope(symbol, moduleName);
}

bool SemanticDiffService::relationshipInScope(
    const SemanticRelationship& relationship,
    const SemanticIndexSnapshot& snapshot,
    const SemanticDiffQuery& query,
    bool afterSide)
{
    const sym_list::SymbolInfo fromSymbol =
        relationshipEndpointSymbol(relationship, snapshot, true);
    const sym_list::SymbolInfo toSymbol =
        relationshipEndpointSymbol(relationship, snapshot, false);
    const QString fileName = afterSide ? query.afterFileName : query.beforeFileName;

    auto endpointInScope = [&](const sym_list::SymbolInfo& symbol) {
        if (symbol.symbolId < 0)
            return false;
        if (!fileName.isEmpty()
            && normalizedFileName(symbol.fileName) != normalizedFileName(fileName)) {
            return false;
        }
        return SymbolTaxonomy::isSymbolInModuleContext(
            symbol,
            query.moduleName);
    };

    return endpointInScope(fromSymbol) || endpointInScope(toSymbol);
}

bool SemanticDiffService::diagnosticInScope(
    const SemanticDiagnostic& diagnostic,
    const QString& fileName)
{
    if (fileName.isEmpty())
        return true;
    return normalizedFileName(diagnostic.fileName) == normalizedFileName(fileName);
}

QString SemanticDiffService::symbolKey(
    const sym_list::SymbolInfo& symbol,
    SemanticDiffSymbolCategory category)
{
    const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<int>(category))
        .arg(record.owner.name)
        .arg(record.name);
}

QString SemanticDiffService::symbolSignature(const sym_list::SymbolInfo& symbol)
{
    const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
    return QStringLiteral("%1:%2")
        .arg(static_cast<int>(record.rawCollectorKind))
        .arg(record.type.rawTypeText);
}

sym_list::SymbolInfo SemanticDiffService::relationshipEndpointSymbol(
    const SemanticRelationship& relationship,
    const SemanticIndexSnapshot& snapshot,
    bool fromEndpoint)
{
    const SymbolStableKey key = fromEndpoint
        ? relationship.fromStableKey
        : relationship.toStableKey;
    if (key.isValid()) {
        const sym_list::SymbolInfo symbol = snapshot.getSymbolByStableKey(key);
        if (symbol.symbolId >= 0 || !symbol.symbolName.isEmpty())
            return symbol;
    }

    const int localHandle = fromEndpoint
        ? relationship.fromId
        : relationship.toId;
    if (localHandle >= 0) {
        for (const sym_list::SymbolInfo& symbol : snapshot.getSymbols()) {
            if (symbol.symbolId == localHandle)
                return symbol;
        }
    }

    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    return missing;
}

SemanticSymbolRecord SemanticDiffService::relationshipEndpointRecord(
    const SemanticRelationship& relationship,
    const SemanticIndexSnapshot& snapshot,
    bool fromEndpoint)
{
    const SymbolStableKey key = fromEndpoint
        ? relationship.fromStableKey
        : relationship.toStableKey;
    if (key.isValid()) {
        const SemanticSymbolRecord record =
            snapshot.getSymbolRecordByStableKey(key);
        if (record.isValid())
            return record;
    }
    return semanticSymbolRecordForSymbol(
        relationshipEndpointSymbol(relationship, snapshot, fromEndpoint));
}

QString SemanticDiffService::relationshipKey(
    const SemanticRelationship& relationship,
    const SemanticIndexSnapshot& snapshot)
{
    const sym_list::SymbolInfo fromSymbol =
        relationshipEndpointSymbol(relationship, snapshot, true);
    const sym_list::SymbolInfo toSymbol =
        relationshipEndpointSymbol(relationship, snapshot, false);
    const SemanticSymbolRecord fromRecord =
        relationshipEndpointRecord(relationship, snapshot, true);
    const SemanticSymbolRecord toRecord =
        relationshipEndpointRecord(relationship, snapshot, false);
    SemanticDiffSymbolCategory fromCategory = SemanticDiffSymbolCategory::Signal;
    SemanticDiffSymbolCategory toCategory = SemanticDiffSymbolCategory::Signal;
    symbolCategory(metadataForRecord(fromRecord, fromSymbol), &fromCategory);
    symbolCategory(metadataForRecord(toRecord, toSymbol), &toCategory);
    const QString fromName = symbolDisplayNameForRecord(fromRecord, fromSymbol);
    const QString toName = symbolDisplayNameForRecord(toRecord, toSymbol);
    const QString fromKey =
        fromRecord.declarationKind == SymbolTaxonomy::DeclarationKind::Module
            ? QStringLiteral("module:%1").arg(fromName)
            : QStringLiteral("%1:%2:%3")
                  .arg(static_cast<int>(fromCategory))
                  .arg(fromRecord.owner.name)
                  .arg(fromName);
    const QString toKey =
        toRecord.declarationKind == SymbolTaxonomy::DeclarationKind::Module
            ? QStringLiteral("module:%1").arg(toName)
            : QStringLiteral("%1:%2:%3")
                  .arg(static_cast<int>(toCategory))
                  .arg(toRecord.owner.name)
                  .arg(toName);
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<int>(relationship.type))
        .arg(fromKey)
        .arg(toKey);
}

QString SemanticDiffService::diagnosticKey(const SemanticDiagnostic& diagnostic)
{
    return QStringLiteral("%1:%2:%3:%4:%5")
        .arg(normalizedFileName(diagnostic.fileName))
        .arg(diagnostic.line)
        .arg(diagnostic.column)
        .arg(static_cast<int>(diagnostic.severity))
        .arg(diagnostic.message);
}

QString SemanticDiffService::normalizedFileName(const QString& fileName)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(fileName));
}

QString SemanticDiffService::changeKindName(SemanticDiffChangeKind kind)
{
    switch (kind) {
    case SemanticDiffChangeKind::Added:
        return QStringLiteral("added");
    case SemanticDiffChangeKind::Removed:
        return QStringLiteral("removed");
    case SemanticDiffChangeKind::Modified:
        return QStringLiteral("modified");
    }
    return QString();
}

QString SemanticDiffService::changeKindDisplayName(SemanticDiffChangeKind kind)
{
    switch (kind) {
    case SemanticDiffChangeKind::Added:
        return QStringLiteral("Added");
    case SemanticDiffChangeKind::Removed:
        return QStringLiteral("Removed");
    case SemanticDiffChangeKind::Modified:
        return QStringLiteral("Modified");
    }
    return QString();
}

QString SemanticDiffService::symbolCategoryDisplayName(
    SemanticDiffSymbolCategory category)
{
    switch (category) {
    case SemanticDiffSymbolCategory::Port:
        return QStringLiteral("port");
    case SemanticDiffSymbolCategory::Parameter:
        return QStringLiteral("parameter");
    case SemanticDiffSymbolCategory::Instance:
        return QStringLiteral("instance");
    case SemanticDiffSymbolCategory::Signal:
        return QStringLiteral("signal");
    case SemanticDiffSymbolCategory::Package:
        return QStringLiteral("package");
    case SemanticDiffSymbolCategory::Interface:
        return QStringLiteral("interface");
    case SemanticDiffSymbolCategory::Type:
        return QStringLiteral("type");
    }
    return QStringLiteral("symbol");
}

QString SemanticDiffService::symbolCategoryGroupDisplayName(
    SemanticDiffSymbolCategory category)
{
    switch (category) {
    case SemanticDiffSymbolCategory::Port:
        return QStringLiteral("Ports");
    case SemanticDiffSymbolCategory::Parameter:
        return QStringLiteral("Parameters");
    case SemanticDiffSymbolCategory::Instance:
        return QStringLiteral("Instances");
    case SemanticDiffSymbolCategory::Signal:
        return QStringLiteral("Signals");
    case SemanticDiffSymbolCategory::Package:
        return QStringLiteral("Packages");
    case SemanticDiffSymbolCategory::Interface:
        return QStringLiteral("Interfaces");
    case SemanticDiffSymbolCategory::Type:
        return QStringLiteral("Types");
    }
    return QStringLiteral("Symbols");
}

QString SemanticDiffService::symbolScopeDisplayName(
    const sym_list::SymbolInfo& symbol)
{
    const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
    if (!record.owner.name.isEmpty())
        return QStringLiteral("scope %1").arg(record.owner.name);
    if (SymbolTaxonomy::isGlobalDefinition(
            SymbolTaxonomy::semanticMetadata(symbol))) {
        return QStringLiteral("global");
    }
    return QStringLiteral("scope unknown");
}

QString SemanticDiffService::relationshipTypeDisplayName(
    SymbolRelationshipEngine::RelationType type)
{
    switch (type) {
    case SymbolRelationshipEngine::CONTAINS:
        return QStringLiteral("Contains");
    case SymbolRelationshipEngine::REFERENCES:
        return QStringLiteral("References");
    case SymbolRelationshipEngine::INSTANTIATES:
        return QStringLiteral("Instantiates");
    case SymbolRelationshipEngine::CALLS:
        return QStringLiteral("Calls");
    case SymbolRelationshipEngine::INHERITS:
        return QStringLiteral("Inherits");
    case SymbolRelationshipEngine::IMPLEMENTS:
        return QStringLiteral("Implements");
    case SymbolRelationshipEngine::ASSIGNS_TO:
        return QStringLiteral("Assigns To");
    case SymbolRelationshipEngine::READS_FROM:
        return QStringLiteral("Reads From");
    case SymbolRelationshipEngine::CLOCKS:
        return QStringLiteral("Clocks");
    case SymbolRelationshipEngine::RESETS:
        return QStringLiteral("Resets");
    case SymbolRelationshipEngine::GENERATES:
        return QStringLiteral("Generates");
    case SymbolRelationshipEngine::CONSTRAINS:
        return QStringLiteral("Constrains");
    }
    return QStringLiteral("Relationship");
}

QString SemanticDiffService::diagnosticSeverityDisplayName(
    SemanticDiagnostic::Severity severity)
{
    switch (severity) {
    case SemanticDiagnostic::Error:
        return QStringLiteral("Error");
    case SemanticDiagnostic::Warning:
        return QStringLiteral("Warning");
    case SemanticDiagnostic::Info:
    default:
        return QStringLiteral("Info");
    }
}

QString SemanticDiffService::notFoundReasonDisplayName(
    SemanticDiffNotFoundReason reason)
{
    switch (reason) {
    case SemanticDiffNotFoundReason::None:
        return QString();
    case SemanticDiffNotFoundReason::MissingSnapshot:
        return QStringLiteral("missing snapshot");
    case SemanticDiffNotFoundReason::NoChanges:
        return QStringLiteral("no semantic changes");
    }
    return QStringLiteral("semantic diff unavailable");
}

QString SemanticDiffService::provenanceDisplayName(
    RelationshipProvenance provenance)
{
    switch (provenance) {
    case RelationshipProvenance::SlangExtracted:
        return QStringLiteral("slang extracted");
    case RelationshipProvenance::Inferred:
        return QStringLiteral("inferred");
    case RelationshipProvenance::LexicalFallback:
        return QStringLiteral("lexical fallback");
    case RelationshipProvenance::OpenDocument:
        return QStringLiteral("open document");
    case RelationshipProvenance::Workspace:
        return QStringLiteral("workspace");
    case RelationshipProvenance::FeatureGenerated:
        return QStringLiteral("feature generated");
    case RelationshipProvenance::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

QString SemanticDiffService::confidenceDisplayName(int confidence)
{
    return confidence > 0
        ? QStringLiteral("%1%").arg(confidence)
        : QStringLiteral("unknown");
}

QString SemanticDiffService::evidenceDisplayName(const QString& evidenceText)
{
    return evidenceText.isEmpty()
        ? QStringLiteral("no evidence detail")
        : evidenceText;
}

void SemanticDiffService::fillDisplayMetadata(
    SemanticDiffSymbolChange& change,
    const sym_list::SymbolInfo& beforeSymbol,
    const sym_list::SymbolInfo& afterSymbol)
{
    const sym_list::SymbolInfo displaySymbol =
        change.kind == SemanticDiffChangeKind::Removed
            ? beforeSymbol
            : afterSymbol;
    change.beforeSymbolRecord = semanticSymbolRecordForSymbol(beforeSymbol);
    change.afterSymbolRecord = semanticSymbolRecordForSymbol(afterSymbol);
    change.displaySymbolRecord = change.kind == SemanticDiffChangeKind::Removed
        ? change.beforeSymbolRecord
        : change.afterSymbolRecord;
    change.beforeStableKey = change.beforeSymbolRecord.stableKey.isValid()
        ? change.beforeSymbolRecord.stableKey
        : symbolStableKeyForSymbol(beforeSymbol);
    change.afterStableKey = change.afterSymbolRecord.stableKey.isValid()
        ? change.afterSymbolRecord.stableKey
        : symbolStableKeyForSymbol(afterSymbol);
    change.displayStableKey = change.displaySymbolRecord.stableKey.isValid()
        ? change.displaySymbolRecord.stableKey
        : symbolStableKeyForSymbol(displaySymbol);
    change.kindDisplayName = changeKindDisplayName(change.kind);
    change.categoryDisplayName = symbolCategoryDisplayName(change.category);
    change.categoryGroupDisplayName = symbolCategoryGroupDisplayName(change.category);
    change.symbolDisplayName =
        symbolDisplayNameForRecord(change.displaySymbolRecord, displaySymbol);
    change.sourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(change.displaySymbolRecord,
                                       displaySymbol);
    change.symbolTypeDisplayName =
        symbolTypeDisplayNameForRecord(change.displaySymbolRecord,
                                       displaySymbol);
    change.scopeDisplayName =
        symbolScopeDisplayNameForRecord(change.displaySymbolRecord,
                                        displaySymbol);
    change.codeLink = codeLinkForRecord(change.displaySymbolRecord,
                                        displaySymbol);
    if (hasDisplaySymbol(beforeSymbol)) {
        change.beforeSymbolTypeDisplayName =
            symbolTypeDisplayNameForRecord(change.beforeSymbolRecord,
                                           beforeSymbol);
        change.beforeScopeDisplayName =
            symbolScopeDisplayNameForRecord(change.beforeSymbolRecord,
                                            beforeSymbol);
        change.beforeSourceRoleDisplayName =
            sourceRoleDisplayNameForRecord(change.beforeSymbolRecord,
                                           beforeSymbol);
        change.beforeDataTypeDisplayName =
            dataTypeDisplayNameForRecord(change.beforeSymbolRecord,
                                         beforeSymbol);
        change.beforeCodeLink = codeLinkForRecord(change.beforeSymbolRecord,
                                                  beforeSymbol);
    }
    if (hasDisplaySymbol(afterSymbol)) {
        change.afterSymbolTypeDisplayName =
            symbolTypeDisplayNameForRecord(change.afterSymbolRecord,
                                           afterSymbol);
        change.afterScopeDisplayName =
            symbolScopeDisplayNameForRecord(change.afterSymbolRecord,
                                            afterSymbol);
        change.afterSourceRoleDisplayName =
            sourceRoleDisplayNameForRecord(change.afterSymbolRecord,
                                           afterSymbol);
        change.afterDataTypeDisplayName =
            dataTypeDisplayNameForRecord(change.afterSymbolRecord,
                                         afterSymbol);
        change.afterCodeLink = codeLinkForRecord(change.afterSymbolRecord,
                                                 afterSymbol);
    }

    const QString displayDataType =
        dataTypeDisplayNameForRecord(change.displaySymbolRecord,
                                     displaySymbol);
    const QString dataType = displayDataType.isEmpty()
        ? QString()
        : QStringLiteral(" %1").arg(displayDataType);
    if (change.kind != SemanticDiffChangeKind::Modified) {
        change.detailDisplayName = QStringLiteral("%1 %2%3, %4, %5")
                                       .arg(change.categoryDisplayName,
                                            change.symbolTypeDisplayName,
                                            dataType,
                                            change.scopeDisplayName,
                                            change.sourceRoleDisplayName);
        return;
    }

    const QString beforeText =
        QStringLiteral("%1%2")
            .arg(symbolTypeDisplayNameForRecord(change.beforeSymbolRecord,
                                                beforeSymbol),
                 change.beforeDataTypeDisplayName.isEmpty()
                     ? QString()
                     : QStringLiteral(" %1").arg(
                           change.beforeDataTypeDisplayName));
    const QString afterText =
        QStringLiteral("%1%2")
            .arg(symbolTypeDisplayNameForRecord(change.afterSymbolRecord,
                                                afterSymbol),
                 change.afterDataTypeDisplayName.isEmpty()
                     ? QString()
                     : QStringLiteral(" %1").arg(
                           change.afterDataTypeDisplayName));
    change.detailDisplayName = QStringLiteral("%1 %2 -> %3, %4, %5")
                                   .arg(change.categoryDisplayName,
                                        beforeText,
                                        afterText,
                                        change.scopeDisplayName,
                                        change.sourceRoleDisplayName);
}

void SemanticDiffService::fillDisplayMetadata(
    SemanticDiffRelationshipChange& change,
    const sym_list::SymbolInfo& beforeFromSymbol,
    const sym_list::SymbolInfo& beforeToSymbol,
    const sym_list::SymbolInfo& afterFromSymbol,
    const sym_list::SymbolInfo& afterToSymbol)
{
    const SemanticRelationship& relationship =
        change.kind == SemanticDiffChangeKind::Removed
            ? change.beforeRelationship
            : change.afterRelationship;
    const sym_list::SymbolInfo displayFromSymbol =
        change.kind == SemanticDiffChangeKind::Removed
            ? beforeFromSymbol
            : afterFromSymbol;
    const sym_list::SymbolInfo displayToSymbol =
        change.kind == SemanticDiffChangeKind::Removed
            ? beforeToSymbol
            : afterToSymbol;
    change.beforeFromSymbolRecord =
        semanticSymbolRecordForSymbol(beforeFromSymbol);
    change.beforeToSymbolRecord =
        semanticSymbolRecordForSymbol(beforeToSymbol);
    change.afterFromSymbolRecord =
        semanticSymbolRecordForSymbol(afterFromSymbol);
    change.afterToSymbolRecord =
        semanticSymbolRecordForSymbol(afterToSymbol);
    change.displayFromSymbolRecord =
        change.kind == SemanticDiffChangeKind::Removed
            ? change.beforeFromSymbolRecord
            : change.afterFromSymbolRecord;
    change.displayToSymbolRecord =
        change.kind == SemanticDiffChangeKind::Removed
            ? change.beforeToSymbolRecord
            : change.afterToSymbolRecord;
    change.beforeFromStableKey =
        change.beforeFromSymbolRecord.stableKey.isValid()
            ? change.beforeFromSymbolRecord.stableKey
            : symbolStableKeyForSymbol(beforeFromSymbol);
    change.beforeToStableKey =
        change.beforeToSymbolRecord.stableKey.isValid()
            ? change.beforeToSymbolRecord.stableKey
            : symbolStableKeyForSymbol(beforeToSymbol);
    change.afterFromStableKey =
        change.afterFromSymbolRecord.stableKey.isValid()
            ? change.afterFromSymbolRecord.stableKey
            : symbolStableKeyForSymbol(afterFromSymbol);
    change.afterToStableKey =
        change.afterToSymbolRecord.stableKey.isValid()
            ? change.afterToSymbolRecord.stableKey
            : symbolStableKeyForSymbol(afterToSymbol);
    change.displayFromStableKey =
        change.displayFromSymbolRecord.stableKey.isValid()
            ? change.displayFromSymbolRecord.stableKey
            : symbolStableKeyForSymbol(displayFromSymbol);
    change.displayToStableKey =
        change.displayToSymbolRecord.stableKey.isValid()
            ? change.displayToSymbolRecord.stableKey
            : symbolStableKeyForSymbol(displayToSymbol);
    change.provenance = relationship.provenance;
    change.confidence = relationship.confidence;
    change.evidenceText = relationship.evidenceText;
    change.kindDisplayName = changeKindDisplayName(change.kind);
    change.relationshipTypeDisplayName =
        relationshipTypeDisplayName(relationship.type);
    change.fromCodeLink = codeLinkForRecord(change.displayFromSymbolRecord,
                                            displayFromSymbol);
    change.toCodeLink = codeLinkForRecord(change.displayToSymbolRecord,
                                          displayToSymbol);
    change.fromSymbolDisplayName =
        symbolDisplayNameForRecord(change.displayFromSymbolRecord,
                                   displayFromSymbol);
    change.toSymbolDisplayName =
        symbolDisplayNameForRecord(change.displayToSymbolRecord,
                                   displayToSymbol);
    change.provenanceDisplayName = provenanceDisplayName(change.provenance);
    change.confidenceDisplayName = confidenceDisplayName(change.confidence);
    change.evidenceDisplayName = evidenceDisplayName(change.evidenceText);
    change.categoryGroupDisplayName = QStringLiteral("Relationships");
    change.codeLink = change.fromCodeLink;
    change.sourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(change.displayFromSymbolRecord,
                                       displayFromSymbol);
    change.detailDisplayName =
        change.fromSymbolDisplayName == QStringLiteral("<unknown>")
            || change.toSymbolDisplayName == QStringLiteral("<unknown>")
        ? change.key
        : QStringLiteral("%1 -> %2")
              .arg(change.fromSymbolDisplayName,
                   change.toSymbolDisplayName);
}

void SemanticDiffService::fillDisplayMetadata(
    SemanticDiffDiagnosticChange& change)
{
    change.displayDiagnostic = change.kind == SemanticDiffChangeKind::Removed
        ? change.beforeDiagnostic
        : change.afterDiagnostic;
    change.kindDisplayName = changeKindDisplayName(change.kind);
    change.severityDisplayName =
        diagnosticSeverityDisplayName(change.displayDiagnostic.severity);
    change.categoryGroupDisplayName = QStringLiteral("Diagnostics");
    change.codeLink = RtlInsightLink::fromDiagnostic(change.displayDiagnostic);
    change.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(
            SymbolTaxonomy::sourceRoleForFileName(change.displayDiagnostic.fileName));
    change.detailDisplayName = QStringLiteral("%1, %2")
                                   .arg(change.severityDisplayName,
                                        change.sourceRoleDisplayName);
}

void SemanticDiffService::sortSymbolChanges(
    QList<SemanticDiffSymbolChange>& changes)
{
    std::sort(changes.begin(), changes.end(),
              [](const SemanticDiffSymbolChange& lhs,
                 const SemanticDiffSymbolChange& rhs) {
                  if (lhs.category != rhs.category)
                      return static_cast<int>(lhs.category)
                          < static_cast<int>(rhs.category);
                  if (lhs.key != rhs.key)
                      return lhs.key < rhs.key;
                  return changeKindName(lhs.kind) < changeKindName(rhs.kind);
              });
}

void SemanticDiffService::sortRelationshipChanges(
    QList<SemanticDiffRelationshipChange>& changes)
{
    std::sort(changes.begin(), changes.end(),
              [](const SemanticDiffRelationshipChange& lhs,
                 const SemanticDiffRelationshipChange& rhs) {
                  if (lhs.key != rhs.key)
                      return lhs.key < rhs.key;
                  return changeKindName(lhs.kind) < changeKindName(rhs.kind);
              });
}

void SemanticDiffService::sortDiagnosticChanges(
    QList<SemanticDiffDiagnosticChange>& changes)
{
    std::sort(changes.begin(), changes.end(),
              [](const SemanticDiffDiagnosticChange& lhs,
                 const SemanticDiffDiagnosticChange& rhs) {
                  if (lhs.key != rhs.key)
                      return lhs.key < rhs.key;
                  return changeKindName(lhs.kind) < changeKindName(rhs.kind);
              });
}
