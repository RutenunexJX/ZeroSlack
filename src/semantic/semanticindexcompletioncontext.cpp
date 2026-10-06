#include "semanticindex.h"

#include "completioncommandkindadapter.h"
#include "semanticindexlookuphelpers.h"
#include <algorithm>

namespace {
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
    SemanticQueryContext context;
    context.moduleName = moduleName;
    const auto variable = resolveVisibleValueRecord(variableName, context);
    if (variable.declarationKind != SymbolTaxonomy::DeclarationKind::StructVariable)
        return {};
    return variable.type.rawTypeText;
}

QList<SemanticSymbolRecord> SemanticIndex::getStructMemberRecords(
    const QString& structTypeName) const
{
    // This compatibility API has no use-site context. Never merge multiple
    // actual type declarations just because their display names match.
    QSet<QString> owners;
    for (const auto& record : getSymbolRecordsByOwner(structTypeName)) {
        if (record.declarationKind == SymbolTaxonomy::DeclarationKind::StructMember
            && record.owner.stableKey.isValid())
            owners.insert(symbolStableKeyText(record.owner.stableKey));
    }
    if (owners.size() > 1)
        return {};
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
            && record.owner.name != structTypeName)
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

SemanticSymbolRecord SemanticIndex::resolveScopeRecord(const SemanticQueryContext& context) const
{
    const QString name = context.moduleName.isEmpty() ? context.packageName : context.moduleName;
    QList<SemanticSymbolRecord> matches;
    for (const auto& record : getSymbolRecordsByName(name)) {
        using Kind = SymbolTaxonomy::DeclarationKind;
        if (record.declarationKind != Kind::Module && record.declarationKind != Kind::Interface
            && record.declarationKind != Kind::Package)
            continue;
        if (!context.fileName.isEmpty()
            && semantic_index_lookup::normalizedLookupFileName(record.location.fileName)
                != semantic_index_lookup::normalizedLookupFileName(context.fileName))
            continue;
        if (context.cursorLine > 0 && record.location.endLine > record.location.startLine
            && (context.cursorLine < record.location.startLine
                || context.cursorLine > record.location.endLine))
            continue;
        if (std::none_of(matches.cbegin(), matches.cend(), [&](const auto& other) {
                return other.stableKey == record.stableKey;
            }))
            matches.append(record);
    }
    return matches.size() == 1 ? matches.first() : SemanticSymbolRecord{};
}

QList<SemanticSymbolRecord> SemanticIndex::getDeclarationMemberRecords(
    const SemanticSymbolRecord& declaration) const
{
    if (declaration.name.isEmpty())
        return {};
    // Old records can be used only when the declaration name is unambiguous.
    QSet<QString> declarations;
    for (const auto& candidate : getSymbolRecordsByName(declaration.name)) {
        if (candidate.declarationKind == declaration.declarationKind)
            declarations.insert(symbolStableKeyText(candidate.stableKey));
    }
    QList<SemanticSymbolRecord> result;
    for (const auto& member : getSymbolRecordsByOwner(declaration.name)) {
        if (member.owner.stableKey.isValid()) {
            if (declaration.stableKey.isValid() && member.owner.stableKey == declaration.stableKey)
                result.append(member);
        } else if (declarations.size() <= 1
                   && semantic_index_lookup::normalizedLookupFileName(member.location.fileName)
                       == semantic_index_lookup::normalizedLookupFileName(declaration.location.fileName)) {
            result.append(member);
        }
    }
    return result;
}

QList<SemanticSymbolRecord> SemanticIndex::getTypeMemberRecords(
    const SemanticSymbolRecord& subject, SymbolTaxonomy::DeclarationKind memberKind) const
{
    SemanticSymbolRecord owner;
    if (subject.type.stableKey.isValid()) {
        owner = getSymbolRecordByStableKey(subject.type.stableKey);
        // A once-bound but removed declaration must never fall back by name.
        if (!owner.isValid())
            return {};
    } else if (subject.declarationKind == SymbolTaxonomy::DeclarationKind::Struct
               || subject.declarationKind == SymbolTaxonomy::DeclarationKind::Typedef
               || subject.declarationKind == SymbolTaxonomy::DeclarationKind::Enum) {
        owner = subject;
    } else {
        const QString name = subject.type.resolvedTypeName.isEmpty()
            ? subject.type.rawTypeText : subject.type.resolvedTypeName;
        for (const auto& candidate : getSymbolRecordsByName(name)) {
            if (candidate.declarationKind != SymbolTaxonomy::DeclarationKind::Typedef
                && candidate.declarationKind != SymbolTaxonomy::DeclarationKind::Struct
                && candidate.declarationKind != SymbolTaxonomy::DeclarationKind::Enum)
                continue;
            // A typedef and its struct presentation describe one source declaration.
            if (owner.isValid() && (owner.location.fileName != candidate.location.fileName
                || owner.location.position != candidate.location.position))
                return {};
            owner = candidate;
        }
        if (!owner.isValid())
            return {};
    }
    QList<SemanticSymbolRecord> result;
    QSet<QString> seen;
    for (const auto& member : getDeclarationMemberRecords(owner)) {
        if (member.declarationKind != memberKind || seen.contains(member.name))
            continue;
        seen.insert(member.name); // SV names are case sensitive within one declaration.
        result.append(member);
    }
    return result;
}

SemanticSymbolRecord SemanticIndex::resolveVisibleValueRecord(
    const QString& name, const SemanticQueryContext& context) const
{
    if (name.isEmpty())
        return {};
    auto records = getCommandCompletionSymbolRecords(context, CompletionCommandKind::VisibleSymbol);
    QList<SemanticSymbolRecord> matches;
    for (const auto& record : records) {
        if (record.name != name)
            continue;
        using Kind = SymbolTaxonomy::DeclarationKind;
        if (record.declarationKind == Kind::StructVariable || record.declarationKind == Kind::Signal
            || record.declarationKind == Kind::Port || record.collectorKind == SymbolTaxonomy::CollectorKind::EnumVariable)
            matches.append(record);
    }
    return matches.size() == 1 ? matches.first() : SemanticSymbolRecord{};
}

SemanticSymbolRecord SemanticIndex::resolveMemberPath(
    const SemanticSymbolRecord& root, const QStringList& memberPath) const
{
    auto current = root;
    for (const auto& name : memberPath) {
        SemanticSymbolRecord next;
        for (const auto& member : getTypeMemberRecords(current, SymbolTaxonomy::DeclarationKind::StructMember)) {
            if (member.name == name) {
                next = member;
                break;
            }
        }
        if (!next.isValid())
            return {};
        current = next;
    }
    return current;
}
