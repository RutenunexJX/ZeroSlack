#include "clockresetdomainservice.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QSet>
#include <algorithm>

namespace {
bool stableKeyMatchesSymbol(const SymbolStableKey& key,
                            const sym_list::SymbolInfo& symbol)
{
    return key.isValid() && symbolStableKeyForSymbol(symbol) == key;
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

QString displayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback,
    const QString& defaultName)
{
    if (!record.name.isEmpty())
        return record.name;
    if (!fallback.symbolName.isEmpty())
        return fallback.symbolName;
    return defaultName;
}

QString sourceRoleDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(fallback);
    if (record.isValid())
        metadata.sourceRole = record.sourceRole;
    return SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
}

QString ownerNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;
    return semanticSymbolRecordForSymbol(fallback).owner.name;
}

QString recordFileName(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    return record.location.fileName.isEmpty()
        ? fallback.fileName
        : record.location.fileName;
}

int recordStartLine(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    return record.location.startLine > 0
        ? record.location.startLine
        : fallback.startLine;
}

int recordStartColumn(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    return record.location.startColumn > 0
        ? record.location.startColumn
        : fallback.startColumn;
}
}

std::unique_ptr<ClockResetDomainService> ClockResetDomainService::instance = nullptr;

ClockResetDomainService* ClockResetDomainService::getInstance()
{
    if (!instance)
        instance = std::make_unique<ClockResetDomainService>();
    return instance.get();
}

ClockResetDomainService::ClockResetDomainService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

ClockResetDomainService::~ClockResetDomainService() = default;

void ClockResetDomainService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

