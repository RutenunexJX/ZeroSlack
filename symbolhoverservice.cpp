#include "symbolhoverservice.h"

#include "editorsemanticcontextservice.h"
#include "sourcenavigationservice.h"
#include "svmacrosemantics.h"
#include "symboltaxonomy.h"

#include <QDir>
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

QString normalizedHoverFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool identifierLooksLikeMacroInvocation(const QString& lineText,
                                        const SourceIdentifierTarget& identifier)
{
    return identifier.matched
        && identifier.startColumn > 0
        && identifier.startColumn <= lineText.size()
        && lineText.at(identifier.startColumn - 1) == QLatin1Char('`');
}

QString macroUnavailableReason(const QString& name)
{
    return QStringLiteral("Macro `%1` was not found in the current file or indexed workspace/include files.")
        .arg(name);
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
        report.unavailableReason =
            identifierLooksLikeMacroInvocation(context.lineText, identifier)
                ? macroUnavailableReason(identifier.identifier)
                : unavailableReasonForContext(context);
        return report;
    }

    report.displayKind = target.symbolTypeText;
    report.ownerName = target.ownerDisplayName;
    report.sourceRole = target.sourceRoleDisplayName;
    report.definitionFile = target.fileName;
    report.definitionLine = target.line;
    if (target.symbolRecord.declarationKind
        == SymbolTaxonomy::DeclarationKind::Macro) {
        const QString contextFile = normalizedHoverFileName(context.fileName);
        const QString targetFile = normalizedHoverFileName(target.fileName);
        const QString definitionContent =
            (!context.documentText.isEmpty()
             && !contextFile.isEmpty()
             && contextFile == targetFile)
                ? context.documentText
                : (index ? index : SemanticIndex::getInstance())
                      ->getCachedFileContent(target.fileName);
        const SvMacroSemantics::MacroDefinition definition =
            SvMacroSemantics::macroDefinitionFromRecord(target.symbolRecord,
                                                        definitionContent);
        report.macroSignatureText =
            SvMacroSemantics::macroSignatureText(definition);
        report.macroBodyText =
            SvMacroSemantics::truncatedMacroBody(definition);
    } else {
        report.typeText = typeTextForRecord(target.symbolRecord);
    }
    return report;
}
