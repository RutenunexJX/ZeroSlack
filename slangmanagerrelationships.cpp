#include "slangmanager.h"
#include "slangrelationshipparse.h"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/CallExpression.h>
#include <slang/ast/expressions/ConversionExpression.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/expressions/SelectExpressions.h>
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/LoopStatements.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/text/SourceManager.h>

#include <QSet>
#include <string>

using namespace slang::ast;

namespace {

QString valueSymbolName(const slang::ast::ValueSymbol& symbol)
{
    return QString::fromStdString(std::string(symbol.name));
}

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

} // namespace

RelationshipExtractionInfo SlangManager::extractRelationshipInfo(const QString& fileName,
                                                                 const QString& content)
{
    return slang_relationship::extractFromText<RelationshipExtractionInfo>(
        fileName,
        content,
        [](RelationshipExtractionInfo& result, const slang::SourceManager* sm) {
            return makeVisitor(
            [&](auto& v, const InstanceSymbol& inst) {
                ModuleInstantiationInfo info;
                info.instanceName = QString::fromStdString(std::string(inst.name));
                info.moduleName = QString::fromStdString(std::string(inst.getDefinition().name));
                size_t line = sm->getLineNumber(inst.location);
                info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
                result.moduleInstantiations.append(info);
                v.visitDefault(inst);
            },
            [&](auto& v, const CallExpression& call) {
                if (!call.isSystemCall()) {
                    SubroutineCallInfo info;
                    info.subroutineName = QString::fromStdString(std::string(call.getSubroutineName()));
                    size_t line = sm->getLineNumber(call.sourceRange.start());
                    info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
                    result.subroutineCalls.append(info);
                }
                v.visitDefault(call);
            },
            [&](auto& v, const AssignmentExpression& assignment) {
                AssignmentInfo info;
                info.leftName = assignmentRootName(assignment.left());
                info.rightNames = collectValueNames(assignment.right());
                size_t line = sm->getLineNumber(assignment.sourceRange.start());
                info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
                if (!info.leftName.isEmpty() && !info.rightNames.isEmpty())
                    result.assignments.append(info);
                v.visitDefault(assignment);
            },
            [&](auto& v, const ConditionalStatement& stmt) {
                for (const auto& condition : stmt.conditions)
                    appendConditionReference(result.conditionReferences, sm, *condition.expr);
                v.visitDefault(stmt);
            },
            [&](auto& v, const CaseStatement& stmt) {
                appendConditionReference(result.conditionReferences, sm, stmt.expr);
                for (const auto& item : stmt.items) {
                    for (const Expression* expr : item.expressions)
                        appendConditionReference(result.conditionReferences, sm, *expr);
                }
                v.visitDefault(stmt);
            },
            [&](auto& v, const PatternCaseStatement& stmt) {
                appendConditionReference(result.conditionReferences, sm, stmt.expr);
                for (const auto& item : stmt.items) {
                    if (item.filter)
                        appendConditionReference(result.conditionReferences, sm, *item.filter);
                }
                v.visitDefault(stmt);
            },
            [&](auto& v, const ForLoopStatement& stmt) {
                if (stmt.stopExpr)
                    appendConditionReference(result.conditionReferences, sm, *stmt.stopExpr);
                v.visitDefault(stmt);
            },
            [&](auto& v, const RepeatLoopStatement& stmt) {
                appendConditionReference(result.conditionReferences, sm, stmt.count);
                v.visitDefault(stmt);
            },
            [&](auto& v, const ForeachLoopStatement& stmt) {
                appendConditionReference(result.conditionReferences, sm, stmt.arrayRef);
                v.visitDefault(stmt);
            },
            [&](auto& v, const WhileLoopStatement& stmt) {
                appendConditionReference(result.conditionReferences, sm, stmt.cond);
                v.visitDefault(stmt);
            },
            [&](auto& v, const DoWhileLoopStatement& stmt) {
                appendConditionReference(result.conditionReferences, sm, stmt.cond);
                v.visitDefault(stmt);
            },
            [&](auto& v, const SignalEventControl& event) {
                appendTimingSignal(result.timingSignals, sm, event);
                v.visitDefault(event);
            });
        });
}

