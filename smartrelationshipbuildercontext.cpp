#include "smartrelationshipbuilder.h"
#include "semanticindex.h"
#include "symboltaxonomy.h"

#include <utility>

void SmartRelationshipBuilder::setupAnalysisContext(const QString& fileName,
                                                    AnalysisContext& context)
{
    context.currentFileName = fileName;
    context.fileSymbols = symbolDatabase->findSymbolsByFileName(fileName);
    context.fileSymbolRecords =
        semanticSymbolRecordsForSymbols(context.fileSymbols);
    context.localSymbolHandles.clear();
    context.localHandleToType.clear();

    for (const SemanticSymbolRecord& record : std::as_const(context.fileSymbolRecords)) {
        context.localSymbolHandles[record.name] = record.localHandle;
        context.localHandleToType[record.localHandle] = record.rawCollectorKind;

        if (SymbolTaxonomy::isModuleDeclaration(
                semanticMetadataForSymbolRecord(record))
            && context.currentModuleLocalHandle == -1) {
            context.currentModuleName = record.name;
            context.currentModuleLocalHandle = record.localHandle;
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
    context.fileSymbolRecords = semanticSymbolRecordsForSymbols(fileSymbols);
    context.localSymbolHandles.clear();
    context.localHandleToType.clear();
    context.snapshot = snapshot;

    for (const SemanticSymbolRecord& record : std::as_const(context.fileSymbolRecords)) {
        context.localSymbolHandles[record.name] = record.localHandle;
        context.localHandleToType[record.localHandle] = record.rawCollectorKind;

        if (SymbolTaxonomy::isModuleDeclaration(
                semanticMetadataForSymbolRecord(record))
            && context.currentModuleLocalHandle == -1) {
            context.currentModuleName = record.name;
            context.currentModuleLocalHandle = record.localHandle;
        }
    }
}

void SmartRelationshipBuilder::setupAnalysisContextFromRecords(
    const QString& fileName,
    const QList<SemanticSymbolRecord>& fileSymbolRecords,
    const SemanticIndexSnapshot* snapshot,
    AnalysisContext& context)
{
    context.currentFileName = fileName;
    context.fileSymbolRecords = fileSymbolRecords;
    context.fileSymbols.clear();
    context.localSymbolHandles.clear();
    context.localHandleToType.clear();
    context.snapshot = snapshot;

    for (const SemanticSymbolRecord& record : std::as_const(fileSymbolRecords)) {
        context.localSymbolHandles[record.name] = record.localHandle;
        context.localHandleToType[record.localHandle] = record.rawCollectorKind;

        if (SymbolTaxonomy::isModuleDeclaration(
                semanticMetadataForSymbolRecord(record))
            && context.currentModuleLocalHandle == -1) {
            context.currentModuleName = record.name;
            context.currentModuleLocalHandle = record.localHandle;
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