ClockResetDomainReport ClockResetDomainService::buildClockResetDomainMap(
    const ClockResetDomainQuery& query) const
{
    ClockResetDomainReport report;
    report.clockGroupDisplayName =
        groupDisplayName(SymbolRelationshipEngine::CLOCKS);
    report.resetGroupDisplayName =
        groupDisplayName(SymbolRelationshipEngine::RESETS);
    report.evidenceGroupDisplayName = QStringLiteral("Domain Evidence");
    report.ambiguityGroupDisplayName = QStringLiteral("Ambiguity");
    report.unmappedGroupDisplayName = QStringLiteral("Unmapped Timing Signals");
    if (!validateQuery(query, &report.notFoundReason)) {
        report.notFoundReasonDisplayName =
            notFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    report.clockDomains = buildDomains(
        SymbolRelationshipEngine::CLOCKS,
        query,
        &report.clockRelationshipCount);
    report.resetDomains = buildDomains(
        SymbolRelationshipEngine::RESETS,
        query,
        &report.resetRelationshipCount);
    report.evidenceRows = evidenceRows(report.clockDomains, report.resetDomains);
    report.ambiguityRows = ambiguityRows(report.clockDomains, report.resetDomains);
    report.unmappedRows = unmappedTimingRows(query);
    report.found = !report.clockDomains.isEmpty()
        || !report.resetDomains.isEmpty()
        || !report.unmappedRows.isEmpty();
    if (!report.found) {
        report.notFoundReason = ClockResetDomainNotFoundReason::NoTimingDomains;
        report.notFoundReasonDisplayName =
            notFoundReasonDisplayName(report.notFoundReason);
    } else {
        report.notFoundReason = ClockResetDomainNotFoundReason::None;
    }
    return report;
}

SemanticIndex* ClockResetDomainService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

bool ClockResetDomainService::validateQuery(
    const ClockResetDomainQuery& query,
    ClockResetDomainNotFoundReason* reason) const
{
    if (reason)
        *reason = ClockResetDomainNotFoundReason::None;

    if (query.moduleStableKey.isValid()) {
        const sym_list::SymbolInfo symbol =
            semanticIndex()->getSymbolByStableKey(query.moduleStableKey);
        if (symbol.symbolId < 0) {
            if (reason)
                *reason = ClockResetDomainNotFoundReason::NoMatchingModule;
            return false;
        }
        if (!SymbolTaxonomy::isModuleDeclaration(
                SymbolTaxonomy::semanticMetadata(symbol))) {
            if (reason)
                *reason = ClockResetDomainNotFoundReason::UnsupportedSymbolKind;
            return false;
        }
        return true;
    }

    if (query.moduleSymbolId >= 0) {
        const sym_list::SymbolInfo symbol =
            semanticIndex()->getSymbolById(query.moduleSymbolId);
        if (symbol.symbolId < 0) {
            if (reason)
                *reason = ClockResetDomainNotFoundReason::NoMatchingModule;
            return false;
        }
        if (!SymbolTaxonomy::isModuleDeclaration(
                SymbolTaxonomy::semanticMetadata(symbol))) {
            if (reason)
                *reason = ClockResetDomainNotFoundReason::UnsupportedSymbolKind;
            return false;
        }
        return true;
    }

    if (query.moduleName.isEmpty())
        return true;

    SemanticDefinitionQuery definitionQuery;
    definitionQuery.symbolName = query.moduleName;
    definitionQuery.fileName = query.fileName;
    const SemanticDefinitionResult definition =
        semanticIndex()->resolveDefinition(definitionQuery);
    if (!definition.found) {
        if (reason)
            *reason = ClockResetDomainNotFoundReason::NoMatchingModule;
        return false;
    }
    if (!SymbolTaxonomy::isModuleDeclaration(
            SymbolTaxonomy::semanticMetadata(definition.symbol))) {
        if (reason)
            *reason = ClockResetDomainNotFoundReason::UnsupportedSymbolKind;
        return false;
    }
    return true;
}

QList<ClockResetDomainEntry> ClockResetDomainService::buildDomains(
    SymbolRelationshipEngine::RelationType type,
    const ClockResetDomainQuery& query,
    int* relationshipCount) const
{
    QList<ClockResetDomainEntry> entries;
    QMap<int, int> entryBySignalId;
    QSet<QString> seenRelationships;
    int count = 0;

    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const QList<SemanticRelationshipResult> relationships =
            semanticIndex()->getRelationshipResults(symbol.symbolId, true);
        for (const SemanticRelationshipResult& relationship : relationships) {
            if (relationship.relationship.type != type)
                continue;
            if (!acceptsRelationship(relationship, query))
                continue;

            const QString relationshipKey =
                QStringLiteral("%1:%2:%3")
                    .arg(relationship.relationship.fromId)
                    .arg(relationship.relationship.toId)
                    .arg(static_cast<int>(relationship.relationship.type));
            if (seenRelationships.contains(relationshipKey))
                continue;
            seenRelationships.insert(relationshipKey);

            const int signalId = relationship.fromSymbol.symbolId;
            if (!entryBySignalId.contains(signalId)) {
                ClockResetDomainEntry entry;
                entry.domainSignal = relationship.fromSymbol;
                entry.domainSignalRecord =
                    semanticSymbolRecordForSymbol(entry.domainSignal);
                entry.domainSignalStableKey =
                    entry.domainSignalRecord.stableKey.isValid()
                        ? entry.domainSignalRecord.stableKey
                        : relationship.fromStableKey;
                entry.domainSignalCodeLink =
                    codeLinkForRecord(entry.domainSignalRecord,
                                      entry.domainSignal);
                entryBySignalId.insert(signalId, entries.size());
                entries.append(entry);
            }

            ClockResetDomainMember member;
            member.moduleSymbol = relationship.toSymbol;
            member.domainSignalRecord =
                entries[entryBySignalId.value(signalId)].domainSignalRecord;
            member.moduleSymbolRecord =
                semanticSymbolRecordForSymbol(member.moduleSymbol);
            member.domainSignalStableKey =
                member.domainSignalRecord.stableKey.isValid()
                    ? member.domainSignalRecord.stableKey
                    : relationship.fromStableKey;
            member.moduleStableKey =
                member.moduleSymbolRecord.stableKey.isValid()
                    ? member.moduleSymbolRecord.stableKey
                    : relationship.toStableKey;
            member.moduleCodeLink =
                codeLinkForRecord(member.moduleSymbolRecord,
                                  member.moduleSymbol);
            member.relationship = relationship;
            member.provenance = relationship.provenance;
            member.confidence = relationship.confidence;
            member.evidenceText = relationship.evidenceText;
            member.sectionDisplayName = QStringLiteral("Module");
            member.detailDisplayName = memberDetailDisplayName(type);
            entries[entryBySignalId.value(signalId)].modules.append(member);
            ++count;
        }
    }

    for (ClockResetDomainEntry& entry : entries) {
        sortMembers(entry.modules);
        fillEntryDisplayMetadata(entry, type);
    }
    sortEntries(entries);

    if (relationshipCount)
        *relationshipCount = count;
    return entries;
}

