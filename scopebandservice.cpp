#include "scopebandservice.h"

#include "symboltaxonomy.h"

#include <QRegularExpression>

std::unique_ptr<ScopeBandService> ScopeBandService::instance = nullptr;

namespace {
RtlInsightCodeLink codeLinkForRecord(const SemanticSymbolRecord& record)
{
    return RtlInsightLink::fromFileLine(record.location.fileName,
                                        record.location.startLine,
                                        record.location.startColumn);
}

QString symbolDisplayNameForRecord(const SemanticSymbolRecord& record)
{
    if (!record.name.isEmpty())
        return record.name;
    return QStringLiteral("<unnamed>");
}

QString stripScopeBandCommentsFromLine(const QString& line, bool& inBlockComment)
{
    QString result;
    result.reserve(line.size());
    for (int i = 0; i < line.size(); ++i) {
        if (inBlockComment) {
            if (line.mid(i, 2) == QStringLiteral("*/")) {
                inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (line.mid(i, 2) == QStringLiteral("//"))
            break;
        if (line.mid(i, 2) == QStringLiteral("/*")) {
            inBlockComment = true;
            ++i;
            continue;
        }
        result.append(line.at(i));
    }
    return result;
}

int endModuleLineForRecord(
    const SemanticSymbolRecord& record,
    const QString& content)
{
    if (record.location.startLine <= 0)
        return record.location.endLine;

    const QStringList lines = content.split('\n');
    if (lines.isEmpty())
        return record.location.endLine;

    int moduleDepth = 0;
    int scanStart = record.location.startLine - 1;
    if (scanStart < 0)
        scanStart = 0;

    bool inBlockComment = false;
    static const QRegularExpression moduleWord(QStringLiteral("\\bmodule\\b"));
    static const QRegularExpression endmoduleWord(QStringLiteral("\\bendmodule\\b"));
    for (int i = scanStart; i < lines.size(); ++i) {
        const QString code = stripScopeBandCommentsFromLine(lines.at(i),
                                                            inBlockComment);
        if (code.contains(moduleWord))
            ++moduleDepth;
        if (code.contains(endmoduleWord)) {
            --moduleDepth;
            if (moduleDepth == 0)
                return i + 1;
        }
    }
    return record.location.endLine;
}

ScopeBandSymbolRange symbolRangeForRecord(
    const SemanticSymbolRecord& record,
    const SymbolTaxonomy::SemanticMetadata& metadata,
    int endLine)
{
    ScopeBandSymbolRange row;
    row.symbolRecord = record;
    row.symbolStableKey = record.stableKey.isValid()
        ? record.stableKey
        : SymbolStableKey();
    row.codeLink = codeLinkForRecord(record);
    row.symbolDisplayName = symbolDisplayNameForRecord(record);
    row.symbolTypeDisplayName = SymbolTaxonomy::symbolTypeLabel(metadata);
    row.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
    row.startLine = row.codeLink.line > 0
        ? row.codeLink.line
        : record.location.startLine;
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
    const QList<SemanticSymbolRecord> records =
        semantic->getSymbolRecords(query.fileName);
    const QString content = semantic->getCachedFileContent(query.fileName);
    for (const SemanticSymbolRecord& record : records) {
        const SymbolTaxonomy::SemanticMetadata metadata =
            semanticMetadataForSymbolRecord(record);
        if (SymbolTaxonomy::isModuleDeclaration(metadata)) {
            if (!semantic->isValidModuleName(record.name))
                continue;
            const int endLine = endModuleLineForRecord(record, content);
            if (endLine >= 0)
                report.modules.append(
                    symbolRangeForRecord(record, metadata, endLine));
        } else if (SymbolTaxonomy::isLogicDeclaration(metadata)) {
            const int endLine =
                record.location.endLine < record.location.startLine
                    ? record.location.startLine
                    : record.location.endLine;
            report.logics.append(
                symbolRangeForRecord(record, metadata, endLine));
        }
    }
    return report;
}

SemanticIndex* ScopeBandService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
