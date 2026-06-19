#include "symboloutlinemodel.h"

#include <utility>

namespace {
QString symbolOutlineRecordDisplayName(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.name.isEmpty())
        return record.name;
    return fallback.symbolName;
}

QString symbolOutlineRecordDetailName(
    const SemanticSymbolRecord& record,
    const QString& displayName)
{
    if (!record.type.rawTypeText.isEmpty())
        return record.type.rawTypeText;
    if (!record.owner.name.isEmpty())
        return record.owner.name;
    if (!record.location.fileName.isEmpty())
        return record.location.fileName;
    return displayName;
}

SymbolOutlineSymbolRow symbolOutlineRowFromCompatibilitySymbol(
    const sym_list::SymbolInfo& symbol,
    const QString& groupDisplayName,
    SymbolOutlineIconKind iconKind)
{
    const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
    const QString displayName =
        symbolOutlineRecordDisplayName(record, symbol);

    SymbolOutlineSymbolRow row;
    row.symbol = symbol;
    row.symbolRecord = record;
    row.displayName = displayName;
    row.typeDisplayName = groupDisplayName.isEmpty()
        ? QStringLiteral("Symbol")
        : groupDisplayName;
    row.detailDisplayName = symbolOutlineRecordDetailName(record, displayName);
    row.iconKind = iconKind;
    return row;
}
}

QList<SymbolOutlineGroup> symbolOutlineGroupsWithRows(
    const QList<SymbolOutlineGroup>& groups)
{
    QList<SymbolOutlineGroup> normalizedGroups;
    normalizedGroups.reserve(groups.size());

    for (SymbolOutlineGroup group : groups) {
        if (group.symbolRows.isEmpty() && !group.symbols.isEmpty()) {
            const QString groupDisplayName = group.displayName.isEmpty()
                ? QStringLiteral("Symbols")
                : group.displayName;
            group.symbolRows.reserve(group.symbols.size());
            for (const sym_list::SymbolInfo& symbol : std::as_const(group.symbols)) {
                group.symbolRows.append(
                    symbolOutlineRowFromCompatibilitySymbol(
                        symbol,
                        groupDisplayName,
                        group.iconKind));
            }
        }
        normalizedGroups.append(group);
    }

    return normalizedGroups;
}