QList<ClockResetDomainEvidenceRow> ClockResetDomainService::unmappedTimingRows(
    const ClockResetDomainQuery& query) const
{
    QList<ClockResetDomainEvidenceRow> rows;
    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        SymbolRelationshipEngine::RelationType type = SymbolRelationshipEngine::CLOCKS;
        if (!isTimingCandidate(symbol, &type))
            continue;

        const sym_list::SymbolInfo moduleSymbol =
            moduleForCandidate(symbol, symbols);
        if (!acceptsCandidate(symbol, query, moduleSymbol))
            continue;
        if (hasMappedTimingRelationship(semanticIndex(), symbol, type, query))
            continue;

        ClockResetDomainEvidenceRow row;
        row.domainSignal = symbol;
        row.moduleSymbol = moduleSymbol;
        row.domainSignalRecord = semanticSymbolRecordForSymbol(symbol);
        row.moduleSymbolRecord = semanticSymbolRecordForSymbol(moduleSymbol);
        row.domainSignalStableKey = row.domainSignalRecord.stableKey.isValid()
            ? row.domainSignalRecord.stableKey
            : symbolStableKeyForSymbol(symbol);
        row.moduleStableKey = row.moduleSymbolRecord.stableKey.isValid()
            ? row.moduleSymbolRecord.stableKey
            : symbolStableKeyForSymbol(moduleSymbol);
        row.signalCodeLink = codeLinkForRecord(row.domainSignalRecord, symbol);
        row.moduleCodeLink =
            codeLinkForRecord(row.moduleSymbolRecord, moduleSymbol);
        row.relationshipType = type;
        row.provenance = RelationshipProvenance::FeatureGenerated;
        row.confidence = 100;
        row.evidenceText = QStringLiteral("timing-name candidate without mapped relationship");
        row.sectionDisplayName = type == SymbolRelationshipEngine::CLOCKS
            ? QStringLiteral("Unmapped Clock")
            : QStringLiteral("Unmapped Reset");
        row.signalDisplayName =
            displayNameForRecord(row.domainSignalRecord,
                                 symbol,
                                 QStringLiteral("<unnamed>"));
        const QString ownerName =
            ownerNameForRecord(row.domainSignalRecord, symbol);
        row.moduleDisplayName = row.moduleSymbolRecord.name.isEmpty()
            ? ownerName
            : displayNameForRecord(row.moduleSymbolRecord,
                                   moduleSymbol,
                                   ownerName);
        if (row.moduleDisplayName.isEmpty())
            row.moduleDisplayName = QStringLiteral("<unknown module>");
        row.relationshipTypeDisplayName = relationshipTypeDisplayName(type);
        row.provenanceDisplayName = provenanceDisplayName(row.provenance);
        row.confidenceDisplayName = confidenceDisplayName(row.confidence);
        row.evidenceDisplayName = evidenceDisplayName(row.evidenceText);
        row.categoryDisplayName = unmappedCategoryDisplayName();
        row.evidenceReasonDisplayName = QStringLiteral("missing relationship");
        row.detailDisplayName =
            unmappedDetailDisplayName(row.signalDisplayName, type);
        row.sourceRoleDisplayName =
            sourceRoleDisplayNameForRecord(row.domainSignalRecord, symbol);
        rows.append(row);
    }

    std::sort(rows.begin(),
              rows.end(),
              [](const ClockResetDomainEvidenceRow& lhs,
                 const ClockResetDomainEvidenceRow& rhs) {
                  if (lhs.moduleDisplayName != rhs.moduleDisplayName)
                      return lhs.moduleDisplayName < rhs.moduleDisplayName;
                  if (lhs.relationshipType != rhs.relationshipType)
                      return lhs.relationshipType < rhs.relationshipType;
                  if (lhs.signalCodeLink.fileName != rhs.signalCodeLink.fileName)
                      return lhs.signalCodeLink.fileName < rhs.signalCodeLink.fileName;
                  if (lhs.signalCodeLink.line != rhs.signalCodeLink.line)
                      return lhs.signalCodeLink.line < rhs.signalCodeLink.line;
                  if (lhs.signalDisplayName != rhs.signalDisplayName)
                      return lhs.signalDisplayName < rhs.signalDisplayName;
                  return lhs.domainSignal.symbolId < rhs.domainSignal.symbolId;
              });
    return rows;
}

