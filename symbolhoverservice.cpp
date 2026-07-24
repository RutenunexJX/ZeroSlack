#include "symbolhoverservice.h"

#include "editorsemanticcontextservice.h"
#include "effectivevalueservice.h"
#include "sourcenavigationservice.h"
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

QString macroSignatureText(const SemanticSymbolRecord& record)
{
    if (record.name.isEmpty())
        return QString();
    if (record.type.modportName.isEmpty())
        return record.name;
    return QStringLiteral("%1(%2)")
        .arg(record.name, record.type.modportName);
}

QString macroBodyText(const SemanticSymbolRecord& record)
{
    constexpr int maxCharacters = 180;
    QString body = record.type.rawTypeText.trimmed();
    if (body.isEmpty())
        return QStringLiteral("<empty>");
    body.replace(QStringLiteral("\r\n"), QStringLiteral("\\n"));
    body.replace(QLatin1Char('\r'), QStringLiteral("\\n"));
    body.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    if (body.size() <= maxCharacters)
        return body;
    return body.left(maxCharacters - 3) + QStringLiteral("...");
}

bool isParameterLikeRecord(const SemanticSymbolRecord& record)
{
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    return record.declarationKind == DeclarationKind::Parameter
        || record.declarationKind == DeclarationKind::Localparam;
}

bool isEnumMemberRecord(const SemanticSymbolRecord& record)
{
    return record.declarationKind
               == SymbolTaxonomy::DeclarationKind::Enum
        && record.collectorKind
               == SymbolTaxonomy::CollectorKind::EnumValue;
}

bool isPortRecord(const SemanticSymbolRecord& record)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    switch (record.collectorKind) {
    case CollectorKind::PortInput:
    case CollectorKind::PortOutput:
    case CollectorKind::PortInout:
    case CollectorKind::PortRef:
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
        return true;
    default:
        return false;
    }
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
        report.macroSignatureText = macroSignatureText(target.symbolRecord);
        report.macroBodyText = macroBodyText(target.symbolRecord);
    } else {
        const QString contextFile = normalizedHoverFileName(context.fileName);
        const QString targetFile = normalizedHoverFileName(target.fileName);
        const QString definitionContent =
            (!context.documentText.isEmpty()
             && !contextFile.isEmpty()
             && contextFile == targetFile)
                ? context.documentText
                : (index ? index : SemanticIndex::getInstance())
                      ->getCachedFileContent(target.fileName);
        EffectiveValueQuery valueQuery;
        valueQuery.symbol = target.symbolRecord;
        valueQuery.instanceContext = context.hierarchyInstance;
        valueQuery.documentText = definitionContent;
        if (!contextFile.isEmpty() && contextFile == targetFile)
            valueQuery.documentRevision = context.documentRevision;
        SemanticIndex* const activeIndex =
            index ? index : SemanticIndex::getInstance();
        std::unique_ptr<EffectiveValueService> localEffectiveValues;
        EffectiveValueService* effectiveValues =
            EffectiveValueService::getInstance();
        if (activeIndex != SemanticIndex::getInstance()) {
            localEffectiveValues =
                std::make_unique<EffectiveValueService>(activeIndex);
            effectiveValues = localEffectiveValues.get();
        }
        const EffectiveValueResult effective =
            effectiveValues->resolve(valueQuery);
        report.parameterLike = isParameterLikeRecord(target.symbolRecord);
        report.enumMember = isEnumMemberRecord(target.symbolRecord);
        report.port = isPortRecord(target.symbolRecord);
        const bool unboundInstanceValue =
            (report.parameterLike || report.enumMember)
            && effective.status == EffectiveValueStatus::Current
            && effective.defaultEvaluation;
        report.effectiveValueStatus =
            unboundInstanceValue
                ? EffectiveValueStatus::Unavailable
                : effective.status;
        report.instanceBound = effective.instanceBound;
        report.defaultEvaluation = effective.defaultEvaluation;
        report.declarationText = effective.declarationText;
        report.valueText =
            unboundInstanceValue
                ? QString()
                : effective.valueText;
        report.expressionText = effective.expressionText;
        report.valueSource = effective.provenance;
        report.instancePath = effective.instancePath;
        report.resolvedTypeText = effective.resolvedTypeText;
        report.packedDimensionsText = effective.packedDimensionsText;
        report.unpackedDimensionsText = effective.unpackedDimensionsText;
        report.bitWidthText = effective.bitWidthText;
        report.signednessText = effective.signednessText;
        report.interfaceName = effective.interfaceName;
        report.modportName = effective.modportName;
        report.enumTypeName = effective.enumTypeName;
        report.enumUnderlyingBitWidthText =
            !effective.bitWidthText.isEmpty()
                ? effective.bitWidthText
                : effective.enumUnderlyingBitWidthText;
        report.evaluationFailureReason =
            unboundInstanceValue
                ? QStringLiteral(
                      "No current instance context is bound; the Slang default is not shown as an effective value.")
                : effective.failureReason;
        report.typeText = report.resolvedTypeText.isEmpty()
            ? typeTextForRecord(target.symbolRecord)
            : report.resolvedTypeText;
    }
    return report;
}
