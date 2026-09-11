#ifndef SEMANTICINDEXMODULECONTEXTHELPERS_H
#define SEMANTICINDEXMODULECONTEXTHELPERS_H

#include "semanticindex.h"

#include <QList>
#include <QString>

namespace semantic_index_module_context {

QString normalizedModuleContextFileName(const QString& fileName);

bool moduleContextNameMatches(const QString& name, const QString& prefix);

void sortModuleContextSymbolRecords(QList<SemanticSymbolRecord>& records);

} // namespace semantic_index_module_context

#endif // SEMANTICINDEXMODULECONTEXTHELPERS_H