bool ClockResetDomainService::acceptsRelationship(
    const SemanticRelationshipResult& relationship,
    const ClockResetDomainQuery& query)
{
    if (relationship.fromSymbol.symbolId < 0 || relationship.toSymbol.symbolId < 0)
        return false;
    if (!SymbolTaxonomy::isModuleDeclaration(
            SymbolTaxonomy::semanticMetadata(relationship.toSymbol))) {
        return false;
    }
    if (query.moduleStableKey.isValid()) {
        const SymbolStableKey relationshipModuleKey =
            relationship.toStableKey.isValid()
                ? relationship.toStableKey
                : symbolStableKeyForSymbol(relationship.toSymbol);
        return relationshipModuleKey == query.moduleStableKey;
    }
    if (query.moduleSymbolId >= 0
        && relationship.toSymbol.symbolId != query.moduleSymbolId) {
        return false;
    }
    if (!query.moduleName.isEmpty()
        && relationship.toSymbol.symbolName != query.moduleName) {
        return false;
    }
    if (!query.fileName.isEmpty()
        && normalizedFileName(relationship.toSymbol.fileName)
            != normalizedFileName(query.fileName)) {
        return false;
    }
    return true;
}

bool ClockResetDomainService::acceptsCandidate(
    const sym_list::SymbolInfo& symbol,
    const ClockResetDomainQuery& query,
    const sym_list::SymbolInfo& moduleSymbol)
{
    const SemanticSymbolRecord signalRecord =
        semanticSymbolRecordForSymbol(symbol);
    const SemanticSymbolRecord moduleRecord =
        semanticSymbolRecordForSymbol(moduleSymbol);
    const QString ownerName = ownerNameForRecord(signalRecord, symbol);
    if (query.moduleStableKey.isValid())
        return stableKeyMatchesSymbol(query.moduleStableKey, moduleSymbol);
    if (query.moduleSymbolId >= 0
        && moduleSymbol.symbolId != query.moduleSymbolId) {
        return false;
    }
    if (!query.moduleName.isEmpty()
        && ownerName != query.moduleName
        && moduleRecord.name != query.moduleName) {
        return false;
    }
    if (!query.fileName.isEmpty()
        && normalizedFileName(symbol.fileName)
            != normalizedFileName(query.fileName)
        && normalizedFileName(moduleSymbol.fileName)
            != normalizedFileName(query.fileName)) {
        return false;
    }
    return true;
}

