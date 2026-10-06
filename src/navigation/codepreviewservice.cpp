#include "codepreviewservice.h"

#include "documentmodel.h"
#include "editorfileidentity.h"
#include "previewsource.h"

#include <QFileInfo>
#include <QtGlobal>

std::unique_ptr<CodePreviewService> CodePreviewService::instance = nullptr;

namespace {
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

CodePreviewService::CodePreviewService() = default;

CodePreviewService::~CodePreviewService() = default;

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
    const bool matchingRange = query.sourceRange.isValid()
        && EditorFileIdentity::same(query.sourceRange.fileName, link.fileName)
        && query.sourceRange.line == link.line
        && query.sourceRange.column == link.column;
    report.targetEndLine = matchingRange ? query.sourceRange.endLine : 0;
    report.targetEndColumn = matchingRange ? query.sourceRange.endColumn : 0;
    report.highlightedLine = link.line;
    report.locationDisplayName =
        locationDisplayName(link.fileName, link.line, link.column);

    const auto source = PreviewSource::capture(link.fileName, documents, query.locationSnapshot);
    const auto excerpt = source.excerpt(link.line, link.column, report.targetEndLine,
        report.targetEndColumn, query.contextBefore, query.contextAfter);
    report.sourceDescription = source.origin;
    report.documentId = source.documentId;
    report.documentRevision = source.documentRevision;
    report.stale = excerpt.stale;
    report.unavailableReason = excerpt.reason;
    if (!excerpt.available) {
        report.highlightedLine = 0;
        return report;
    }
    report.firstLineNumber = excerpt.firstLine;
    report.codeLines = excerpt.lines;
    const QStringList lines = source.text.split('\n');

    report.preciseRange = matchingRange && report.targetEndLine >= link.line
        && report.targetEndColumn > 0;
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
