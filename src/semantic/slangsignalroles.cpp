#include "slangsignalroles.h"
#include "slangcompilationcollectors.h"
#include "slangsymbolcollectorhelpers.h"
#include <slang/ast/ASTVisitor.h>
#include <slang/ast/expressions/AssertionExpr.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/EvalContext.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/expressions/OperatorExpressions.h>
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/MiscStatements.h>
#include <slang/ast/symbols/BlockSymbols.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/text/SourceManager.h>
#include <QCryptographicHash>
#include <QSet>
#include <QFileInfo>

QByteArray SemanticSignalRoleEvidence::sourceHash(QString text) {
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    while (text.endsWith(QChar(0))) text.chop(1);
    text.replace("\r\n", "\n");
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
}

namespace {
using namespace slang::ast;
QString path(const Symbol &symbol) { return QString::fromStdString(symbol.getHierarchicalPath()); }
struct Reference { const ValueSymbol *symbol = nullptr; bool inverted = false; };
Reference reference(const Expression &expression, bool scalar = true) {
    const auto &expr = expression.unwrapImplicitConversions();
    if (scalar && (!expr.type->isIntegral() || expr.type->getBitWidth() != 1)) return {};
    if (ValueExpressionBase::isKind(expr.kind))
        return {&expr.as<ValueExpressionBase>().symbol, false};
    if (scalar && expr.kind == ExpressionKind::UnaryOp) {
        const auto &unary = expr.as<UnaryExpression>();
        if (unary.op == UnaryOperator::LogicalNot || unary.op == UnaryOperator::BitwiseNot) {
            auto result = reference(unary.operand()); result.inverted = !result.inverted; return result;
        }
    }
    return {};
}
slang::ConstantValue constant(const Expression &expr, const Symbol &scope) {
    EvalContext context(scope); return expr.eval(context);
}
const Statement &unwrap(const Statement &statement) {
    if (statement.kind == StatementKind::Block) return unwrap(statement.as<BlockStatement>().body);
    if (statement.kind == StatementKind::List && statement.as<StatementList>().list.size() == 1)
        return unwrap(*statement.as<StatementList>().list.front());
    return statement;
}
// A reset branch must deterministically assign complete state objects. Partial
// writes, loops, calls and conditional initialization are deliberately unknown.
bool straightAssignments(const Statement &statement, const Symbol &scope,
                         QSet<QString> &targets, bool constantsOnly, bool &dynamic) {
    const auto &stmt = unwrap(statement);
    if (stmt.kind == StatementKind::List) {
        for (const auto *part : stmt.as<StatementList>().list)
            if (!straightAssignments(*part, scope, targets, constantsOnly, dynamic)) return false;
        return !targets.isEmpty();
    }
    if (stmt.kind != StatementKind::ExpressionStatement) return false;
    const auto &expr = stmt.as<ExpressionStatement>().expr;
    if (expr.kind != ExpressionKind::Assignment) return false;
    const auto &assign = expr.as<AssignmentExpression>();
    const auto lhs = reference(assign.left(), false);
    if (!assign.isNonBlocking() || assign.op || assign.timingControl || !lhs.symbol) return false;
    const auto key = path(*lhs.symbol);
    if (targets.contains(key)) return false;
    targets.insert(key);
    const auto value = constant(assign.right(), scope);
    const bool known = value.isInteger() && !value.integer().hasUnknown();
    dynamic |= !known;
    return !constantsOnly || known;
}
Reference condition(const Expression &expression, const Symbol &scope) {
    auto ref = reference(expression);
    if (ref.symbol) return ref;
    const auto &expr = expression.unwrapImplicitConversions();
    if (expr.kind != ExpressionKind::BinaryOp) return {};
    const auto &binary = expr.as<BinaryExpression>();
    if (binary.op != BinaryOperator::Equality && binary.op != BinaryOperator::Inequality) return {};
    for (int side = 0; side < 2; ++side) {
        ref = reference(side ? binary.right() : binary.left());
        if (!ref.symbol) continue;
        const auto value = constant(side ? binary.left() : binary.right(), scope);
        if (!value.isInteger() || value.integer().hasUnknown()) continue;
        const auto number = value.integer().as<int>();
        if (!number || (*number != 0 && *number != 1)) continue;
        ref.inverted ^= (*number == 0) ^ (binary.op == BinaryOperator::Inequality);
        return ref;
    }
    return {};
}
struct Link { QString target; bool inverted = false; QString description; };
struct Facts { QList<SemanticSignalRoleEvidence> roles; QStringList issues; bool conflict = false; };
QString flippedEdge(QString edge) { return edge == "posedge" ? "negedge" : edge == "negedge" ? "posedge" : edge; }
}

