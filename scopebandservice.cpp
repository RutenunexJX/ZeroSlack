#include "scopebandservice.h"

#include "symboltaxonomy.h"

std::unique_ptr<ScopeBandService> ScopeBandService::instance = nullptr;

ScopeBandService* ScopeBandService::getInstance()
{
    if (!instance)
        instance = std::make_unique<ScopeBandService>();
    return instance.get();
}

ScopeBandService::ScopeBandService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

ScopeBandService::~ScopeBandService() = default;

void ScopeBandService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

ScopeBandReport ScopeBandService::scopeBands(const ScopeBandQuery& query) const
{
    ScopeBandReport report;
    if (query.fileName.isEmpty())
        return report;

    SemanticIndex* semantic = semanticIndex();
    const QList<sym_list::SymbolInfo> symbols = semantic->getSymbols(query.fileName);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const SymbolTaxonomy::SemanticMetadata metadata =
            SymbolTaxonomy::semanticMetadata(symbol);
        if (SymbolTaxonomy::isModuleDeclaration(metadata)) {
            if (!semantic->isValidModuleName(symbol.symbolName))
                continue;
            const int endLine = semantic->findEndModuleLine(query.fileName, symbol);
            if (endLine >= 0)
                report.modules.append({symbol, endLine});
        } else if (SymbolTaxonomy::isLogicDeclaration(metadata)) {
            const int endLine = symbol.endLine < symbol.startLine
                ? symbol.startLine
                : symbol.endLine;
            report.logics.append({symbol, endLine});
        }
    }
    return report;
}

SemanticIndex* ScopeBandService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