bool ClockResetDomainService::isTimingCandidate(
    const sym_list::SymbolInfo& symbol,
    SymbolRelationshipEngine::RelationType* type)
{
    const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
    if (record.name.isEmpty() || record.owner.name.isEmpty())
        return false;
    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(symbol);
    if (!SymbolTaxonomy::isPortDeclaration(metadata)
        && !SymbolTaxonomy::isSignalDeclaration(metadata)) {
        return false;
    }

    const QString name = record.name.toLower();
    if (name.contains(QStringLiteral("reset"))
        || name.contains(QStringLiteral("rst"))) {
        if (type)
            *type = SymbolRelationshipEngine::RESETS;
        return true;
    }
    if (name.contains(QStringLiteral("clock"))
        || name.contains(QStringLiteral("clk"))) {
        if (type)
            *type = SymbolRelationshipEngine::CLOCKS;
        return true;
    }
    return false;
}

bool ClockResetDomainService::hasMappedTimingRelationship(
    SemanticIndex* index,
    const sym_list::SymbolInfo& symbol,
    SymbolRelationshipEngine::RelationType type,
    const ClockResetDomainQuery& query)
{
    const QList<SemanticRelationshipResult> relationships =
        index->getRelationshipResults(symbol.symbolId, true);
    for (const SemanticRelationshipResult& relationship : relationships) {
        if (relationship.relationship.type != type)
            continue;
        if (acceptsRelationship(relationship, query))
            return true;
    }
    return false;
}

sym_list::SymbolInfo ClockResetDomainService::moduleForCandidate(
    const sym_list::SymbolInfo& symbol,
    const QList<sym_list::SymbolInfo>& symbols)
{
    const SemanticSymbolRecord symbolRecord =
        semanticSymbolRecordForSymbol(symbol);
    const QString ownerName = ownerNameForRecord(symbolRecord, symbol);
    for (const sym_list::SymbolInfo& candidate : symbols) {
        const SemanticSymbolRecord candidateRecord =
            semanticSymbolRecordForSymbol(candidate);
        if (candidateRecord.name == ownerName
            && SymbolTaxonomy::isModuleDeclaration(
                SymbolTaxonomy::semanticMetadata(candidate))) {
            return candidate;
        }
    }
    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    missing.symbolName = ownerName;
    missing.fileName = symbol.fileName;
    return missing;
}

QString ClockResetDomainService::normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QString ClockResetDomainService::groupDisplayName(
    SymbolRelationshipEngine::RelationType type)
{
    return type == SymbolRelationshipEngine::CLOCKS
        ? QStringLiteral("Clock Domains")
        : QStringLiteral("Reset Domains");
}

QString ClockResetDomainService::domainSectionDisplayName(
    SymbolRelationshipEngine::RelationType type)
{
    return type == SymbolRelationshipEngine::CLOCKS
        ? QStringLiteral("Clock")
        : QStringLiteral("Reset");
}

QString ClockResetDomainService::domainDetailDisplayName(
    SymbolRelationshipEngine::RelationType type,
    int moduleCount)
{
    const QString verb = type == SymbolRelationshipEngine::CLOCKS
        ? QStringLiteral("drives")
        : QStringLiteral("resets");
    return QStringLiteral("%1 %2 modules").arg(verb).arg(moduleCount);
}

QString ClockResetDomainService::memberDetailDisplayName(
    SymbolRelationshipEngine::RelationType type)
{
    return type == SymbolRelationshipEngine::CLOCKS
        ? QStringLiteral("clocked")
        : QStringLiteral("reset");
}

QList<ClockResetDomainEvidenceRow> ClockResetDomainService::evidenceRows(
    const QList<ClockResetDomainEntry>& clockDomains,
    const QList<ClockResetDomainEntry>& resetDomains)
{
    QList<ClockResetDomainEvidenceRow> rows;
    for (const ClockResetDomainEntry& entry : clockDomains) {
        for (const ClockResetDomainMember& member : entry.modules) {
            rows.append(evidenceRow(entry,
                                    member,
                                    SymbolRelationshipEngine::CLOCKS));
        }
    }
    for (const ClockResetDomainEntry& entry : resetDomains) {
        for (const ClockResetDomainMember& member : entry.modules) {
            rows.append(evidenceRow(entry,
                                    member,
                                    SymbolRelationshipEngine::RESETS));
        }
    }
    return rows;
}

