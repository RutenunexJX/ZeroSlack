#include "completionservice.h"

#include "completionmanager.h"

#include <QSet>
#include <algorithm>

std::unique_ptr<CompletionService> CompletionService::instance = nullptr;

CompletionService* CompletionService::getInstance()
{
    if (!instance)
        instance = std::make_unique<CompletionService>();
    return instance.get();
}

CompletionService::CompletionService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

CompletionService::~CompletionService() = default;

void CompletionService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

QStringList CompletionService::findCompletions(const CompletionQuery& query) const
{
    CompletionManager* manager = CompletionManager::getInstance();
    if (!query.structTypeNameForMember.isEmpty()) {
        QStringList result;
        QSet<QString> seen;
        const QList<sym_list::SymbolInfo> members = findStructMemberSymbols(query);
        for (const sym_list::SymbolInfo& member : members) {
            const QString key = member.symbolName.toCaseFolded();
            if (seen.contains(key))
                continue;
            seen.insert(key);
            result.append(member.symbolName);
        }
        result.sort(Qt::CaseInsensitive);
        return result;
    }

    if (query.prefix.isEmpty())
        return {};

    if (!query.moduleName.isEmpty())
        return manager->getModuleInternalVariables(query.moduleName, query.prefix);

    return manager->getGlobalSymbolCompletions(query.prefix);
}

QList<sym_list::SymbolInfo> CompletionService::findCompletionSymbols(
    const CompletionQuery& query) const
{
    if (!query.structTypeNameForMember.isEmpty())
        return findStructMemberSymbols(query);

    const QStringList completions = findCompletions(query);
    QList<sym_list::SymbolInfo> result;

    for (const QString& name : completions) {
        const QList<sym_list::SymbolInfo> candidates =
            semanticIndex()->findDefinitions(name);
        bool appended = false;
        for (const sym_list::SymbolInfo& symbol : candidates) {
            if (!query.structTypeNameForMember.isEmpty()) {
                if (symbol.symbolType != sym_list::sym_struct_member
                    || symbol.moduleScope != query.structTypeNameForMember) {
                    continue;
                }
            }

            result.append(symbol);
            appended = true;
            break;
        }

        if (!appended) {
            sym_list::SymbolInfo dummySymbol;
            dummySymbol.symbolName = name;
            dummySymbol.symbolType = sym_list::sym_user;
            result.append(dummySymbol);
        }
    }

    return result;
}

QStringList CompletionService::findCommandCompletions(const CommandCompletionQuery& query) const
{
    CompletionManager* manager = CompletionManager::getInstance();
    if (!query.moduleName.isEmpty())
        return manager->getModuleInternalVariablesByType(
            query.moduleName,
            query.symbolType,
            query.prefix);

    return manager->getGlobalSymbolsByType(query.symbolType, query.prefix);
}

QList<sym_list::SymbolInfo> CompletionService::findCommandCompletionSymbols(
    const CommandCompletionQuery& query) const
{
    const bool useSymbolInfoDirectly =
        query.symbolType == sym_list::sym_packed_struct_var
        || query.symbolType == sym_list::sym_unpacked_struct_var
        || query.symbolType == sym_list::sym_packed_struct
        || query.symbolType == sym_list::sym_unpacked_struct;

    if (useSymbolInfoDirectly) {
        if (query.moduleName.isEmpty())
            return {};

        semanticIndex()->refreshStructTypedefEnumForFile(query.fileName, query.documentText);

        return semanticIndex()->getModuleContextSymbolsByType(
            query.moduleName,
            query.fileName,
            query.symbolType,
            query.prefix);
    }

    const QStringList symbolNames = findCommandCompletions(query);
    QList<sym_list::SymbolInfo> result;

    for (const QString& symbolName : symbolNames) {
        const QList<sym_list::SymbolInfo> matchingSymbols =
            semanticIndex()->findDefinitions(symbolName);
        for (const sym_list::SymbolInfo& symbol : matchingSymbols) {
            bool typeOk = symbol.symbolType == query.symbolType;
            if (query.symbolType == sym_list::sym_enum && !typeOk)
                typeOk = symbol.symbolType == sym_list::sym_typedef
                         && symbol.dataType == QLatin1String("enum");

            if (typeOk && (query.moduleName.isEmpty() || symbol.moduleScope == query.moduleName)) {
                result.append(symbol);
                break;
            }
        }
    }

    return result;
}

QString CompletionService::getStructTypeForVariable(const QString& variableName,
                                                    const QString& moduleName) const
{
    return semanticIndex()->getStructTypeForVariable(variableName, moduleName);
}

bool CompletionService::tryParseStructMemberContext(const QString& line,
                                                    QString& outVariableName,
                                                    QString& outMemberPrefix) const
{
    return CompletionManager::getInstance()->tryParseStructMemberContext(
        line,
        outVariableName,
        outMemberPrefix);
}

SemanticIndex* CompletionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

QList<sym_list::SymbolInfo> CompletionService::findStructMemberSymbols(
    const CompletionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> members =
        semanticIndex()->getStructMembers(query.structTypeNameForMember);
    for (const sym_list::SymbolInfo& member : members) {
        if (!completionNameMatches(member.symbolName, query.prefix))
            continue;
        const QString key = member.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(member);
    }
    return result;
}

bool CompletionService::completionNameMatches(const QString& name,
                                              const QString& prefix) const
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
