#include "completionmodel.h"

#include "completionservice.h"

#include <QSet>

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
                item.symbolType = symbols[i].symbolType;
                item.symbolStableKey = symbolStableKeyForSymbol(symbols[i]);
                item.score = completionService->completionItemScore(keywords[i], prefix);
                item.description =
                    completionService->symbolTypeDescription(symbols[i]);

                fillDisplayMetadata(item);
                completions.append(item);
            }
        } else {
            for (const sym_list::SymbolInfo &symbol : symbols) {
                CompletionItem item;
                item.text = symbol.symbolName;
                item.type = SymbolCompletion;
                item.symbolType = symbol.symbolType;
                item.symbolStableKey = symbolStableKeyForSymbol(symbol);
                item.score = completionService->completionItemScore(symbol.symbolName, prefix);
                item.description =
                    completionService->symbolTypeDescription(symbol);

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
        item.symbolStableKey = symbolStableKeyForSymbol(symbol);
        item.text = serviceItem.text;
        item.description = serviceItem.description;
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