QList<ClockResetDomainEvidenceRow> ClockResetDomainService::ambiguityRows(
    const QList<ClockResetDomainEntry>& clockDomains,
    const QList<ClockResetDomainEntry>& resetDomains)
{
    auto collectAmbiguityRows =
        [](const QList<ClockResetDomainEntry>& domains,
           SymbolRelationshipEngine::RelationType type) {
            QList<ClockResetDomainEvidenceRow> rows;
            QHash<int, QList<ClockResetDomainEvidenceRow>> rowsByModuleId;
            QHash<int, QSet<int>> domainIdsByModuleId;
            for (const ClockResetDomainEntry& entry : domains) {
                for (const ClockResetDomainMember& member : entry.modules) {
                    const int moduleId = member.moduleSymbol.symbolId;
                    if (moduleId < 0)
                        continue;
                    rowsByModuleId[moduleId].append(evidenceRow(entry, member, type));
                    domainIdsByModuleId[moduleId].insert(entry.domainSignal.symbolId);
                }
            }

            for (auto it = rowsByModuleId.constBegin();
                 it != rowsByModuleId.constEnd();
                 ++it) {
                const int domainCount = domainIdsByModuleId.value(it.key()).size();
                if (domainCount <= 1)
                    continue;
                for (ClockResetDomainEvidenceRow row : it.value()) {
                    row.sectionDisplayName = type == SymbolRelationshipEngine::CLOCKS
                        ? QStringLiteral("Multiple Clocks")
                        : QStringLiteral("Multiple Resets");
                    row.categoryDisplayName = ambiguityCategoryDisplayName();
                    row.evidenceReasonDisplayName =
                        QStringLiteral("ambiguous domain membership");
                    row.detailDisplayName =
                        ambiguityDetailDisplayName(row.moduleDisplayName,
                                                   type,
                                                   domainCount);
                    rows.append(row);
                }
            }
            return rows;
        };

    QList<ClockResetDomainEvidenceRow> rows =
        collectAmbiguityRows(clockDomains, SymbolRelationshipEngine::CLOCKS);
    rows.append(collectAmbiguityRows(resetDomains, SymbolRelationshipEngine::RESETS));
    return rows;
}

ClockResetDomainEvidenceRow ClockResetDomainService::evidenceRow(
    const ClockResetDomainEntry& entry,
    const ClockResetDomainMember& member,
    SymbolRelationshipEngine::RelationType type)
{
    ClockResetDomainEvidenceRow row;
    row.domainSignal = entry.domainSignal;
    row.moduleSymbol = member.moduleSymbol;
    row.domainSignalRecord = entry.domainSignalRecord;
    row.moduleSymbolRecord = member.moduleSymbolRecord;
    row.domainSignalStableKey = entry.domainSignalStableKey;
    row.moduleStableKey = member.moduleStableKey;
    row.signalCodeLink = entry.domainSignalCodeLink;
    row.moduleCodeLink = member.moduleCodeLink;
    row.relationshipType = type;
    row.provenance = member.provenance;
    row.confidence = member.confidence;
    row.evidenceText = member.evidenceText;
    row.sectionDisplayName = domainSectionDisplayName(type);
    row.signalDisplayName =
        displayNameForRecord(row.domainSignalRecord,
                             entry.domainSignal,
                             QStringLiteral("<unnamed>"));
    row.moduleDisplayName =
        displayNameForRecord(row.moduleSymbolRecord,
                             member.moduleSymbol,
                             QStringLiteral("<unnamed>"));
    row.relationshipTypeDisplayName = relationshipTypeDisplayName(type);
    row.provenanceDisplayName = provenanceDisplayName(row.provenance);
    row.confidenceDisplayName = confidenceDisplayName(row.confidence);
    row.evidenceDisplayName = evidenceDisplayName(row.evidenceText);
    row.categoryDisplayName = evidenceCategoryDisplayName();
    row.evidenceReasonDisplayName = QStringLiteral("relationship");
    row.detailDisplayName =
        evidenceDetailDisplayName(row.signalDisplayName,
                                  row.moduleDisplayName,
                                  type);
    row.sourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(row.moduleSymbolRecord,
                                       member.moduleSymbol);
    return row;
}

