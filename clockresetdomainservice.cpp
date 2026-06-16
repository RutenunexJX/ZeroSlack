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
    report.found = !report.clockDomains.isEmpty() || !report.resetDomains.isEmpty();
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
                entryBySignalId.insert(signalId, entries.size());
                entries.append(entry);
            }

            ClockResetDomainMember member;
            member.moduleSymbol = relationship.toSymbol;
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
    row.relationshipType = type;
    row.sectionDisplayName = domainSectionDisplayName(type);
    row.signalDisplayName = entry.domainSignal.symbolName.isEmpty()
        ? QStringLiteral("<unnamed>")
        : entry.domainSignal.symbolName;
    row.moduleDisplayName = member.moduleSymbol.symbolName.isEmpty()
        ? QStringLiteral("<unnamed>")
        : member.moduleSymbol.symbolName;
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
        if (member.detailDisplayName.isEmpty())
            member.detailDisplayName = memberDetailDisplayName(type);
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