void slang_symbols::populateSignalRoles(slang::ast::Compilation &compilation,
                                        QList<SemanticSymbolRecord> &records,
                                        const std::function<bool()> &cancelled) {
    if ((cancelled && cancelled()) || !compilation.getSourceManager()) return;
    const auto *sm = compilation.getSourceManager();
    QMap<QString, Facts> facts;
    QMap<QString, SemanticElaboratedSymbolInfo> known;
    for (const auto &record : records)
        for (auto it = record.presentation.objectInfoByPath.cbegin(); it != record.presentation.objectInfoByPath.cend(); ++it)
            known.insert(it.key(), it.value());
    if (slang_collectors::hasBlockingErrors(compilation)) {
        for (auto &record : records) for (auto &info : record.presentation.objectInfoByPath)
            info.signalRoleIssues << QStringLiteral("Slang compilation has errors; role inference is unavailable.");
        return;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QMap<QString, QByteArray> sourceHashes;
    for (const auto buffer : sm->getAllBuffers()) {
        const auto text = sm->getSourceText(buffer);
        hash.addData(text.data(), qsizetype(text.size())); hash.addData("\0", 1);
        const auto file = detail::sourceIdentityFileName(sm, slang::SourceLocation(buffer, 0));
        if (QFileInfo(file).isAbsolute())
            sourceHashes[file] = SemanticSignalRoleEvidence::sourceHash(QString::fromUtf8(text.data(), qsizetype(text.size())));
    }
    const auto analysisIdentity = QStringLiteral("slang-timing-v1:") + QString::fromLatin1(hash.result().toHex());
    QMap<QString, Link> links;
    QString currentInstance;
    QMap<QString, QString> parameters;
    auto issue = [&](const Expression &expr, const QString &reason) {
        auto refs = makeVisitor([&](auto &, const NamedValueExpression &value) {
            facts[path(value.symbol)].issues << reason;
        }); expr.visit(refs);
    };
    auto addRole = [&](Reference ref, QString role, QString edge, int level,
                       const ProceduralBlockSymbol &process, const QString &rule) {
        if (!ref.symbol) return;
        SemanticSignalRoleEvidence evidence;
        evidence.role = role; evidence.edge = ref.inverted ? flippedEdge(edge) : edge;
        evidence.activeLevel = level < 0 ? level : level ^ int(ref.inverted);
        evidence.sourceSignal = path(*ref.symbol); evidence.sourceInstance = currentInstance;
        evidence.rule = rule; evidence.parameters = parameters; evidence.analysisIdentity = analysisIdentity;
        evidence.sourceHashes = sourceHashes;
        const auto location = detail::qTextDocumentSourcePosition(sm, process.location);
        evidence.sourceFile = location.fileName; evidence.sourceLine = location.line;
        evidence.mappingChain << evidence.sourceSignal;
        facts[evidence.sourceSignal].roles << evidence;
    };
    auto visitor = makeVisitor(
        [&](auto &v, const GenerateBlockSymbol &block) { if (!block.isUninstantiated) v.visitDefault(block); },
        [&](auto &, const UninstantiatedDefSymbol &instance) {
            auto unresolved = makeVisitor([&](auto &, const NamedValueExpression &value) {
                facts[path(value.symbol)].issues << QStringLiteral("Unresolved/black-box module at %1; no process or port-role evidence is available.").arg(path(instance));
            });
            for (const auto* connection : instance.getPortConnections()) if (connection) connection->visit(unresolved);
        },
        [&](auto &v, const InstanceSymbol &instance) {
            if (cancelled && cancelled()) return;
            const auto previous = currentInstance; const auto previousParameters = parameters;
            currentInstance = path(instance);
            for (const auto *parameter : instance.body.getParameters()) {
                const auto &symbol = parameter->symbol;
                if (symbol.kind == SymbolKind::Parameter)
                    parameters[currentInstance + "." + QString::fromUtf8(symbol.name.data(), qsizetype(symbol.name.size()))]
                        = QString::fromStdString(symbol.as<ParameterSymbol>().getValue().toString());
            }
            for (const auto *symbol : instance.body.getPortList()) {
                if (symbol->kind != SymbolKind::Port) continue;
                const auto &port = symbol->as<PortSymbol>();
                if (port.direction != ArgumentDirection::In || !port.internalSymbol ||
                    !port.getType().isIntegral() || port.getType().getBitWidth() != 1) continue;
                const auto source = path(*port.internalSymbol);
                const auto *connection = instance.getPortConnection(port);
                const auto *expr = connection ? connection->getExpression() : nullptr;
                const auto ref = expr ? reference(*expr) : Reference{};
                if (!ref.symbol) {
                    const auto reason = QStringLiteral("Unconnected or compound input connection at %1; no proven signal mapping.").arg(source);
                    facts[source].issues << reason;
                    if (expr) issue(*expr, reason);
                    continue;
                }
                if (known.value(source).proceduralDriverCount || known.value(source).continuousDriverCount) {
                    facts[source].issues << QStringLiteral("Input also has an internal driver; mapping is uncertain."); continue;
                }
                links[source] = {path(*ref.symbol), ref.inverted, source + (ref.inverted ? " -> !" : " -> ") + path(*ref.symbol)};
            }
            v.visitDefault(instance);
            currentInstance = previous; parameters = previousParameters;
        },
        [&](auto &, const ContinuousAssignSymbol &assignment) {
            const auto &expr = assignment.getAssignment();
            if (expr.kind != ExpressionKind::Assignment) return;
            const auto &assign = expr.as<AssignmentExpression>();
            const auto lhs = reference(assign.left()), rhs = reference(assign.right());
            if (!lhs.symbol) return;
            const auto source = path(*lhs.symbol); const auto info = known.value(source);
            if (rhs.symbol && !lhs.inverted && info.driverCount == 1 && info.continuousDriverCount == 1)
                links[source] = {path(*rhs.symbol), rhs.inverted, source + (rhs.inverted ? " -> !" : " -> ") + path(*rhs.symbol)};
            else {
                const auto reason = QStringLiteral("%1 is derived or multiply driven; role is not propagated through its expression.").arg(source);
                facts[source].issues << reason; issue(assign.right(), reason);
            }
        },
        [&](auto &, const ProceduralBlockSymbol &process) {
            if (cancelled && cancelled()) return;
            if (process.procedureKind != ProceduralBlockKind::AlwaysFF && process.procedureKind != ProceduralBlockKind::Always) return;
            const auto &body = unwrap(process.getBody());
            if (body.kind != StatementKind::Timed) return;
            const auto &timed = body.as<TimedStatement>();
            QList<const SignalEventControl*> events;
            bool valid = true;
            auto event = [&](const TimingControl &control) {
                if (control.kind != TimingControlKind::SignalEvent) { valid = false; return; }
                const auto &signal = control.as<SignalEventControl>();
                if (signal.iffCondition || signal.edge == EdgeKind::None || signal.edge == EdgeKind::BothEdges || !reference(signal.expr).symbol) valid = false;
                events << &signal;
            };
            if (timed.timing.kind == TimingControlKind::EventList)
                for (const auto *control : timed.timing.as<EventListControl>().events) event(*control);
            else event(timed.timing);
            auto unresolved = [&]() {
                for (const auto *ev : events) issue(ev->expr, QStringLiteral("Unsupported or ambiguous edge process at %1; role is undetermined.").arg(currentInstance));
            };
            if (!valid || events.isEmpty() || events.size() > 2) { unresolved(); return; }
            QSet<QString> allState;
            auto stateVisitor = makeVisitor([&](auto &, const AssignmentExpression &assign) {
                if (assign.isNonBlocking()) { auto lhs = reference(assign.left(), false); if (lhs.symbol) allState.insert(path(*lhs.symbol)); }
            }); timed.stmt.visit(stateVisitor);
            if (allState.isEmpty()) { unresolved(); return; }
            Reference reset;
            bool resetPattern = false;
            const auto &operation = unwrap(timed.stmt);
            if (operation.kind == StatementKind::Conditional) {
                const auto &branch = operation.as<ConditionalStatement>();
                if (branch.conditions.size() == 1 && !branch.conditions[0].pattern && branch.ifFalse) {
                    reset = condition(*branch.conditions[0].expr, process);
                    QSet<QString> initState, normalState; bool initDynamic = false, normalDynamic = false;
                    resetPattern = reset.symbol && straightAssignments(branch.ifTrue, process, initState, true, initDynamic)
                        && straightAssignments(*branch.ifFalse, process, normalState, false, normalDynamic)
                        && normalDynamic && initState == normalState && initState == allState;
                    if (reset.symbol && !resetPattern)
                        facts[path(*reset.symbol)].issues << QStringLiteral("Control branch is not a complete state initialization; Reset is undetermined.");
                }
            }
            int resetEvent = -1;
            if (events.size() == 2) {
                if (resetPattern) for (int i=0; i<events.size(); ++i) {
                    const auto ref = reference(events[i]->expr);
                    const int active = 1 ^ int(reset.inverted);
                    const int edgeLevel = (events[i]->edge == EdgeKind::PosEdge ? 1 : 0) ^ int(ref.inverted);
                    if (ref.symbol == reset.symbol && edgeLevel == active) resetEvent = i;
                }
                if (resetEvent < 0 || reference(events[0]->expr).symbol == reference(events[1]->expr).symbol) { unresolved(); return; }
            }
            const int clockEvent = resetEvent == 0 ? 1 : 0;
            const auto *clock = events[clockEvent];
            addRole(reference(clock->expr), "Clock", clock->edge == EdgeKind::PosEdge ? "posedge" : "negedge", -1,
                    process, "edge-triggered nonblocking state update");
            if (resetEvent >= 0)
                addRole(reset, "Reset", "posedge", 1,
                        process, "asynchronous priority full-state initialization");
            else if (resetPattern && allState.size() >= 2 && reset.symbol != reference(clock->expr).symbol)
                addRole(reset, "Reset", {}, 1, process, "synchronous priority full-state initialization (multiple state objects)");
            else if (reset.symbol)
                facts[path(*reset.symbol)].issues << QStringLiteral("Single-state initialization or ordinary control is ambiguous; Reset is undetermined.");
        });
    for (const auto *top : compilation.getRootNoFinalize().topInstances) if (top) top->visit(visitor);
    if (cancelled && cancelled()) return;
    // Propagate each source proof along one proven connection chain. A cycle or
    // unsupported expression stops the proof; it never fans out to all operands.
    const auto seeds = facts;
    for (auto it = seeds.cbegin(); it != seeds.cend(); ++it) for (auto evidence : it->roles) {
        QSet<QString> visited{it.key()}; auto source = it.key();
        while (links.contains(source)) {
            const auto link = links.value(source);
            if (visited.contains(link.target)) { facts[source].issues << "Cyclic signal mapping."; break; }
            visited.insert(link.target); evidence.mappingChain << link.description;
            if (link.inverted) { evidence.edge = flippedEdge(evidence.edge); if (evidence.activeLevel >= 0) evidence.activeLevel ^= 1; }
            facts[link.target].roles << evidence; source = link.target;
        }
    }
    for (auto &fact : facts) {
        QSet<QString> roles, edges; QSet<int> levels;
        for (const auto &evidence : fact.roles) {
            roles.insert(evidence.role);
            if (evidence.role == "Reset" && evidence.activeLevel >= 0) levels.insert(evidence.activeLevel);
            if (evidence.role == "Clock" && !evidence.edge.isEmpty()) edges.insert(evidence.edge);
        }
        fact.conflict = roles.size() > 1 || levels.size() > 1 || edges.size() > 1;
        if (fact.conflict) fact.issues << "Conflicting role, edge or reset polarity evidence.";
        fact.issues.removeDuplicates();
    }
    for (auto &record : records) {
        for (auto it = record.presentation.objectInfoByPath.begin(); it != record.presentation.objectInfoByPath.end(); ++it) {
            const auto fact = facts.value(it.key());
            it->signalRoles = fact.roles; it->signalRoleIssues = fact.issues; it->signalRoleConflict = fact.conflict;
        }
        for (auto it = record.presentation.instanceInfoByPath.begin(); it != record.presentation.instanceInfoByPath.end(); ++it) {
            const auto object = record.presentation.objectInfoByPath.constFind(it.key() + "." + record.name);
            if (object != record.presentation.objectInfoByPath.cend()) {
                it->signalRoles = object->signalRoles; it->signalRoleIssues = object->signalRoleIssues; it->signalRoleConflict = object->signalRoleConflict;
            }
        }
    }
}
