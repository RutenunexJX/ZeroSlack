#include "slangsymbolpresentation.h"
#include "slangsymbolcollectorhelpers.h"

#include <slang/analysis/AnalysisManager.h>
#include <slang/analysis/ValueDriver.h>
#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/EvalContext.h>
#include <slang/ast/expressions/LiteralExpressions.h>
#include <slang/ast/expressions/OperatorExpressions.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxPrinter.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/text/SourceManager.h>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <exception>
#include <limits>
#include <type_traits>
#include <utility>

namespace {
using namespace slang::ast;
using namespace slang::syntax;

QString normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString result = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

QString flattenSyntaxText(QString text)
{
    QString result;
    result.reserve(text.size());
    bool inString = false;
    bool escaped = false;
    bool pendingSpace = false;
    for (const QChar ch : std::as_const(text)) {
        if (inString) {
            result.append(ch);
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            if (pendingSpace && !result.isEmpty())
                result.append(QLatin1Char(' '));
            pendingSpace = false;
            inString = true;
            result.append(ch);
            continue;
        }
        if (ch.isSpace()) {
            pendingSpace = !result.isEmpty();
            continue;
        }
        if (pendingSpace)
            result.append(QLatin1Char(' '));
        pendingSpace = false;
        result.append(ch);
    }
    return result.trimmed();
}

QString printedSyntax(const SyntaxNode& syntax)
{
    SyntaxPrinter printer;
    printer.setIncludeComments(false);
    printer.setIncludeDirectives(false);
    printer.setIncludeSkipped(false);
    printer.setExpandIncludes(false);
    printer.setExpandMacros(false);
    printer.setSquashNewlines(true);
    printer.printExcludingLeadingComments(syntax);
    return flattenSyntaxText(
        QString::fromStdString(std::string(printer.str())));
}

QString printedToken(slang::parsing::Token token)
{
    if (!token)
        return QString();
    SyntaxPrinter printer;
    printer.setIncludeComments(false);
    printer.setIncludeDirectives(false);
    printer.setIncludeSkipped(false);
    printer.setExpandIncludes(false);
    printer.setExpandMacros(false);
    printer.setSquashNewlines(true);
    printer.print(token);
    return flattenSyntaxText(
        QString::fromStdString(std::string(printer.str())));
}

QString joinedParts(const QStringList& parts)
{
    QStringList result;
    for (const QString& part : parts) {
        const QString clean = flattenSyntaxText(part);
        if (!clean.isEmpty())
            result.append(clean);
    }
    return result.join(QLatin1Char(' '));
}

QString explicitAnsiPortDeclarationText(
    const ExplicitAnsiPortSyntax& port)
{
    SyntaxPrinter printer;
    printer.setIncludeComments(false);
    printer.setIncludeDirectives(false);
    printer.setIncludeSkipped(false);
    printer.setExpandIncludes(false);
    printer.setExpandMacros(false);
    printer.setSquashNewlines(true);
    if (port.direction)
        printer.print(port.direction);
    printer.print(port.dot);
    printer.print(port.name);
    printer.print(port.openParen);
    if (port.expr)
        printer.print(*port.expr);
    printer.print(port.closeParen);
    return flattenSyntaxText(
        QString::fromStdString(std::string(printer.str())));
}

QString dimensionsText(
    const SyntaxList<VariableDimensionSyntax>& dimensions)
{
    QStringList parts;
    for (const VariableDimensionSyntax* dimension : dimensions) {
        if (dimension)
            parts.append(printedSyntax(*dimension));
    }
    return joinedParts(parts);
}

QString sourcePackedDimensionsText(const PortHeaderSyntax& header)
{
    const DataTypeSyntax* dataType = nullptr;
    if (header.kind == SyntaxKind::VariablePortHeader) {
        constexpr auto typeMember = &VariablePortHeaderSyntax::dataType;
        dataType = header.as<VariablePortHeaderSyntax>().*typeMember;
    } else if (header.kind == SyntaxKind::NetPortHeader) {
        constexpr auto typeMember = &NetPortHeaderSyntax::dataType;
        dataType = header.as<NetPortHeaderSyntax>().*typeMember;
    }
    if (!dataType)
        return QString();

    QStringList parts;
    auto visitor = makeSyntaxVisitor(
        [&](auto& nestedVisitor,
            const VariableDimensionSyntax& dimension) {
            parts.append(printedSyntax(dimension));
            nestedVisitor.visitDefault(dimension);
        });
    dataType->visit(visitor);
    return joinedParts(parts);
}

QString portSourcePresentationKey(
    const slang::SourceManager* sourceManager,
    slang::SourceLocation location,
    const QString& portName)
{
    if (!sourceManager || !location.valid() || portName.isEmpty())
        return QString();
    const auto position =
        slang_symbols::detail::qTextDocumentSourcePosition(
            sourceManager, location);
    if (!position.isValid())
        return QString();
    return QStringLiteral("%1|%2|%3|o")
        .arg(normalizedFileName(position.fileName))
        .arg(position.position)
        .arg(portName);
}

void appendAnsiPortPresentations(
    const ModuleDeclarationSyntax& module,
    const slang::SourceManager* sourceManager,
    QHash<QString, SemanticSymbolPresentation>* result)
{
    if (!result || !module.header->ports
        || module.header->ports->kind != SyntaxKind::AnsiPortList) {
        return;
    }

    const auto& ports = module.header->ports->as<AnsiPortListSyntax>();
    QString inheritedHeader;
    QString inheritedPackedDimensions;
    for (const MemberSyntax* member : ports.ports) {
        if (!member)
            continue;

        if (member->kind == SyntaxKind::ExplicitAnsiPort) {
            const auto& port = member->as<ExplicitAnsiPortSyntax>();
            const QString name = QString::fromStdString(
                std::string(port.name.valueText()));
            const QString key = portSourcePresentationKey(
                sourceManager, port.name.location(), name);
            if (!key.isEmpty()) {
                SemanticSymbolPresentation presentation;
                presentation.declarationText =
                    explicitAnsiPortDeclarationText(port);
                result->insert(key, std::move(presentation));
            }
            continue;
        }
        if (member->kind != SyntaxKind::ImplicitAnsiPort)
            continue;

        const auto& port = member->as<ImplicitAnsiPortSyntax>();
        QString header = printedSyntax(*port.header);
        QString packed = sourcePackedDimensionsText(*port.header);
        if (header.isEmpty()) {
            header = inheritedHeader;
            packed = inheritedPackedDimensions;
        } else {
            inheritedHeader = header;
            inheritedPackedDimensions = packed;
        }

        const QString name = QString::fromStdString(
            std::string(port.declarator->name.valueText()));
        const QString key = portSourcePresentationKey(
            sourceManager, port.declarator->name.location(), name);
        if (key.isEmpty())
            continue;

        SemanticSymbolPresentation presentation;
        presentation.declarationText = joinedParts(
            {header,
             printedSyntax(*port.declarator)});
        presentation.packedDimensionsText = packed;
        presentation.unpackedDimensionsText =
            dimensionsText(port.declarator->dimensions);
        result->insert(key, std::move(presentation));
    }
}

void appendNonAnsiPortPresentations(
    const ModuleDeclarationSyntax& module,
    const slang::SourceManager* sourceManager,
    QHash<QString, SemanticSymbolPresentation>* result)
{
    if (!result)
        return;

    for (const MemberSyntax* member : module.members) {
        if (!member || member->kind != SyntaxKind::PortDeclaration)
            continue;

        const auto& declaration = member->as<PortDeclarationSyntax>();
        const QString header = printedSyntax(*declaration.header);
        const QString packed =
            sourcePackedDimensionsText(*declaration.header);
        for (const DeclaratorSyntax* declarator : declaration.declarators) {
            if (!declarator)
                continue;
            const QString name = QString::fromStdString(
                std::string(declarator->name.valueText()));
            const QString key = portSourcePresentationKey(
                sourceManager, declarator->name.location(), name);
            if (key.isEmpty())
                continue;

            SemanticSymbolPresentation presentation;
            presentation.declarationText = joinedParts(
                {header, printedSyntax(*declarator)});
            presentation.packedDimensionsText = packed;
            presentation.unpackedDimensionsText =
                dimensionsText(declarator->dimensions);
            result->insert(key, std::move(presentation));
        }
    }
}

QHash<QString, SemanticSymbolPresentation> collectPortSourcePresentations(
    Compilation& compilation,
    const slang::SourceManager* sourceManager,
    const std::function<bool()>& isCancelled)
{
    QHash<QString, SemanticSymbolPresentation> result;
    if (!sourceManager)
        return result;

    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    for (const std::shared_ptr<SyntaxTree>& tree :
         compilation.getSyntaxTrees()) {
        if (!tree || cancelled())
            continue;
        auto visitor = makeSyntaxVisitor(
            [&](auto& nestedVisitor,
                const ModuleDeclarationSyntax& module) {
                if (cancelled())
                    return;
                appendAnsiPortPresentations(module,
                                            sourceManager,
                                            &result);
                appendNonAnsiPortPresentations(module,
                                               sourceManager,
                                               &result);
                nestedVisitor.visitDefault(module);
            });
        tree->root().visit(visitor);
    }
    return result;
}

void mergePortSourcePresentation(
    const SemanticSymbolPresentation& source,
    SemanticSymbolPresentation* destination)
{
    if (!destination || source.declarationText.isEmpty())
        return;
    destination->declarationText = source.declarationText;
    destination->packedDimensionsText = source.packedDimensionsText;
    destination->unpackedDimensionsText = source.unpackedDimensionsText;
}

QChar identityKind(const SemanticSymbolRecord& record)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    switch (record.collectorKind) {
    case CollectorKind::Parameter:
    case CollectorKind::ModuleParameter:
    case CollectorKind::InterfaceParameter:
    case CollectorKind::DefParameter:
    case CollectorKind::Localparam:
        return QLatin1Char('p');
    case CollectorKind::EnumValue:
        return QLatin1Char('e');
    case CollectorKind::PortInput:
    case CollectorKind::PortOutput:
    case CollectorKind::PortInout:
    case CollectorKind::PortRef:
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
        return QLatin1Char('o');
    case CollectorKind::Typedef:
        return QLatin1Char('t');
    default:
        return record.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Signal
                   ? QLatin1Char('v')
                   : QLatin1Char('?');
    }
}

QChar identityKind(const Symbol& symbol)
{
    switch (symbol.kind) {
    case SymbolKind::Parameter:
        return QLatin1Char('p');
    case SymbolKind::EnumValue:
        return QLatin1Char('e');
    case SymbolKind::Port:
    case SymbolKind::InterfacePort:
        return QLatin1Char('o');
    case SymbolKind::TypeAlias:
        return QLatin1Char('t');
    case SymbolKind::Variable:
    case SymbolKind::Net:
        return QLatin1Char('v');
    default:
        return QLatin1Char('?');
    }
}

QString identityKey(const slang::SourceManager* sourceManager,
                    const Symbol& symbol)
{
    if (!sourceManager || !symbol.location.valid())
        return QString();
    const slang_symbols::detail::QTextDocumentSourcePosition position =
        slang_symbols::detail::qTextDocumentSourcePosition(
            sourceManager, symbol.location);
    const QString fileName = normalizedFileName(position.fileName);
    if (fileName.isEmpty() || !position.isValid())
        return QString();
    return QStringLiteral("%1|%2|%3|%4")
        .arg(fileName)
        .arg(position.position)
        .arg(QString::fromStdString(std::string(symbol.name)))
        .arg(identityKind(symbol));
}

