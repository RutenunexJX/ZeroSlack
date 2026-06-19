#ifndef SYMBOLOUTLINEMODEL_H
#define SYMBOLOUTLINEMODEL_H

#include "semanticindex.h"
#include "syminfo.h"

#include <QList>
#include <QString>

enum class SymbolOutlineIconKind {
    Symbol,
    Module,
    Signal,
    Subroutine,
    Parameter,
    Port,
    Instance,
    Type
};

struct SymbolOutlineSymbolRow {
    sym_list::SymbolInfo symbol = {};
    SemanticSymbolRecord symbolRecord;
    QString displayName;
    QString typeDisplayName;
    QString detailDisplayName;
    SymbolOutlineIconKind iconKind = SymbolOutlineIconKind::Symbol;
};

struct SymbolOutlineGroup {
    sym_list::sym_type_e symbolType = sym_list::sym_module;
    QString displayName;
    SymbolOutlineIconKind iconKind = SymbolOutlineIconKind::Symbol;
    QList<SymbolOutlineSymbolRow> symbolRows;
};

sym_list::SymbolInfo symbolOutlineCompatibilitySymbolForRecord(
    const SemanticSymbolRecord& record);

#endif // SYMBOLOUTLINEMODEL_H
