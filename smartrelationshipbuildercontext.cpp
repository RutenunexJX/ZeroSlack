#include "smartrelationshipbuilder.h"
#include "symboltaxonomy.h"

#include <utility>

void SmartRelationshipBuilder::setupAnalysisContext(const QString& fileName,
                                                    AnalysisContext& context)
{
    context.currentFileName = fileName;
    context.fileSymbols = symbolDatabase->findSymbolsByFileName(fileName);
    context.localSymbolHandles.clear();
    context.localHandleToType.clear();

    for (const sym_list::SymbolInfo& symbol : std::as_const(context.fileSymbols)) {
        context.localSymbolHandles[symbol.symbolName] = symbol.symbolId;
        context.localHandleToType[symbol.symbolId] = symbol.symbolType;

        if (SymbolTaxonomy::isModuleDeclaration(symbol)
            && context.currentModuleLocalHandle == -1) {
            context.currentModuleName = symbol.symbolName;
            context.currentModuleLocalHandle = symbol.symbolId;
        }
    }
}

void SmartRelationshipBuilder::setupAnalysisContextFromSymbols(
    const QString& fileName,
    const QList<sym_list::SymbolInfo>& fileSymbols,
    AnalysisContext& context)
{
    setupAnalysisContextFromSymbols(fileName, fileSymbols, nullptr, context);
}

void SmartRelationshipBuilder::setupAnalysisContextFromSymbols(
    const QString& fileName,
    const QList<sym_list::SymbolInfo>& fileSymbols,
    const SemanticIndexSnapshot* snapshot,
    AnalysisContext& context)
{
    context.currentFileName = fileName;
    context.fileSymbols = fileSymbols;
    context.localSymbolHandles.clear();
    context.localHandleToType.clear();
    context.snapshot = snapshot;

    for (const sym_list::SymbolInfo& symbol : std::as_const(fileSymbols)) {
        context.localSymbolHandles[symbol.symbolName] = symbol.symbolId;
        context.localHandleToType[symbol.symbolId] = symbol.symbolType;

        if (SymbolTaxonomy::isModuleDeclaration(symbol)
            && context.currentModuleLocalHandle == -1) {
            context.currentModuleName = symbol.symbolName;
            context.currentModuleLocalHandle = symbol.symbolId;
        }
    }
}

void SmartRelationshipBuilder::ensureRelationshipInfo(const QString& content,
                                                      AnalysisContext& context)
{
    if (context.relationshipInfoLoaded || !m_slangManager)
        return;

    context.relationshipInfo =
        m_slangManager->extractRelationshipInfo(context.currentFileName,
                                                content,
                                                context.includeDirs,
                                                context.defines);
    context.relationshipInfoLoaded = true;
}