QString identityKey(const SemanticSymbolRecord& record)
{
    const QString fileName = normalizedFileName(record.location.fileName);
    if (fileName.isEmpty() || record.location.position < 0)
        return QString();
    return QStringLiteral("%1|%2|%3|%4")
        .arg(fileName)
        .arg(record.location.position)
        .arg(record.name)
        .arg(identityKind(record));
}

QString exactConstantText(const slang::ConstantValue& value)
{
    if (!value)
        return QString();
    return QString::fromStdString(value.toString(
        std::numeric_limits<slang::bitwidth_t>::max(),
        true));
}

QString semanticRangeText(const slang::ConstantRange& range)
{
    return QString::fromStdString(range.toString());
}

QString semanticDeclarationIdentity(
    const slang::SourceManager* sourceManager,
    const Symbol& symbol)
{
    if (!sourceManager || !symbol.location.valid())
        return QString();
    const auto position =
        slang_symbols::detail::qTextDocumentSourcePosition(
            sourceManager, symbol.location);
    const QString fileName = normalizedFileName(position.fileName);
    if (fileName.isEmpty() || !position.isValid())
        return QString();
    return QStringLiteral("%1|%2|%3|%4")
        .arg(fileName)
        .arg(position.position)
        .arg(QString::fromStdString(std::string(symbol.name)))
        .arg(static_cast<int>(symbol.kind));
}

QString expressionKindName(ExpressionKind kind)
{
    return QString::fromStdString(
        std::string(toString(kind)));
}

QList<const Expression*> directExpressionChildren(
    const Expression& expression)
{
    QList<const Expression*> result;
    auto childCollector = makeVisitor(
        [&](auto&, const auto& child) {
            using Child =
                std::remove_cvref_t<decltype(child)>;
            if constexpr (std::is_base_of_v<Expression, Child>)
                result.append(&child);
        });
    auto dispatcher = makeVisitor(
        [&](auto&, const auto& concreteExpression) {
            using Concrete =
                std::remove_cvref_t<
                    decltype(concreteExpression)>;
            if constexpr (
                std::is_base_of_v<Expression, Concrete>
                && requires {
                       concreteExpression.visitExprs(
                           childCollector);
                   }) {
                concreteExpression.visitExprs(childCollector);
            }
        });
    expression.visit(dispatcher);
    return result;
}

slang::ConstantValue evaluatedExpressionValue(
    const Expression& expression,
    const Symbol& contextSymbol)
{
    if (const slang::ConstantValue* cached =
            expression.getConstant()) {
        return *cached;
    }
    EvalContext context(contextSymbol);
    return expression.eval(context);
}

const SelectorSyntax* selectorSyntaxForDimension(
    const SyntaxNode* syntax)
{
    if (!syntax)
        return nullptr;

    if (syntax->kind == SyntaxKind::VariableDimension) {
        const auto& dimension =
            syntax->as<VariableDimensionSyntax>();
        if (!dimension.specifier
            || dimension.specifier->kind
                   != SyntaxKind::RangeDimensionSpecifier) {
            return nullptr;
        }
        return dimension.specifier
            ->as<RangeDimensionSpecifierSyntax>()
            .selector;
    }
    if (syntax->kind == SyntaxKind::ElementSelect)
        return syntax->as<ElementSelectSyntax>().selector;
    return SelectorSyntax::isKind(syntax->kind)
        ? &syntax->as<SelectorSyntax>()
        : nullptr;
}

struct DeclaredDimensionSyntaxes {
    QList<const SelectorSyntax*> packed;
    QList<const SelectorSyntax*> unpacked;
};

DeclaredDimensionSyntaxes declaredDimensionSyntaxes(
    const DeclaredType* declaredType)
{
    DeclaredDimensionSyntaxes result;
    if (!declaredType)
        return result;

    QSet<const SelectorSyntax*> seen;
    const auto append =
        [&seen](QList<const SelectorSyntax*>* target,
                const SelectorSyntax* selector) {
            if (!target || !selector || seen.contains(selector))
                return;
            seen.insert(selector);
            target->append(selector);
        };

    if (const auto* dimensions =
            declaredType->getDimensionSyntax()) {
        for (const VariableDimensionSyntax* dimension :
             *dimensions) {
            append(
                &result.unpacked,
                dimension
                    ? selectorSyntaxForDimension(dimension)
                    : nullptr);
        }
    }

    if (const DataTypeSyntax* typeSyntax =
            declaredType->getTypeSyntax()) {
        auto visitor = makeSyntaxVisitor(
            [&](auto&,
                const VariableDimensionSyntax& dimension) {
                append(
                    &result.packed,
                    selectorSyntaxForDimension(&dimension));
            },
            [&](auto&,
                const ElementSelectSyntax& dimension) {
                append(
                    &result.packed,
                    selectorSyntaxForDimension(&dimension));
            });
        typeSyntax->visit(visitor);
    }
    return result;
}

bool syntaxContainsTypeDimension(const SyntaxNode& syntax)
{
    bool found = false;
    auto visitor = makeSyntaxVisitor(
        [&](auto&, const VariableDimensionSyntax&) {
            found = true;
        },
        [&](auto&, const ElementSelectSyntax&) {
            found = true;
        });
    syntax.visit(visitor);
    return found;
}

class SemanticConstantGraphBuilder
{
public:
    SemanticConstantGraphBuilder(
        const slang::SourceManager* sourceManager,
        SemanticConstantDependencyGraph* graph) :
        m_sourceManager(sourceManager),
        m_graph(graph)
    {
        if (m_graph)
            m_graph->complete = true;
    }

    QString buildBoundExpression(
        const ExpressionSyntax& syntax,
        const Symbol& contextSymbol,
        const QString& idPrefix)
    {
        if (!m_graph || !contextSymbol.getParentScope()) {
            fail(QStringLiteral(
                "Slang has no scope in which to bind a dimension bound."));
            return QString();
        }

        try {
            ASTContext context(
                *contextSymbol.getParentScope(),
                LookupLocation::after(contextSymbol),
                ASTFlags::NoReference
                    | ASTFlags::NonProcedural);
            const Expression& expression =
                Expression::bind(syntax, context);
            return buildExpression(
                expression, contextSymbol, idPrefix);
        } catch (const std::exception& error) {
            fail(QStringLiteral(
                     "Slang failed to bind a dimension bound: %1")
                     .arg(QString::fromUtf8(error.what())));
        } catch (...) {
            fail(QStringLiteral(
                "Slang failed to bind a dimension bound."));
        }
        return QString();
    }

    QString ensureParameter(const ParameterSymbol& parameter)
    {
        if (!m_graph)
            return QString();

        const QString identity =
            parameterIdentity(parameter);
        if (identity.isEmpty()) {
            fail(QStringLiteral(
                "A referenced parameter has no stable Slang identity."));
            return QString();
        }

        const auto existing =
            m_parameterIndexByIdentity.constFind(identity);
        if (existing
            != m_parameterIndexByIdentity.constEnd()) {
            if (m_parameterStates.value(identity)
                == ParameterState::Building) {
                fail(QStringLiteral(
                    "Slang exposed a cyclic parameter dependency."));
            }
            return identity;
        }

        SemanticParameterDependencyNode node;
        node.identity = identity;
        node.name = QString::fromStdString(
            std::string(parameter.name));
        node.localparam = parameter.isLocalParam();
        node.evidenceId = identity;
        const int parameterIndex = m_graph->parameters.size();
        m_graph->parameters.append(node);
        m_parameterIndexByIdentity.insert(
            identity, parameterIndex);
        m_parameterStates.insert(
            identity, ParameterState::Building);

        const Expression* initializer =
            parameter.getInitializer();
        QHash<QString, const ParameterSymbol*> dependencies;
        if (initializer) {
            initializer->visitSymbolReferences(
                [&](const Expression&, const Symbol& symbol) {
                    const auto* dependency =
                        symbol.as_if<ParameterSymbol>();
                    if (!dependency)
                        return;
                    const QString dependencyIdentity =
                        parameterIdentity(*dependency);
                    if (!dependencyIdentity.isEmpty()) {
                        dependencies.insert(
                            dependencyIdentity,
                            dependency);
                    }
                });
        }

        QStringList dependencyIds = dependencies.keys();
        std::sort(dependencyIds.begin(), dependencyIds.end());
        for (const QString& dependencyId :
             std::as_const(dependencyIds)) {
            const ParameterSymbol* dependency =
                dependencies.value(dependencyId);
            if (dependency)
                ensureParameter(*dependency);
        }

        QString rootId;
        if (initializer) {
            rootId = buildExpression(
                *initializer,
                parameter,
                identity + QStringLiteral(":root"));
        } else {
            fail(QStringLiteral(
                     "Parameter %1 has no bound initializer.")
                     .arg(node.name));
        }

        const slang::ConstantValue& value =
            parameter.getValue();
        SemanticParameterDependencyNode& stored =
            m_graph->parameters[parameterIndex];
        stored.rootEvaluationNodeId = rootId;
        stored.dependencyIds = dependencyIds;
        stored.valueText = exactConstantText(value);
        stored.evaluated =
            value && !stored.valueText.isEmpty()
            && !rootId.isEmpty();
        if (!stored.evaluated) {
            fail(QStringLiteral(
                     "Parameter %1 has no exact Slang value.")
                     .arg(stored.name));
        } else {
            const auto rootIndex =
                m_evaluationIndexById.constFind(rootId);
            if (rootIndex
                != m_evaluationIndexById.constEnd()) {
                SemanticConstantEvaluationNode& root =
                    m_graph->evaluationNodes[*rootIndex];
                root.resultValueText = stored.valueText;
                root.evaluated = true;
            }
        }

        m_parameterStates.insert(
            identity, ParameterState::Complete);
        return identity;
    }

    QString addEvaluatedNode(
        const QString& id,
        const QString& operationName,
        const QString& expressionText,
        const QStringList& operandIds,
        const QString& resultValueText)
    {
        if (!m_graph || id.isEmpty()
            || resultValueText.isEmpty()) {
            fail(QStringLiteral(
                "A derived Slang constant node is incomplete."));
            return QString();
        }
        if (m_evaluationIndexById.contains(id))
            return id;

        SemanticConstantEvaluationNode node;
        node.id = id;
        node.operationName = operationName;
        node.expressionText = expressionText;
        node.operandIds = operandIds;
        node.resultValueText = resultValueText;
        node.evaluated = true;
        node.evidenceId = id;
        const int index = m_graph->evaluationNodes.size();
        m_graph->evaluationNodes.append(std::move(node));
        m_evaluationIndexById.insert(id, index);
        return id;
    }

    bool evaluationNodeComplete(const QString& id) const
    {
        if (!m_graph || id.isEmpty())
            return false;
        const auto index =
            m_evaluationIndexById.constFind(id);
        return index != m_evaluationIndexById.constEnd()
            && m_graph->evaluationNodes.at(*index).evaluated
            && !m_graph->evaluationNodes.at(*index)
                    .resultValueText.isEmpty();
    }

private:
    enum class ParameterState {
        Building,
        Complete
    };

    QString parameterIdentity(
        const ParameterSymbol& parameter) const
    {
        const QString declaration =
            semanticDeclarationIdentity(
                m_sourceManager, parameter);
        return declaration.isEmpty()
            ? QString()
            : QStringLiteral("slang:parameter:%1")
                  .arg(declaration);
    }

