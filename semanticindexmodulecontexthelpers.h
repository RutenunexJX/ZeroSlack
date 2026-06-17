#ifndef SEMANTICINDEXMODULECONTEXTHELPERS_H
#define SEMANTICINDEXMODULECONTEXTHELPERS_H

#include "semanticindex.h"

#include <QList>
#include <QString>

namespace semantic_index_module_context {

QString normalizedModuleContextFileName(const QString& fileName);

bool moduleContextSymbolTypeMatches(sym_list::sym_type_e symbolType,
                                    sym_list::sym_type_e commandType,
                                    const QString& dataType = QString());
bool moduleContextSymbolTypeMatches(const sym_list::SymbolInfo& symbol,
                                    sym_list::sym_type_e commandType);
bool moduleContextSymbolTypeMatches(const SemanticSymbolRecord& record,
                                    sym_list::sym_type_e commandType);

bool moduleContextNameMatches(const QString& name, const QString& prefix);

bool isModuleRangeSymbolType(sym_list::sym_type_e type);

void sortModuleContextSymbols(QList<sym_list::SymbolInfo>& symbols);

} // namespace semantic_index_module_context

#endif // SEMANTICINDEXMODULECONTEXTHELPERS_H
