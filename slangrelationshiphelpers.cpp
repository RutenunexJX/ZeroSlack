#include "slangrelationshiphelpers.h"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/expressions/ConversionExpression.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/expressions/SelectExpressions.h>
#include <slang/text/SourceLocation.h>
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

slang::SourceLocation fileOrExpansionLocation(const slang::SourceManager* sm,
                                              slang::SourceLocation loc)
{
    if (!sm || !loc)
        return {};
    return sm->isFileLoc(loc) ? loc : sm->getExpansionLoc(loc);
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

SemanticSourceRange relationshipEvidenceRange(
    const slang::SourceManager* sm,
    slang::SourceRange range)
{
    SemanticSourceRange evidence;
    if (!sm || !range.start())
        return evidence;

    const slang::SourceLocation start =
        fileOrExpansionLocation(sm, range.start());
    const slang::SourceLocation end =
        fileOrExpansionLocation(sm, range.end());
    if (!start)
        return evidence;

    evidence.fileName =
        QString::fromStdString(std::string(sm->getFileName(start)));
    evidence.line = static_cast<int>(sm->getLineNumber(start));
    evidence.column = static_cast<int>(sm->getColumnNumber(start));
    if (end && sm->getFileName(end) == sm->getFileName(start)) {
        evidence.endLine = static_cast<int>(sm->getLineNumber(end));
        evidence.endColumn = static_cast<int>(sm->getColumnNumber(end));
    } else {
        evidence.endLine = evidence.line;
        evidence.endColumn = evidence.column;
    }
    return evidence;
}

void appendConditionReference(QVector<ConditionReferenceInfo>& result,
                              const slang::SourceManager* sm,
                              const Expression& expr)
{
    ConditionReferenceInfo info;
    info.symbolNames = collectValueNames(expr);
    size_t line = sm ? sm->getLineNumber(expr.sourceRange.start()) : 0;
    info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
    info.sourceRange = relationshipEvidenceRange(sm, expr.sourceRange);
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
    const SemanticSourceRange sourceRange =
        relationshipEvidenceRange(sm, event.sourceRange);

    for (const QString& name : names) {
        TimingSignalInfo info;
        info.signalName = name;
        info.lineNumber = lineNumber;
        info.edgeSensitive = edgeSensitive;
        info.sourceRange = sourceRange;
        result.append(info);
    }
}

} // namespace slang_relationship::detail