    QString buildExpression(
        const Expression& expression,
        const Symbol& contextSymbol,
        const QString& id)
    {
        if (!m_graph || id.isEmpty()) {
            fail(QStringLiteral(
                "A constant expression node has no stable identity."));
            return QString();
        }
        if (m_evaluationIndexById.contains(id))
            return id;

        SemanticConstantEvaluationNode node;
        node.id = id;
        node.operationName =
            expressionKindName(expression.kind);
        if (expression.syntax)
            node.expressionText =
                printedSyntax(*expression.syntax);
        node.evidenceId = id;

        const QList<const Expression*> children =
            directExpressionChildren(expression);
        for (int index = 0; index < children.size(); ++index) {
            const Expression* child = children.at(index);
            if (!child)
                continue;
            const QString childId = buildExpression(
                *child,
                contextSymbol,
                QStringLiteral("%1.%2").arg(id).arg(index));
            if (!childId.isEmpty())
                node.operandIds.append(childId);
        }

        if (expression.kind == ExpressionKind::NamedValue
            || expression.kind
                   == ExpressionKind::HierarchicalValue) {
            const Symbol* referenced =
                expression.getSymbolReference();
            const auto* parameter = referenced
                ? referenced->as_if<ParameterSymbol>()
                : nullptr;
            if (parameter) {
                const QString parameterId =
                    ensureParameter(*parameter);
                if (!parameterId.isEmpty()) {
                    node.parameterDependencyIds.append(
                        parameterId);
                    const int parameterIndex =
                        m_parameterIndexByIdentity.value(
                            parameterId, -1);
                    if (parameterIndex >= 0) {
                        const QString rootId =
                            m_graph->parameters
                                .at(parameterIndex)
                                .rootEvaluationNodeId;
                        if (!rootId.isEmpty()
                            && rootId != id
                            && !node.operandIds.contains(rootId)) {
                            node.operandIds.append(rootId);
                        }
                    }
                }
            }
        }

        try {
            const slang::ConstantValue value =
                evaluatedExpressionValue(
                    expression, contextSymbol);
            node.resultValueText = exactConstantText(value);
            node.evaluated =
                value && !node.resultValueText.isEmpty();
        } catch (const std::exception&) {
            node.evaluated = false;
        } catch (...) {
            node.evaluated = false;
        }
        if (!node.evaluated) {
            fail(QStringLiteral(
                     "Slang could not evaluate constant node %1.")
                     .arg(id));
        }

        const int index = m_graph->evaluationNodes.size();
        m_graph->evaluationNodes.append(std::move(node));
        m_evaluationIndexById.insert(id, index);
        return id;
    }

    void fail(const QString& reason)
    {
        if (!m_graph)
            return;
        m_graph->complete = false;
        if (m_graph->failureReason.isEmpty())
            m_graph->failureReason = reason;
    }

    const slang::SourceManager* m_sourceManager = nullptr;
    SemanticConstantDependencyGraph* m_graph = nullptr;
    QHash<QString, int> m_evaluationIndexById;
    QHash<QString, int> m_parameterIndexByIdentity;
    QHash<QString, ParameterState> m_parameterStates;
};

QString semanticTypeIdentity(
    const Type& type,
    const slang::SourceManager* sourceManager,
    bool signednessKnown,
    bool signedIntegral)
{
    const Type& canonical = type.getCanonicalType();
    if (canonical.kind == SymbolKind::EnumType
        || canonical.kind == SymbolKind::PackedStructType
        || canonical.kind == SymbolKind::UnpackedStructType
        || canonical.kind == SymbolKind::PackedUnionType
        || canonical.kind == SymbolKind::UnpackedUnionType
        || canonical.kind == SymbolKind::ClassType) {
        const QString declaration =
            semanticDeclarationIdentity(
                sourceManager, canonical);
        if (!declaration.isEmpty()) {
            return QStringLiteral("slang:nominal:%1")
                .arg(declaration);
        }
    }
    if (canonical.isIntegral()) {
        return QStringLiteral(
                   "slang:integral:%1:%2:%3:%4")
            .arg(static_cast<int>(canonical.kind))
            .arg(signednessKnown && signedIntegral
                     ? QStringLiteral("signed")
                     : QStringLiteral("unsigned"))
            .arg(canonical.isFourState()
                     ? QStringLiteral("four-state")
                     : QStringLiteral("two-state"))
            .arg(canonical.getBitWidth());
    }
    if (canonical.kind == SymbolKind::StringType)
        return QStringLiteral("slang:string");
    if (canonical.kind == SymbolKind::FloatingType) {
        return QStringLiteral("slang:floating:%1")
            .arg(static_cast<int>(
                canonical.as<FloatingType>().floatKind));
    }

    const QString declaration =
        semanticDeclarationIdentity(
            sourceManager, canonical);
    if (!declaration.isEmpty()) {
        return QStringLiteral("slang:type:%1")
            .arg(declaration);
    }
    return QString();
}

QString integralDeclarationBase(
    const Type& terminalType,
    bool signedIntegral)
{
    QString result;
    if (terminalType.kind == SymbolKind::ScalarType) {
        switch (terminalType.as<ScalarType>().scalarKind) {
        case ScalarType::Bit:
            result = QStringLiteral("bit");
            break;
        case ScalarType::Logic:
            result = QStringLiteral("logic");
            break;
        case ScalarType::Reg:
            result = QStringLiteral("reg");
            break;
        }
    } else if (terminalType.kind
               == SymbolKind::PredefinedIntegerType) {
        switch (terminalType
                    .as<PredefinedIntegerType>()
                    .integerKind) {
        case PredefinedIntegerType::ShortInt:
            result = QStringLiteral("shortint");
            break;
        case PredefinedIntegerType::Int:
            result = QStringLiteral("int");
            break;
        case PredefinedIntegerType::LongInt:
            result = QStringLiteral("longint");
            break;
        case PredefinedIntegerType::Byte:
            result = QStringLiteral("byte");
            break;
        case PredefinedIntegerType::Integer:
            result = QStringLiteral("integer");
            break;
        case PredefinedIntegerType::Time:
            result = QStringLiteral("time");
            break;
        }
    }
    if (!result.isEmpty()) {
        result += signedIntegral
            ? QStringLiteral(" signed")
            : QStringLiteral(" unsigned");
    }
    return result;
}

