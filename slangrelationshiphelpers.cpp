#include "slangrelationshiphelpers.h"
#include "slangsymbolcollectorhelpers.h"

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

QString astSymbolName(const Symbol& symbol)
{
    return QString::fromStdString(std::string(symbol.name));
}

slang::SourceLocation fileOrExpansionLocation(const slang::SourceManager* sm,
                                              slang::SourceLocation loc)
{
    if (!sm || !loc)
        return {};

    const slang::SourceLocation expanded = sm->getFullyExpandedLoc(loc);
    return expanded && sm->isFileLoc(expanded) ? expanded : slang::SourceLocation();
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

QString expressionAccessPath(const Expression& expr)
{
    if (const auto* value = expr.as_if<ValueExpressionBase>())
        return valueSymbolName(value->symbol);
    if (const auto* select = expr.as_if<ElementSelectExpression>())
        return expressionAccessPath(select->value());
    if (const auto* select = expr.as_if<RangeSelectExpression>())
        return expressionAccessPath(select->value());
    if (const auto* member = expr.as_if<MemberAccessExpression>()) {
        const QString basePath = expressionAccessPath(member->value());
        const QString memberName = astSymbolName(member->member);
        if (basePath.isEmpty())
            return memberName;
        if (memberName.isEmpty())
            return basePath;
        return QStringLiteral("%1.%2").arg(basePath, memberName);
    }
    if (const auto* conversion = expr.as_if<ConversionExpression>())
        return expressionAccessPath(conversion->operand());

    return QString();
}

bool isDirectValueForwardExpression(const Expression& expr)
{
    if (const auto* conversion = expr.as_if<ConversionExpression>())
        return isDirectValueForwardExpression(conversion->operand());
    // Selects, member access, unary/binary operators, concatenations, calls,
    // and casts are deliberately rejected. Only one whole Slang value symbol
    // can prove an identity bridge.
    return expr.as_if<ValueExpressionBase>() != nullptr;
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

QStringList collectValueAccessPaths(const Expression& expr)
{
    QStringList accessPaths;
    QSet<QString> seen;

    auto append = [&](const QString& accessPath) {
        if (!accessPath.isEmpty() && !seen.contains(accessPath)) {
            seen.insert(accessPath);
            accessPaths.append(accessPath);
        }
    };

    auto visitor = makeVisitor(
        [&](auto& v, const MemberAccessExpression& member) {
            append(expressionAccessPath(member));
            Q_UNUSED(v);
        },
        [&](auto& v, const ElementSelectExpression& select) {
            append(expressionAccessPath(select));
            select.selector().visit(v);
        },
        [&](auto& v, const RangeSelectExpression& select) {
            append(expressionAccessPath(select));
            select.left().visit(v);
            select.right().visit(v);
        },
        [&](auto& v, const NamedValueExpression& named) {
            append(valueSymbolName(named.symbol));
            v.visitDefault(named);
        },
        [&](auto& v, const HierarchicalValueExpression& hierarchical) {
            append(valueSymbolName(hierarchical.symbol));
            v.visitDefault(hierarchical);
        }
    );
    expr.visit(visitor);

    return accessPaths;
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

    evidence.fileName = slang_symbols::detail::sourceIdentityFileName(
        sm, start);
    evidence.line = static_cast<int>(sm->getLineNumber(start));
    evidence.column = static_cast<int>(sm->getColumnNumber(start));
    if (end
        && slang_symbols::detail::sourceIdentityFileName(sm, end)
               == evidence.fileName) {
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
    info.symbolAccessPaths = collectValueAccessPaths(expr);
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
    const QStringList accessPaths = collectValueAccessPaths(event.expr);
    size_t line = sm ? sm->getLineNumber(event.sourceRange.start()) : 0;
    const int lineNumber = (line == 0) ? 1 : static_cast<int>(line);
    const bool edgeSensitive = event.edge != EdgeKind::None;
    const SemanticSourceRange sourceRange =
        relationshipEvidenceRange(sm, event.sourceRange);

    for (int i = 0; i < names.size(); ++i) {
        const QString name = names.at(i);
        TimingSignalInfo info;
        info.signalName = name;
        if (i < accessPaths.size())
            info.signalAccessPath = accessPaths.at(i);
        if (info.signalAccessPath.isEmpty())
            info.signalAccessPath = name;
        info.lineNumber = lineNumber;
        info.edgeSensitive = edgeSensitive;
        info.sourceRange = sourceRange;
        result.append(info);
    }
}

} // namespace slang_relationship::detail
