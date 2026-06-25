#include "completionservice.h"

#include "completionsemanticquery.h"
#include "completionsymbolquery.h"
#include "semanticindex.h"

#include <QStringList>
#include <QVector>

#include <algorithm>

namespace {

struct CompletionAnalysisBandCount {
    SemanticAnalysisBandMetadata metadata;
    int count = 0;
};

SemanticAnalysisBandMetadata normalizedCompletionAnalysisBand(
    const SemanticAnalysisBandMetadata& metadata)
{
    if (metadata.isValid())
        return metadata;

    SemanticAnalysisBandMetadata unbanded;
    unbanded.label = QStringLiteral("unbanded");
    unbanded.displayName = QStringLiteral("unbanded");
    return unbanded;
}

QList<CompletionAnalysisBandCount> completionAnalysisBandCounts(
    const QList<CompletionResult::SemanticCompletionItem>& items)
{
    QList<CompletionAnalysisBandCount> counts;
    for (const CompletionResult::SemanticCompletionItem& item : items) {
        const SemanticAnalysisBandMetadata metadata =
            normalizedCompletionAnalysisBand(item.analysisBand);
        auto existing =
            std::find_if(counts.begin(),
                         counts.end(),
                         [&](const CompletionAnalysisBandCount& count) {
                             return count.metadata.label == metadata.label;
                         });
        if (existing == counts.end()) {
            CompletionAnalysisBandCount next;
            next.metadata = metadata;
            next.count = 1;
            counts.append(next);
        } else {
            ++existing->count;
        }
    }

    std::sort(counts.begin(),
              counts.end(),
              [](const CompletionAnalysisBandCount& lhs,
                 const CompletionAnalysisBandCount& rhs) {
                  const int leftPriority =
                      semanticAnalysisBandSortPriority(lhs.metadata);
                  const int rightPriority =
                      semanticAnalysisBandSortPriority(rhs.metadata);
                  if (leftPriority != rightPriority)
                      return leftPriority < rightPriority;
                  return QString::compare(lhs.metadata.label,
                                          rhs.metadata.label,
                                          Qt::CaseInsensitive) < 0;
              });
    return counts;
}

QString completionItemCountText(int count)
{
    return QStringLiteral("%1 %2")
        .arg(count)
        .arg(count == 1 ? QStringLiteral("item") : QStringLiteral("items"));
}

QString ownerScopeNameForRecord(const SemanticSymbolRecord& record)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;

    switch (record.owner.kind) {
    case SymbolTaxonomy::SymbolOwnerScope::Global:
        return QStringLiteral("global");
    case SymbolTaxonomy::SymbolOwnerScope::Module:
        return QStringLiteral("module");
    case SymbolTaxonomy::SymbolOwnerScope::Interface:
        return QStringLiteral("interface");
    case SymbolTaxonomy::SymbolOwnerScope::Package:
        return QStringLiteral("package");
    case SymbolTaxonomy::SymbolOwnerScope::Struct:
        return QStringLiteral("struct");
    case SymbolTaxonomy::SymbolOwnerScope::Unknown:
        break;
    }
    return QString();
}

CompletionResult::SemanticCompletionItem semanticCompletionItemForRecord(
    const SemanticSymbolRecord& record)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    const QString displayName = record.name;

    CompletionResult::SemanticCompletionItem item;
    item.label = displayName;
    item.insertText = displayName;
    item.typeDisplayName = SymbolTaxonomy::symbolTypeLabel(metadata);
    item.ownerScopeName = ownerScopeNameForRecord(record);
    item.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
    item.analysisBand = record.analysisBand;
    item.analysisBandDisplayName =
        semanticAnalysisBandDisplayName(record.analysisBand);
    item.symbolRecord = record;
    item.symbolStableKey = record.stableKey;
    item.declarationKind = metadata.declarationKind;
    item.usageRole = metadata.usageRole;
    item.ownerScope = metadata.ownerScope;
    item.sourceRole = metadata.sourceRole;
    return item;
}

QList<CompletionResult::SemanticCompletionItem> semanticCompletionItemsForRecords(
    const QList<SemanticSymbolRecord>& records)
{
    QList<CompletionResult::SemanticCompletionItem> items;
    items.reserve(records.size());
    for (const SemanticSymbolRecord& record : records)
        items.append(semanticCompletionItemForRecord(record));
    return items;
}

QList<SemanticSymbolRecord> completionRecords(
    SemanticIndex* semanticIndex,
    const CompletionQuery& query)
{
    if (!semanticIndex)
        return {};

    if (!query.structTypeNameForMember.isEmpty()) {
        return CompletionSymbolQuery::structMemberRecords(
            semanticIndex, query.structTypeNameForMember, query.prefix);
    }

    if (query.prefix.isEmpty())
        return {};

    if (!query.moduleName.isEmpty())
        return semanticIndex->getModuleCompletionSymbolRecords(
            query.moduleName, query.prefix);

    return semanticIndex->getGlobalCompletionSymbolRecords(query.prefix);
}

} // namespace

std::unique_ptr<CompletionService> CompletionService::instance = nullptr;

int CompletionResult::analysisBandGroupCount() const
{
    return completionAnalysisBandCounts(items).size();
}

QString CompletionResult::analysisBandSummaryText() const
{
    const QList<CompletionAnalysisBandCount> counts =
        completionAnalysisBandCounts(items);
    if (counts.isEmpty())
        return QStringLiteral("bands none");

    QStringList parts;
    for (const CompletionAnalysisBandCount& count : counts) {
        parts.append(QStringLiteral("%1 %2")
                         .arg(semanticAnalysisBandDisplayName(count.metadata),
                              completionItemCountText(count.count)));
    }

    return QStringLiteral("bands %1")
        .arg(parts.join(QStringLiteral(", ")));
}

CompletionService* CompletionService::getInstance()
{
    if (!instance)
        instance = std::make_unique<CompletionService>();
    return instance.get();
}

CompletionService::CompletionService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

CompletionService::~CompletionService() = default;

void CompletionService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

QStringList CompletionService::findCompletions(const CompletionQuery& query) const
{
    return findCompletionResult(query).names;
}

CompletionResult CompletionService::findCompletionResult(
    const CompletionQuery& query) const
{
    CompletionResult result;
    const QList<SemanticSymbolRecord> records =
        completionRecords(semanticIndex(), query);
    result.names = CompletionSymbolQuery::namesFromRecords(records);
    result.items = semanticCompletionItemsForRecords(records);
    return result;
}

QVector<QPair<QString, int>> CompletionService::findScoredAllSymbolCompletions(
    const QString& prefix,
    int maxResults) const
{
    return CompletionSymbolQuery::scoredNames(
        semanticIndex()->getCompletionSymbolNames(), prefix, maxResults);
}

QStringList CompletionService::findAllSymbolCompletions(
    const QString& prefix,
    int maxResults) const
{
    const QVector<QPair<QString, int>> scored =
        findScoredAllSymbolCompletions(prefix, maxResults);
    return CompletionSymbolQuery::namesFromScored(scored, maxResults);
}

QStringList CompletionService::findSymbolCompletionsByKind(
    CompletionCommandKind commandKind,
    const QString& prefix,
    int maxResults) const
{
    const QStringList names = CompletionSymbolQuery::namesFromRecords(
        CompletionSemanticQuery::typedSymbolRecords(
            semanticIndex(),
            commandKind,
            prefix));
    return CompletionSymbolQuery::namesFromScored(
        CompletionSymbolQuery::scoredNames(names, prefix, maxResults),
        maxResults);
}

SemanticIndex* CompletionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