QString ClockResetDomainService::evidenceDetailDisplayName(
    const QString& signalName,
    const QString& moduleName,
    SymbolRelationshipEngine::RelationType type)
{
    const QString verb = type == SymbolRelationshipEngine::CLOCKS
        ? QStringLiteral("clocks")
        : QStringLiteral("resets");
    return QStringLiteral("%1 %2 %3").arg(signalName, verb, moduleName);
}

QString ClockResetDomainService::evidenceCategoryDisplayName()
{
    return QStringLiteral("mapped domain");
}

QString ClockResetDomainService::relationshipTypeDisplayName(
    SymbolRelationshipEngine::RelationType type)
{
    return type == SymbolRelationshipEngine::CLOCKS
        ? QStringLiteral("Clock")
        : QStringLiteral("Reset");
}

QString ClockResetDomainService::notFoundReasonDisplayName(
    ClockResetDomainNotFoundReason reason)
{
    switch (reason) {
    case ClockResetDomainNotFoundReason::None:
        return QString();
    case ClockResetDomainNotFoundReason::NoMatchingModule:
        return QStringLiteral("no matching module");
    case ClockResetDomainNotFoundReason::UnsupportedSymbolKind:
        return QStringLiteral("unsupported symbol kind");
    case ClockResetDomainNotFoundReason::NoTimingDomains:
        return QStringLiteral("no timing domains");
    }
    return QStringLiteral("clock reset domain map unavailable");
}

