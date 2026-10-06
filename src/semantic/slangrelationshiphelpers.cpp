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

struct ValueAccess {
    const ValueSymbol* symbol = nullptr;
    QString path;
    QList<const Expression*> selectors;
};

ValueAccess valueAccess(const Expression& expr)
{
    if (const auto* value = expr.as_if<ValueExpressionBase>())
        return {&value->symbol, valueSymbolName(value->symbol), {}};
    if (const auto* select = expr.as_if<ElementSelectExpression>()) {
        auto access = valueAccess(select->value());
        access.selectors.append(&select->selector());
        return access;
    }
    if (const auto* select = expr.as_if<RangeSelectExpression>()) {
        auto access = valueAccess(select->value());
        access.selectors.append(&select->left());
        access.selectors.append(&select->right());
        return access;
    }
    if (const auto* member = expr.as_if<MemberAccessExpression>()) {
        auto access = valueAccess(member->value());
        if (access.symbol)
            access.path += QLatin1Char('.') + astSymbolName(member->member);
        return access;
    }
    if (const auto* conversion = expr.as_if<ConversionExpression>())
        return valueAccess(conversion->operand());
    return {};
}

SemanticValueReference capturedValue(const ValueSymbol& symbol, const slang::SourceManager* sm)
{
    SemanticSymbolRecord record;
    SemanticValueReference reference;
    reference.name = valueSymbolName(symbol);
    reference.bindingCaptured = true;
    if (slang_symbols::detail::fillSymbolRecord(sm, symbol, record, nullptr)) {
        reference.location = record.location;
        reference.declaringScope = record.owner.stableKey;
    }
    return reference;
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

SemanticValueReference valueReference(const Expression& expr, const slang::SourceManager* sm)
{
    const auto access = valueAccess(expr);
    if (!access.symbol) return {};
    auto reference = capturedValue(*access.symbol, sm);
    reference.accessPath = access.path;
    return reference;
}

QList<SemanticValueReference> collectValueReferences(const Expression& expr, const slang::SourceManager* sm)
{
    QList<SemanticValueReference> references;
    // These AST pointers live only during this traversal, never in capture facts.
    QHash<const ValueSymbol*, SemanticValueReference> declarations;
    QHash<const ValueSymbol*, QSet<QString>> seenPaths;
    auto captureAccess = [&](auto& visitor, const auto& expression) {
        const auto access = valueAccess(expression);
        if (!access.symbol) {
            visitor.visitDefault(expression);
            return;
        }
        auto& paths = seenPaths[access.symbol];
        if (!paths.contains(access.path)) {
            paths.insert(access.path);
            auto declaration = declarations.find(access.symbol);
            if (declaration == declarations.end())
                declaration = declarations.insert(access.symbol, capturedValue(*access.symbol, sm));
            auto reference = declaration.value();
            reference.accessPath = access.path;
            references.append(reference);
        }
        // The value/member chain was consumed above. Visit its selectors once,
        // including selectors nested underneath a member access.
        for (const auto* selector : access.selectors) selector->visit(visitor);
    };
    auto visitor = makeVisitor(
        [&](auto& v, const NamedValueExpression& value) { captureAccess(v, value); },
        [&](auto& v, const HierarchicalValueExpression& value) { captureAccess(v, value); },
        [&](auto& v, const MemberAccessExpression& value) { captureAccess(v, value); },
        [&](auto& v, const ElementSelectExpression& value) { captureAccess(v, value); },
        [&](auto& v, const RangeSelectExpression& value) { captureAccess(v, value); });
    expr.visit(visitor);
    return references;
}

void projectValueReferences(const QList<SemanticValueReference>& references,
                            QStringList& names, QStringList& accessPaths)
{
    QSet<QString> seenNames, seenPaths;
    for (const auto& reference : references) {
        if (!reference.name.isEmpty() && !seenNames.contains(reference.name)) {
            seenNames.insert(reference.name);
            names.append(reference.name);
        }
        if (!reference.accessPath.isEmpty() && !seenPaths.contains(reference.accessPath)) {
            seenPaths.insert(reference.accessPath);
            accessPaths.append(reference.accessPath);
        }
    }
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
    info.references = collectValueReferences(expr, sm);
    projectValueReferences(info.references, info.symbolNames, info.symbolAccessPaths);
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
    const auto references = collectValueReferences(event.expr, sm);
    size_t line = sm ? sm->getLineNumber(event.sourceRange.start()) : 0;
    const int lineNumber = (line == 0) ? 1 : static_cast<int>(line);
    const bool edgeSensitive = event.edge != EdgeKind::None;
    const SemanticSourceRange sourceRange =
        relationshipEvidenceRange(sm, event.sourceRange);

    for (const auto& reference : references) {
        TimingSignalInfo info;
        info.signalName = reference.name;
        info.signalAccessPath = reference.accessPath;
        info.lineNumber = lineNumber;
        info.edgeSensitive = edgeSensitive;
        info.sourceRange = sourceRange;
        info.reference = reference;
        result.append(info);
    }
}

} // namespace slang_relationship::detail