SemanticDeclaredTypeFacts declaredTypeFacts(
    const Symbol& publishedSymbol,
    const Symbol& bindingSymbol,
    const Type& resolvedType,
    const DeclaredType* declaredType,
    const slang::SourceManager* sourceManager)
{
    SemanticDeclaredTypeFacts result;
    result.evidenceId =
        semanticDeclarationIdentity(
            sourceManager, publishedSymbol);
    result.constants.evidenceId =
        result.evidenceId
        + QStringLiteral(":constant-graph");
    SemanticConstantGraphBuilder constantGraph(
        sourceManager, &result.constants);

    const Type* signedType = &resolvedType;
    while (signedType->kind
           == SymbolKind::FixedSizeUnpackedArrayType) {
        signedType = &signedType
                          ->as<FixedSizeUnpackedArrayType>()
                          .elementType;
    }
    result.signednessKnown = signedType->isIntegral();
    result.signedIntegral =
        result.signednessKnown && signedType->isSigned();

    int packedOrdinal = 0;
    int unpackedOrdinal = 0;
    auto appendDimension =
        [&](const Type& arrayType,
            const slang::ConstantRange& range,
            SemanticTypeDimensionKind kind,
            bool emitInDeclaration,
            const Symbol& expressionContext,
            const SelectorSyntax* declaredSelector,
            QStringList* introducedDimensionIds) {
            const int ordinal =
                kind == SemanticTypeDimensionKind::Packed
                ? packedOrdinal++
                : unpackedOrdinal++;
            SemanticTypeDimensionFact dimension;
            dimension.kind = kind;
            dimension.canonicalId =
                QStringLiteral("%1:%2:%3:%4")
                    .arg(kind
                                 == SemanticTypeDimensionKind::Packed
                             ? QStringLiteral("packed")
                             : QStringLiteral("unpacked"))
                    .arg(ordinal)
                    .arg(range.left)
                    .arg(range.right);
            dimension.elementCountText =
                QString::number(range.fullWidth());
            dimension.emitInDeclaration =
                emitInDeclaration;
            dimension.evidenceId =
                result.evidenceId
                + QStringLiteral(":dimension:")
                + dimension.canonicalId;

            const SyntaxNode* syntax =
                arrayType.getSyntax();
            // Declarations produced in a different module cannot safely
            // reuse a formal-local parameter name such as N. Render the
            // exact Slang-evaluated range; the original bound syntax remains
            // available on the evaluation nodes for preview and evidence.
            dimension.declarationText =
                semanticRangeText(range);
            const SelectorSyntax* selectorSyntax =
                declaredSelector
                ? declaredSelector
                : selectorSyntaxForDimension(syntax);
            bool boundStructureComplete = false;
            if (selectorSyntax
                && RangeSelectSyntax::isKind(
                    selectorSyntax->kind)) {
                const auto& rangeSyntax =
                    selectorSyntax->as<RangeSelectSyntax>();
                dimension.leftEvaluationNodeId =
                    constantGraph.buildBoundExpression(
                        *rangeSyntax.left,
                        expressionContext,
                        dimension.evidenceId
                            + QStringLiteral(":left"));
                dimension.rightEvaluationNodeId =
                    constantGraph.buildBoundExpression(
                        *rangeSyntax.right,
                        expressionContext,
                        dimension.evidenceId
                            + QStringLiteral(":right"));
                boundStructureComplete = true;
            } else if (selectorSyntax
                       && selectorSyntax->kind
                              == SyntaxKind::BitSelect) {
                const auto& abbreviated =
                    selectorSyntax->as<BitSelectSyntax>();
                const QString sizeNode =
                    constantGraph.buildBoundExpression(
                        *abbreviated.expr,
                        expressionContext,
                        dimension.evidenceId
                            + QStringLiteral(":size"));
                dimension.leftEvaluationNodeId =
                    constantGraph.addEvaluatedNode(
                        dimension.evidenceId
                            + QStringLiteral(":left"),
                        QStringLiteral(
                            "SlangAbbreviatedRangeLeft"),
                        syntax ? printedSyntax(*syntax)
                               : QString(),
                        {},
                        QString::number(range.left));
                dimension.rightEvaluationNodeId =
                    constantGraph.addEvaluatedNode(
                        dimension.evidenceId
                            + QStringLiteral(":right"),
                        QStringLiteral(
                            "SlangAbbreviatedRangeRight"),
                        syntax ? printedSyntax(*syntax)
                               : QString(),
                        sizeNode.isEmpty()
                            ? QStringList{}
                            : QStringList{sizeNode},
                        QString::number(range.right));
                boundStructureComplete =
                    !sizeNode.isEmpty();
            }
            dimension.complete =
                !dimension.canonicalId.isEmpty()
                && !dimension.declarationText.isEmpty()
                && boundStructureComplete
                && !dimension.leftEvaluationNodeId.isEmpty()
                && !dimension.rightEvaluationNodeId.isEmpty()
                && constantGraph.evaluationNodeComplete(
                    dimension.leftEvaluationNodeId)
                && constantGraph.evaluationNodeComplete(
                    dimension.rightEvaluationNodeId);
            if (!dimension.complete)
                result.constants.complete = false;
            if (introducedDimensionIds)
                introducedDimensionIds->append(
                    dimension.canonicalId);
            result.dimensions.append(
                std::move(dimension));
        };

    auto consumeDimensions =
        [&](const Type*& current,
            bool emitInDeclaration,
            const Symbol& expressionContext,
            const DeclaredType* sourceDeclaredType,
            QStringList* introducedDimensionIds) {
            const DeclaredDimensionSyntaxes declaredSyntaxes =
                declaredDimensionSyntaxes(sourceDeclaredType);
            int packedSyntaxIndex = 0;
            int unpackedSyntaxIndex = 0;
            while (current) {
                if (current->kind
                    == SymbolKind::FixedSizeUnpackedArrayType) {
                    const auto& array =
                        current->as<
                            FixedSizeUnpackedArrayType>();
                    appendDimension(
                        *current,
                        array.range,
                        SemanticTypeDimensionKind::Unpacked,
                        emitInDeclaration,
                        expressionContext,
                        unpackedSyntaxIndex
                                    < declaredSyntaxes.unpacked.size()
                            ? declaredSyntaxes.unpacked.at(
                                  unpackedSyntaxIndex++)
                            : nullptr,
                        introducedDimensionIds);
                    current = &array.elementType;
                    continue;
                }
                if (current->kind
                    == SymbolKind::PackedArrayType) {
                    const auto& array =
                        current->as<PackedArrayType>();
                    appendDimension(
                        *current,
                        array.range,
                        SemanticTypeDimensionKind::Packed,
                        emitInDeclaration,
                        expressionContext,
                        packedSyntaxIndex
                                    < declaredSyntaxes.packed.size()
                            ? declaredSyntaxes.packed.at(
                                  packedSyntaxIndex++)
                            : nullptr,
                        introducedDimensionIds);
                    current = &array.elementType;
                    continue;
                }
                break;
            }
        };

    const Type* current = &resolvedType;
    consumeDimensions(
        current,
        true,
        bindingSymbol,
        declaredType,
        nullptr);

    const TypeAliasType* rootAlias =
        current && current->kind == SymbolKind::TypeAlias
        ? &current->as<TypeAliasType>()
        : nullptr;
    QSet<QString> visitedAliases;
    while (current
           && current->kind == SymbolKind::TypeAlias) {
        const auto& alias = current->as<TypeAliasType>();
        SemanticTypedefResolutionStep step;
        const QString aliasIdentity =
            semanticDeclarationIdentity(
                sourceManager, alias);
        if (!aliasIdentity.isEmpty()) {
            step.sourceTypeId =
                QStringLiteral("slang:typedef:%1")
                    .arg(aliasIdentity);
        }
        step.sourceTypeName =
            QString::fromStdString(
                std::string(alias.name));
        step.declarationIdentity = aliasIdentity;
        step.evidenceId =
            step.declarationIdentity;
        if (result.rootTypeId.isEmpty())
            result.rootTypeId = step.sourceTypeId;

        if (step.sourceTypeId.isEmpty()
            || visitedAliases.contains(
                step.sourceTypeId)) {
            result.failureReason = QStringLiteral(
                "Slang exposed an unresolved or cyclic typedef chain.");
            result.constants.complete = false;
            break;
        }
        visitedAliases.insert(step.sourceTypeId);

        const Type* target =
            &alias.targetType.getType();
        consumeDimensions(
            target,
            false,
            alias,
            &alias.targetType,
            &step.introducedDimensionIds);
        if (target
            && target->kind == SymbolKind::TypeAlias) {
            const QString targetIdentity =
                semanticDeclarationIdentity(
                    sourceManager,
                    target->as<TypeAliasType>());
            if (!targetIdentity.isEmpty()) {
                step.targetTypeId =
                    QStringLiteral("slang:typedef:%1")
                        .arg(targetIdentity);
            }
        } else if (target) {
            step.targetTypeId =
                semanticTypeIdentity(
                    *target,
                    sourceManager,
                    result.signednessKnown,
                    result.signedIntegral);
        }
        result.typedefChain.append(std::move(step));
        current = target;
    }

    if (current) {
        result.canonicalTypeId =
            semanticTypeIdentity(
                *current,
                sourceManager,
                result.signednessKnown,
                result.signedIntegral);
    }
    if (result.rootTypeId.isEmpty())
        result.rootTypeId = result.canonicalTypeId;

    if (declaredType) {
        if (const DataTypeSyntax* syntax =
                declaredType->getTypeSyntax()) {
            if (!syntaxContainsTypeDimension(*syntax))
                result.declarationBaseText =
                    printedSyntax(*syntax);
        }
    }
    if (result.declarationBaseText.isEmpty()
        && rootAlias) {
        result.declarationBaseText =
            QString::fromStdString(
                std::string(rootAlias->name));
    }
    if (result.declarationBaseText.isEmpty()
        && current) {
        if (current->isIntegral()) {
            result.declarationBaseText =
                integralDeclarationBase(
                    *current,
                    result.signedIntegral);
        }
        if (result.declarationBaseText.isEmpty()) {
            result.declarationBaseText =
                QString::fromStdString(
                    current->toString());
        }
    }

    QStringList declarationShape{
        result.rootTypeId,
        result.declarationBaseText,
    };
    for (const SemanticTypeDimensionFact& dimension :
         std::as_const(result.dimensions)) {
        if (!dimension.emitInDeclaration)
            continue;
        declarationShape.append(
            QStringLiteral("%1:%2")
                .arg(dimension.kind
                             == SemanticTypeDimensionKind::Packed
                         ? QStringLiteral("p")
                         : QStringLiteral("u"),
                     dimension.canonicalId));
    }
    if (!result.rootTypeId.isEmpty()
        && !result.declarationBaseText.isEmpty()) {
        result.declarationShapeId =
            declarationShape.join(QChar(u'\x1f'));
    }

    bool dimensionsComplete = true;
    for (const SemanticTypeDimensionFact& dimension :
         std::as_const(result.dimensions)) {
        dimensionsComplete =
            dimensionsComplete && dimension.complete;
    }
    bool typedefChainComplete = true;
    QString expectedType = result.rootTypeId;
    for (const SemanticTypedefResolutionStep& step :
         std::as_const(result.typedefChain)) {
        if (step.sourceTypeId != expectedType
            || step.targetTypeId.isEmpty()
            || step.declarationIdentity.isEmpty()) {
            typedefChainComplete = false;
            break;
        }
        expectedType = step.targetTypeId;
    }
    typedefChainComplete =
        typedefChainComplete
        && expectedType == result.canonicalTypeId;
    result.complete =
        !resolvedType.isError()
        && !result.rootTypeId.isEmpty()
        && !result.canonicalTypeId.isEmpty()
        && !result.declarationShapeId.isEmpty()
        && !result.declarationBaseText.isEmpty()
        && dimensionsComplete
        && typedefChainComplete
        && result.constants.complete;
    if (!result.complete
        && result.failureReason.isEmpty()) {
        result.failureReason =
            !result.constants.failureReason.isEmpty()
            ? result.constants.failureReason
            : QStringLiteral(
                  "Slang declared-type facts are incomplete.");
    }
    return result;
}

void fillSemanticDimensions(const Type& type,
                            SemanticElaboratedSymbolInfo* result)
{
    if (!result)
        return;

    QStringList unpacked;
    uint64_t unpackedElements = 1;
    bool hasUnpackedElements = false;
    const Type* current = &type.getCanonicalType();
    while (current->kind == SymbolKind::FixedSizeUnpackedArrayType) {
        const auto& array = current->as<FixedSizeUnpackedArrayType>();
        unpacked.append(semanticRangeText(array.range));
        hasUnpackedElements = true;
        const uint64_t extent = array.range.fullWidth();
        if (extent == 0
            || unpackedElements
                   > std::numeric_limits<uint64_t>::max() / extent) {
            unpackedElements = 0;
        } else if (unpackedElements > 0) {
            unpackedElements *= extent;
        }
        current = &array.elementType.getCanonicalType();
    }

    QStringList packed;
    while (current->kind == SymbolKind::PackedArrayType) {
        const auto& array = current->as<PackedArrayType>();
        packed.append(semanticRangeText(array.range));
        current = &array.elementType.getCanonicalType();
    }

    result->packedDimensionsText = packed.join(QLatin1Char(' '));
    result->unpackedDimensionsText = unpacked.join(QLatin1Char(' '));
    if (hasUnpackedElements && unpackedElements > 0) {
        result->unpackedElementCountText =
            QString::number(unpackedElements);
    }
}

SemanticElaboratedSymbolInfo typeInfo(const Type& type)
{
    SemanticElaboratedSymbolInfo result;
    result.available = !type.isError();
    if (type.isError()) {
        result.resolvedTypeText = QString::fromStdString(type.toString());
        result.fixedSize = false;
        result.integral = false;
        result.unpackedArray = false;
        result.interfaceType = false;
        result.signedIntegral = false;
        result.bitWidth = 0;
        result.failureReason = QStringLiteral(
            "slang resolved the symbol to an error type.");
        return result;
    }
    const Type& canonicalType = type.getCanonicalType();
    result.fixedSize = type.isFixedSize();
    result.integral = type.isIntegral();
    result.unpackedArray = canonicalType.isUnpackedArray();
    result.interfaceType = false;
    result.signedIntegral = result.integral && type.isSigned();
    if (canonicalType.kind == SymbolKind::EnumType) {
        // Slang's full enum TypePrinter evaluates every member. Presentation
        // collection needs the resolved type, not a second enum evaluation
        // pass, so use the Slang-resolved base type directly.
        const auto& enumType = canonicalType.as<EnumType>();
        result.resolvedTypeText = QStringLiteral("enum %1").arg(
            QString::fromStdString(enumType.baseType.toString()));
    } else {
        result.resolvedTypeText = QString::fromStdString(type.toString());
    }
    fillSemanticDimensions(type, &result);
    const uint64_t bitWidth = type.isFixedSize()
        ? type.getBitstreamWidth()
        : uint64_t(type.getBitWidth());
    result.bitWidth = bitWidth;
    result.bitWidthText = bitWidth > 0
        ? QString::number(bitWidth)
        : QStringLiteral("not statically known");

    const Type* signedType = &type.getCanonicalType();
    while (signedType->kind == SymbolKind::FixedSizeUnpackedArrayType) {
        signedType = &signedType
                          ->as<FixedSizeUnpackedArrayType>()
                          .elementType
                          .getCanonicalType();
    }
    result.signednessText = signedType->isNumeric()
        ? (signedType->isSigned() ? QStringLiteral("signed")
                                  : QStringLiteral("unsigned"))
        : QStringLiteral("not applicable");
    return result;
}

void fillInterfaceDimensions(const InterfacePortSymbol& port,
                             SemanticElaboratedSymbolInfo* result)
{
    if (!result)
        return;
    const auto ranges = port.getDeclaredRange();
    if (!ranges)
        return;

    QStringList dimensions;
    uint64_t elementCount = 1;
    for (const slang::ConstantRange& range : *ranges) {
        dimensions.append(semanticRangeText(range));
        const uint64_t extent = range.fullWidth();
        if (extent == 0
            || elementCount
                   > std::numeric_limits<uint64_t>::max() / extent) {
            elementCount = 0;
        } else if (elementCount > 0) {
            elementCount *= extent;
        }
    }
    result->unpackedDimensionsText =
        dimensions.join(QLatin1Char(' '));
    if (!dimensions.isEmpty() && elementCount > 0) {
        result->unpackedElementCountText =
            QString::number(elementCount);
    }
}

