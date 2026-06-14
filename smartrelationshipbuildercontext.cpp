#include "smartrelationshipbuilder.h"

#include <utility>

void SmartRelationshipBuilder::setupAnalysisContext(const QString& fileName,
                                                    AnalysisContext& context)
{
    context.currentFileName = fileName;
    context.fileSymbols = symbolDatabase->findSymbolsByFileName(fileName);
    context.localSymbolIds.clear();
    context.symbolIdToType.clear();

    for (const sym_list::SymbolInfo& symbol : std::as_const(context.fileSymbols)) {
        context.localSymbolIds[symbol.symbolName] = symbol.symbolId;
        context.symbolIdToType[symbol.symbolId] = symbol.symbolType;

        if (symbol.symbolType == sym_list::sym_module && context.currentModuleId == -1) {
            context.currentModuleName = symbol.symbolName;
            context.currentModuleId = symbol.symbolId;
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
    context.localSymbolIds.clear();
    context.symbolIdToType.clear();
    context.snapshot = snapshot;

    for (const sym_list::SymbolInfo& symbol : std::as_const(fileSymbols)) {
        context.localSymbolIds[symbol.symbolName] = symbol.symbolId;
        context.symbolIdToType[symbol.symbolId] = symbol.symbolType;

        if (symbol.symbolType == sym_list::sym_module && context.currentModuleId == -1) {
            context.currentModuleName = symbol.symbolName;
            context.currentModuleId = symbol.symbolId;
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
