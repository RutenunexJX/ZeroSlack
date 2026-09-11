#include "codepreviewservice.h"

#include "documentmodel.h"
#include "semanticindex.h"

#include <QFileInfo>
#include <QtGlobal>

std::unique_ptr<CodePreviewService> CodePreviewService::instance = nullptr;

namespace {
QStringList splitLinesPreservingEmptyTail(const QString& text)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines;
}

QString locationDisplayName(const QString& fileName, int line, int column)
{
    if (fileName.isEmpty() || line <= 0)
        return QString();

    QString location = QStringLiteral("%1:%2")
                           .arg(QFileInfo(fileName).fileName())
                           .arg(line);
    if (column > 0)
        location += QStringLiteral(":%1").arg(column);
    return location;
}

QString caretLineForRange(const QString& codeLine,
                          int startColumn,
                          int endColumn)
{
    if (startColumn <= 0)
        return QString();

    const int codeWidth = codeLine.size();
    const int startIndex = qBound(0, startColumn - 1, codeWidth);
    int markerWidth = 1;
    if (endColumn > startColumn)
        markerWidth = qMax(1, endColumn - startColumn);
    markerWidth = qMin(markerWidth, qMax(1, codeWidth - startIndex));

    return QString(startIndex, QLatin1Char(' '))
        + QString(markerWidth, QLatin1Char('^'));
}

RtlInsightCodeLink effectiveLinkForQuery(const CodePreviewQuery& query)
{
    if (!query.codeLink.fileName.isEmpty() && query.codeLink.line > 0)
        return query.codeLink;
    if (query.sourceRange.isValid()) {
        return RtlInsightLink::fromFileLine(query.sourceRange.fileName,
                                            query.sourceRange.line,
                                            query.sourceRange.column);
    }
    return {};
}
}

CodePreviewService* CodePreviewService::getInstance()
{
    if (!instance)
        instance = std::make_unique<CodePreviewService>();
    return instance.get();
}

CodePreviewService::CodePreviewService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

CodePreviewService::~CodePreviewService() = default;

void CodePreviewService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

void CodePreviewService::setDocumentModel(DocumentModel* documentModel)
{
    documents = documentModel;
}

CodePreviewReport CodePreviewService::previewForCodeLink(
    const CodePreviewQuery& query) const
{
    CodePreviewReport report;
    report.title = query.title;
    report.detail = query.detail;

    const RtlInsightCodeLink link = effectiveLinkForQuery(query);
    if (link.fileName.isEmpty() || link.line <= 0) {
        report.unavailableReason =
            QStringLiteral("Code location unavailable.");
        return report;
    }

    report.fileName = link.fileName;
    report.fileDisplayName = RtlInsightLink::fileDisplayName(link.fileName);
    report.targetLine = link.line;
    report.targetColumn = link.column;
    report.targetEndLine = query.sourceRange.endLine;
    report.targetEndColumn = query.sourceRange.endColumn;
    report.highlightedLine = link.line;
    report.locationDisplayName =
        locationDisplayName(link.fileName, link.line, link.column);

    const QString text = previewTextForFile(link.fileName);
    if (text.isEmpty()) {
        report.unavailableReason =
            QStringLiteral("Code preview unavailable.");
        return report;
    }

    const QStringList lines = splitLinesPreservingEmptyTail(text);
    if (link.line <= 0 || link.line > lines.size()) {
        report.unavailableReason =
            QStringLiteral("Code preview line unavailable.");
        return report;
    }

    const int before = qMax(0, query.contextBefore);
    const int after = qMax(0, query.contextAfter);
    const int firstLine = qMax(1, link.line - before);
    const int lastLine = qMin(lines.size(), link.line + after);
    report.firstLineNumber = firstLine;
    for (int line = firstLine; line <= lastLine; ++line)
        report.codeLines.append(lines.at(line - 1));

    report.preciseRange = query.sourceRange.isValid()
        && query.sourceRange.fileName == link.fileName
        && query.sourceRange.line == link.line;
    if (report.preciseRange) {
        const QString targetCodeLine = lines.at(link.line - 1);
        report.caretLine = caretLineForRange(targetCodeLine,
                                             query.sourceRange.column,
                                             query.sourceRange.endColumn);
        report.caretLineNumber = link.line;
    } else if (link.column > 0) {
        const QString targetCodeLine = lines.at(link.line - 1);
        report.caretLine = caretLineForRange(targetCodeLine,
                                             link.column,
                                             link.column + 1);
        report.caretLineNumber = link.line;
    }

    report.available = true;
    return report;
}

QString CodePreviewService::previewTextForFile(const QString& fileName) const
{
    if (fileName.isEmpty())
        return QString();

    if (documents) {
        const QString openText = documents->documentTextForFile(fileName);
        if (!openText.isEmpty())
            return openText;
    }

    return index ? index->getCachedFileContent(fileName) : QString();
}
