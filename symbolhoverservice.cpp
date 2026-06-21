#include "symbolhoverservice.h"

#include "editorsemanticcontextservice.h"
#include "sourcenavigationservice.h"
#include "symboltaxonomy.h"

#include <QFileInfo>

std::unique_ptr<SymbolHoverService> SymbolHoverService::instance = nullptr;

namespace {
QString typeTextForRecord(const SemanticSymbolRecord& record)
{
    if (!record.type.rawTypeText.isEmpty())
        return record.type.rawTypeText;
    if (!record.type.resolvedTypeName.isEmpty())
        return record.type.resolvedTypeName;
    return QString();
}

QString unavailableReasonForContext(const EditorSemanticContext& context)
{
    if (context.fileName.isEmpty())
        return QStringLiteral("No source file is associated with this editor.");
    return QStringLiteral("Definition unavailable.");
}
}

SymbolHoverService* SymbolHoverService::getInstance()
{
    if (!instance)
        instance = std::make_unique<SymbolHoverService>();
    return instance.get();
}

SymbolHoverService::SymbolHoverService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance()),
      definitionNavigation(std::make_unique<DefinitionNavigationService>(index))
{
}

SymbolHoverService::~SymbolHoverService() = default;

void SymbolHoverService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
    definitionNavigation->setSemanticIndex(index);
}

SymbolHoverReport SymbolHoverService::hoverForContext(
    const EditorSemanticContext& context) const
{
    SymbolHoverReport report;
    const SourceIdentifierTarget identifier =
        SourceNavigationService::getInstance()->identifierAtColumn(
            context.lineText,
            context.column);
    if (!identifier.matched || identifier.identifier.isEmpty())
        return report;

    report.available = true;
    report.symbolName = identifier.identifier;

    DefinitionNavigationContext navigationContext;
    navigationContext.symbolName = identifier.identifier;
    navigationContext.fileName = context.fileName;
    navigationContext.moduleName = context.moduleName;
    navigationContext.lineText = context.lineText;
    navigationContext.cursorLine = context.cursorLine;
    navigationContext.column = context.column;
    const DefinitionNavigationQuery query =
        definitionNavigation->navigationQueryForContext(navigationContext);
    const DefinitionNavigationTarget target =
        definitionNavigation->resolveTarget(query);
    if (!target.found) {
        report.unavailableReason = unavailableReasonForContext(context);
        return report;
    }

    report.displayKind = target.symbolTypeText;
    report.ownerName = target.ownerDisplayName;
    report.sourceRole = target.sourceRoleDisplayName;
    report.typeText = typeTextForRecord(target.symbolRecord);
    report.definitionFile = target.fileName;
    report.definitionLine = target.line;
    return report;
}
