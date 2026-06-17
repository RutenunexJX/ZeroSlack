#include "completionmodel.h"

#include "completionservice.h"

#include <QSet>

namespace {
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

QString ownerScopeNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;
    if (!fallback.moduleScope.isEmpty())
        return fallback.moduleScope;

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

void fillSymbolMetadataFromRecord(
    CompletionModel::CompletionItem& item,
    const sym_list::SymbolInfo& symbol)
{
    item.symbolType = symbol.symbolType;
    item.symbolRecord = semanticSymbolRecordForSymbol(symbol);
    item.symbolStableKey = item.symbolRecord.stableKey.isValid()
        ? item.symbolRecord.stableKey
        : symbolStableKeyForSymbol(symbol);

    const SymbolTaxonomy::SemanticMetadata metadata =
        metadataForRecord(item.symbolRecord, symbol);
    item.typeDisplayName = SymbolTaxonomy::symbolTypeLabel(metadata);
    item.ownerScopeName = ownerScopeNameForRecord(item.symbolRecord, symbol);
    item.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
    item.declarationKind = metadata.declarationKind;
    item.usageRole = metadata.usageRole;
    item.ownerScope = metadata.ownerScope;
    item.sourceRole = metadata.sourceRole;
}
}

void CompletionModel::updateCompletions(const QStringList &keywords,
                                       const QList<sym_list::SymbolInfo> &symbols,
                                       const QString &prefix,
                                       CompletionType type)
{
    beginResetModel();
    completions.clear();

    CompletionService* completionService = CompletionService::getInstance();
    if (type == KeywordCompletion) {
        for (const QString &keyword : keywords) {
            CompletionItem item;
            item.text = keyword;
            item.type = KeywordCompletion;
            item.score = completionService->completionItemScore(keyword, prefix);
            fillDisplayMetadata(item);
            completions.append(item);
        }
    } else if (type == SymbolCompletion) {
        if (symbols.size() == keywords.size()) {
            for (int i = 0; i < keywords.size() && i < symbols.size(); i++) {
                CompletionItem item;
                item.text = keywords[i];
                item.type = SymbolCompletion;
                fillSymbolMetadataFromRecord(item, symbols[i]);
                item.score = completionService->completionItemScore(keywords[i], prefix);
                item.description = item.typeDisplayName;

                fillDisplayMetadata(item);
                completions.append(item);
            }
        } else {
            for (const sym_list::SymbolInfo &symbol : symbols) {
                CompletionItem item;
                item.text = symbol.symbolName;
                item.type = SymbolCompletion;
                fillSymbolMetadataFromRecord(item, symbol);
                item.score = completionService->completionItemScore(symbol.symbolName, prefix);
                item.description = item.typeDisplayName;

                fillDisplayMetadata(item);
                completions.append(item);
            }
        }
    }

    // Sort by score, cap total to avoid UI stutter, then limit visible.
    sortCompletionsByScore();
    if (completions.size() > MaxCompletionItems) {
        completions = completions.mid(0, MaxCompletionItems);
    }
    if (completions.size() > 15) {
        completions = completions.mid(0, 15);  // Normal mode: top 15 by score.
    }

    endResetModel();
}

void CompletionModel::updateCompletions(const CompletionResult &completion,
                                        const QString &prefix)
{
    beginResetModel();
    completions.clear();

    CompletionService* completionService = CompletionService::getInstance();
    for (const CompletionResult::SemanticCompletionItem &semanticItem : completion.items) {
        CompletionItem item;
        item.text = semanticItem.insertText.isEmpty()
            ? semanticItem.label
            : semanticItem.insertText;
        item.type = SymbolCompletion;
        item.symbolRecord = semanticItem.symbolRecord;
        item.symbolStableKey = item.symbolRecord.stableKey.isValid()
            ? item.symbolRecord.stableKey
            : semanticItem.symbolStableKey;
        item.score = completionService->completionItemScore(semanticItem.label, prefix);
        item.description = semanticItem.typeDisplayName;
        item.typeDisplayName = semanticItem.typeDisplayName;
        item.ownerScopeName = semanticItem.ownerScopeName;
        item.sourceRoleDisplayName = semanticItem.sourceRoleDisplayName;
        item.declarationKind = semanticItem.declarationKind;
        item.usageRole = semanticItem.usageRole;
        item.ownerScope = semanticItem.ownerScope;
        item.sourceRole = semanticItem.sourceRole;

        fillDisplayMetadata(item);
        completions.append(item);
    }

    sortCompletionsByScore();
    if (completions.size() > MaxCompletionItems) {
        completions = completions.mid(0, MaxCompletionItems);
    }
    if (completions.size() > 15) {
        completions = completions.mid(0, 15);
    }

    endResetModel();
}

