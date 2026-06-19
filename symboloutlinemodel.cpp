#include "symboloutlinemodel.h"

sym_list::SymbolInfo symbolOutlineCompatibilitySymbolForRecord(
    const SemanticSymbolRecord& record)
{
    sym_list::SymbolInfo symbol;
    symbol.fileName = record.location.fileName;
    symbol.symbolName = record.name;
    symbol.symbolType = record.rawCollectorKind;
    symbol.startLine = record.location.startLine;
    symbol.startColumn = record.location.startColumn;
    symbol.endLine = record.location.endLine;
    symbol.endColumn = record.location.endColumn;
    symbol.position = record.location.position;
    symbol.length = record.location.length;
    symbol.symbolId = record.localHandle;
    symbol.moduleScope = record.owner.name;
    symbol.dataType = record.type.rawTypeText;
    return symbol;
}
