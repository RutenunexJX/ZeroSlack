#include "slangmanager.h"
#include "slangcompilationcollectors.h"
#include "semanticanalysisinput.h"
#include "slangparseoptions.h"
#include "slangrelationshiphelpers.h"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/CallExpression.h>
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/LoopStatements.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace slang::ast;
using namespace slang_relationship::detail;

namespace {

AssignmentInfo assignmentInfoFromExpression(const AssignmentExpression& assignment,
                                            const slang::SourceManager* sm)
{
    AssignmentInfo info;
    info.leftReference = valueReference(assignment.left(), sm);
    info.leftName = info.leftReference.name;
    info.leftAccessPath = info.leftReference.accessPath;
    info.rightReferences = collectValueReferences(assignment.right(), sm);
    projectValueReferences(info.rightReferences, info.rightNames, info.rightAccessPaths);
    info.exactValueForward =
        isDirectValueForwardExpression(assignment.right())
        && info.rightNames.size() == 1
        && info.rightAccessPaths.size() == 1;
    size_t line = sm ? sm->getLineNumber(assignment.sourceRange.start()) : 0;
    info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
    info.sourceRange = relationshipEvidenceRange(sm, assignment.sourceRange);
    return info;
}
bool appendAssignmentInfo(QVector<AssignmentInfo>& result,
                          const AssignmentExpression& assignment,
                          const slang::SourceManager* sm)
{
    AssignmentInfo info = assignmentInfoFromExpression(assignment, sm);
    if (info.leftName.isEmpty() || info.rightNames.isEmpty())
        return false;

    result.append(info);
    return true;
}

bool appendContinuousAssignmentInfo(QVector<AssignmentInfo>& result,
                                    const ContinuousAssignSymbol& continuousAssign,
                                    const slang::SourceManager* sm)
{
    const int before = result.size();
    const Expression& expression = continuousAssign.getAssignment();
    if (const auto* assignment = expression.as_if<AssignmentExpression>())
        appendAssignmentInfo(result, *assignment, sm);
    return result.size() > before;
}

QString normalizedSourceFileName(const QString& path)
{
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

void mergeRelationshipInfo(RelationshipExtractionInfo& target,
                           const RelationshipExtractionInfo& source)
{
    target.moduleInstantiations += source.moduleInstantiations;
    target.subroutineCalls += source.subroutineCalls;
    target.assignments += source.assignments;
    target.conditionReferences += source.conditionReferences;
    target.timingSignals += source.timingSignals;
}

void appendRelationshipInfoForFile(
    QHash<QString, RelationshipExtractionInfo>& grouped,
    const QString& fileName,
    const RelationshipExtractionInfo& source)
{
    const QString key = normalizedSourceFileName(fileName);
    if (key.isEmpty())
        return;
    mergeRelationshipInfo(grouped[key], source);
}

void clearRelationshipInfo(RelationshipExtractionInfo* info)
{
    if (!info)
        return;
    info->moduleInstantiations.clear();
    info->subroutineCalls.clear();
    info->assignments.clear();
    info->conditionReferences.clear();
    info->timingSignals.clear();
}

void appendModuleInstantiation(RelationshipExtractionInfo& result,
                               const slang::SourceManager* sm,
                               const InstanceSymbol& inst)
{
    const SemanticSourceRange sourceRange =
        relationshipEvidenceRange(sm, slang::SourceRange(inst.location, inst.location));
    if (sourceRange.line <= 0)
        return;

    ModuleInstantiationInfo info;
    info.instanceName = QString::fromStdString(std::string(inst.name));
    info.moduleName = QString::fromStdString(std::string(inst.getDefinition().name));
    info.lineNumber = sourceRange.line;
    info.sourceRange = sourceRange;
    result.moduleInstantiations.append(info);
}

void appendSubroutineCall(RelationshipExtractionInfo& result,
                          const slang::SourceManager* sm,
                          const CallExpression& call)
{
    if (call.isSystemCall())
        return;

    SubroutineCallInfo info;
    info.subroutineName = QString::fromStdString(std::string(call.getSubroutineName()));
    size_t line = sm ? sm->getLineNumber(call.sourceRange.start()) : 0;
    info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
    info.sourceRange = relationshipEvidenceRange(sm, call.sourceRange);
    result.subroutineCalls.append(info);
}

auto makeRelationshipVisitor(RelationshipExtractionInfo& result,
                             const slang::SourceManager* sm,
                             std::function<bool()> isCancelled = nullptr,
                             bool* cancellationReached = nullptr)
{
    auto cancelled = [isCancelled = std::move(isCancelled),
                      cancellationReached]() {
        if (isCancelled && isCancelled()) {
            if (cancellationReached)
                *cancellationReached = true;
            return true;
        }
        return false;
    };
    return makeVisitor(
        [&, cancelled](auto& v, const InstanceSymbol& inst) {
            if (cancelled())
                return;
            if (inst.instanceDepth == 0) {
                inst.body.visit(v);
                return;
            }
            appendModuleInstantiation(result, sm, inst);
            if (cancelled())
                return;
            v.visitDefault(inst);
        },
        [&, cancelled](auto& v, const CallExpression& call) {
            if (cancelled())
                return;
            appendSubroutineCall(result, sm, call);
            if (cancelled())
                return;
            v.visitDefault(call);
        },
        [&, cancelled](auto& v, const AssignmentExpression& assignment) {
            if (cancelled())
                return;
            appendAssignmentInfo(result.assignments, assignment, sm);
            if (cancelled())
                return;
            v.visitDefault(assignment);
        },
        [&, cancelled](auto& v, const ContinuousAssignSymbol& continuousAssign) {
            if (cancelled())
                return;
            if (appendContinuousAssignmentInfo(result.assignments,
                                               continuousAssign,
                                               sm)) {
                return;
            }
            if (cancelled())
                return;
            v.visitDefault(continuousAssign);
        },
        [&, cancelled](auto& v, const ConditionalStatement& stmt) {
            if (cancelled())
                return;
            for (const auto& condition : stmt.conditions)
                appendConditionReference(result.conditionReferences, sm, *condition.expr);
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const CaseStatement& stmt) {
            if (cancelled())
                return;
            appendConditionReference(result.conditionReferences, sm, stmt.expr);
            for (const auto& item : stmt.items) {
                if (cancelled())
                    return;
                for (const Expression* expr : item.expressions)
                    appendConditionReference(result.conditionReferences, sm, *expr);
            }
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const PatternCaseStatement& stmt) {
            if (cancelled())
                return;
            appendConditionReference(result.conditionReferences, sm, stmt.expr);
            for (const auto& item : stmt.items) {
                if (cancelled())
                    return;
                if (item.filter)
                    appendConditionReference(result.conditionReferences, sm, *item.filter);
            }
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const ForLoopStatement& stmt) {
            if (cancelled())
                return;
            if (stmt.stopExpr)
                appendConditionReference(result.conditionReferences, sm, *stmt.stopExpr);
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const RepeatLoopStatement& stmt) {
            if (cancelled())
                return;
            appendConditionReference(result.conditionReferences, sm, stmt.count);
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const ForeachLoopStatement& stmt) {
            if (cancelled())
                return;
            appendConditionReference(result.conditionReferences, sm, stmt.arrayRef);
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const WhileLoopStatement& stmt) {
            if (cancelled())
                return;
            appendConditionReference(result.conditionReferences, sm, stmt.cond);
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const DoWhileLoopStatement& stmt) {
            if (cancelled())
                return;
            appendConditionReference(result.conditionReferences, sm, stmt.cond);
            if (cancelled())
                return;
            v.visitDefault(stmt);
        },
        [&, cancelled](auto& v, const SignalEventControl& event) {
            if (cancelled())
                return;
            appendTimingSignal(result.timingSignals, sm, event);
            if (cancelled())
                return;
            v.visitDefault(event);
        });
}

bool collectWorkspaceRelationships(
    Compilation& compilation,
    QHash<QString, RelationshipExtractionInfo>* grouped,
    const std::function<bool()>& isCancelled)
{
    if (!grouped)
        return false;
    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    if (cancelled())
        return false;

    if (cancelled())
        return false;

    RelationshipExtractionInfo allRelationships;
    const RootSymbol& root = compilation.getRoot();
    const slang::SourceManager* sm = compilation.getSourceManager();
    if (!sm || cancelled())
        return false;

    bool cancellationReached = false;
    auto visitor = makeRelationshipVisitor(allRelationships,
                                           sm,
                                           isCancelled,
                                           &cancellationReached);
    root.visit(visitor);
    if (cancellationReached || cancelled()) {
        clearRelationshipInfo(&allRelationships);
        return false;
    }

    // Stage the complete grouping separately so cancellation never publishes
    // a mixture of relationship facts from different files.
    QHash<QString, RelationshipExtractionInfo> staged = *grouped;
    for (const ModuleInstantiationInfo& item
         : std::as_const(allRelationships.moduleInstantiations)) {
        if (cancelled())
            return false;
        RelationshipExtractionInfo one;
        one.moduleInstantiations.append(item);
        appendRelationshipInfoForFile(staged, item.sourceRange.fileName, one);
    }
    for (const SubroutineCallInfo& item
         : std::as_const(allRelationships.subroutineCalls)) {
        if (cancelled())
            return false;
        RelationshipExtractionInfo one;
        one.subroutineCalls.append(item);
        appendRelationshipInfoForFile(staged, item.sourceRange.fileName, one);
    }
    for (const AssignmentInfo& item
         : std::as_const(allRelationships.assignments)) {
        if (cancelled())
            return false;
        RelationshipExtractionInfo one;
        one.assignments.append(item);
        appendRelationshipInfoForFile(staged, item.sourceRange.fileName, one);
    }
    for (const ConditionReferenceInfo& item
         : std::as_const(allRelationships.conditionReferences)) {
        if (cancelled())
            return false;
        RelationshipExtractionInfo one;
        one.conditionReferences.append(item);
        appendRelationshipInfoForFile(staged, item.sourceRange.fileName, one);
    }
    for (const TimingSignalInfo& item
         : std::as_const(allRelationships.timingSignals)) {
        if (cancelled())
            return false;
        RelationshipExtractionInfo one;
        one.timingSignals.append(item);
        appendRelationshipInfoForFile(staged, item.sourceRange.fileName, one);
    }
    if (cancelled())
        return false;

    *grouped = std::move(staged);
    return true;
}

} // namespace

QHash<QString, RelationshipExtractionInfo> slang_collectors::relationships(
    Compilation& compilation, const std::function<bool()>& cancelled)
{
    QHash<QString, RelationshipExtractionInfo> grouped;
    if (!collectWorkspaceRelationships(compilation, &grouped, cancelled))
        return {};
    return grouped;
}

QHash<QString, RelationshipExtractionInfo> SlangManager::extractWorkspaceRelationshipInfo(
    const QStringList& filePaths, const QStringList& includeDirs,
    const QHash<QString, QString>& defines, std::function<bool()> isCancelled)
{
    SemanticInputCapture input(SemanticAnalysisRequest{});
    auto result = analyzeCapturedWorkspace(input, filePaths, includeDirs, defines, {},
                                           isCancelled, {false, false, true}).relationships;
    for (const auto& file : filePaths) {
        const auto key = SemanticInputCapture::pathKey(file);
        const auto display = normalizedSourceFileName(file);
        if (key != display && result.contains(key))
            result.insert(display, result.take(key));
    }
    return result;
}

QHash<QString, RelationshipExtractionInfo> SlangManager::extractOverlayWorkspaceRelationshipInfo(
    const QHash<QString, QString>& contents, const QStringList& includeDirs,
    const QHash<QString, QString>& defines, std::function<bool()> isCancelled,
    const QStringList& orderedFilePaths)
{
    auto result = analyzeOverlayWorkspace(contents, orderedFilePaths, includeDirs, defines,
                                          isCancelled, {false, false, true}).relationships;
    for (auto it = contents.cbegin(); it != contents.cend(); ++it) {
        const auto key = SemanticInputCapture::pathKey(it.key());
        const auto display = normalizedSourceFileName(it.key());
        if (key != display && result.contains(key))
            result.insert(display, result.take(key));
    }
    return result;
}
