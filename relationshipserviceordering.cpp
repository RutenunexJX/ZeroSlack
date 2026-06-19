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

QString recordFileName(const SemanticSymbolRecord& record,
                       const sym_list::SymbolInfo& fallback)
{
    return record.location.fileName.isEmpty()
        ? fallback.fileName
        : record.location.fileName;
}

int recordLine(const SemanticSymbolRecord& record,
               const sym_list::SymbolInfo& fallback)
{
    return record.location.startLine > 0
        ? record.location.startLine
        : fallback.startLine;
}

int recordColumn(const SemanticSymbolRecord& record,
                 const sym_list::SymbolInfo& fallback)
{
    return record.location.startColumn > 0
        ? record.location.startColumn
        : fallback.startColumn;
}

QString recordName(const SemanticSymbolRecord& record,
                   const sym_list::SymbolInfo& fallback)
{
    return record.name.isEmpty()
        ? fallback.symbolName
        : record.name;
}

int recordLocalHandle(const SemanticSymbolRecord& record,
                      const sym_list::SymbolInfo& fallback)
{
    return record.localHandle >= 0
        ? record.localHandle
        : fallback.symbolId;
}

bool relationshipRecordLess(const SemanticSymbolRecord& lhsRecord,
                            const sym_list::SymbolInfo& lhsFallback,
                            const SemanticSymbolRecord& rhsRecord,
                            const sym_list::SymbolInfo& rhsFallback)
{
    const int fileCompare = QString::compare(normalizedRelationshipFileName(
                                                 recordFileName(lhsRecord, lhsFallback)),
                                             normalizedRelationshipFileName(
                                                 recordFileName(rhsRecord, rhsFallback)),
                                             Qt::CaseInsensitive);
    if (fileCompare != 0)
        return fileCompare < 0;
    const int lhsLine = recordLine(lhsRecord, lhsFallback);
    const int rhsLine = recordLine(rhsRecord, rhsFallback);
    if (lhsLine != rhsLine)
        return lhsLine < rhsLine;
    const int lhsColumn = recordColumn(lhsRecord, lhsFallback);
    const int rhsColumn = recordColumn(rhsRecord, rhsFallback);
    if (lhsColumn != rhsColumn)
        return lhsColumn < rhsColumn;
    const int nameCompare = QString::compare(recordName(lhsRecord, lhsFallback),
                                             recordName(rhsRecord, rhsFallback),
                                             Qt::CaseInsensitive);
    if (nameCompare != 0)
        return nameCompare < 0;
    return recordLocalHandle(lhsRecord, lhsFallback)
        < recordLocalHandle(rhsRecord, rhsFallback);
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

                  const SemanticSymbolRecord& lhsRecord =
                      outgoing ? lhs.toSymbolRecord : lhs.fromSymbolRecord;
                  const SemanticSymbolRecord& rhsRecord =
                      outgoing ? rhs.toSymbolRecord : rhs.fromSymbolRecord;
                  const sym_list::SymbolInfo& lhsFallback =
                      outgoing ? lhs.toSymbol : lhs.fromSymbol;
                  const sym_list::SymbolInfo& rhsFallback =
                      outgoing ? rhs.toSymbol : rhs.fromSymbol;
                  return relationshipRecordLess(lhsRecord,
                                                lhsFallback,
                                                rhsRecord,
                                                rhsFallback);
              });
}

} // namespace relationship_service_ordering
