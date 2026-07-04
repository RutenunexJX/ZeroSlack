#include "signalusagehotspotservice.h"

#include "symboltaxonomy.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <algorithm>

std::unique_ptr<SignalUsageHotspotService>
    SignalUsageHotspotService::instance = nullptr;

namespace {
using Role = SignalUsageHotspotRole;
using NotFoundReason = SignalUsageHotspotNotFoundReason;

QString normalizedAccessPath(const QString& accessPath)
{
    QString normalized = accessPath.trimmed();
    normalized.remove(QLatin1Char(' '));
    return normalized;
}

QString rootNameForAccessPath(const QString& accessPath)
{
    const QString normalized = normalizedAccessPath(accessPath);
    const int dotIndex = normalized.indexOf(QLatin1Char('.'));
    return dotIndex < 0 ? normalized : normalized.left(dotIndex);
}

QString memberNameForAccessPath(const QString& accessPath)
{
    const QString normalized = normalizedAccessPath(accessPath);
    const int dotIndex = normalized.lastIndexOf(QLatin1Char('.'));
    return dotIndex < 0 ? QString() : normalized.mid(dotIndex + 1);
}

SymbolTaxonomy::SemanticMetadata metadataForRecord(
    const SemanticSymbolRecord& record)
{
    return semanticMetadataForSymbolRecord(record);
}

QString displayNameForRecord(const SemanticSymbolRecord& record)
{
    return record.name.isEmpty() ? QStringLiteral("<unknown>") : record.name;
}

QString typeDisplayNameForRecord(const SemanticSymbolRecord& record)
{
    return SymbolTaxonomy::symbolTypeLabel(metadataForRecord(record));
}

QString moduleNameForRecord(const SemanticSymbolRecord& record)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;
    if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Module
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Interface
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Package) {
        return record.name;
    }
    return QString();
}

QString displayModuleNameForRecord(const SemanticSymbolRecord& record)
{
    const QString moduleName = moduleNameForRecord(record);
    if (!moduleName.isEmpty())
        return moduleName;
    return QFileInfo(record.location.fileName).fileName();
}

RtlInsightCodeLink codeLinkForRecord(const SemanticSymbolRecord& record)
{
    return RtlInsightLink::fromFileLine(record.location.fileName,
                                        record.location.startLine,
                                        record.location.startColumn);
}

RtlInsightCodeLink codeLinkForRange(const SemanticSourceRange& range)
{
    return RtlInsightLink::fromFileLine(range.fileName,
                                        range.line,
                                        range.column);
}

bool isHotspotDeclaration(const SemanticSymbolRecord& record)
{
    const SymbolTaxonomy::SemanticMetadata metadata = metadataForRecord(record);
    if (metadata.collectorKind == SymbolTaxonomy::CollectorKind::EnumValue)
        return false;
    return SymbolTaxonomy::isSignalDeclaration(metadata)
        || SymbolTaxonomy::isPortDeclaration(metadata)
        || metadata.declarationKind == SymbolTaxonomy::DeclarationKind::StructMember;
}

bool isEnumValueRecord(const SemanticSymbolRecord& record)
{
    return metadataForRecord(record).collectorKind
        == SymbolTaxonomy::CollectorKind::EnumValue;
}

bool sameSourceFile(const QString& lhs, const QString& rhs)
{
    if (lhs.isEmpty() || rhs.isEmpty())
        return false;
    QFileInfo lhsInfo(lhs);
    QFileInfo rhsInfo(rhs);
    const QString lhsCanonical = lhsInfo.canonicalFilePath();
    const QString rhsCanonical = rhsInfo.canonicalFilePath();
    if (!lhsCanonical.isEmpty() && !rhsCanonical.isEmpty())
        return lhsCanonical.compare(rhsCanonical, Qt::CaseInsensitive) == 0;
    if (lhs.compare(rhs, Qt::CaseInsensitive) == 0)
        return true;
    return lhsInfo.fileName().compare(rhsInfo.fileName(), Qt::CaseInsensitive)
        == 0;
}

bool isScopeRecord(const SemanticSymbolRecord& record)
{
    return record.declarationKind == SymbolTaxonomy::DeclarationKind::Module
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Interface
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Package
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Process
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Generate;
}

bool isPrimaryLaneScopeRecord(const SemanticSymbolRecord& record)
{
    return record.declarationKind == SymbolTaxonomy::DeclarationKind::Module
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Interface
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Package;
}

