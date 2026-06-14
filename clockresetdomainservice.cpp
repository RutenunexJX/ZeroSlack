#include "clockresetdomainservice.h"

#include <QDir>
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
    report.clockDomains = buildDomains(
        SymbolRelationshipEngine::CLOCKS,
        query,
        &report.clockRelationshipCount);
    report.resetDomains = buildDomains(
        SymbolRelationshipEngine::RESETS,
        query,
        &report.resetRelationshipCount);
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
            entries[entryBySignalId.value(signalId)].modules.append(member);
            ++count;
        }
    }

    for (ClockResetDomainEntry& entry : entries)
        sortMembers(entry.modules);
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
    return QDir::cleanPath(QDir::fromNativeSeparators(fileName));
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
