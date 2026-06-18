#include "relationshipservice.h"

#include "relationshipserviceordering.h"

#include <QDir>
#include <QFileInfo>

std::unique_ptr<RelationshipService> RelationshipService::instance = nullptr;

using relationship_service_ordering::sortRelationshipResults;

namespace {
QString normalizedRelationshipFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QString relationVerb(SymbolRelationshipEngine::RelationType type)
{
    switch (type) {
    case SymbolRelationshipEngine::CONTAINS:
        return QStringLiteral("contains");
    case SymbolRelationshipEngine::REFERENCES:
        return QStringLiteral("references");
    case SymbolRelationshipEngine::INSTANTIATES:
        return QStringLiteral("instantiates");
    case SymbolRelationshipEngine::CALLS:
        return QStringLiteral("calls");
    case SymbolRelationshipEngine::INHERITS:
        return QStringLiteral("inherits");
    case SymbolRelationshipEngine::IMPLEMENTS:
        return QStringLiteral("implements");
    case SymbolRelationshipEngine::ASSIGNS_TO:
        return QStringLiteral("drives");
    case SymbolRelationshipEngine::READS_FROM:
        return QStringLiteral("reads from");
    case SymbolRelationshipEngine::CLOCKS:
        return QStringLiteral("clocks");
    case SymbolRelationshipEngine::RESETS:
        return QStringLiteral("resets");
    case SymbolRelationshipEngine::GENERATES:
        return QStringLiteral("generates");
    case SymbolRelationshipEngine::CONSTRAINS:
        return QStringLiteral("constrains");
    }
    return QStringLiteral("relates to");
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

QString relationshipDirectionDisplayName(DirectedRelationshipResult::Direction direction)
{
    return direction == DirectedRelationshipResult::Outgoing
        ? QStringLiteral("Outgoing")
        : QStringLiteral("Incoming");
}

QString relationshipProvenanceDisplayName(RelationshipProvenance provenance)
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

QString confidenceDisplayName(int confidence)
{
    return confidence > 0
        ? QStringLiteral("%1%").arg(confidence)
        : QStringLiteral("unknown");
}

QString evidenceDisplayName(const QString& evidenceText)
{
    return evidenceText.isEmpty()
        ? QStringLiteral("no evidence detail")
        : evidenceText;
}

QString roleFor(SymbolRelationshipEngine::RelationType type, bool sourceSide)
{
    switch (type) {
    case SymbolRelationshipEngine::CONTAINS:
        return sourceSide ? QStringLiteral("container") : QStringLiteral("contained");
    case SymbolRelationshipEngine::REFERENCES:
        return sourceSide ? QStringLiteral("referencer") : QStringLiteral("referenced");
    case SymbolRelationshipEngine::INSTANTIATES:
        return sourceSide ? QStringLiteral("instantiator") : QStringLiteral("instantiated");
    case SymbolRelationshipEngine::CALLS:
        return sourceSide ? QStringLiteral("caller") : QStringLiteral("callee");
    case SymbolRelationshipEngine::INHERITS:
        return sourceSide ? QStringLiteral("derived") : QStringLiteral("base");
    case SymbolRelationshipEngine::IMPLEMENTS:
        return sourceSide ? QStringLiteral("implementation") : QStringLiteral("interface");
    case SymbolRelationshipEngine::ASSIGNS_TO:
        return sourceSide ? QStringLiteral("driver") : QStringLiteral("driven");
    case SymbolRelationshipEngine::READS_FROM:
        return sourceSide ? QStringLiteral("reader") : QStringLiteral("source");
    case SymbolRelationshipEngine::CLOCKS:
        return sourceSide ? QStringLiteral("clock") : QStringLiteral("clocked");
    case SymbolRelationshipEngine::RESETS:
        return sourceSide ? QStringLiteral("reset") : QStringLiteral("reset");
    case SymbolRelationshipEngine::GENERATES:
        return sourceSide ? QStringLiteral("generator") : QStringLiteral("generated");
    case SymbolRelationshipEngine::CONSTRAINS:
        return sourceSide ? QStringLiteral("constraint") : QStringLiteral("constrained");
    }
    return QStringLiteral("related");
}

QString symbolDisplayName(const sym_list::SymbolInfo& symbol)
{
    return symbol.symbolName.isEmpty()
        ? QStringLiteral("<unnamed>")
        : symbol.symbolName;
}

QString symbolRecordDisplayName(const SemanticSymbolRecord& record,
                                const sym_list::SymbolInfo& fallbackSymbol)
{
    return record.name.isEmpty()
        ? symbolDisplayName(fallbackSymbol)
        : record.name;
}

QString symbolRecordFileName(const SemanticSymbolRecord& record,
                             const sym_list::SymbolInfo& fallbackSymbol)
{
    return record.location.fileName.isEmpty()
        ? fallbackSymbol.fileName
        : record.location.fileName;
}

int symbolRecordLine(const SemanticSymbolRecord& record,
                     const sym_list::SymbolInfo& fallbackSymbol)
{
    return record.location.startLine > 0
        ? record.location.startLine
        : fallbackSymbol.startLine;
}

int symbolRecordColumn(const SemanticSymbolRecord& record,
                       const sym_list::SymbolInfo& fallbackSymbol)
{
    return record.location.startColumn > 0
        ? record.location.startColumn
        : fallbackSymbol.startColumn;
}

QString fileDisplayName(const QString& fileName)
{
    QString displayName = QFileInfo(fileName).fileName();
    if (displayName.isEmpty())
        displayName = fileName;
    return displayName;
}

QString lineDisplayName(int line)
{
    return QString::number(line);
}

QString reportNotFoundReasonDisplayName(
    RelationshipReportNotFoundReason reason)
{
    switch (reason) {
    case RelationshipReportNotFoundReason::None:
        return QString();
    case RelationshipReportNotFoundReason::NoSubjectSymbol:
        return QStringLiteral("no subject symbol");
    case RelationshipReportNotFoundReason::NoRelationships:
        return QStringLiteral("no relationships");
    }
    return QStringLiteral("relationship report unavailable");
}
}

RelationshipService* RelationshipService::getInstance()
{
    if (!instance)
        instance = std::make_unique<RelationshipService>();
    return instance.get();
}

RelationshipService::RelationshipService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

RelationshipService::~RelationshipService() = default;

void RelationshipService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

QList<RelationshipResult> RelationshipService::findRelationships(const RelationshipQuery& query) const
{
    const RelationshipQuery normalized = normalizedQuery(query);
    QList<RelationshipResult> result;
    const SymbolStableKey subjectStableKey = normalized.symbolStableKey.isValid()
        ? normalized.symbolStableKey
        : resolveSubjectSymbolRecord(normalized).stableKey;
    if (!subjectStableKey.isValid())
        return result;
    const QList<SemanticRelationshipResult> relationships =
        semanticIndex()->getRelationshipResults(subjectStableKey,
                                                normalized.outgoing);

    for (const SemanticRelationshipResult& rel : relationships) {
        if (!typeMatches(rel.relationship.type, normalized.types))
            continue;
        result.append(rel);
    }
    sortRelationshipResults(result, normalized.outgoing);

    return result;
}

QList<RelationshipResult> RelationshipService::findOutgoingRelationships(
    const RelationshipQuery& query) const
{
    RelationshipQuery outgoingQuery = query;
    outgoingQuery.outgoing = true;
    return findRelationships(outgoingQuery);
}

QList<RelationshipResult> RelationshipService::findIncomingRelationships(
    const RelationshipQuery& query) const
{
    RelationshipQuery incomingQuery = query;
    incomingQuery.outgoing = false;
    return findRelationships(incomingQuery);
}

RelationshipReport RelationshipService::findRelationshipReport(
    const RelationshipBrowseQuery& query) const
{
    const RelationshipBrowseQuery normalized = normalizedQuery(query);
    RelationshipReport report;
    report.subjectSymbol = resolveSubjectSymbol(normalized);
    if (report.subjectSymbol.symbolId < 0) {
        report.notFoundReason =
            RelationshipReportNotFoundReason::NoSubjectSymbol;
        report.notFoundReasonDisplayName =
            reportNotFoundReasonDisplayName(report.notFoundReason);
        return report;
    }
    report.subjectSymbolRecord = semanticSymbolRecordForSymbol(report.subjectSymbol);
    report.subjectStableKey = report.subjectSymbolRecord.stableKey.isValid()
        ? report.subjectSymbolRecord.stableKey
        : symbolStableKeyForSymbol(report.subjectSymbol);

    QMap<DirectedRelationshipResult::Direction, int> directionGroupIndexes;
    QMap<DirectedRelationshipResult::Direction,
         QMap<SymbolRelationshipEngine::RelationType, int>> typeGroupIndexes;

    auto appendRelationships = [&](const QList<RelationshipResult>& relationships,
                                   DirectedRelationshipResult::Direction direction) {
        for (const RelationshipResult& relationship : relationships) {
            DirectedRelationshipResult directed;
            directed.relationship = relationship;
            directed.direction = direction;
            directed.peerSymbol = direction == DirectedRelationshipResult::Outgoing
                ? relationship.toSymbol
                : relationship.fromSymbol;
            if (directed.peerSymbol.symbolId < 0)
                continue;
            directed.peerSymbolRecord = direction == DirectedRelationshipResult::Outgoing
                ? relationship.toSymbolRecord
                : relationship.fromSymbolRecord;
            if (!directed.peerSymbolRecord.isValid())
                directed.peerSymbolRecord =
                    semanticSymbolRecordForSymbol(directed.peerSymbol);
            directed.subjectStableKey = report.subjectStableKey;
            directed.peerStableKey = directed.peerSymbolRecord.stableKey.isValid()
                ? directed.peerSymbolRecord.stableKey
                : (direction == DirectedRelationshipResult::Outgoing
                       ? relationship.toStableKey
                       : relationship.fromStableKey);
            directed.directionDisplayName = relationshipDirectionDisplayName(direction);
            directed.typeDisplayName =
                relationshipTypeDisplayName(relationship.relationship.type);
            directed.peerSymbolDisplayName =
                symbolRecordDisplayName(directed.peerSymbolRecord,
                                        directed.peerSymbol);
            directed.peerFileDisplayName = fileDisplayName(
                symbolRecordFileName(directed.peerSymbolRecord,
                                     directed.peerSymbol));
            directed.peerLineDisplayName = lineDisplayName(
                symbolRecordLine(directed.peerSymbolRecord,
                                 directed.peerSymbol));
            directed.provenance = relationship.provenance;
            directed.confidence = relationship.confidence;
            directed.evidenceText = relationship.evidenceText;
            directed.provenanceDisplayName =
                relationshipProvenanceDisplayName(directed.provenance);
            directed.confidenceDisplayName =
                confidenceDisplayName(directed.confidence);
            directed.evidenceDisplayName =
                evidenceDisplayName(directed.evidenceText);
            const bool subjectIsSource =
                direction == DirectedRelationshipResult::Outgoing;
            directed.subjectRole =
                roleFor(relationship.relationship.type, subjectIsSource);
            directed.peerRole =
                roleFor(relationship.relationship.type, !subjectIsSource);
            const QString sourceName = subjectIsSource
                ? symbolRecordDisplayName(report.subjectSymbolRecord,
                                          report.subjectSymbol)
                : symbolRecordDisplayName(directed.peerSymbolRecord,
                                          directed.peerSymbol);
            const QString targetName = subjectIsSource
                ? symbolRecordDisplayName(directed.peerSymbolRecord,
                                          directed.peerSymbol)
                : symbolRecordDisplayName(report.subjectSymbolRecord,
                                          report.subjectSymbol);
            directed.explanation = QStringLiteral("%1 %2 %3")
                                       .arg(sourceName,
                                            relationVerb(relationship.relationship.type),
                                            targetName);
            report.relationships.append(directed);
            report.typeCounts[relationship.relationship.type]++;
            report.directionCounts[direction]++;
            report.directionTypeCounts[direction][relationship.relationship.type]++;
            if (!directionGroupIndexes.contains(direction)) {
                RelationshipDirectionGroup directionGroup;
                directionGroup.direction = direction;
                directionGroup.displayName = relationshipDirectionDisplayName(direction);
                directionGroupIndexes.insert(direction, report.directionGroups.size());
                report.directionGroups.append(directionGroup);
            }

            RelationshipDirectionGroup& directionGroup =
                report.directionGroups[directionGroupIndexes.value(direction)];
            directionGroup.count++;

            const SymbolRelationshipEngine::RelationType type =
                relationship.relationship.type;
            if (!typeGroupIndexes[direction].contains(type)) {
                RelationshipTypeGroup typeGroup;
                typeGroup.type = type;
                typeGroup.displayName = relationshipTypeDisplayName(type);
                typeGroupIndexes[direction].insert(type,
                                                   directionGroup.typeGroups.size());
                directionGroup.typeGroups.append(typeGroup);
            }

            RelationshipTypeGroup& typeGroup =
                directionGroup.typeGroups[typeGroupIndexes[direction].value(type)];
            typeGroup.relationships.append(directed);
            typeGroup.count++;
            if (direction == DirectedRelationshipResult::Outgoing)
                report.outgoingCount++;
            else
                report.incomingCount++;
        }
    };

    RelationshipQuery relationshipQuery;
    relationshipQuery.symbolStableKey = report.subjectStableKey;
    relationshipQuery.symbolName = normalized.symbolName;
    relationshipQuery.fileName = normalized.fileName;
    relationshipQuery.moduleName = normalized.moduleName;
    relationshipQuery.types = normalized.types;

    if (normalized.includeOutgoing) {
        relationshipQuery.outgoing = true;
        appendRelationships(findRelationships(relationshipQuery),
                            DirectedRelationshipResult::Outgoing);
    }
    if (normalized.includeIncoming) {
        relationshipQuery.outgoing = false;
        appendRelationships(findRelationships(relationshipQuery),
                            DirectedRelationshipResult::Incoming);
    }

    report.totalCount = report.relationships.size();
    if (report.totalCount == 0) {
        report.notFoundReason =
            RelationshipReportNotFoundReason::NoRelationships;
        report.notFoundReasonDisplayName =
            reportNotFoundReasonDisplayName(report.notFoundReason);
    } else {
        report.notFoundReason = RelationshipReportNotFoundReason::None;
    }
    return report;
}

QList<SymbolStableKey> RelationshipService::findRelatedSymbolKeys(
    const RelationshipQuery& query) const
{
    QList<SymbolStableKey> result;
    const QList<RelationshipResult> relationships = findRelationships(query);
    for (const RelationshipResult& relationship : relationships) {
        const SymbolStableKey key = query.outgoing
            ? relationship.toStableKey
            : relationship.fromStableKey;
        if (key.isValid())
            result.append(key);
    }
    return result;
}

RelationshipBrowseQuery RelationshipService::queryForPanel(
    const RelationshipPanelQueryOptions& options) const
{
    RelationshipBrowseQuery query;
    query.symbolName = options.symbolName;
    query.fileName = options.fileName;
    query.moduleName = options.moduleName;
    query.includeOutgoing = options.direction == RelationshipPanelDirection::All
        || options.direction == RelationshipPanelDirection::Outgoing;
    query.includeIncoming = options.direction == RelationshipPanelDirection::All
        || options.direction == RelationshipPanelDirection::Incoming;
    if (options.typeFilter >= 0) {
        query.types = {
            static_cast<SymbolRelationshipEngine::RelationType>(options.typeFilter)
        };
    }
    return normalizedQuery(query);
}

QList<RelationshipTypeFilterOption>
RelationshipService::relationshipPanelTypeFilterOptions()
{
    const QList<SymbolRelationshipEngine::RelationType> types = {
        SymbolRelationshipEngine::REFERENCES,
        SymbolRelationshipEngine::INSTANTIATES,
        SymbolRelationshipEngine::CALLS,
        SymbolRelationshipEngine::ASSIGNS_TO,
        SymbolRelationshipEngine::READS_FROM,
        SymbolRelationshipEngine::CLOCKS,
        SymbolRelationshipEngine::RESETS,
        SymbolRelationshipEngine::CONTAINS,
        SymbolRelationshipEngine::GENERATES,
    };
    QList<RelationshipTypeFilterOption> options;
    options.append({QStringLiteral("All Types"), -1});
    for (const SymbolRelationshipEngine::RelationType type : types)
        options.append({relationshipTypeDisplayName(type), static_cast<int>(type)});
    return options;
}

QList<RelationshipTypeFilterOption>
RelationshipService::referencePanelTypeFilterOptions()
{
    const QList<SymbolRelationshipEngine::RelationType> types = {
        SymbolRelationshipEngine::REFERENCES,
        SymbolRelationshipEngine::INSTANTIATES,
        SymbolRelationshipEngine::CALLS,
        SymbolRelationshipEngine::ASSIGNS_TO,
        SymbolRelationshipEngine::READS_FROM,
        SymbolRelationshipEngine::CLOCKS,
        SymbolRelationshipEngine::RESETS,
    };
    QList<RelationshipTypeFilterOption> options;
    options.append({QStringLiteral("All Types"), -1});
    for (const SymbolRelationshipEngine::RelationType type : types)
        options.append({relationshipTypeDisplayName(type), static_cast<int>(type)});
    return options;
}

bool RelationshipService::hasRelationship(
    const SymbolStableKey& fromStableKey,
    const SymbolStableKey& toStableKey,
    SymbolRelationshipEngine::RelationType type) const
{
    if (!fromStableKey.isValid() || !toStableKey.isValid())
        return false;

    RelationshipQuery query;
    query.symbolStableKey = fromStableKey;
    query.outgoing = true;
    query.types = {type};

    const QList<RelationshipResult> relationships = findRelationships(query);
    for (const RelationshipResult& relationship : relationships) {
        const SymbolStableKey relatedFromKey = relationship.fromStableKey.isValid()
            ? relationship.fromStableKey
            : semanticSymbolRecordForSymbol(relationship.fromSymbol).stableKey;
        const SymbolStableKey relatedToKey = relationship.toStableKey.isValid()
            ? relationship.toStableKey
            : semanticSymbolRecordForSymbol(relationship.toSymbol).stableKey;
        if (relatedFromKey == fromStableKey
            && relatedToKey == toStableKey
            && relationship.relationship.type == type) {
            return true;
        }
    }
    return false;
}

bool RelationshipService::hasNamedRelationship(
    const QString& fromSymbolName,
    const QString& toSymbolName,
    SymbolRelationshipEngine::RelationType type) const
{
    if (fromSymbolName.isEmpty() || toSymbolName.isEmpty())
        return false;

    RelationshipQuery query;
    query.symbolName = fromSymbolName;
    query.outgoing = true;
    query.types = {type};
    const QList<RelationshipResult> relationships = findRelationships(query);
    for (const RelationshipResult& relationship : relationships) {
        if (relationship.toSymbol.symbolName == toSymbolName)
            return true;
    }
    return false;
}

bool RelationshipService::hasRelationships(const RelationshipQuery& query) const
{
    return !findRelationships(query).isEmpty();
}

SemanticIndex* RelationshipService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

SemanticSymbolRecord RelationshipService::resolveSubjectSymbolRecord(
    const RelationshipQuery& query) const
{
    if (query.symbolStableKey.isValid())
        return semanticIndex()->getSymbolRecordByStableKey(query.symbolStableKey);

    if (query.symbolName.isEmpty())
        return {};

    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;

    const QList<sym_list::SymbolInfo> definitions =
        semanticIndex()->findDefinitions(query.symbolName, context);
    if (definitions.isEmpty())
        return {};
    return semanticSymbolRecordForSymbol(definitions.first());
}

SemanticSymbolRecord RelationshipService::resolveSubjectSymbolRecord(
    const RelationshipBrowseQuery& query) const
{
    if (query.symbolStableKey.isValid())
        return semanticIndex()->getSymbolRecordByStableKey(query.symbolStableKey);

    if (query.symbolName.isEmpty())
        return {};

    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;

    const QList<sym_list::SymbolInfo> definitions =
        semanticIndex()->findDefinitions(query.symbolName, context);
    if (definitions.isEmpty())
        return {};
    return semanticSymbolRecordForSymbol(definitions.first());
}

sym_list::SymbolInfo RelationshipService::resolveSubjectSymbol(
    const RelationshipQuery& query) const
{
    const SemanticSymbolRecord record = resolveSubjectSymbolRecord(query);
    if (!record.stableKey.isValid()) {
        sym_list::SymbolInfo missing;
        missing.symbolId = -1;
        return missing;
    }
    return semanticIndex()->getSymbolByStableKey(record.stableKey);
}

sym_list::SymbolInfo RelationshipService::resolveSubjectSymbol(
    const RelationshipBrowseQuery& query) const
{
    const SemanticSymbolRecord record = resolveSubjectSymbolRecord(query);
    if (!record.stableKey.isValid()) {
        sym_list::SymbolInfo missing;
        missing.symbolId = -1;
        return missing;
    }
    return semanticIndex()->getSymbolByStableKey(record.stableKey);
}

RelationshipQuery RelationshipService::normalizedQuery(const RelationshipQuery& query)
{
    RelationshipQuery normalized = query;
    normalized.fileName = normalizedRelationshipFileName(query.fileName);
    return normalized;
}

RelationshipBrowseQuery RelationshipService::normalizedQuery(
    const RelationshipBrowseQuery& query)
{
    RelationshipBrowseQuery normalized = query;
    normalized.fileName = normalizedRelationshipFileName(query.fileName);
    return normalized;
}

bool RelationshipService::typeMatches(
    SymbolRelationshipEngine::RelationType type,
    const QList<SymbolRelationshipEngine::RelationType>& allowedTypes) const
{
    return allowedTypes.isEmpty() || allowedTypes.contains(type);
}
