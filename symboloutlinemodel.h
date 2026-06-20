#ifndef SYMBOLOUTLINEMODEL_H
#define SYMBOLOUTLINEMODEL_H

#include "semanticindex.h"

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
    SemanticSymbolRecord symbolRecord;
    SymbolStableKey symbolStableKey;
    QString displayName;
    QString typeDisplayName;
    QString detailDisplayName;
    SymbolOutlineIconKind iconKind = SymbolOutlineIconKind::Symbol;
};

struct SymbolOutlineGroup {
    SymbolTaxonomy::DeclarationKind declarationKind =
        SymbolTaxonomy::DeclarationKind::Module;
    QString displayName;
    SymbolOutlineIconKind iconKind = SymbolOutlineIconKind::Symbol;
    QList<SymbolOutlineSymbolRow> symbolRows;
};

#endif // SYMBOLOUTLINEMODEL_H