SemanticElaboratedSymbolInfo constantInfo(
    const slang::ConstantValue& value,
    const Type& type)
{
    SemanticElaboratedSymbolInfo result = typeInfo(type);
    if (!value) {
        result.available = false;
        result.failureReason = QStringLiteral(
            "slang reported an invalid constant value.");
        return result;
    }
    result.available = true;
    result.valueText = exactConstantText(value);
    return result;
}

SemanticElaboratedSymbolInfo enumConstantInfo(
    const slang::ConstantValue& value,
    const Type& type)
{
    SemanticElaboratedSymbolInfo result = constantInfo(value, type);
    if (!result.available || !value.isInteger())
        return result;

    // Both renderings come from Slang's elaborated SVInt; neither interprets
    // the source expression. Keep the lossless binary value as the semantic
    // result, while offering plain decimal for enum UI when every bit is
    // known. Decimal cannot preserve partial X / Z positions, so unknown
    // values deliberately fall back to the exact binary representation.
    const slang::SVInt& integer = value.integer();
    result.valueText = QString::fromStdString(integer.toString(
        slang::LiteralBase::Binary,
        true,
        std::numeric_limits<slang::bitwidth_t>::max()));
    result.displayValueText = integer.hasUnknown()
        ? result.valueText
        : QString::fromStdString(integer.toString(
              slang::LiteralBase::Decimal,
              false,
              std::numeric_limits<slang::bitwidth_t>::max()));
    return result;
}

bool isDirectValueLiteralKind(ExpressionKind kind)
{
    switch (kind) {
    case ExpressionKind::IntegerLiteral:
    case ExpressionKind::RealLiteral:
    case ExpressionKind::TimeLiteral:
    case ExpressionKind::UnbasedUnsizedIntegerLiteral:
    case ExpressionKind::StringLiteral:
        return true;
    default:
        return false;
    }
}

const Expression* directSourceValueExpression(
    const Expression& initializer)
{
    const Expression& expression =
        initializer.unwrapImplicitConversions();
    if (expression.isParenthesized())
        return nullptr;
    if (isDirectValueLiteralKind(expression.kind))
        return &expression;
    if (expression.kind != ExpressionKind::UnaryOp)
        return nullptr;

    const auto& unary = expression.as<UnaryExpression>();
    if (unary.op != UnaryOperator::Plus
        && unary.op != UnaryOperator::Minus) {
        return nullptr;
    }
    const Expression& operand =
        unary.operand().unwrapImplicitConversions();
    if (operand.isParenthesized()
        || !isDirectValueLiteralKind(operand.kind)) {
        return nullptr;
    }
    // Use the unary expression's Slang value so the sign operation remains
    // part of the source value being compared.
    return &expression;
}

bool sameDisplayedConstantValue(const slang::ConstantValue& source,
                                const slang::ConstantValue& effective)
{
    if (!source || !effective)
        return false;
    if (!source.isInteger() || !effective.isInteger())
        return source == effective;

    const slang::SVInt& sourceInteger = source.integer();
    const slang::SVInt& effectiveInteger = effective.integer();
    if (sourceInteger.hasUnknown() || effectiveInteger.hasUnknown()) {
        // Decimal cannot represent individual X / Z positions. Be
        // conservative: suppress only if width, signedness, and all four-
        // state bits are identical according to Slang.
        return sourceInteger.getBitWidth() == effectiveInteger.getBitWidth()
            && sourceInteger.isSigned() == effectiveInteger.isSigned()
            && exactlyEqual(sourceInteger, effectiveInteger);
    }

    // Ask Slang for the mathematical decimal interpretation on both sides.
    // This detects truncation and signed coercion while allowing harmless
    // width extension of a value the source already displays.
    const auto decimalText = [](const slang::SVInt& integer) {
        return integer.toString(
            slang::LiteralBase::Decimal,
            false,
            std::numeric_limits<slang::bitwidth_t>::max());
    };
    return decimalText(sourceInteger) == decimalText(effectiveInteger);
}

slang::ConstantValue sourceDisplayedConstantValue(
    const Expression& expression)
{
    switch (expression.kind) {
    case ExpressionKind::IntegerLiteral:
        // getConstant() can reflect the assignment target's contextual
        // conversion. IntegerLiteral::getValue() is the value encoded by the
        // source token before truncation or signed coercion.
        return expression.as<IntegerLiteral>().getValue();
    case ExpressionKind::UnbasedUnsizedIntegerLiteral:
        // '0 / '1 / 'x / 'z are context-sized by definition; Slang's node
        // value is therefore the source-displayed value in that context.
        return expression.as<UnbasedUnsizedIntegerLiteral>().getValue();
    case ExpressionKind::RealLiteral:
        return slang::ConstantValue(
            slang::real_t(expression.as<RealLiteral>().getValue()));
    case ExpressionKind::StringLiteral:
        return slang::ConstantValue(std::string(
            expression.as<StringLiteral>().getValue()));
    case ExpressionKind::TimeLiteral:
        // Time scaling is itself Slang semantics. The unwrapped expression
        // was evaluated while ParameterSymbol produced its final value, so
        // its cached constant is the correct source-side result.
        return expression.getConstant()
            ? *expression.getConstant()
            : slang::ConstantValue();
    case ExpressionKind::UnaryOp: {
        const auto& unary = expression.as<UnaryExpression>();
        const slang::ConstantValue operand =
            sourceDisplayedConstantValue(
                unary.operand().unwrapImplicitConversions());
        if (!operand || unary.op == UnaryOperator::Plus)
            return operand;
        if (unary.op != UnaryOperator::Minus)
            return slang::ConstantValue();
        if (operand.isInteger())
            return slang::ConstantValue(-operand.integer());
        if (operand.isReal())
            return slang::ConstantValue(
                slang::real_t(-static_cast<double>(operand.real())));
        if (operand.isShortReal())
            return slang::ConstantValue(
                slang::shortreal_t(
                    -static_cast<float>(operand.shortReal())));
        return slang::ConstantValue();
    }
    default:
        return slang::ConstantValue();
    }
}

bool sourceValueDisplaysEffectiveParameterValue(
    const ParameterSymbol& parameter,
    const slang::ConstantValue& effectiveValue)
{
    const Expression* initializer = parameter.getInitializer();
    if (!initializer)
        return false;
    const Expression* source = directSourceValueExpression(*initializer);
    if (!source)
        return false;
    slang::ConstantValue sourceValue =
        sourceDisplayedConstantValue(*source);
    if (source->kind == ExpressionKind::StringLiteral
        && effectiveValue.isInteger()) {
        // Slang represents an implicitly typed string parameter as the
        // literal's packed integer value. Compare against that Slang-owned
        // representation so `parameter STR = "text"` is recognized as
        // already displaying its effective value.
        sourceValue = source->as<StringLiteral>().getIntValue();
    }
    return sameDisplayedConstantValue(sourceValue, effectiveValue);
}

QString parameterDefaultExpressionText(const ParameterSymbol& parameter)
{
    if (!parameter.defaultValSyntax)
        return QString();
    if (parameter.defaultValSyntax->kind == SyntaxKind::Declarator) {
        const auto& declarator =
            parameter.defaultValSyntax->as<DeclaratorSyntax>();
        if (declarator.initializer)
            return printedSyntax(*declarator.initializer->expr);
    }
    return printedSyntax(*parameter.defaultValSyntax);
}

QString parameterExpressionText(const ParameterSymbol& parameter)
{
    if (const Expression* initializer = parameter.getInitializer()) {
        if (initializer->syntax)
            return printedSyntax(*initializer->syntax);
    }
    return parameterDefaultExpressionText(parameter);
}

QString parameterValueSourceText(const ParameterSymbol& parameter)
{
    if (parameter.isLocalParam())
        return QStringLiteral("slang elaborated localparam constant");
    if (parameter.isFromConfig())
        return QStringLiteral("slang elaborated config parameter override");
    if (parameter.isOverridden())
        return QStringLiteral("slang elaborated parameter override");
    return QStringLiteral("slang elaborated parameter default");
}

QString parentInstancePath(const QString& instancePath)
{
    const int separator = instancePath.lastIndexOf(QLatin1Char('.'));
    return separator < 0 ? QString() : instancePath.left(separator);
}

void fillParameterSourcePresentation(
    const ParameterSymbol& parameter,
    SemanticSymbolPresentation* presentation)
{
    if (!presentation)
        return;
    if (presentation->expressionText.isEmpty())
        presentation->expressionText =
            parameterDefaultExpressionText(parameter);

    if (!presentation->declarationText.isEmpty())
        return;
    const SyntaxNode* syntax = parameter.getSyntax();
    if (!syntax)
        return;

    if (syntax->kind == SyntaxKind::Declarator
        && syntax->parent
        && syntax->parent->kind == SyntaxKind::ParameterDeclaration) {
        const auto& declaration =
            syntax->parent->as<ParameterDeclarationSyntax>();
        presentation->declarationText = joinedParts(
            {printedToken(declaration.keyword),
             printedSyntax(*declaration.type),
             printedSyntax(*syntax)});
        return;
    }
    presentation->declarationText = printedSyntax(*syntax);
}

void fillEnumSourcePresentation(
    const EnumValueSymbol& enumValue,
    SemanticSymbolPresentation* presentation)
{
    if (!presentation)
        return;
    if (const SyntaxNode* syntax = enumValue.getSyntax()) {
        if (presentation->declarationText.isEmpty())
            presentation->declarationText = printedSyntax(*syntax);
        if (presentation->expressionText.isEmpty()
            && syntax->kind == SyntaxKind::Declarator) {
            const auto& declarator = syntax->as<DeclaratorSyntax>();
            if (declarator.initializer)
                presentation->expressionText =
                    printedSyntax(*declarator.initializer->expr);
        }
    }

    const Type& canonical = enumValue.getType().getCanonicalType();
    if (canonical.kind == SymbolKind::EnumType) {
        const auto& enumType = canonical.as<EnumType>();
        presentation->enumUnderlyingBitWidthText =
            QString::number(enumType.baseType.getBitWidth());
    }
}

bool definitionHasDefaults(const DefinitionSymbol& definition)
{
    for (const DefinitionSymbol::ParameterDecl& parameter :
         definition.parameters) {
        if (!parameter.isLocalParam && !parameter.hasDefault())
            return false;
    }
    return true;
}

QString qualifiedLexicalScopePath(const Symbol& symbol,
                                  const Scope& collectionRoot,
                                  const QString& rootPath)
{
    QStringList nestedScopes;
    const Scope* scope = symbol.getParentScope();
    for (int depth = 0; scope && scope != &collectionRoot && depth < 128;
         ++depth) {
        const Symbol& scopeSymbol = scope->asSymbol();
        const QString name = QString::fromStdString(
            std::string(scopeSymbol.name));
        if (!name.isEmpty() && !name.startsWith(QLatin1Char('$')))
            nestedScopes.prepend(name);

        const Scope* parent = scopeSymbol.getParentScope();
        if (parent == scope)
            break;
        scope = parent;
    }

    QString result = rootPath;
    for (const QString& nested : std::as_const(nestedScopes)) {
        if (!result.isEmpty())
            result += QStringLiteral("::");
        result += nested;
    }
    return result;
}

