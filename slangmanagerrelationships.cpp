#include "slangmanager.h"
#include "slangrelationshiphelpers.h"
#include "slangrelationshipparse.h"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/CallExpression.h>
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/LoopStatements.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/text/SourceManager.h>

#include <string>

using namespace slang::ast;
using namespace slang_relationship::detail;

RelationshipExtractionInfo SlangManager::extractRelationshipInfo(const QString& fileName,
                                                                 const QString& content,
                                                                 const QStringList& includeDirs,
                                                                 const QHash<QString, QString>& defines)
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
                info.sourceRange = relationshipEvidenceRange(
                    sm,
                    slang::SourceRange(inst.location, inst.location));
                result.moduleInstantiations.append(info);
                v.visitDefault(inst);
            },
            [&](auto& v, const CallExpression& call) {
                if (!call.isSystemCall()) {
                    SubroutineCallInfo info;
                    info.subroutineName = QString::fromStdString(std::string(call.getSubroutineName()));
                    size_t line = sm->getLineNumber(call.sourceRange.start());
                    info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
                    info.sourceRange = relationshipEvidenceRange(sm, call.sourceRange);
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
                info.sourceRange =
                    relationshipEvidenceRange(sm, assignment.sourceRange);
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
        },
        includeDirs,
        defines);
}

QVector<ModuleInstantiationInfo> SlangManager::extractModuleInstantiations(const QString& fileName,
                                                                           const QString& content,
                                                                           const QStringList& includeDirs,
                                                                           const QHash<QString, QString>& defines)
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
                info.sourceRange = relationshipEvidenceRange(
                    sm,
                    slang::SourceRange(inst.location, inst.location));
                result.append(info);
                v.visitDefault(inst);
            });
        },
        includeDirs,
        defines);
}

QVector<SubroutineCallInfo> SlangManager::extractSubroutineCalls(const QString& fileName,
                                                                 const QString& content,
                                                                 const QStringList& includeDirs,
                                                                 const QHash<QString, QString>& defines)
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
                    info.sourceRange = relationshipEvidenceRange(sm, call.sourceRange);
                    result.append(info);
                }
                v.visitDefault(call);
            });
        },
        includeDirs,
        defines);
}

QVector<AssignmentInfo> SlangManager::extractAssignments(const QString& fileName,
                                                         const QString& content,
                                                         const QStringList& includeDirs,
                                                         const QHash<QString, QString>& defines)
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
                info.sourceRange =
                    relationshipEvidenceRange(sm, assignment.sourceRange);
                if (!info.leftName.isEmpty() && !info.rightNames.isEmpty())
                    result.append(info);
                v.visitDefault(assignment);
            });
        },
        includeDirs,
        defines);
}

QVector<ConditionReferenceInfo> SlangManager::extractConditionReferences(const QString& fileName,
                                                                         const QString& content,
                                                                         const QStringList& includeDirs,
                                                                         const QHash<QString, QString>& defines)
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
        },
        includeDirs,
        defines);
}

QVector<TimingSignalInfo> SlangManager::extractTimingSignals(const QString& fileName,
                                                             const QString& content,
                                                             const QStringList& includeDirs,
                                                             const QHash<QString, QString>& defines)
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
        },
        includeDirs,
        defines);
}
