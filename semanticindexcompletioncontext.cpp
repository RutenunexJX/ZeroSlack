#include "semanticindex.h"

#include "completioncommandkindadapter.h"
#include "completiontypes.h"

#include <QSet>
#include <algorithm>

namespace {
bool semanticCompletionContextNameMatches(const QString& name, const QString& prefix)
{
    if (prefix.isEmpty())
        return true;
    if (name.isEmpty())
        return false;

    const QString lowerName = name.toLower();
    const QString lowerPrefix = prefix.toLower();
    if (lowerName.startsWith(lowerPrefix))
        return true;

    int namePos = 0;
    int prefixPos = 0;
    while (prefixPos < lowerPrefix.length() && namePos < lowerName.length()) {
        if (lowerPrefix.at(prefixPos) == lowerName.at(namePos))
            ++prefixPos;
        ++namePos;
    }
    return prefixPos == lowerPrefix.length();
}

QString displayNameForCompletionContextRecord(
    const SemanticSymbolRecord& record)
{
    if (!record.name.isEmpty())
        return record.name;
    return QString();
}

QStringList uniqueSortedCompletionContextSymbolNames(
    const QList<SemanticSymbolRecord>& records)
{
    QStringList result;
    QSet<QString> seenNames;
    for (const SemanticSymbolRecord& record : records) {
        const QString displayName =
            displayNameForCompletionContextRecord(record);
        const QString key = displayName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(displayName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

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

QStringList SemanticIndex::getEnumValueCompletionNames(
    const QString& prefix,
    const QString& enumTypeName) const
{
    QList<SemanticSymbolRecord> result;
    const QList<SemanticSymbolRecord> records =
        completionContextRecordsByKind(
            enumTypeName.isEmpty()
                ? getSymbolRecordsByDeclarationKind(
                    SymbolTaxonomy::DeclarationKind::Enum)
                : getSymbolRecordsByOwner(enumTypeName),
            CompletionCommandKind::EnumValue);
    for (const SemanticSymbolRecord& record : records) {
        if (!enumTypeName.isEmpty()
            && ownerNameForCompletionContextRecord(record) != enumTypeName)
            continue;
        if (!semanticCompletionContextNameMatches(
                displayNameForCompletionContextRecord(record), prefix))
            continue;
        result.append(record);
    }
    return uniqueSortedCompletionContextSymbolNames(result);
}

QString SemanticIndex::enumTypeForVariable(
    const QString& variableName,
    const QString& moduleName) const
{
    if (!moduleName.isEmpty()) {
        const QList<SemanticSymbolRecord> moduleRecords =
            getModuleInternalSymbolRecordsByType(
                moduleName,
                CompletionCommandKind::EnumVariable);
        for (const SemanticSymbolRecord& record : moduleRecords) {
            if (record.name == variableName)
                return ownerNameForCompletionContextRecord(record);
        }
    }

    const QList<SemanticSymbolRecord> records =
        completionContextRecordsByKind(getSymbolRecordsByName(variableName),
                                       CompletionCommandKind::EnumVariable);
    for (const SemanticSymbolRecord& record : records) {
        if (record.name == variableName)
            return ownerNameForCompletionContextRecord(record);
    }

    return QString();
}

QStringList SemanticIndex::getModulePortCompletionNames(
    const QString& prefix,
    const QString& moduleTypeName) const
{
    if (moduleTypeName.isEmpty())
        return {};

    bool moduleExists = false;
    for (const SemanticSymbolRecord& record : getSymbolRecordsByName(moduleTypeName)) {
        if (record.declarationKind != SymbolTaxonomy::DeclarationKind::Module)
            continue;
        if (record.name == moduleTypeName) {
            moduleExists = true;
            break;
        }
    }
    if (!moduleExists)
        return {};

    QList<SemanticSymbolRecord> portRecords;
    portRecords.append(getCommandCompletionSymbolRecords(moduleTypeName,
                                                         CompletionCommandKind::Wire,
                                                         prefix));
    portRecords.append(getCommandCompletionSymbolRecords(moduleTypeName,
                                                         CompletionCommandKind::Reg,
                                                         prefix));
    portRecords.append(getCommandCompletionSymbolRecords(moduleTypeName,
                                                         CompletionCommandKind::Logic,
                                                         prefix));
    return uniqueSortedCompletionContextSymbolNames(
        portRecords);
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