void CompletionModel::updateCommandCompletions(const QStringList &commands, const QString &prefix)
{
    beginResetModel();
    completions.clear();

    CompletionItem headerItem;
    headerItem.text = prefix.isEmpty() ? ":: ALTERNATE MODE - COMMAND INTERFACE ::"
                                      : QString(":: ALTERNATE MODE - Input: '%1' ::").arg(prefix);
    headerItem.type = CommandCompletion;
    headerItem.description = "Command Interface";
    headerItem.score = 1000;
    fillDisplayMetadata(headerItem);
    completions.append(headerItem);

    CompletionService* completionService = CompletionService::getInstance();
    int matchCount = 0;
    for (const QString &command : commands) {
        if (prefix.isEmpty() || command.startsWith(prefix, Qt::CaseInsensitive)) {
            CompletionItem item;
            item.text = command;
            item.type = CommandCompletion;
            item.description = QString("Execute %1 command").arg(command);
            item.score = completionService->completionItemScore(command, prefix);
            fillDisplayMetadata(item);
            completions.append(item);
            matchCount++;
        }
    }

    if (matchCount == 0 && !prefix.isEmpty()) {
        CompletionItem noMatchItem;
        noMatchItem.text = "No matching commands";
        noMatchItem.type = CommandCompletion;
        noMatchItem.description = "No commands match your input";
        noMatchItem.score = 0;
        fillDisplayMetadata(noMatchItem);
        completions.append(noMatchItem);
    }

    sortCompletionsByScore();
    if (completions.size() > MaxCompletionItems) {
        completions = completions.mid(0, MaxCompletionItems);
    }
    endResetModel();
}

void CompletionModel::updateSymbolCompletions(const QList<sym_list::SymbolInfo> &symbols,
                                              const QString &prefix,
                                              sym_list::sym_type_e symbolType)
{
    beginResetModel();
    completions.clear();

    CompletionService* completionService = CompletionService::getInstance();
    const CommandSymbolPresentation presentation =
        completionService->commandSymbolPresentation(symbolType);

    CompletionItem descItem;
    descItem.text = QString(":: COMMAND MODE - %1 ::").arg(presentation.typeDescription);
    descItem.type = SymbolCompletion;
    descItem.description = "Command Mode";
    descItem.score = 1000;
    descItem.defaultValue = presentation.defaultValue;
    fillDisplayMetadata(descItem);
    completions.append(descItem);

    CompletionItem defaultItem;
    defaultItem.text = QString("[DEFAULT] %1").arg(presentation.defaultValue);
    defaultItem.type = SymbolCompletion;
    defaultItem.symbolType = symbolType;
    defaultItem.description =
        QString("Default %1 declaration").arg(presentation.typeDescription.split(' ').value(0));
    defaultItem.defaultValue = presentation.defaultValue;
    defaultItem.score = 999;  // High score but less than header.
    fillDisplayMetadata(defaultItem);
    completions.append(defaultItem);

    QSet<QString> addedItems;

    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.symbolName == presentation.defaultValue) {
            continue;
        }

        const CommandSymbolCompletionItem serviceItem =
            completionService->commandSymbolCompletionItem(symbol, symbolType, prefix);
        if (addedItems.contains(serviceItem.uniqueKey))
            continue;
        addedItems.insert(serviceItem.uniqueKey);

        CompletionItem item;
        item.type = SymbolCompletion;
        item.symbolType = symbolType;
        fillSymbolMetadataFromRecord(item, symbol);
        item.symbolType = symbolType;
        item.text = serviceItem.text;
        item.description = item.typeDisplayName.isEmpty()
            ? serviceItem.description
            : item.typeDisplayName;
        item.defaultValue = serviceItem.defaultValue;
        item.score = serviceItem.score;

        fillDisplayMetadata(item);
        completions.append(item);
    }

    sortCompletionsByScore();
    if (completions.size() > MaxCompletionItems) {
        completions = completions.mid(0, MaxCompletionItems);
    }
    if (completions.size() > 32) {
        completions = completions.mid(0, 32);
    }

    endResetModel();
}
