#include "semanticindex.h"

#include "completioncommandkindadapter.h"
#include <algorithm>

namespace {
QString ownerNameForCompletionContextRecord(
    const SemanticSymbolRecord& record)
{
    return record.owner.name;
}

QString rawTypeTextForCompletionContextRecord(
    const SemanticSymbolRecord& record)
{
    return record.type.rawTypeText;
}

QList<SemanticSymbolRecord> completionContextRecordsByKind(
    const QList<SemanticSymbolRecord>& records,
    CompletionCommandKind kind)
{
    QList<SemanticSymbolRecord> result;
    for (const SemanticSymbolRecord& record : records) {
        if (completionCommandKindMatchesCommandRecord(record, kind))
            result.append(record);
    }
    return result;
}

}

QString SemanticIndex::getStructTypeForVariable(const QString& variableName,
                                                const QString& moduleName) const
{
    if (variableName.isEmpty())
        return QString();

    const QList<SemanticSymbolRecord> variableCandidates =
        getSymbolRecordsByName(variableName);
    QList<SemanticSymbolRecord> structVariables =
        completionContextRecordsByKind(
            variableCandidates,
            CompletionCommandKind::PackedStructVariable);
    structVariables.append(
        completionContextRecordsByKind(
            variableCandidates,
            CompletionCommandKind::UnpackedStructVariable));

    if (!moduleName.isEmpty()) {
        for (const SemanticSymbolRecord& record : std::as_const(structVariables)) {
            const QString ownerName =
                ownerNameForCompletionContextRecord(record);
            const QString rawTypeText =
                rawTypeTextForCompletionContextRecord(record);
            if (record.name == variableName
                && ownerName == moduleName
                && !rawTypeText.isEmpty()) {
                return rawTypeText;
            }
        }
    }

    for (const SemanticSymbolRecord& record : std::as_const(structVariables)) {
        const QString rawTypeText =
            rawTypeTextForCompletionContextRecord(record);
        if (record.name == variableName && !rawTypeText.isEmpty())
            return rawTypeText;
    }

    return QString();
}

QList<SemanticSymbolRecord> SemanticIndex::getStructMemberRecords(
    const QString& structTypeName) const
{
    QList<SemanticSymbolRecord> result;
    const QList<SemanticSymbolRecord> members =
        completionContextRecordsByKind(
            structTypeName.isEmpty()
                ? getSymbolRecordsByDeclarationKind(
                    SymbolTaxonomy::DeclarationKind::StructMember)
                : getSymbolRecordsByOwner(structTypeName),
            CompletionCommandKind::StructMember);
    for (const SemanticSymbolRecord& record : members) {
        if (!structTypeName.isEmpty()
            && ownerNameForCompletionContextRecord(record) != structTypeName)
            continue;
        result.append(record);
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const SemanticSymbolRecord& a,
                        const SemanticSymbolRecord& b) {
        const int nameCompare = QString::compare(a.name,
                                                 b.name,
                                                 Qt::CaseInsensitive);
        if (nameCompare != 0)
            return nameCompare < 0;
        if (a.location.fileName != b.location.fileName)
            return a.location.fileName < b.location.fileName;
        if (a.location.startLine != b.location.startLine)
            return a.location.startLine < b.location.startLine;
        return a.localHandle < b.localHandle;
    });
    return result;
}
