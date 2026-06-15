#ifndef SYMBOLOUTLINEMODEL_H
#define SYMBOLOUTLINEMODEL_H

#include "syminfo.h"

#include <QList>
#include <QString>

struct SymbolOutlineGroup {
    sym_list::sym_type_e symbolType = sym_list::sym_module;
    QString displayName;
    QList<sym_list::SymbolInfo> symbols;
};

#endif // SYMBOLOUTLINEMODEL_H