struct EffectiveScopeDescriptor {
    SemanticEffectiveScopeKind kind =
        SemanticEffectiveScopeKind::Unknown;
    const Scope* root = nullptr;
    QString qualifiedPath;
};

EffectiveScopeDescriptor effectiveScopeForSymbol(
    const Symbol& symbol,
    const Scope& collectionRoot,
    SemanticEffectiveScopeKind collectionKind,
    const QString& collectionPath)
{
    EffectiveScopeDescriptor result{
        collectionKind, &collectionRoot, collectionPath};
    if (symbol.getDeclaringDefinition())
        return result;

    const Scope* scope = symbol.getParentScope();
    for (int depth = 0; scope && depth < 128; ++depth) {
        if (scope == &collectionRoot)
            return result;
        const Symbol& scopeSymbol = scope->asSymbol();
        if (const auto* package = scopeSymbol.as_if<PackageSymbol>()) {
            result.kind = SemanticEffectiveScopeKind::Package;
            result.root = package;
            result.qualifiedPath = QString::fromStdString(
                std::string(package->name));
            return result;
        }
        if (const auto* unit =
                scopeSymbol.as_if<CompilationUnitSymbol>()) {
            result.kind = SemanticEffectiveScopeKind::CompilationUnit;
            result.root = unit;
            result.qualifiedPath = QStringLiteral("$unit");
            return result;
        }
        const Scope* parent = scopeSymbol.getParentScope();
        if (parent == scope)
            break;
        scope = parent;
    }
    return result;
}

EffectiveValueFact factForRange(
    EffectiveValueFactKind kind,
    slang::SourceRange range,
    const slang::SourceManager* sourceManager,
    const QString& instancePath,
    bool defaultEvaluation,
    SemanticEffectiveScopeKind effectiveScopeKind,
    const QString& qualifiedScopePath,
    const QString& anchorInstancePath)
{
    EffectiveValueFact fact;
    fact.kind = kind;
    fact.instancePath = instancePath;
    fact.anchorInstancePath = anchorInstancePath;
    fact.defaultEvaluation = defaultEvaluation;
    fact.effectiveScopeKind = effectiveScopeKind;
    fact.qualifiedScopePath = qualifiedScopePath;
    fact.scopePath = qualifiedScopePath;
    fact.provenance = QStringLiteral("Slang AST/type elaboration");
    if (!sourceManager || !range.start().valid() || !range.end().valid())
        return fact;

    const slang_symbols::detail::QTextDocumentSourcePosition start =
        slang_symbols::detail::qTextDocumentSourcePosition(
            sourceManager, range.start());
    const slang_symbols::detail::QTextDocumentSourcePosition end =
        slang_symbols::detail::qTextDocumentSourcePosition(
            sourceManager, range.end());
    if (!start.isValid() || !end.isValid()
        || normalizedFileName(start.fileName)
               != normalizedFileName(end.fileName)) {
        return fact;
    }
    fact.fileName = normalizedFileName(start.fileName);
    fact.startPosition = start.position;
    fact.endPosition = end.position;
    fact.line = start.line;
    fact.status = EffectiveValueStatus::Current;
    fact.stableSourceIdentity = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(fact.fileName)
        .arg(fact.startPosition)
        .arg(fact.endPosition)
        .arg(static_cast<int>(kind))
        .arg(static_cast<int>(effectiveScopeKind))
        .arg(qualifiedScopePath)
        .arg(instancePath)
        .arg(anchorInstancePath);
    return fact;
}

void fillFactType(const Type& type, EffectiveValueFact* fact)
{
    if (!fact)
        return;
    const SemanticElaboratedSymbolInfo info = typeInfo(type);
    fact->resolvedTypeText = info.resolvedTypeText;
    fact->packedDimensionsText = info.packedDimensionsText;
    fact->unpackedDimensionsText = info.unpackedDimensionsText;
    fact->unpackedElementCountText = info.unpackedElementCountText;
    fact->bitWidthText = info.bitWidthText;
    fact->signednessText = info.signednessText;
    if (!info.available) {
        fact->status = EffectiveValueStatus::Error;
        fact->failureReason = info.failureReason;
    }
}

void appendExpressionFact(
    EffectiveValueFactKind kind,
    const Expression& expression,
    const QString& instancePath,
    bool defaultEvaluation,
    SemanticEffectiveScopeKind effectiveScopeKind,
    const QString& qualifiedScopePath,
    const slang::SourceManager* sourceManager,
    QList<EffectiveValueFact>* facts,
    const std::function<bool()>& isCancelled)
{
    if (!facts || !expression.syntax || (isCancelled && isCancelled()))
        return;
    EffectiveValueFact fact = factForRange(kind,
                                           expression.sourceRange,
                                           sourceManager,
                                           instancePath,
                                           defaultEvaluation,
                                           effectiveScopeKind,
                                           qualifiedScopePath,
                                           instancePath);
    if (!fact.isValid())
        return;
    fact.expressionText = printedSyntax(*expression.syntax);
    fillFactType(*expression.type, &fact);
    facts->append(std::move(fact));
}