bool recordHasLineRange(const SemanticSymbolRecord& record)
{
    return record.location.startLine > 0 && record.location.endLine > 0
        && record.location.endLine >= record.location.startLine;
}

SemanticSymbolRecord scopeRecordForOwner(SemanticIndex* index,
                                         const SemanticSymbolOwner& owner,
                                         const QString& fileName)
{
    if (!index)
        return {};
    if (owner.stableKey.isValid()) {
        const SemanticSymbolRecord record =
            index->getSymbolRecordByStableKey(owner.stableKey);
        if (record.isValid() && isScopeRecord(record)
            && recordHasLineRange(record)
            && sameSourceFile(record.location.fileName, fileName)) {
            return record;
        }
    }
    if (owner.name.isEmpty())
        return {};
    const QList<SemanticSymbolRecord> records =
        index->getSymbolRecordsByName(owner.name);
    for (const SemanticSymbolRecord& record : records) {
        if (isScopeRecord(record) && recordHasLineRange(record)
            && sameSourceFile(record.location.fileName, fileName)) {
            return record;
        }
    }
    return {};
}

SemanticSymbolRecord scopeRecordForUsageItem(
    SemanticIndex* index,
    const SignalUsageHotspotItem& item)
{
    const QList<SemanticSymbolRecord> candidates{
        item.peerSymbolRecord,
        item.fromSymbolRecord,
        item.toSymbolRecord,
        item.subjectSymbolRecord,
    };

    for (const SemanticSymbolRecord& candidate : candidates) {
        const SemanticSymbolRecord ownerScope =
            scopeRecordForOwner(index, candidate.owner, item.fileName);
        if (ownerScope.isValid())
            return ownerScope;
        if (candidate.isValid() && isPrimaryLaneScopeRecord(candidate)
            && recordHasLineRange(candidate)
            && sameSourceFile(candidate.location.fileName, item.fileName)) {
            return candidate;
        }
    }

    if (!index)
        return {};

    if (!item.moduleName.isEmpty()) {
        const QList<SemanticSymbolRecord> namedRecords =
            index->getSymbolRecordsByName(item.moduleName);
        for (const SemanticSymbolRecord& record : namedRecords) {
            if (isScopeRecord(record) && recordHasLineRange(record)
                && sameSourceFile(record.location.fileName, item.fileName)) {
                return record;
            }
        }
    }

    const QList<SemanticSymbolRecord> fileRecords =
        index->getSymbolRecords(item.fileName);
    for (const SemanticSymbolRecord& record : fileRecords) {
        if (!isScopeRecord(record) || !recordHasLineRange(record))
            continue;
        if (!sameSourceFile(record.location.fileName, item.fileName))
            continue;
        const bool containsUsage =
            item.line <= 0
            || (item.line >= record.location.startLine
                && item.line <= record.location.endLine);
        if (containsUsage && !item.moduleName.isEmpty()
            && record.name == item.moduleName) {
            return record;
        }
    }
    for (const SemanticSymbolRecord& record : fileRecords) {
        if (!isScopeRecord(record) || !recordHasLineRange(record))
            continue;
        if (!sameSourceFile(record.location.fileName, item.fileName))
            continue;
        if (item.line <= 0
            || (item.line >= record.location.startLine
                && item.line <= record.location.endLine)) {
            return record;
        }
    }
    return {};
}

SemanticSymbolRecord missingRecord()
{
    return {};
}

SemanticSymbolRecord memberRecordForAccessPath(SemanticIndex* index,
                                               const SemanticSymbolRecord& root,
                                               const QString& accessPath)
{
    if (!index || accessPath.isEmpty())
        return {};

    QString structTypeName = root.type.resolvedTypeName;
    if (structTypeName.isEmpty())
        structTypeName = root.type.rawTypeText;
    const QString memberName = memberNameForAccessPath(accessPath);
    if (structTypeName.isEmpty() || memberName.isEmpty())
        return {};

    const QList<SemanticSymbolRecord> members =
        index->getStructMemberRecords(structTypeName);
    for (const SemanticSymbolRecord& member : members) {
        if (member.name == memberName)
            return member;
    }
    return {};
}

