#include "definitionpreviewservice.h"

#include "documentmodel.h"
#include "editorsemanticcontextservice.h"
#include "sourcenavigationservice.h"

#include <QtGlobal>

std::unique_ptr<DefinitionPreviewService> DefinitionPreviewService::instance = nullptr;

namespace {
constexpr int kPreviewContextBefore = 5;
constexpr int kPreviewContextAfter = 10;

QStringList splitLinesPreservingEmptyTail(const QString& text)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines;
}
}

DefinitionPreviewService* DefinitionPreviewService::getInstance()
{
    if (!instance)
        instance = std::make_unique<DefinitionPreviewService>();
    return instance.get();
}

DefinitionPreviewService::DefinitionPreviewService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance()),
      definitionNavigation(std::make_unique<DefinitionNavigationService>(index))
{
}

DefinitionPreviewService::~DefinitionPreviewService() = default;

void DefinitionPreviewService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
    definitionNavigation->setSemanticIndex(index);
}

void DefinitionPreviewService::setDocumentModel(DocumentModel* documentModel)
{
    documents = documentModel;
}

DefinitionPreviewReport DefinitionPreviewService::previewForContext(
    const EditorSemanticContext& context) const
{
    DefinitionPreviewReport report;
    const SourceIdentifierTarget identifier =
        SourceNavigationService::getInstance()->identifierAtColumn(
            context.lineText,
            context.column);
    if (!identifier.matched || identifier.identifier.isEmpty()) {
        report.unavailableReason =
            QStringLiteral("No symbol under cursor.");
        return report;
    }

    DefinitionNavigationContext navigationContext;
    navigationContext.symbolName = identifier.identifier;
    navigationContext.fileName = context.fileName;
    navigationContext.moduleName = context.moduleName;
    navigationContext.lineText = context.lineText;
    navigationContext.cursorLine = context.cursorLine;
    navigationContext.column = context.column;
    const DefinitionNavigationTarget target =
        definitionNavigation->resolveTarget(
            definitionNavigation->navigationQueryForContext(navigationContext));
    if (!target.found) {
        report.symbolName = identifier.identifier;
        report.unavailableReason =
            QStringLiteral("Definition unavailable.");
        return report;
    }

    report.targetResolved = true;
    report.symbolName = target.symbolName;
    report.displayKind = target.symbolTypeText;
    report.targetFile = target.fileName;
    report.targetLine = target.line;
    report.targetColumn = target.column;
    report.highlightedLine = target.line;

    const QString text = previewTextForFile(target.fileName);
    if (text.isEmpty()) {
        report.unavailableReason =
            QStringLiteral("Definition found, preview unavailable.");
        return report;
    }

    const QStringList lines = splitLinesPreservingEmptyTail(text);
    if (target.line <= 0 || target.line > lines.size()) {
        report.unavailableReason =
            QStringLiteral("Definition found, preview line unavailable.");
        return report;
    }

    const int firstLine = qMax(1, target.line - kPreviewContextBefore);
    const int lastLine =
        qMin(lines.size(), target.line + kPreviewContextAfter);
    report.firstLineNumber = firstLine;
    for (int line = firstLine; line <= lastLine; ++line)
        report.codeLines.append(lines.at(line - 1));
    report.available = true;
    return report;
}

QString DefinitionPreviewService::previewTextForFile(
    const QString& fileName) const
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