void gatherScopePresentations(
    const Scope& scope,
    const QString& instancePath,
    bool storeAsDefault,
    SemanticEffectiveScopeKind effectiveScopeKind,
    const QString& qualifiedScopePath,
    const slang::SourceManager* sourceManager,
    QHash<QString, SemanticSymbolPresentation>* presentations,
    QList<EffectiveValueFact>* effectiveValueFacts,
    const std::function<bool()>& isCancelled)
{
    if (!sourceManager || !presentations
        || (!storeAsDefault && instancePath.isEmpty())
        || (isCancelled && isCancelled())) {
        return;
    }

    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };

    auto storeInfo = [&](const Symbol& symbol,
                         const SemanticElaboratedSymbolInfo& info,
                         const SemanticDeclaredTypeFacts*
                             declaredType,
                         auto&& fillSource,
                         bool storeElaboratedInfo) {
        if (cancelled())
            return;
        const QString key = identityKey(sourceManager, symbol);
        if (key.isEmpty())
            return;
        const EffectiveScopeDescriptor effectiveScope =
            effectiveScopeForSymbol(symbol,
                                    scope,
                                    effectiveScopeKind,
                                    qualifiedScopePath);
        SemanticSymbolPresentation& presentation = (*presentations)[key];
        const auto isStaticScope = [](SemanticEffectiveScopeKind kind) {
            return kind == SemanticEffectiveScopeKind::Package
                || kind == SemanticEffectiveScopeKind::CompilationUnit;
        };
        // A package / compilation-unit declaration can be encountered again
        // through an instance-local type reference. Its declaration identity
        // and value remain static; an instance traversal must never turn that
        // record into an instance-bound value.
        if (isStaticScope(presentation.effectiveScopeKind)
            && !isStaticScope(effectiveScope.kind)) {
            fillSource(&presentation);
            return;
        }
        presentation.effectiveScopeKind = effectiveScope.kind;
        presentation.qualifiedScopePath = qualifiedLexicalScopePath(
            symbol,
            effectiveScope.root ? *effectiveScope.root : scope,
            effectiveScope.qualifiedPath);
        fillSource(&presentation);
        const bool staticValue = effectiveScope.kind
                == SemanticEffectiveScopeKind::Package
            || effectiveScope.kind
                == SemanticEffectiveScopeKind::CompilationUnit;
        if (staticValue) {
            presentation.instanceInfoByPath.clear();
            presentation.declaredTypeFactsByPath.clear();
        }
        // Detached definition elaboration supplies declaration defaults only.
        // It must never manufacture an instance-map entry such as "child" or
        // overwrite an actual top.u* entry produced by the real hierarchy.
        if (!staticValue && !storeAsDefault && !instancePath.isEmpty()) {
            if (storeElaboratedInfo) {
                presentation.instanceInfoByPath.insert(
                    instancePath, info);
            }
            if (declaredType) {
                presentation.declaredTypeFactsByPath.insert(
                    instancePath, *declaredType);
            }
        }
        if (staticValue || storeAsDefault) {
            if (storeElaboratedInfo)
                presentation.defaultInfo = info;
            if (declaredType) {
                presentation.defaultDeclaredTypeFacts =
                    *declaredType;
            }
        }
    };

    auto storeEnumType = [&](const EnumType& enumType) {
        for (const EnumValueSymbol& enumValue : enumType.values()) {
            if (cancelled())
                return;
            SemanticElaboratedSymbolInfo info = enumConstantInfo(
                enumValue.getValue(), enumValue.getType());
            info.valueSourceText = QStringLiteral(
                "slang elaborated enum constant");
            storeInfo(enumValue,
                      info,
                      nullptr,
                      [&](SemanticSymbolPresentation* presentation) {
                          fillEnumSourcePresentation(enumValue,
                                                     presentation);
                      },
                      true);
        }
    };

    auto visitor = makeVisitor(
        [&](auto&, const InstanceSymbol&) {
            // Nested instances are gathered separately with their own path.
        },
        [&](auto&, const PackageSymbol&) {
            // Packages are gathered independently so their values never
            // inherit an enclosing compilation-unit or instance context.
        },
        [&](auto&, const DefinitionSymbol&) {
            // Definitions nested in a compilation unit are elaborated via
            // their instance trees. Static-scope collection must not publish
            // their members as compilation-unit defaults.
        },
        [&](auto& nestedVisitor, const ParameterSymbol& parameter) {
            if (cancelled())
                return;
            if (parameter.isFromGenvar()) {
                nestedVisitor.visitDefault(parameter);
                return;
            }
            const slang::ConstantValue& parameterValue =
                parameter.getValue();
            const bool sourceDisplaysValue =
                sourceValueDisplaysEffectiveParameterValue(
                    parameter, parameterValue);
            SemanticElaboratedSymbolInfo info = constantInfo(
                parameterValue, parameter.getType());
            // Symbol value Ghosts are anchored at the declaration. An
            // instance override is written elsewhere and therefore cannot
            // make the declaration text redundant.
            info.sourceTextDisplaysEffectiveValue =
                !parameter.isOverridden() && sourceDisplaysValue;
            info.expressionText = parameterExpressionText(parameter);
            info.valueSourceText = parameterValueSourceText(parameter);
            storeInfo(parameter, info,
                      nullptr,
                      [&](SemanticSymbolPresentation* presentation) {
                          fillParameterSourcePresentation(parameter,
                                                          presentation);
                      },
                      true);
            if (effectiveValueFacts && parameter.isOverridden()) {
                const Expression* initializer = parameter.getInitializer();
                if (initializer && initializer->syntax) {
                    EffectiveValueFact fact = factForRange(
                        EffectiveValueFactKind::ParameterOverride,
                        initializer->sourceRange,
                        sourceManager,
                        instancePath,
                        storeAsDefault,
                        effectiveScopeKind,
                        qualifiedLexicalScopePath(
                            parameter, scope, qualifiedScopePath),
                        parentInstancePath(instancePath));
                    if (fact.isValid()) {
                        fact.expressionText = parameterExpressionText(parameter);
                        fact.valueText = info.valueText;
                        fact.scopePath = fact.qualifiedScopePath;
                        fact.provenance = info.valueSourceText;
                        fact.resolvedTypeText = info.resolvedTypeText;
                        fact.packedDimensionsText = info.packedDimensionsText;
                        fact.unpackedDimensionsText = info.unpackedDimensionsText;
                        fact.unpackedElementCountText =
                            info.unpackedElementCountText;
                        fact.bitWidthText = info.bitWidthText;
                        fact.signednessText = info.signednessText;
                        fact.status = info.available
                            ? EffectiveValueStatus::Current
                            : EffectiveValueStatus::Error;
                        fact.failureReason = info.failureReason;
                        fact.sourceTextDisplaysEffectiveValue =
                            sourceDisplaysValue;
                        effectiveValueFacts->append(std::move(fact));
                    }
                }
            }
            nestedVisitor.visitDefault(parameter);
        },
        [&](auto& nestedVisitor, const EnumValueSymbol& enumValue) {
            if (cancelled())
                return;
            SemanticElaboratedSymbolInfo info = enumConstantInfo(
                enumValue.getValue(), enumValue.getType());
            info.valueSourceText = QStringLiteral(
                "slang elaborated enum constant");
            storeInfo(enumValue, info,
                      nullptr,
                      [&](SemanticSymbolPresentation* presentation) {
                          fillEnumSourcePresentation(enumValue,
                                                     presentation);
                      },
                      true);
            nestedVisitor.visitDefault(enumValue);
        },
        [&](auto& nestedVisitor, const TypeAliasType& typeAlias) {
            if (cancelled())
                return;
            const Type& canonical = typeAlias.getCanonicalType();
            if (canonical.kind == SymbolKind::EnumType)
                storeEnumType(canonical.as<EnumType>());
            const SemanticDeclaredTypeFacts typeFacts =
                declaredTypeFacts(
                    typeAlias,
                    typeAlias,
                    typeAlias,
                    &typeAlias.targetType,
                    sourceManager);
            storeInfo(typeAlias,
                      typeInfo(typeAlias),
                      &typeFacts,
                      [](SemanticSymbolPresentation*) {},
                      false);
            nestedVisitor.visitDefault(typeAlias);
        },
        [&](auto& nestedVisitor, const VariableSymbol& variable) {
            if (cancelled())
                return;
            const Type& canonical = variable.getType().getCanonicalType();
            if (canonical.kind == SymbolKind::EnumType)
                storeEnumType(canonical.as<EnumType>());
            const SemanticDeclaredTypeFacts typeFacts =
                declaredTypeFacts(
                    variable,
                    variable,
                    variable.getType(),
                    static_cast<const DeclaredType*>(
                        variable.getDeclaredType()),
                    sourceManager);
            storeInfo(variable,
                      typeInfo(variable.getType()),
                      &typeFacts,
                      [](SemanticSymbolPresentation*) {},
                      true);
            nestedVisitor.visitDefault(variable);
        },
        [&](auto& nestedVisitor, const NetSymbol& net) {
            if (cancelled())
                return;
            const SemanticDeclaredTypeFacts typeFacts =
                declaredTypeFacts(
                    net,
                    net,
                    net.getType(),
                    static_cast<const DeclaredType*>(
                        net.getDeclaredType()),
                    sourceManager);
            storeInfo(net,
                      typeInfo(net.getType()),
                      &typeFacts,
                      [](SemanticSymbolPresentation*) {},
                      true);
            nestedVisitor.visitDefault(net);
        },
        [&](auto& nestedVisitor, const PortSymbol& port) {
            if (cancelled())
                return;
            const SemanticElaboratedSymbolInfo info = typeInfo(port.getType());
            const ValueSymbol* internal =
                port.internalSymbol
                    && port.internalSymbol->isValue()
                ? &port.internalSymbol->as<ValueSymbol>()
                : nullptr;
            const DeclaredType* portDeclaredType =
                internal
                ? static_cast<const DeclaredType*>(
                      internal->getDeclaredType())
                : nullptr;
            const SemanticDeclaredTypeFacts typeFacts =
                declaredTypeFacts(
                    port,
                    internal ? static_cast<const Symbol&>(*internal)
                             : static_cast<const Symbol&>(port),
                    port.getType(),
                    portDeclaredType,
                    sourceManager);
            storeInfo(port,
                      info,
                      &typeFacts,
                      [](SemanticSymbolPresentation*) {},
                      true);
            nestedVisitor.visitDefault(port);
        },
        [&](auto& nestedVisitor, const InterfacePortSymbol& port) {
            if (cancelled())
                return;
            SemanticElaboratedSymbolInfo info;
            info.available = !port.isInvalid();
            info.fixedSize = false;
            info.integral = false;
            info.unpackedArray = false;
            info.interfaceType = true;
            info.signedIntegral = false;
            info.bitWidth = 0;
            info.interfaceName = port.interfaceDef
                ? QString::fromStdString(
                      std::string(port.interfaceDef->name))
                : QStringLiteral("interface");
            info.modportName = QString::fromStdString(
                std::string(port.modport));
            info.resolvedTypeText = info.interfaceName;
            if (!info.modportName.isEmpty()) {
                info.resolvedTypeText += QLatin1Char('.');
                info.resolvedTypeText += info.modportName;
            }
            info.signednessText = QStringLiteral("not applicable");
            info.bitWidthText = QStringLiteral("not applicable");
            fillInterfaceDimensions(port, &info);
            if (!info.available) {
                info.failureReason = QStringLiteral(
                    "slang could not resolve the interface port type.");
            }
            storeInfo(port,
                      info,
                      nullptr,
                      [](SemanticSymbolPresentation*) {},
                      true);
            nestedVisitor.visitDefault(port);
        },
        [&](auto& nestedVisitor, const RangeSelectExpression& expression) {
            if (cancelled())
                return;
            appendExpressionFact(
                EffectiveValueFactKind::PartSelectWidth,
                expression,
                instancePath,
                storeAsDefault,
                effectiveScopeKind,
                qualifiedScopePath,
                sourceManager,
                effectiveValueFacts,
                isCancelled);
            nestedVisitor.visitDefault(expression);
        },
        [&](auto& nestedVisitor,
            const ConcatenationExpression& expression) {
            if (cancelled())
                return;
            appendExpressionFact(
                EffectiveValueFactKind::ConcatenationWidth,
                expression,
                instancePath,
                storeAsDefault,
                effectiveScopeKind,
                qualifiedScopePath,
                sourceManager,
                effectiveValueFacts,
                isCancelled);
            nestedVisitor.visitDefault(expression);
        },
        [&](auto&, const ReplicationExpression& expression) {
            if (cancelled())
                return;
            appendExpressionFact(
                EffectiveValueFactKind::ConcatenationWidth,
                expression,
                instancePath,
                storeAsDefault,
                effectiveScopeKind,
                qualifiedScopePath,
                sourceManager,
                effectiveValueFacts,
                isCancelled);
        },
        [&](auto& nestedVisitor,
            const GenerateBlockArraySymbol& generateArray) {
            if (cancelled())
                return;
            if (effectiveValueFacts) {
                const SyntaxNode* syntax = generateArray.getSyntax();
                while (syntax && syntax->kind != SyntaxKind::LoopGenerate)
                    syntax = syntax->parent;
                if (syntax) {
                    EffectiveValueFact fact = factForRange(
                        EffectiveValueFactKind::GenerateCount,
                        syntax->sourceRange(),
                        sourceManager,
                        instancePath,
                        storeAsDefault,
                        effectiveScopeKind,
                        qualifiedScopePath,
                        instancePath);
                    if (fact.isValid()) {
                        fact.expressionText = printedSyntax(*syntax);
                        fact.valueText = QString::number(
                            generateArray.entries.size());
                        fact.provenance = QStringLiteral(
                            "Slang elaborated generate block array");
                        effectiveValueFacts->append(std::move(fact));
                    }
                }
            }
            nestedVisitor.visitDefault(generateArray);
        });
    for (const Symbol& member : scope.members()) {
        if (cancelled())
            return;
        member.visit(visitor);
    }
}

void gatherInstanceTree(
    const InstanceSymbol& root,
    const slang::SourceManager* sourceManager,
    QHash<QString, SemanticSymbolPresentation>* presentations,
    QList<EffectiveValueFact>* effectiveValueFacts,
    const std::function<bool()>& isCancelled)
{
    auto visitor = makeVisitor(
        [&](auto& nestedVisitor, const InstanceSymbol& instance) {
            if (isCancelled && isCancelled())
                return;
            const QString path = QString::fromStdString(
                instance.getHierarchicalPath());
            const QString lexicalPath = QString::fromStdString(
                std::string(instance.getDefinition().name));
            gatherScopePresentations(instance.body,
                                     path,
                                     false,
                                     SemanticEffectiveScopeKind::Instance,
                                     lexicalPath,
                                     sourceManager,
                                     presentations,
                                     effectiveValueFacts,
                                     isCancelled);
            if (isCancelled && isCancelled())
                return;
            nestedVisitor.visitDefault(instance);
        });
    root.visit(visitor);
}

void gatherStaticScope(
    const Scope& scope,
    SemanticEffectiveScopeKind effectiveScopeKind,
    const QString& qualifiedScopePath,
    const slang::SourceManager* sourceManager,
    QHash<QString, SemanticSymbolPresentation>* presentations,
    QList<EffectiveValueFact>* effectiveValueFacts,
    const std::function<bool()>& isCancelled)
{
    gatherScopePresentations(scope,
                             QString(),
                             true,
                             effectiveScopeKind,
                             qualifiedScopePath,
                             sourceManager,
                             presentations,
                             effectiveValueFacts,
                             isCancelled);
}
}

