#include "slangmanager.h"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/CallExpression.h>
#include <slang/ast/expressions/ConversionExpression.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/expressions/SelectExpressions.h>
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/LoopStatements.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

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
    RelationshipExtractionInfo result;
    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        auto tree = slang::syntax::SyntaxTree::fromText(
            std::string_view(src),
            std::string_view(nameStr),
            std::string_view{});

        if (!tree)
            return result;

        slang::Bag bag;
        auto& opts = bag.insertOrGet<CompilationOptions>();
        opts.flags |= CompilationFlags::IgnoreUnknownModules;

        Compilation compilation(bag);
        compilation.addSyntaxTree(tree);
        const RootSymbol& root = compilation.getRoot();
        const slang::SourceManager* sm = compilation.getSourceManager();
        if (!sm)
            return result;

        auto visitor = makeVisitor(
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

        root.visit(visitor);
    } catch (const std::exception&) {
        result = RelationshipExtractionInfo();
    } catch (...) {
        result = RelationshipExtractionInfo();
    }
    return result;
}

QVector<ModuleInstantiationInfo> SlangManager::extractModuleInstantiations(const QString& fileName,
                                                                           const QString& content)
{
    QVector<ModuleInstantiationInfo> result;
    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        auto tree = slang::syntax::SyntaxTree::fromText(
            std::string_view(src),
            std::string_view(nameStr),
            std::string_view{});

        if (!tree)
            return result;

        slang::Bag bag;
        auto& opts = bag.insertOrGet<CompilationOptions>();
        opts.flags |= CompilationFlags::IgnoreUnknownModules;

        Compilation compilation(bag);
        compilation.addSyntaxTree(tree);
        const RootSymbol& root = compilation.getRoot();
        const slang::SourceManager* sm = compilation.getSourceManager();
        if (!sm)
            return result;

        auto visitor = makeVisitor(
            [&](auto& v, const InstanceSymbol& inst) {
                ModuleInstantiationInfo info;
                info.instanceName = QString::fromStdString(std::string(inst.name));
                info.moduleName = QString::fromStdString(std::string(inst.getDefinition().name));
                size_t line = sm->getLineNumber(inst.location);
                info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);
                result.append(info);
                v.visitDefault(inst);
            });

        root.visit(visitor);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}

QVector<SubroutineCallInfo> SlangManager::extractSubroutineCalls(const QString& fileName,
                                                                 const QString& content)
{
    QVector<SubroutineCallInfo> result;
    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        auto tree = slang::syntax::SyntaxTree::fromText(
            std::string_view(src),
            std::string_view(nameStr),
            std::string_view{});

        if (!tree)
            return result;

        slang::Bag bag;
        auto& opts = bag.insertOrGet<CompilationOptions>();
        opts.flags |= CompilationFlags::IgnoreUnknownModules;

        Compilation compilation(bag);
        compilation.addSyntaxTree(tree);
        const RootSymbol& root = compilation.getRoot();
        const slang::SourceManager* sm = compilation.getSourceManager();
        if (!sm)
            return result;

        auto visitor = makeVisitor(
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

        root.visit(visitor);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}

QVector<AssignmentInfo> SlangManager::extractAssignments(const QString& fileName,
                                                         const QString& content)
{
    QVector<AssignmentInfo> result;
    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        auto tree = slang::syntax::SyntaxTree::fromText(
            std::string_view(src),
            std::string_view(nameStr),
            std::string_view{});

        if (!tree)
            return result;

        slang::Bag bag;
        auto& opts = bag.insertOrGet<CompilationOptions>();
        opts.flags |= CompilationFlags::IgnoreUnknownModules;

        Compilation compilation(bag);
        compilation.addSyntaxTree(tree);
        const RootSymbol& root = compilation.getRoot();
        const slang::SourceManager* sm = compilation.getSourceManager();
        if (!sm)
            return result;

        auto visitor = makeVisitor(
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

        root.visit(visitor);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}

QVector<ConditionReferenceInfo> SlangManager::extractConditionReferences(const QString& fileName,
                                                                         const QString& content)
{
    QVector<ConditionReferenceInfo> result;
    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        auto tree = slang::syntax::SyntaxTree::fromText(
            std::string_view(src),
            std::string_view(nameStr),
            std::string_view{});

        if (!tree)
            return result;

        slang::Bag bag;
        auto& opts = bag.insertOrGet<CompilationOptions>();
        opts.flags |= CompilationFlags::IgnoreUnknownModules;

        Compilation compilation(bag);
        compilation.addSyntaxTree(tree);
        const RootSymbol& root = compilation.getRoot();
        const slang::SourceManager* sm = compilation.getSourceManager();
        if (!sm)
            return result;

        auto visitor = makeVisitor(
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

        root.visit(visitor);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}

QVector<TimingSignalInfo> SlangManager::extractTimingSignals(const QString& fileName,
                                                             const QString& content)
{
    QVector<TimingSignalInfo> result;
    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        auto tree = slang::syntax::SyntaxTree::fromText(
            std::string_view(src),
            std::string_view(nameStr),
            std::string_view{});

        if (!tree)
            return result;

        slang::Bag bag;
        auto& opts = bag.insertOrGet<CompilationOptions>();
        opts.flags |= CompilationFlags::IgnoreUnknownModules;

        Compilation compilation(bag);
        compilation.addSyntaxTree(tree);
        const RootSymbol& root = compilation.getRoot();
        const slang::SourceManager* sm = compilation.getSourceManager();
        if (!sm)
            return result;

        auto visitor = makeVisitor(
            [&](auto& v, const SignalEventControl& event) {
                appendTimingSignal(result, sm, event);
                v.visitDefault(event);
            });

        root.visit(visitor);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}