QString subjectAccessPathForQuery(const SignalUsageHotspotQuery& query,
                                  const SemanticSymbolRecord& signal)
{
    const QString accessPath = normalizedAccessPath(query.signalAccessPath);
    if (accessPath.isEmpty() || !accessPath.contains(QLatin1Char('.')))
        return QString();
    const QString rootName = rootNameForAccessPath(accessPath);
    if (!rootName.isEmpty() && rootName != signal.name)
        return QString();
    return accessPath;
}

bool relationshipMatchesSubjectAccessPath(
    const SemanticRelationshipResult& relationship,
    bool outgoing,
    const QString& subjectAccessPath)
{
    if (subjectAccessPath.isEmpty())
        return true;

    const QString endpointAccessPath = normalizedAccessPath(
        outgoing ? relationship.fromAccessPath : relationship.toAccessPath);
    return endpointAccessPath == subjectAccessPath;
}

SemanticSymbolRecord resolveSignal(SemanticIndex* index,
                                   const SignalUsageHotspotQuery& query,
                                   NotFoundReason* reason)
{
    if (reason)
        *reason = NotFoundReason::None;

    if (!index) {
        if (reason)
            *reason = NotFoundReason::NoMatchingSignal;
        return missingRecord();
    }

    if (query.signalStableKey.isValid()) {
        const SemanticSymbolRecord record =
            index->getSymbolRecordByStableKey(query.signalStableKey);
        if (!record.isValid()) {
            if (reason)
                *reason = NotFoundReason::NoMatchingSignal;
            return missingRecord();
        }
        if (!isHotspotDeclaration(record)) {
            if (reason)
                *reason = NotFoundReason::UnsupportedSymbolKind;
            return missingRecord();
        }
        return record;
    }

    const QString querySignalName = !query.signalAccessPath.isEmpty()
        ? rootNameForAccessPath(query.signalAccessPath)
        : query.signalName;
    if (querySignalName.isEmpty()) {
        if (reason)
            *reason = NotFoundReason::EmptySignalName;
        return missingRecord();
    }

    SemanticDefinitionQuery definitionQuery;
    definitionQuery.symbolName = querySignalName;
    definitionQuery.fileName = query.fileName;
    definitionQuery.moduleName = query.moduleName;
    const SemanticDefinitionResult definition =
        index->resolveDefinition(definitionQuery);
    if (!definition.found) {
        if (reason)
            *reason = NotFoundReason::NoMatchingSignal;
        return missingRecord();
    }
    if (!isHotspotDeclaration(definition.symbolRecord)) {
        if (reason)
            *reason = NotFoundReason::UnsupportedSymbolKind;
        return missingRecord();
    }
    if (definition.symbolRecord.localHandle < 0)
        return missingRecord();
    return definition.symbolRecord;
}

QString relationshipTypeDisplayName(SymbolRelationshipEngine::RelationType type)
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

QString provenanceDisplayName(RelationshipProvenance provenance)
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

bool hintLooksCaseLike(const QString& hint)
{
    const QString lower = hint.toLower();
    return lower.contains(QStringLiteral("case selector"))
        || lower.contains(QStringLiteral("case item"))
        || lower.contains(QStringLiteral("case expression"))
        || lower.contains(QStringLiteral("case("))
        || lower.contains(QStringLiteral("case "));
}

bool hintLooksConditionLike(const QString& hint)
{
    const QString lower = hint.toLower();
    return lower.contains(QStringLiteral("condition"))
        || lower.contains(QStringLiteral("guard"))
        || lower.contains(QStringLiteral("ternary"))
        || lower.contains(QStringLiteral(" if "))
        || lower.contains(QStringLiteral("if("))
        || lower.contains(QLatin1Char('?'));
}