void slang_symbols::populateSymbolPresentations(
    Compilation& compilation,
    QList<SemanticSymbolRecord>& records,
    QList<EffectiveValueFact>* effectiveValueFacts,
    const std::function<bool()>& isCancelled)
{
    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    const slang::SourceManager* sourceManager = compilation.getSourceManager();
    if (!sourceManager || records.isEmpty() || cancelled())
        return;
    slang_symbols::detail::resetQTextDocumentSourcePositionCache(
        sourceManager);
    struct SourcePositionCacheReset {
        ~SourcePositionCacheReset()
        {
            slang_symbols::detail::resetQTextDocumentSourcePositionCache(
                nullptr);
        }
    } sourcePositionCacheReset;

    // Build the entire publication off to the side. A cancelled or throwing
    // Slang traversal must not expose a mixture of old and partial values.
    QHash<QString, SemanticSymbolPresentation> presentations;
    const QHash<QString, SemanticSymbolPresentation>
        portSourcePresentations = collectPortSourcePresentations(
            compilation, sourceManager, isCancelled);
    if (cancelled())
        return;
    QList<EffectiveValueFact> collectedFacts;
    QList<EffectiveValueFact>* facts = effectiveValueFacts
        ? &collectedFacts : nullptr;
    const RootSymbol& root = compilation.getRoot();
    for (const InstanceSymbol* top : root.topInstances) {
        if (cancelled())
            return;
        if (!top)
            continue;
        gatherInstanceTree(*top,
                           sourceManager,
                           &presentations,
                           facts,
                           isCancelled);
    }

    // Package and compilation-unit constants live outside the elaborated
    // instance tree. Their values are global to the compilation, so publish
    // them as defaults only and never attach an instance path.
    for (const PackageSymbol* package : compilation.getPackages()) {
        if (cancelled())
            return;
        if (!package || !package->location.valid())
            continue;
        gatherStaticScope(*package,
                          SemanticEffectiveScopeKind::Package,
                          QString::fromStdString(
                              std::string(package->name)),
                          sourceManager,
                          &presentations,
                          facts,
                          isCancelled);
    }
    for (const CompilationUnitSymbol* unit : root.compilationUnits) {
        if (cancelled())
            return;
        if (!unit)
            continue;
        gatherStaticScope(*unit,
                          SemanticEffectiveScopeKind::CompilationUnit,
                          QStringLiteral("$unit"),
                          sourceManager,
                          &presentations,
                          facts,
                          isCancelled);
    }

    QList<const DefinitionSymbol*> definitions;
    for (const Symbol* symbol : compilation.getDefinitions()) {
        if (cancelled())
            return;
        const auto* definition = symbol
            ? symbol->as_if<DefinitionSymbol>()
            : nullptr;
        if (definition)
            definitions.append(definition);
    }

    for (const DefinitionSymbol* definition : std::as_const(definitions)) {
        if (cancelled())
            return;
        if (!definition || !definitionHasDefaults(*definition))
            continue;
        InstanceSymbol& defaultInstance =
            InstanceSymbol::createDefault(compilation, *definition);
        // createDefault() normally becomes usable only after Compilation adds
        // it to the root scope. This collector owns a detached default used
        // solely for presentation, so provide the same parent compilation
        // context before ports or interface-dependent constants elaborate.
        defaultInstance.setParent(root);
        // Every definition is elaborated independently for declaration
        // defaults. Do not traverse nested instances here: those paths are
        // detached pseudo-hierarchy and each nested definition receives its
        // own default elaboration in this loop.
        gatherScopePresentations(defaultInstance.body,
                                 QString(),
                                 true,
                                 SemanticEffectiveScopeKind::Instance,
                                 QString::fromStdString(
                                     std::string(definition->name)),
                                 sourceManager,
                                 &presentations,
                                 facts,
                                 isCancelled);
    }

    QList<SemanticSymbolRecord> updatedRecords = records;
    for (SemanticSymbolRecord& record : updatedRecords) {
        if (cancelled())
            return;
        const QChar kind = identityKind(record);
        if (kind == QLatin1Char('?'))
            continue;
        const QString key = identityKey(record);
        const auto it = presentations.constFind(key);
        if (it != presentations.constEnd())
            record.presentation = it.value();

        if (kind == QLatin1Char('o')) {
            const auto sourcePort = portSourcePresentations.constFind(
                identityKey(record));
            if (sourcePort != portSourcePresentations.constEnd()) {
                mergePortSourcePresentation(sourcePort.value(),
                                            &record.presentation);
            }
        }

        if (record.collectorKind
            == SymbolTaxonomy::CollectorKind::EnumValue) {
            record.presentation.enumTypeName = record.owner.name;
        }
        if (kind != QLatin1Char('t')
            && !record.presentation.defaultInfo.available
            && record.presentation.defaultInfo.failureReason.isEmpty()) {
            record.presentation.defaultInfo.failureReason = QStringLiteral(
                "No valid slang default elaboration is available for this declaration.");
        }
    }

    if (cancelled())
        return;
    records = std::move(updatedRecords);
    if (effectiveValueFacts)
        *effectiveValueFacts = std::move(collectedFacts);
}

namespace {

struct SlangDriverSummary {
    SemanticDriverPresenceState presence =
        SemanticDriverPresenceState::Unknown;
    std::uint64_t total = 0;
    std::uint64_t continuous = 0;
    std::uint64_t procedural = 0;
    std::uint64_t portConnections = 0;
};

SlangDriverSummary driverSummary(
    const slang::analysis::AnalysisManager& analysis,
    const ValueSymbol* value,
    bool absenceIsProven)
{
    SlangDriverSummary result;
    if (!value)
        return result;

    QSet<const slang::analysis::ValueDriver*> unique;
    for (const auto& item : analysis.getDrivers(*value)) {
        const slang::analysis::ValueDriver* driver = item.first;
        if (!driver || unique.contains(driver))
            continue;
        unique.insert(driver);
        ++result.total;
        if (driver->kind == slang::analysis::DriverKind::Continuous)
            ++result.continuous;
        else if (driver->kind == slang::analysis::DriverKind::Procedural)
            ++result.procedural;
        if (driver->flags.has(
                slang::analysis::DriverFlags::OutputPort)) {
            ++result.portConnections;
        }
    }
    if (result.total > 0) {
        result.presence = SemanticDriverPresenceState::Present;
    } else if (absenceIsProven) {
        result.presence = SemanticDriverPresenceState::ProvenZero;
    }
    return result;
}

void storePortDriverSummary(
    const PortSymbol& port,
    const QString& instancePath,
    const slang::SourceManager* sourceManager,
    const slang::analysis::AnalysisManager& analysis,
    bool absenceIsProven,
    QHash<QString, SemanticSymbolPresentation>* presentations)
{
    if (!presentations || instancePath.isEmpty()
        || port.direction != ArgumentDirection::Out) {
        return;
    }
    const QString key = identityKey(sourceManager, port);
    auto presentation = presentations->find(key);
    if (key.isEmpty() || presentation == presentations->end())
        return;
    auto instance =
        presentation->instanceInfoByPath.find(instancePath);
    if (instance
        == presentation->instanceInfoByPath.end()) {
        return;
    }

    const ValueSymbol* value = nullptr;
    if (port.internalSymbol && port.internalSymbol->isValue())
        value = &port.internalSymbol->as<ValueSymbol>();
    const SlangDriverSummary summary =
        driverSummary(analysis, value, absenceIsProven);
    instance->driverPresence = summary.presence;
    instance->driverCount = summary.total;
    instance->continuousDriverCount = summary.continuous;
    instance->proceduralDriverCount = summary.procedural;
    instance->portConnectionDriverCount =
        summary.portConnections;
}

void storeValueDriverSummary(
    const ValueSymbol& value,
    const QString& instancePath,
    const slang::SourceManager* sourceManager,
    const slang::analysis::AnalysisManager& analysis,
    bool absenceIsProven,
    QHash<QString, SemanticSymbolPresentation>* presentations)
{
    if (!presentations || instancePath.isEmpty())
        return;
    const QString key =
        identityKey(sourceManager, value);
    auto presentation = presentations->find(key);
    if (key.isEmpty()
        || presentation == presentations->end()) {
        return;
    }
    auto instance =
        presentation->instanceInfoByPath.find(
            instancePath);
    if (instance
        == presentation->instanceInfoByPath.end()) {
        return;
    }

    const SlangDriverSummary summary =
        driverSummary(
            analysis, &value, absenceIsProven);
    instance->driverPresence = summary.presence;
    instance->driverCount = summary.total;
    instance->continuousDriverCount =
        summary.continuous;
    instance->proceduralDriverCount =
        summary.procedural;
    instance->portConnectionDriverCount =
        summary.portConnections;
}

void gatherScopeDriverSummaries(
    const Scope& scope,
    const QString& instancePath,
    const slang::SourceManager* sourceManager,
    const slang::analysis::AnalysisManager& analysis,
    bool absenceIsProven,
    QHash<QString, SemanticSymbolPresentation>* presentations,
    const std::function<bool()>& isCancelled)
{
    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    auto visitor = makeVisitor(
        [&](auto&, const InstanceSymbol&) {
            // Nested instances are handled with their own elaborated path.
        },
        [&](auto& nestedVisitor, const PortSymbol& port) {
            if (cancelled())
                return;
            storePortDriverSummary(port,
                                   instancePath,
                                   sourceManager,
                                   analysis,
                                   absenceIsProven,
                                   presentations);
            nestedVisitor.visitDefault(port);
        },
        [&](auto& nestedVisitor,
            const VariableSymbol& variable) {
            if (cancelled())
                return;
            storeValueDriverSummary(
                variable,
                instancePath,
                sourceManager,
                analysis,
                absenceIsProven,
                presentations);
            nestedVisitor.visitDefault(variable);
        },
        [&](auto& nestedVisitor,
            const NetSymbol& net) {
            if (cancelled())
                return;
            storeValueDriverSummary(
                net,
                instancePath,
                sourceManager,
                analysis,
                absenceIsProven,
                presentations);
            nestedVisitor.visitDefault(net);
        });
    for (const Symbol& member : scope.members()) {
        if (cancelled())
            return;
        member.visit(visitor);
    }
}

void gatherInstanceDriverSummaries(
    const InstanceSymbol& root,
    const slang::SourceManager* sourceManager,
    const slang::analysis::AnalysisManager& analysis,
    bool absenceIsProven,
    QHash<QString, SemanticSymbolPresentation>* presentations,
    const std::function<bool()>& isCancelled)
{
    auto visitor = makeVisitor(
        [&](auto& nestedVisitor, const InstanceSymbol& instance) {
            if (isCancelled && isCancelled())
                return;
            const QString path = QString::fromStdString(
                instance.getHierarchicalPath());
            gatherScopeDriverSummaries(instance.body,
                                       path,
                                       sourceManager,
                                       analysis,
                                       absenceIsProven,
                                       presentations,
                                       isCancelled);
            if (isCancelled && isCancelled())
                return;
            nestedVisitor.visitDefault(instance);
        });
    root.visit(visitor);
}

} // namespace

void slang_symbols::populateSymbolDriverSummaries(
    Compilation& compilation,
    QList<SemanticSymbolRecord>& records,
    const std::function<bool()>& isCancelled)
{
    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    const slang::SourceManager* sourceManager =
        compilation.getSourceManager();
    if (!sourceManager || records.isEmpty() || cancelled())
        return;

    QHash<QString, SemanticSymbolPresentation> presentations;
    for (const SemanticSymbolRecord& record : std::as_const(records)) {
        const QChar kind = identityKind(record);
        if (kind != QLatin1Char('o')
            && kind != QLatin1Char('v')) {
            continue;
        }
        const QString key = identityKey(record);
        if (!key.isEmpty())
            presentations.insert(key, record.presentation);
    }
    if (presentations.isEmpty())
        return;

    try {
        if (!compilation.isFrozen()) {
            compilation.getAllDiagnostics();
            compilation.freeze();
        }
        if (!compilation.isElaborated()
            || compilation.hasFatalErrors()) {
            return;
        }

        slang::analysis::AnalysisManager analysis;
        analysis.analyze(compilation);
        const bool absenceIsProven =
            !compilation.hasIssuedErrors();

        slang_symbols::detail::resetQTextDocumentSourcePositionCache(
            sourceManager);
        struct SourcePositionCacheReset {
            ~SourcePositionCacheReset()
            {
                slang_symbols::detail::
                    resetQTextDocumentSourcePositionCache(nullptr);
            }
        } sourcePositionCacheReset;

        const RootSymbol& root = compilation.getRootNoFinalize();
        for (const InstanceSymbol* top : root.topInstances) {
            if (cancelled())
                return;
            if (!top)
                continue;
            gatherInstanceDriverSummaries(
                *top,
                sourceManager,
                analysis,
                absenceIsProven,
                &presentations,
                isCancelled);
        }
    } catch (const std::exception&) {
        // Preserve Unknown. A failed Slang analysis must never be published
        // as proof that an endpoint has no drivers.
        return;
    } catch (...) {
        return;
    }

    QList<SemanticSymbolRecord> updatedRecords = records;
    for (SemanticSymbolRecord& record : updatedRecords) {
        if (cancelled())
            return;
        const QString key = identityKey(record);
        const auto presentation = presentations.constFind(key);
        if (presentation != presentations.constEnd())
            record.presentation = presentation.value();
    }
    if (!cancelled())
        records = std::move(updatedRecords);
}
