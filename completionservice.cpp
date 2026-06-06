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
    if (query.prefix.isEmpty())
        return {};

    CompletionManager* manager = CompletionManager::getInstance();
    if (!query.moduleName.isEmpty())
        return manager->getModuleInternalVariables(query.moduleName, query.prefix);

    return manager->getGlobalSymbolCompletions(query.prefix);
}

SemanticIndex* CompletionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
