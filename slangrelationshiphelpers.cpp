#include "slangrelationshiphelpers.h"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/expressions/ConversionExpression.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/expressions/SelectExpressions.h>
#include <slang/text/SourceManager.h>

#include <QSet>
#include <string>

using namespace slang::ast;

namespace slang_relationship::detail {

namespace {

QString valueSymbolName(const ValueSymbol& symbol)
{
    return QString::fromStdString(std::string(symbol.name));
}

} // namespace

QString assignmentRootName(const Expression& expr)
{
    if (const auto* value = expr.as_if<ValueExpressionBase>())
        return valueSymbolName(value->symbol);
    if (const auto* select = expr.as_if<ElementSelectExpression>())
        return assignmentRootName(select->value());
    if (const auto* select = expr.as_if<RangeSelectExpression>())
        return assignmentRootName(select->value());
    if (const auto* member = expr.as_if<MemberAccessExpression>())
        return assignmentRootName(member->value());
    if (const auto* conversion = expr.as_if<ConversionExpression>())
        return assignmentRootName(conversion->operand());

    return QString();
}

QStringList collectValueNames(const Expression& expr)
{
    QStringList names;
    QSet<QString> seen;

    auto append = [&](const ValueSymbol& symbol) {
        QString name = valueSymbolName(symbol);
        if (!name.isEmpty() && !seen.contains(name)) {
            seen.insert(name);
            names.append(name);
        }
    };

    auto visitor = makeVisitor(
        [&](auto& v, const NamedValueExpression& named) {
            append(named.symbol);
            v.visitDefault(named);
        },
        [&](auto& v, const HierarchicalValueExpression& hierarchical) {
            append(hierarchical.symbol);
            v.visitDefault(hierarchical);
        }
    );
    expr.visit(visitor);

    return names;
}

void appendConditionReference(QVector<ConditionReferenceInfo>& result,
                              const slang::SourceManager* sm,
                              const Expression& expr)
{
    ConditionReferenceInfo info;
    info.symbolNames = collectValueNames(expr);
    size_t line = sm ? sm->getLineNumber(expr.sourceRange.start()) : 0;
    info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
    if (!info.symbolNames.isEmpty())
        result.append(info);
}

void appendTimingSignal(QVector<TimingSignalInfo>& result,
                        const slang::SourceManager* sm,
                        const SignalEventControl& event)
{
    const QStringList names = collectValueNames(event.expr);
    size_t line = sm ? sm->getLineNumber(event.sourceRange.start()) : 0;
    const int lineNumber = (line == 0) ? 1 : static_cast<int>(line);
    const bool edgeSensitive = event.edge != EdgeKind::None;

    for (const QString& name : names) {
        TimingSignalInfo info;
        info.signalName = name;
        info.lineNumber = lineNumber;
        info.edgeSensitive = edgeSensitive;
        result.append(info);
    }
}

} // namespace slang_relationship::detail