Role classifyRelationship(const SemanticRelationshipResult& relationship,
                          bool outgoing,
                          const SemanticSymbolRecord& peerRecord,
                          QString* reason)
{
    const auto type = relationship.relationship.type;
    const QString hint = QStringLiteral("%1 %2")
                             .arg(relationship.evidenceText,
                                  relationshipTypeDisplayName(type));

    if (type == SymbolRelationshipEngine::CLOCKS
        || type == SymbolRelationshipEngine::RESETS) {
        if (reason)
            *reason = QStringLiteral("timing relationship");
        return Role::Timing;
    }

    if (SymbolTaxonomy::isPortConnectionPeer(metadataForRecord(peerRecord))) {
        if (reason)
            *reason = QStringLiteral("port connection peer");
        return Role::Port;
    }

    if (hintLooksCaseLike(hint)) {
        if (reason)
            *reason = QStringLiteral("case evidence");
        return Role::Case;
    }

    if (hintLooksConditionLike(hint)) {
        if (reason)
            *reason = QStringLiteral("condition evidence");
        return Role::Condition;
    }

    if (type == SymbolRelationshipEngine::ASSIGNS_TO) {
        if (outgoing) {
            if (reason)
                *reason = QStringLiteral("assignment source");
            return Role::Read;
        }
        if (reason)
            *reason = QStringLiteral("assignment target");
        return Role::Write;
    }

    if (type == SymbolRelationshipEngine::READS_FROM
        || type == SymbolRelationshipEngine::REFERENCES) {
        if (reason)
            *reason = QStringLiteral("read/reference relationship");
        return Role::Read;
    }

    if (reason)
        *reason = QStringLiteral("unclassified relationship");
    return Role::Unknown;
}

QString itemKey(const SemanticRelationshipResult& relationship,
                bool outgoing,
                const QString& subjectAccessPath)
{
    return QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
        .arg(relationship.relationship.fromId)
        .arg(relationship.relationship.toId)
        .arg(static_cast<int>(relationship.relationship.type))
        .arg(outgoing ? 1 : 0)
        .arg(subjectAccessPath)
        .arg(relationship.evidenceRange.fileName)
        .arg(relationship.evidenceRange.line);
}

SignalUsageHotspotItem hotspotItemForRelationship(
    const SemanticSymbolRecord& signal,
    const QString& subjectAccessPath,
    const SemanticRelationshipResult& relationship,
    bool outgoing)
{
    SignalUsageHotspotItem item;
    item.outgoing = outgoing;
    item.subjectSymbolRecord = signal;
    item.subjectStableKey = signal.stableKey;
    item.fromSymbolRecord = relationship.fromSymbolRecord;
    item.toSymbolRecord = relationship.toSymbolRecord;
    item.fromStableKey = relationship.fromStableKey;
    item.toStableKey = relationship.toStableKey;
    item.fromAccessPath = relationship.fromAccessPath;
    item.toAccessPath = relationship.toAccessPath;
    item.subjectAccessPath = subjectAccessPath;
    item.peerSymbolRecord =
        outgoing ? relationship.toSymbolRecord : relationship.fromSymbolRecord;
    item.peerStableKey = outgoing ? relationship.toStableKey
                                  : relationship.fromStableKey;
    item.peerAccessPath = outgoing ? relationship.toAccessPath
                                   : relationship.fromAccessPath;
    item.relationshipType = relationship.relationship.type;
    item.relationshipTypeDisplayName =
        relationshipTypeDisplayName(item.relationshipType);
    item.provenance = relationship.provenance;
    item.confidence = relationship.confidence;
    item.evidenceText = relationship.evidenceText;
    item.snippet = relationship.evidenceText;
    item.evidenceRange = relationship.evidenceRange;
    item.preciseEvidence = item.evidenceRange.isValid();
    item.evidenceKindDisplayName =
        QStringLiteral("%1 %2").arg(provenanceDisplayName(item.provenance),
                                    item.relationshipTypeDisplayName);
    item.role = classifyRelationship(relationship,
                                     outgoing,
                                     item.peerSymbolRecord,
                                     &item.roleReasonDisplayName);

    if (item.preciseEvidence) {
        item.fileName = item.evidenceRange.fileName;
        item.line = item.evidenceRange.line;
        item.column = item.evidenceRange.column;
        item.endLine = item.evidenceRange.endLine;
        item.endColumn = item.evidenceRange.endColumn;
        item.codeLink = codeLinkForRange(item.evidenceRange);
    } else {
        const SemanticSymbolRecord fallbackRecord =
            item.peerSymbolRecord.isValid() ? item.peerSymbolRecord : signal;
        item.fileName = fallbackRecord.location.fileName;
        item.line = fallbackRecord.location.startLine;
        item.column = fallbackRecord.location.startColumn;
        item.endLine = fallbackRecord.location.endLine;
        item.endColumn = fallbackRecord.location.endColumn;
        item.codeLink = codeLinkForRecord(fallbackRecord);
    }

    item.moduleName = displayModuleNameForRecord(item.peerSymbolRecord);
    if (item.moduleName.isEmpty())
        item.moduleName = displayModuleNameForRecord(signal);
    return item;
}

