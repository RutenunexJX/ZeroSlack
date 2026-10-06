#include "definitionpreviewservice.h"

#include "documentmodel.h"
#include "previewsource.h"
#include "editorsemanticcontextservice.h"
#include "sourcenavigationservice.h"

#include <QtGlobal>

std::unique_ptr<DefinitionPreviewService> DefinitionPreviewService::instance = nullptr;

namespace {
constexpr int kPreviewContextBefore = 5;
constexpr int kPreviewContextAfter = 10;
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
    const EditorSemanticContext& context,
    const SourceIdentifierTarget* sourceIdentifier) const
{
    DefinitionPreviewReport report;
    const SourceIdentifierTarget identifier =
        sourceIdentifier ? *sourceIdentifier : SourceNavigationService::getInstance()->identifierAtColumn(
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
    const auto locationSnapshot = index ? index->snapshot() : nullptr;
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

    if (!index || index->snapshot() != locationSnapshot) {
        report.stale = true;
        report.unavailableReason = QStringLiteral("Analysis changed while locating the definition. Retry preview.");
        return report;
    }
    const auto source = PreviewSource::capture(target.fileName, documents, locationSnapshot);
    // Definitions supply a name start, not a reliable lexical token end (an
    // escaped identifier's semantic name omits its escape). Verify the whole
    // original target line when live text differs; appends remain valid.
    const auto excerpt = source.excerpt(target.line, target.column, 0, 0,
        kPreviewContextBefore, kPreviewContextAfter);
    report.sourceDescription = source.origin;
    report.documentId = source.documentId;
    report.documentRevision = source.documentRevision;
    report.available = excerpt.available;
    report.stale = excerpt.stale;
    report.unavailableReason = excerpt.reason;
    report.firstLineNumber = excerpt.firstLine;
    report.codeLines = excerpt.lines;
    if (!report.available) report.highlightedLine = -1;
    return report;
}
