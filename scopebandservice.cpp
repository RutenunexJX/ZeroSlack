#include "scopebandservice.h"

#include "symboltaxonomy.h"

std::unique_ptr<ScopeBandService> ScopeBandService::instance = nullptr;

namespace {
SymbolTaxonomy::SemanticMetadata semanticMetadataForRecord(
    const SemanticSymbolRecord& record)
{
    SymbolTaxonomy::SemanticMetadata metadata;
    metadata.declarationKind = record.declarationKind;
    metadata.usageRole = record.usageRole;
    metadata.ownerScope = record.owner.kind;
    metadata.visibility = record.visibility;
    metadata.sourceRole = record.sourceRole;
    metadata.rawCollectorKind = record.rawCollectorKind;
    metadata.interfaceLikeOwner = record.owner.interfaceLike;
    return metadata;
}

RtlInsightCodeLink codeLinkForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (record.isValid()) {
        const QString fileName = fallback.fileName.isEmpty()
            ? record.location.fileName
            : fallback.fileName;
        const int line = record.location.startLine > 0
            ? record.location.startLine
            : fallback.startLine;
        const int column = record.location.startColumn > 0
            ? record.location.startColumn
            : fallback.startColumn;
        return RtlInsightLink::fromFileLine(fileName, line, column);
    }
    return RtlInsightLink::fromSymbol(fallback);
}

QString symbolDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.name.isEmpty())
        return record.name;
    if (!fallback.symbolName.isEmpty())
        return fallback.symbolName;
    return QStringLiteral("<unnamed>");
}

ScopeBandSymbolRange symbolRangeForRecord(
    const sym_list::SymbolInfo& symbol,
    const SemanticSymbolRecord& record,
    const SymbolTaxonomy::SemanticMetadata& metadata,
    int endLine)
{
    ScopeBandSymbolRange row;
    row.symbolRecord = record;
    row.symbolStableKey = record.stableKey.isValid()
        ? record.stableKey
        : symbolStableKeyForSymbol(symbol);
    row.codeLink = codeLinkForRecord(record, symbol);
    row.symbolDisplayName = symbolDisplayNameForRecord(record, symbol);
    row.symbolTypeDisplayName = SymbolTaxonomy::symbolTypeLabel(metadata);
    row.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
    row.startLine = row.codeLink.line > 0
        ? row.codeLink.line
        : symbol.startLine;
    row.endLine = endLine;
    return row;
}
}

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
    const QList<sym_list::SymbolInfo> symbols =
        semanticSymbolInfoCarriersForRecords(
            semantic->getSymbolRecords(query.fileName));
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
        const SymbolTaxonomy::SemanticMetadata metadata =
            semanticMetadataForRecord(record);
        if (SymbolTaxonomy::isModuleDeclaration(metadata)) {
            if (!semantic->isValidModuleName(record.name))
                continue;
            const int endLine = semantic->findEndModuleLine(query.fileName, symbol);
            if (endLine >= 0)
                report.modules.append(
                    symbolRangeForRecord(symbol, record, metadata, endLine));
        } else if (SymbolTaxonomy::isLogicDeclaration(metadata)) {
            const int endLine = symbol.endLine < symbol.startLine
                ? symbol.startLine
                : symbol.endLine;
            report.logics.append(
                symbolRangeForRecord(symbol, record, metadata, endLine));
        }
    }
    return report;
}

SemanticIndex* ScopeBandService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