QList<Role> orderedRoles()
{
    return {
        Role::Write,
        Role::Read,
        Role::Port,
        Role::Condition,
        Role::Case,
        Role::Timing,
        Role::Unknown,
    };
}

int roleSortIndex(Role role)
{
    const QList<Role> roles = orderedRoles();
    return roles.indexOf(role);
}

QList<SignalUsageHotspotRoleSummary> roleSummariesFromCounts(
    const QHash<int, int>& counts)
{
    QList<SignalUsageHotspotRoleSummary> summaries;
    for (Role role : orderedRoles()) {
        const int count = counts.value(static_cast<int>(role), 0);
        if (count <= 0)
            continue;
        SignalUsageHotspotRoleSummary summary;
        summary.role = role;
        summary.roleDisplayName =
            SignalUsageHotspotService::roleDisplayName(role);
        summary.count = count;
        summaries.append(summary);
    }
    return summaries;
}

void buildSummaries(SignalUsageHotspotReport& report, SemanticIndex* index)
{
    QHash<int, int> roleCounts;
    QHash<QString, int> moduleIndexes;
    QHash<QString, int> fileIndexes;
    QHash<QString, int> matrixIndexes;
    QHash<QString, int> laneIndexes;

    for (int i = 0; i < report.items.size(); ++i) {
        const SignalUsageHotspotItem& item = report.items.at(i);
        const int roleKey = static_cast<int>(item.role);
        ++roleCounts[roleKey];

        const QString moduleName = item.moduleName.isEmpty()
            ? QStringLiteral("<unknown>")
            : item.moduleName;
        const QString fileName = item.fileName.isEmpty()
            ? QStringLiteral("<unknown>")
            : item.fileName;

        if (!moduleIndexes.contains(moduleName)) {
            SignalUsageHotspotModuleSummary summary;
            summary.moduleName = moduleName;
            moduleIndexes.insert(moduleName, report.moduleSummaries.size());
            report.moduleSummaries.append(summary);
        }
        SignalUsageHotspotModuleSummary& moduleSummary =
            report.moduleSummaries[moduleIndexes.value(moduleName)];
        ++moduleSummary.count;
        QHash<int, int> moduleRoleCounts;
        for (const SignalUsageHotspotRoleSummary& roleSummary
             : moduleSummary.roleCounts) {
            moduleRoleCounts.insert(static_cast<int>(roleSummary.role),
                                    roleSummary.count);
        }
        ++moduleRoleCounts[roleKey];
        moduleSummary.roleCounts = roleSummariesFromCounts(moduleRoleCounts);

        if (!fileIndexes.contains(fileName)) {
            SignalUsageHotspotFileSummary summary;
            summary.fileName = fileName;
            fileIndexes.insert(fileName, report.fileSummaries.size());
            report.fileSummaries.append(summary);
        }
        SignalUsageHotspotFileSummary& fileSummary =
            report.fileSummaries[fileIndexes.value(fileName)];
        ++fileSummary.count;
        QHash<int, int> fileRoleCounts;
        for (const SignalUsageHotspotRoleSummary& roleSummary
             : fileSummary.roleCounts) {
            fileRoleCounts.insert(static_cast<int>(roleSummary.role),
                                  roleSummary.count);
        }
        ++fileRoleCounts[roleKey];
        fileSummary.roleCounts = roleSummariesFromCounts(fileRoleCounts);

        const QString matrixKey = QStringLiteral("%1\n%2\n%3")
                                      .arg(moduleName,
                                           fileName)
                                      .arg(roleKey);
        if (!matrixIndexes.contains(matrixKey)) {
            SignalUsageHotspotMatrixCell cell;
            cell.moduleName = moduleName;
            cell.fileName = fileName;
            cell.role = item.role;
            cell.roleDisplayName =
                SignalUsageHotspotService::roleDisplayName(item.role);
            matrixIndexes.insert(matrixKey, report.matrixCells.size());
            report.matrixCells.append(cell);
        }
        ++report.matrixCells[matrixIndexes.value(matrixKey)].count;

        const QString laneKey = QStringLiteral("%1\n%2").arg(moduleName, fileName);
        if (!laneIndexes.contains(laneKey)) {
            SignalUsageHotspotTrackLane lane;
            lane.moduleName = moduleName;
            lane.fileName = fileName;
            const SemanticSymbolRecord scope =
                scopeRecordForUsageItem(index, item);
            if (scope.isValid() && recordHasLineRange(scope)) {
                lane.startLine = scope.location.startLine;
                lane.endLine = scope.location.endLine;
            }
            laneIndexes.insert(laneKey, report.trackLanes.size());
            report.trackLanes.append(lane);
        }
        SignalUsageHotspotTrackLane& lane =
            report.trackLanes[laneIndexes.value(laneKey)];
        ++lane.count;
        if (item.line > 0) {
            lane.startLine = lane.startLine <= 0
                ? item.line
                : std::min(lane.startLine, item.line);
            lane.endLine = std::max(lane.endLine,
                                    item.endLine > 0 ? item.endLine : item.line);
        }
        SignalUsageHotspotTrackPosition position;
        position.itemIndex = i;
        position.role = item.role;
        position.roleDisplayName =
            SignalUsageHotspotService::roleDisplayName(item.role);
        position.line = item.line;
        position.column = item.column;
        position.endLine = item.endLine;
        position.endColumn = item.endColumn;
        lane.positions.append(position);
    }

    report.roleSummaries = roleSummariesFromCounts(roleCounts);

    auto roleSummaryLess = [](const SignalUsageHotspotRoleSummary& lhs,
                              const SignalUsageHotspotRoleSummary& rhs) {
        return roleSortIndex(lhs.role) < roleSortIndex(rhs.role);
    };
    for (SignalUsageHotspotModuleSummary& summary : report.moduleSummaries)
        std::sort(summary.roleCounts.begin(),
                  summary.roleCounts.end(),
                  roleSummaryLess);
    for (SignalUsageHotspotFileSummary& summary : report.fileSummaries)
        std::sort(summary.roleCounts.begin(),
                  summary.roleCounts.end(),
                  roleSummaryLess);

    std::sort(report.moduleSummaries.begin(),
              report.moduleSummaries.end(),
              [](const SignalUsageHotspotModuleSummary& lhs,
                 const SignalUsageHotspotModuleSummary& rhs) {
                  return lhs.moduleName < rhs.moduleName;
              });
    std::sort(report.fileSummaries.begin(),
              report.fileSummaries.end(),
              [](const SignalUsageHotspotFileSummary& lhs,
                 const SignalUsageHotspotFileSummary& rhs) {
                  return lhs.fileName < rhs.fileName;
              });
    std::sort(report.matrixCells.begin(),
              report.matrixCells.end(),
              [](const SignalUsageHotspotMatrixCell& lhs,
                 const SignalUsageHotspotMatrixCell& rhs) {
                  if (lhs.moduleName != rhs.moduleName)
                      return lhs.moduleName < rhs.moduleName;
                  if (lhs.fileName != rhs.fileName)
                      return lhs.fileName < rhs.fileName;
                  return roleSortIndex(lhs.role) < roleSortIndex(rhs.role);
              });
    for (SignalUsageHotspotTrackLane& lane : report.trackLanes) {
        std::sort(lane.positions.begin(),
                  lane.positions.end(),
                  [](const SignalUsageHotspotTrackPosition& lhs,
                     const SignalUsageHotspotTrackPosition& rhs) {
                      if (lhs.line != rhs.line)
                          return lhs.line < rhs.line;
                      if (lhs.column != rhs.column)
                          return lhs.column < rhs.column;
                      return lhs.itemIndex < rhs.itemIndex;
                  });
    }
    std::sort(report.trackLanes.begin(),
              report.trackLanes.end(),
              [](const SignalUsageHotspotTrackLane& lhs,
                 const SignalUsageHotspotTrackLane& rhs) {
                  if (lhs.moduleName != rhs.moduleName)
                      return lhs.moduleName < rhs.moduleName;
                  return lhs.fileName < rhs.fileName;
              });
}
}

