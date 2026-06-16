#include "clockresetdomainservice.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QSet>
#include <algorithm>

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
    return report;
}

SemanticIndex* ClockResetDomainService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
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
                entry.domainSignalCodeLink =
                    RtlInsightLink::fromSymbol(relationship.fromSymbol);
                entryBySignalId.insert(signalId, entries.size());
                entries.append(entry);
            }

            ClockResetDomainMember member;
            member.moduleSymbol = relationship.toSymbol;
            member.moduleCodeLink =
                RtlInsightLink::fromSymbol(relationship.toSymbol);
            member.relationship = relationship;
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
        row.signalCodeLink = RtlInsightLink::fromSymbol(symbol);
        row.moduleCodeLink = RtlInsightLink::fromSymbol(moduleSymbol);
        row.relationshipType = type;
        row.sectionDisplayName = type == SymbolRelationshipEngine::CLOCKS
            ? QStringLiteral("Unmapped Clock")
            : QStringLiteral("Unmapped Reset");
        row.signalDisplayName = symbol.symbolName.isEmpty()
            ? QStringLiteral("<unnamed>")
            : symbol.symbolName;
        row.moduleDisplayName = moduleSymbol.symbolName.isEmpty()
            ? symbol.moduleScope
            : moduleSymbol.symbolName;
        if (row.moduleDisplayName.isEmpty())
            row.moduleDisplayName = QStringLiteral("<unknown module>");
        row.relationshipTypeDisplayName = relationshipTypeDisplayName(type);
        row.categoryDisplayName = unmappedCategoryDisplayName();
        row.evidenceReasonDisplayName = QStringLiteral("missing relationship");
        row.detailDisplayName =
            unmappedDetailDisplayName(row.signalDisplayName, type);
        row.sourceRoleDisplayName =
            sourceRoleDisplayName(SymbolTaxonomy::sourceRoleForFileName(symbol.fileName));
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
    if (!SymbolTaxonomy::isModuleDeclaration(relationship.toSymbol.symbolType))
        return false;
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
    if (query.moduleSymbolId >= 0
        && moduleSymbol.symbolId != query.moduleSymbolId) {
        return false;
    }
    if (!query.moduleName.isEmpty()
        && symbol.moduleScope != query.moduleName
        && moduleSymbol.symbolName != query.moduleName) {
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
    if (symbol.symbolName.isEmpty() || symbol.moduleScope.isEmpty())
        return false;
    if (!SymbolTaxonomy::isPortDeclaration(symbol.symbolType)
        && !SymbolTaxonomy::isSignalDeclaration(symbol.symbolType)) {
        return false;
    }

    const QString name = symbol.symbolName.toLower();
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
    for (const sym_list::SymbolInfo& candidate : symbols) {
        if (candidate.symbolName == symbol.moduleScope
            && SymbolTaxonomy::isModuleDeclaration(candidate.symbolType)) {
            return candidate;
        }
    }
    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    missing.symbolName = symbol.moduleScope;
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
    row.signalCodeLink = entry.domainSignalCodeLink;
    row.moduleCodeLink = member.moduleCodeLink;
    row.relationshipType = type;
    row.sectionDisplayName = domainSectionDisplayName(type);
    row.signalDisplayName = entry.domainSignal.symbolName.isEmpty()
        ? QStringLiteral("<unnamed>")
        : entry.domainSignal.symbolName;
    row.moduleDisplayName = member.moduleSymbol.symbolName.isEmpty()
        ? QStringLiteral("<unnamed>")
        : member.moduleSymbol.symbolName;
    row.relationshipTypeDisplayName = relationshipTypeDisplayName(type);
    row.categoryDisplayName = evidenceCategoryDisplayName();
    row.evidenceReasonDisplayName = QStringLiteral("relationship");
    row.detailDisplayName =
        evidenceDetailDisplayName(row.signalDisplayName,
                                  row.moduleDisplayName,
                                  type);
    row.sourceRoleDisplayName =
        sourceRoleDisplayName(
            SymbolTaxonomy::sourceRoleForFileName(member.moduleSymbol.fileName));
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

QString ClockResetDomainService::sourceRoleDisplayName(SymbolTaxonomy::SourceRole role)
{
    switch (role) {
    case SymbolTaxonomy::SourceRole::DesignSource:
        return QStringLiteral("design source");
    case SymbolTaxonomy::SourceRole::Header:
        return QStringLiteral("header");
    case SymbolTaxonomy::SourceRole::Unknown:
    default:
        return QStringLiteral("source");
    }
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
        member.moduleDisplayName = member.moduleSymbol.symbolName.isEmpty()
            ? QStringLiteral("<unnamed>")
            : member.moduleSymbol.symbolName;
        member.relationshipTypeDisplayName = relationshipTypeDisplayName(type);
        if (member.detailDisplayName.isEmpty())
            member.detailDisplayName = memberDetailDisplayName(type);
        member.sourceRoleDisplayName =
            sourceRoleDisplayName(
                SymbolTaxonomy::sourceRoleForFileName(member.moduleSymbol.fileName));
    }
}

void ClockResetDomainService::sortEntries(QList<ClockResetDomainEntry>& entries)
{
    std::sort(entries.begin(), entries.end(),
              [](const ClockResetDomainEntry& lhs,
                 const ClockResetDomainEntry& rhs) {
                  const sym_list::SymbolInfo& left = lhs.domainSignal;
                  const sym_list::SymbolInfo& right = rhs.domainSignal;
                  if (left.fileName != right.fileName)
                      return left.fileName < right.fileName;
                  if (left.startLine != right.startLine)
                      return left.startLine < right.startLine;
                  if (left.startColumn != right.startColumn)
                      return left.startColumn < right.startColumn;
                  if (left.symbolName != right.symbolName)
                      return left.symbolName < right.symbolName;
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
                  if (left.fileName != right.fileName)
                      return left.fileName < right.fileName;
                  if (left.startLine != right.startLine)
                      return left.startLine < right.startLine;
                  if (left.startColumn != right.startColumn)
                      return left.startColumn < right.startColumn;
                  if (left.symbolName != right.symbolName)
                      return left.symbolName < right.symbolName;
                  return left.symbolId < right.symbolId;
              });
}
