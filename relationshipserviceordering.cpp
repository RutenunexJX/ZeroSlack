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

QString recordFileName(const SemanticSymbolRecord& record)
{
    return record.location.fileName;
}

int recordLine(const SemanticSymbolRecord& record)
{
    return record.location.startLine;
}

int recordColumn(const SemanticSymbolRecord& record)
{
    return record.location.startColumn;
}

QString recordName(const SemanticSymbolRecord& record)
{
    return record.name;
}

int recordLocalHandle(const SemanticSymbolRecord& record)
{
    return record.localHandle;
}

bool relationshipRecordLess(const SemanticSymbolRecord& lhsRecord,
                            const SemanticSymbolRecord& rhsRecord)
{
    const int fileCompare = QString::compare(normalizedRelationshipFileName(
                                                 recordFileName(lhsRecord)),
                                             normalizedRelationshipFileName(
                                                 recordFileName(rhsRecord)),
                                             Qt::CaseInsensitive);
    if (fileCompare != 0)
        return fileCompare < 0;
    const int lhsLine = recordLine(lhsRecord);
    const int rhsLine = recordLine(rhsRecord);
    if (lhsLine != rhsLine)
        return lhsLine < rhsLine;
    const int lhsColumn = recordColumn(lhsRecord);
    const int rhsColumn = recordColumn(rhsRecord);
    if (lhsColumn != rhsColumn)
        return lhsColumn < rhsColumn;
    const int nameCompare = QString::compare(recordName(lhsRecord),
                                             recordName(rhsRecord),
                                             Qt::CaseInsensitive);
    if (nameCompare != 0)
        return nameCompare < 0;
    return recordLocalHandle(lhsRecord) < recordLocalHandle(rhsRecord);
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
                  return relationshipRecordLess(lhsRecord, rhsRecord);
              });
}

} // namespace relationship_service_ordering
