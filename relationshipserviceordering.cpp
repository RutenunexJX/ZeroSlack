#include "relationshipserviceordering.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace relationship_service_ordering {

namespace {

QString normalizedRelationshipFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool relationshipSymbolLess(const sym_list::SymbolInfo& lhs,
                            const sym_list::SymbolInfo& rhs)
{
    const int fileCompare = QString::compare(normalizedRelationshipFileName(lhs.fileName),
                                             normalizedRelationshipFileName(rhs.fileName),
                                             Qt::CaseInsensitive);
    if (fileCompare != 0)
        return fileCompare < 0;
    if (lhs.startLine != rhs.startLine)
        return lhs.startLine < rhs.startLine;
    if (lhs.startColumn != rhs.startColumn)
        return lhs.startColumn < rhs.startColumn;
    return QString::compare(lhs.symbolName, rhs.symbolName, Qt::CaseInsensitive) < 0;
}

} // namespace

void sortRelationshipResults(QList<RelationshipResult>& relationships, bool outgoing)
{
    std::sort(relationships.begin(), relationships.end(),
              [outgoing](const RelationshipResult& lhs, const RelationshipResult& rhs) {
                  if (lhs.relationship.type != rhs.relationship.type) {
                      return static_cast<int>(lhs.relationship.type)
                          < static_cast<int>(rhs.relationship.type);
                  }

                  const sym_list::SymbolInfo& lhsSymbol = outgoing ? lhs.toSymbol : lhs.fromSymbol;
                  const sym_list::SymbolInfo& rhsSymbol = outgoing ? rhs.toSymbol : rhs.fromSymbol;
                  return relationshipSymbolLess(lhsSymbol, rhsSymbol);
              });
}

} // namespace relationship_service_ordering