SignalUsageHotspotService* SignalUsageHotspotService::getInstance()
{
    if (!instance)
        instance = std::make_unique<SignalUsageHotspotService>();
    return instance.get();
}

SignalUsageHotspotService::SignalUsageHotspotService(
    SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

SignalUsageHotspotService::~SignalUsageHotspotService() = default;

void SignalUsageHotspotService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

SignalUsageHotspotReport SignalUsageHotspotService::buildSignalUsageHotspot(
    const SignalUsageHotspotQuery& query) const
{
    SignalUsageHotspotReport report;
    const SemanticSymbolRecord signal =
        resolveSignal(semanticIndex(), query, &report.notFoundReason);
    if (signal.localHandle < 0) {
        report.notFoundReasonDisplayName =
            notFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    const QString subjectAccessPath = subjectAccessPathForQuery(query, signal);
    SemanticSymbolRecord declaration = signal;
    if (!subjectAccessPath.isEmpty()) {
        const SemanticSymbolRecord memberRecord =
            memberRecordForAccessPath(semanticIndex(), signal, subjectAccessPath);
        if (memberRecord.isValid()) {
            declaration = memberRecord;
            declaration.name = subjectAccessPath;
        } else {
            declaration.name = subjectAccessPath;
            declaration.declarationKind =
                SymbolTaxonomy::DeclarationKind::StructMember;
            declaration.collectorKind =
                SymbolTaxonomy::CollectorKind::StructMember;
        }
    }

    report.found = true;
    report.notFoundReason = NotFoundReason::None;
    report.declarationSymbolRecord = declaration;
    report.declarationStableKey = declaration.stableKey;
    report.declarationCodeLink = codeLinkForRecord(declaration);
    report.declarationDisplayName = displayNameForRecord(declaration);
    report.declarationTypeDisplayName = typeDisplayNameForRecord(declaration);
    report.declarationFileDisplayName = report.declarationCodeLink.fileDisplayName;
    report.declarationLineDisplayName = report.declarationCodeLink.lineDisplayName;

    QSet<QString> seen;
    auto appendDirection = [&](bool outgoing) {
        const QList<SemanticRelationshipResult> relationships =
            semanticIndex()->getRelationshipResults(signal.stableKey, outgoing);
        for (const SemanticRelationshipResult& relationship : relationships) {
            const SemanticSymbolRecord peer =
                outgoing ? relationship.toSymbolRecord
                         : relationship.fromSymbolRecord;
            if (isEnumValueRecord(peer))
                continue;
            if (!relationshipMatchesSubjectAccessPath(relationship,
                                                      outgoing,
                                                      subjectAccessPath)) {
                continue;
            }
            const QString key =
                itemKey(relationship, outgoing, subjectAccessPath);
            if (seen.contains(key))
                continue;
            seen.insert(key);
            report.items.append(hotspotItemForRelationship(signal,
                                                           subjectAccessPath,
                                                           relationship,
                                                           outgoing));
        }
    };
    appendDirection(false);
    appendDirection(true);

    std::sort(report.items.begin(),
              report.items.end(),
              [](const SignalUsageHotspotItem& lhs,
                 const SignalUsageHotspotItem& rhs) {
                  if (lhs.fileName != rhs.fileName)
                      return lhs.fileName < rhs.fileName;
                  if (lhs.line != rhs.line)
                      return lhs.line < rhs.line;
                  if (lhs.column != rhs.column)
                      return lhs.column < rhs.column;
                  if (lhs.moduleName != rhs.moduleName)
                      return lhs.moduleName < rhs.moduleName;
                  return roleSortIndex(lhs.role) < roleSortIndex(rhs.role);
              });

    buildSummaries(report, semanticIndex());
    return report;
}

SemanticIndex* SignalUsageHotspotService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

QString SignalUsageHotspotService::roleDisplayName(Role role)
{
    switch (role) {
    case Role::Write:
        return QStringLiteral("Write");
    case Role::Read:
        return QStringLiteral("Read");
    case Role::Port:
        return QStringLiteral("Port");
    case Role::Condition:
        return QStringLiteral("Condition");
    case Role::Case:
        return QStringLiteral("Case");
    case Role::Timing:
        return QStringLiteral("Timing");
    case Role::Unknown:
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QString SignalUsageHotspotService::notFoundReasonDisplayName(
    NotFoundReason reason)
{
    switch (reason) {
    case NotFoundReason::None:
        return QString();
    case NotFoundReason::EmptySignalName:
        return QStringLiteral("empty signal name");
    case NotFoundReason::NoMatchingSignal:
        return QStringLiteral("no matching signal");
    case NotFoundReason::UnsupportedSymbolKind:
        return QStringLiteral("unsupported symbol kind");
    }
    return QStringLiteral("signal usage hotspot unavailable");
}