QVector<ModuleInstantiationInfo> SlangManager::extractModuleInstantiations(const QString& fileName,
                                                                           const QString& content)
{
    return slang_relationship::extractFromText<QVector<ModuleInstantiationInfo>>(
        fileName,
        content,
        [](QVector<ModuleInstantiationInfo>& result, const slang::SourceManager* sm) {
            return makeVisitor(
            [&](auto& v, const InstanceSymbol& inst) {
                ModuleInstantiationInfo info;
                info.instanceName = QString::fromStdString(std::string(inst.name));
                info.moduleName = QString::fromStdString(std::string(inst.getDefinition().name));
                size_t line = sm->getLineNumber(inst.location);
                info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
                result.append(info);
                v.visitDefault(inst);
            });
        });
}

QVector<SubroutineCallInfo> SlangManager::extractSubroutineCalls(const QString& fileName,
                                                                 const QString& content)
{
    return slang_relationship::extractFromText<QVector<SubroutineCallInfo>>(
        fileName,
        content,
        [](QVector<SubroutineCallInfo>& result, const slang::SourceManager* sm) {
            return makeVisitor(
            [&](auto& v, const CallExpression& call) {
                if (!call.isSystemCall()) {
                    SubroutineCallInfo info;
                    info.subroutineName = QString::fromStdString(std::string(call.getSubroutineName()));
                    size_t line = sm->getLineNumber(call.sourceRange.start());
                    info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
                    result.append(info);
                }
                v.visitDefault(call);
            });
        });
}

QVector<AssignmentInfo> SlangManager::extractAssignments(const QString& fileName,
                                                         const QString& content)
{
    return slang_relationship::extractFromText<QVector<AssignmentInfo>>(
        fileName,
        content,
        [](QVector<AssignmentInfo>& result, const slang::SourceManager* sm) {
            return makeVisitor(
            [&](auto& v, const AssignmentExpression& assignment) {
                AssignmentInfo info;
                info.leftName = assignmentRootName(assignment.left());
                info.rightNames = collectValueNames(assignment.right());
                size_t line = sm->getLineNumber(assignment.sourceRange.start());
                info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
                if (!info.leftName.isEmpty() && !info.rightNames.isEmpty())
                    result.append(info);
                v.visitDefault(assignment);
            });
        });
}

QVector<ConditionReferenceInfo> SlangManager::extractConditionReferences(const QString& fileName,
                                                                         const QString& content)
{
    return slang_relationship::extractFromText<QVector<ConditionReferenceInfo>>(
        fileName,
        content,
        [](QVector<ConditionReferenceInfo>& result, const slang::SourceManager* sm) {
            return makeVisitor(
            [&](auto& v, const ConditionalStatement& stmt) {
                for (const auto& condition : stmt.conditions)
                    appendConditionReference(result, sm, *condition.expr);
                v.visitDefault(stmt);
            },
            [&](auto& v, const CaseStatement& stmt) {
                appendConditionReference(result, sm, stmt.expr);
                for (const auto& item : stmt.items) {
                    for (const Expression* expr : item.expressions)
                        appendConditionReference(result, sm, *expr);
                }
                v.visitDefault(stmt);
            },
            [&](auto& v, const PatternCaseStatement& stmt) {
                appendConditionReference(result, sm, stmt.expr);
                for (const auto& item : stmt.items) {
                    if (item.filter)
                        appendConditionReference(result, sm, *item.filter);
                }
                v.visitDefault(stmt);
            },
            [&](auto& v, const ForLoopStatement& stmt) {
                if (stmt.stopExpr)
                    appendConditionReference(result, sm, *stmt.stopExpr);
                v.visitDefault(stmt);
            },
            [&](auto& v, const RepeatLoopStatement& stmt) {
                appendConditionReference(result, sm, stmt.count);
                v.visitDefault(stmt);
            },
            [&](auto& v, const ForeachLoopStatement& stmt) {
                appendConditionReference(result, sm, stmt.arrayRef);
                v.visitDefault(stmt);
            },
            [&](auto& v, const WhileLoopStatement& stmt) {
                appendConditionReference(result, sm, stmt.cond);
                v.visitDefault(stmt);
            },
            [&](auto& v, const DoWhileLoopStatement& stmt) {
                appendConditionReference(result, sm, stmt.cond);
                v.visitDefault(stmt);
            });
        });
}

QVector<TimingSignalInfo> SlangManager::extractTimingSignals(const QString& fileName,
                                                             const QString& content)
{
    return slang_relationship::extractFromText<QVector<TimingSignalInfo>>(
        fileName,
        content,
        [](QVector<TimingSignalInfo>& result, const slang::SourceManager* sm) {
            return makeVisitor(
            [&](auto& v, const SignalEventControl& event) {
                appendTimingSignal(result, sm, event);
                v.visitDefault(event);
            });
        });
}
