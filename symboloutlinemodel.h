#ifndef SYMBOLOUTLINEMODEL_H
#define SYMBOLOUTLINEMODEL_H

#include "syminfo.h"

#include <QList>

struct SymbolOutlineGroup {
    sym_list::sym_type_e symbolType = sym_list::sym_module;
    QList<sym_list::SymbolInfo> symbols;
};

#endif // SYMBOLOUTLINEMODEL_H