QString ClockResetDomainService::provenanceDisplayName(
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

QString ClockResetDomainService::confidenceDisplayName(int confidence)
{
    return confidence > 0
        ? QStringLiteral("%1%").arg(confidence)
        : QStringLiteral("unknown");
}

QString ClockResetDomainService::evidenceDisplayName(
    const QString& evidenceText)
{
    return evidenceText.isEmpty()
        ? QStringLiteral("no evidence detail")
        : evidenceText;
}

QString ClockResetDomainService::ambiguityCategoryDisplayName()
{
    return QStringLiteral("ambiguous domain");
}

QString ClockResetDomainService::ambiguityDetailDisplayName(
    const QString& moduleName,
    SymbolRelationshipEngine::RelationType type,
    int domainCount)
{
    const QString noun = type == SymbolRelationshipEngine::CLOCKS
        ? QStringLiteral("clock domains")
        : QStringLiteral("reset domains");
    return QStringLiteral("%1 has %2 %3").arg(moduleName).arg(domainCount).arg(noun);
}

QString ClockResetDomainService::unmappedCategoryDisplayName()
{
    return QStringLiteral("unmapped timing");
}

QString ClockResetDomainService::unmappedDetailDisplayName(
    const QString& signalName,
    SymbolRelationshipEngine::RelationType type)
{
    const QString noun = type == SymbolRelationshipEngine::CLOCKS
        ? QStringLiteral("clock domain")
        : QStringLiteral("reset domain");
    return QStringLiteral("%1 has no %2 relationship").arg(signalName, noun);
}

void ClockResetDomainService::fillEntryDisplayMetadata(
    ClockResetDomainEntry& entry,
    SymbolRelationshipEngine::RelationType type)
{
    entry.sectionDisplayName = domainSectionDisplayName(type);
    entry.detailDisplayName = domainDetailDisplayName(type, entry.modules.size());
    for (ClockResetDomainMember& member : entry.modules) {
        if (member.sectionDisplayName.isEmpty())
            member.sectionDisplayName = QStringLiteral("Module");
        member.moduleDisplayName =
            displayNameForRecord(member.moduleSymbolRecord,
                                 member.moduleSymbol,
                                 QStringLiteral("<unnamed>"));
        member.relationshipTypeDisplayName = relationshipTypeDisplayName(type);
        member.provenanceDisplayName = provenanceDisplayName(member.provenance);
        member.confidenceDisplayName = confidenceDisplayName(member.confidence);
        member.evidenceDisplayName = evidenceDisplayName(member.evidenceText);
        if (member.detailDisplayName.isEmpty())
            member.detailDisplayName = memberDetailDisplayName(type);
        member.sourceRoleDisplayName =
            sourceRoleDisplayNameForRecord(member.moduleSymbolRecord,
                                           member.moduleSymbol);
    }
}

void ClockResetDomainService::sortEntries(QList<ClockResetDomainEntry>& entries)
{
    std::sort(entries.begin(), entries.end(),
              [](const ClockResetDomainEntry& lhs,
                 const ClockResetDomainEntry& rhs) {
                  const sym_list::SymbolInfo& left = lhs.domainSignal;
                  const sym_list::SymbolInfo& right = rhs.domainSignal;
                  const QString leftFileName =
                      recordFileName(lhs.domainSignalRecord, left);
                  const QString rightFileName =
                      recordFileName(rhs.domainSignalRecord, right);
                  if (leftFileName != rightFileName)
                      return leftFileName < rightFileName;
                  const int leftLine =
                      recordStartLine(lhs.domainSignalRecord, left);
                  const int rightLine =
                      recordStartLine(rhs.domainSignalRecord, right);
                  if (leftLine != rightLine)
                      return leftLine < rightLine;
                  const int leftColumn =
                      recordStartColumn(lhs.domainSignalRecord, left);
                  const int rightColumn =
                      recordStartColumn(rhs.domainSignalRecord, right);
                  if (leftColumn != rightColumn)
                      return leftColumn < rightColumn;
                  const QString leftName =
                      displayNameForRecord(lhs.domainSignalRecord, left, QString());
                  const QString rightName =
                      displayNameForRecord(rhs.domainSignalRecord, right, QString());
                  if (leftName != rightName)
                      return leftName < rightName;
                  return left.symbolId < right.symbolId;
              });
}

void ClockResetDomainService::sortMembers(QList<ClockResetDomainMember>& members)
{
    std::sort(members.begin(), members.end(),
              [](const ClockResetDomainMember& lhs,
                 const ClockResetDomainMember& rhs) {
                  const sym_list::SymbolInfo& left = lhs.moduleSymbol;
                  const sym_list::SymbolInfo& right = rhs.moduleSymbol;
                  const QString leftFileName =
                      recordFileName(lhs.moduleSymbolRecord, left);
                  const QString rightFileName =
                      recordFileName(rhs.moduleSymbolRecord, right);
                  if (leftFileName != rightFileName)
                      return leftFileName < rightFileName;
                  const int leftLine =
                      recordStartLine(lhs.moduleSymbolRecord, left);
                  const int rightLine =
                      recordStartLine(rhs.moduleSymbolRecord, right);
                  if (leftLine != rightLine)
                      return leftLine < rightLine;
                  const int leftColumn =
                      recordStartColumn(lhs.moduleSymbolRecord, left);
                  const int rightColumn =
                      recordStartColumn(rhs.moduleSymbolRecord, right);
                  if (leftColumn != rightColumn)
                      return leftColumn < rightColumn;
                  const QString leftName =
                      displayNameForRecord(lhs.moduleSymbolRecord, left, QString());
                  const QString rightName =
                      displayNameForRecord(rhs.moduleSymbolRecord, right, QString());
                  if (leftName != rightName)
                      return leftName < rightName;
                  return left.symbolId < right.symbolId;
              });
}
