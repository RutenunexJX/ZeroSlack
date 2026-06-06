#include "completionservice.h"

#include "completionmanager.h"

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
    if (!query.structTypeNameForMember.isEmpty())
        return manager->getStructMemberCompletions(query.prefix, query.structTypeNameForMember);

    if (query.prefix.isEmpty())
        return {};

    if (!query.moduleName.isEmpty())
        return manager->getModuleInternalVariables(query.moduleName, query.prefix);

    return manager->getGlobalSymbolCompletions(query.prefix);
}

QList<sym_list::SymbolInfo> CompletionService::findCompletionSymbols(
    const CompletionQuery& query) const
{
    const QStringList completions = findCompletions(query);
    QList<sym_list::SymbolInfo> result;
    sym_list* db = semanticIndex()->symbolDatabase();

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

        if (!appended && db && query.structTypeNameForMember.isEmpty()) {
            const QList<sym_list::SymbolInfo> fallback = db->findSymbolsByName(name);
            if (!fallback.isEmpty())
                result.append(fallback.first());
        }
    }

    return result;
}

QString CompletionService::getStructTypeForVariable(const QString& variableName,
                                                    const QString& moduleName) const
{
    return CompletionManager::getInstance()->getStructTypeForVariable(variableName, moduleName);
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
