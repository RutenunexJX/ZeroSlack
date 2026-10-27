#include "slangsignaldependencies.h"
#include "slangrelationshiphelpers.h"
#include "slangsymbolcollectorhelpers.h"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/EvalContext.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/CallExpression.h>
#include <slang/ast/expressions/ConversionExpression.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/expressions/OperatorExpressions.h>
#include <slang/ast/expressions/SelectExpressions.h>
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/MiscStatements.h>
#include <slang/ast/symbols/BlockSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <QCryptographicHash>
#include <QMap>
#include <QSet>
#include <algorithm>

using namespace slang::ast;
using namespace slang_relationship::detail;

namespace {
QString text(std::string_view value) { return QString::fromUtf8(value.data(), qsizetype(value.size())); }
QString instanceOf(const Symbol& symbol) {
    for (auto* scope = symbol.getParentScope(); scope; scope = scope->asSymbol().getParentScope()) {
        if (const auto* body = scope->asSymbol().as_if<InstanceBodySymbol>())
            return body->parentInstance ? QString::fromStdString(body->parentInstance->getHierarchicalPath()) : QString();
    }
    return {};
}
struct Access {
    const ValueSymbol* symbol = nullptr;
    SemanticValueReference binding;
    QString instance, object;
    SignalBitRange bits;
    bool dynamic = false;
    QString key() const { return object + QLatin1Char('|') + bits.key(); }
};
struct Origin {
    Access access;
    SignalDependencyKind kind = SignalDependencyKind::Data;
    QList<SemanticSourceRange> trail;
    bool aligned = true;
};
struct WrittenValue {
    Access target;
    QList<Origin> origins;
    SemanticSourceRange evidence;
    bool definite = false;
    QStringList limitations;
    bool direct = false;
};
using Flow = QMap<QString, WrittenValue>;

class Collector {
public:
    const slang::SourceManager* sm;
    std::function<bool()> cancelled;
    QVector<SignalDependencyInfo> facts;
    int steps = 0;
    QStringList processIssues;

    Access access(const ValueSymbol& symbol) const {
        Access result;
        result.symbol = &symbol;
        result.binding.name = text(symbol.name);
        result.binding.accessPath = result.binding.name;
        result.binding.bindingCaptured = true;
        SemanticSymbolRecord record;
        if (slang_symbols::detail::fillSymbolRecord(sm, symbol, record, nullptr)) {
            result.binding.location = record.location;
            result.binding.declaringScope = record.owner.stableKey;
        }
        result.object = QString::fromStdString(symbol.getHierarchicalPath());
        result.instance = instanceOf(symbol);
        const auto& type = symbol.getType();
        if (type.isIntegral() && type.getBitWidth() > 0)
            result.bits = {true, 0, qint64(type.getBitWidth()) - 1};
        return result;
    }
    std::optional<qint64> integer(const Expression& expr, const ValueSymbol* root) const {
        if (!root) return {};
        EvalContext context(*root);
        const auto value = expr.eval(context);
        return value.isInteger() ? value.integer().as<qint64>() : std::nullopt;
    }
    Access access(const Expression& expr) const {
        if (const auto* conversion = expr.as_if<ConversionExpression>()) return access(conversion->operand());
        if (const auto* value = expr.as_if<ValueExpressionBase>()) return access(value->symbol);
        if (const auto* member = expr.as_if<MemberAccessExpression>()) {
            auto value = access(member->value());
            value.binding.accessPath += QLatin1Char('.') + text(member->member.name);
            const auto* field = member->member.as_if<FieldSymbol>();
            if (field && member->value().type->isIntegral() && value.bits.known) {
                value.bits.low += qint64(field->bitOffset);
                value.bits.high = value.bits.low + qint64(member->type->getBitWidth()) - 1;
            } else value.bits.known = false;
            return value;
        }
        if (const auto* select = expr.as_if<ElementSelectExpression>()) {
            auto value = access(select->value());
            const auto index = integer(select->selector(), value.symbol);
            if (index && select->value().type->isIntegral() && value.bits.known) {
                const auto range = select->value().type->getFixedRange();
                if (*index >= range.lower() && *index <= range.upper()) {
                    const qint64 width = select->type->getBitWidth();
                    value.bits.low += qint64(range.translateIndex(int(*index))) * width;
                    value.bits.high = value.bits.low + width - 1;
                    value.binding.accessPath += QStringLiteral("[%1]").arg(*index);
                    return value;
                }
            }
            value.binding.accessPath += QStringLiteral("[?]");
            value.bits.known = false; value.dynamic = true;
            return value;
        }
        if (const auto* select = expr.as_if<RangeSelectExpression>()) {
            auto value = access(select->value());
            auto left = integer(select->left(), value.symbol), right = integer(select->right(), value.symbol);
            if (left && right && select->value().type->isIntegral() && value.bits.known) {
                if (select->getSelectionKind() == RangeSelectionKind::IndexedUp) right = *left + *right - 1;
                else if (select->getSelectionKind() == RangeSelectionKind::IndexedDown) right = *left - *right + 1;
                const auto range = select->value().type->getFixedRange();
                if (qMin(*left,*right) >= range.lower() && qMax(*left,*right) <= range.upper()) {
                    const auto low = qMin(range.translateIndex(int(*left)), range.translateIndex(int(*right)));
                    const auto high = qMax(range.translateIndex(int(*left)), range.translateIndex(int(*right)));
                    value.bits = {true, value.bits.low + low, value.bits.low + high};
                    value.binding.accessPath += QStringLiteral("[%1:%2]").arg(*left).arg(*right);
                    return value;
                }
            }
            value.binding.accessPath += QStringLiteral("[?:?]");
            value.bits.known = false; value.dynamic = true;
            return value;
        }
        return {};
    }
    void append(QList<Origin>& list, Origin origin) const {
        if (!origin.access.symbol || !origin.access.binding.isValid()) return;
        const auto found = std::find_if(list.cbegin(), list.cend(), [&](const Origin& other) {
            return other.kind == origin.kind && other.access.key() == origin.access.key();
        });
        if (found == list.cend()) list.append(std::move(origin));
        else {
            auto& existing=list[std::distance(list.cbegin(),found)];
            existing.trail.append(origin.trail);existing.aligned &= origin.aligned;
        }
    }
    QList<Origin> read(const Access& value, const Flow& flow, SignalDependencyKind kind) {
        QList<Origin> result;
        if (!value.symbol) return result;
        const auto found = flow.constFind(value.key());
        if (found != flow.cend()) {
            if (found->limitations.contains(QStringLiteral("Nonblocking assignment."))) {
                append(result,{value,kind});
                return result;
            }
            for (auto origin : found->origins) {
                origin.aligned &= found->direct;
                if (kind == SignalDependencyKind::Control) origin.kind = kind;
                origin.trail.append(found->evidence);
                append(result, origin);
            }
            processIssues += found->limitations;
            return result;
        }
        for (const auto& other : flow) {
            if (other.target.object == value.object && other.target.bits.overlaps(value.bits))
                processIssues << QStringLiteral("Partial procedural range substitution is unresolved.");
        }
        append(result, {value,kind});
        return result;
    }
    QList<Origin> reads(const Expression& expr, const Flow& flow,
                        SignalDependencyKind kind = SignalDependencyKind::Data) {
        QList<Origin> result;
        auto consume = [&](auto& visitor, const auto& expression) {
            const auto value = access(expression);
            if (!value.symbol) { visitor.visitDefault(expression); return; }
            if (value.symbol->kind == SymbolKind::Parameter || value.symbol->kind == SymbolKind::EnumValue) return;
            for (const auto& origin : read(value, flow, kind)) append(result, origin);
            if (value.dynamic) processIssues << QStringLiteral("Dynamic or unsupported selection bounds.");
            // Index expressions influence which value is read, rather than becoming another data slice.
            if constexpr (std::is_same_v<std::decay_t<decltype(expression)>, ElementSelectExpression>) {
                for (const auto& origin : reads(expression.selector(), flow, SignalDependencyKind::Control)) append(result, origin);
            }
        };
        auto visitor = makeVisitor(
            [&](auto& v, const NamedValueExpression& value) { consume(v,value); },
            [&](auto& v, const HierarchicalValueExpression& value) { consume(v,value); },
            [&](auto& v, const MemberAccessExpression& value) { consume(v,value); },
            [&](auto& v, const ElementSelectExpression& value) { consume(v,value); },
            [&](auto& v, const RangeSelectExpression& value) { consume(v,value); },
            [&](auto&, const ConditionalExpression& value) {
                for (const auto& condition : value.conditions)
                    for (const auto& origin : reads(*condition.expr,flow,SignalDependencyKind::Control)) append(result,origin);
                for (const auto& origin : reads(value.left(),flow,kind)) append(result,origin);
                for (const auto& origin : reads(value.right(),flow,kind)) append(result,origin);
            },
            [&](auto& v, const CallExpression& call) {
                processIssues << QStringLiteral("Subroutine effects are outside the supported dependency scope.");
                v.visitDefault(call);
            });
        expr.visit(visitor);
        if(!bitwiseExpression(expr))for(auto& origin:result)origin.aligned=false;
        if (result.size() > 512) {
            result = result.mid(0,512);
            processIssues << QStringLiteral("Expression dependency limit reached (512).");
        }
        return result;
    }
    bool bitwiseExpression(const Expression& expr) const {
        if(access(expr).symbol)return true;
        if(const auto* unary=expr.as_if<UnaryExpression>())
            return (unary->op==UnaryOperator::BitwiseNot || unary->op==UnaryOperator::Plus)
                && bitwiseExpression(unary->operand());
        if(const auto* binary=expr.as_if<BinaryExpression>())
            return OpInfo::isBitwise(binary->op) && bitwiseExpression(binary->left()) && bitwiseExpression(binary->right());
        if(const auto* conversion=expr.as_if<ConversionExpression>())
            return conversion->type->getBitWidth()==conversion->operand().type->getBitWidth() && bitwiseExpression(conversion->operand());
        return expr.kind==ExpressionKind::IntegerLiteral || expr.kind==ExpressionKind::UnbasedUnsizedIntegerLiteral;
    }
    void recordReferences(const Expression& expr, SignalTraceRole role) {
        auto consume = [&](auto& v,const auto& expression) {
            const auto value=access(expression);
            if (!value.symbol) { v.visitDefault(expression); return; }
            if (value.symbol->kind==SymbolKind::Parameter || value.symbol->kind==SymbolKind::EnumValue) return;
            const auto range=relationshipEvidenceRange(sm,expression.sourceRange);
            WrittenValue target{value,{},range,true,{}};
            const auto before=facts.size();
            emitFact(value,target,SignalDependencyKind::Reference,SignalStorageBoundary::Unknown,
                {},QStringLiteral("Reference"),{},{});
            if (facts.size()>before) facts.last().dependency.referenceRole=role;
            auto selectors=makeVisitor(
                [&](auto&,const ElementSelectExpression& select) { recordReferences(select.selector(),SignalTraceRole::Control); },
                [&](auto&,const RangeSelectExpression& select) {
                    recordReferences(select.left(),SignalTraceRole::Control);
                    recordReferences(select.right(),SignalTraceRole::Control);
                });
            expression.visit(selectors);
        };
        auto visitor=makeVisitor(
            [&](auto&,const AssignmentExpression& assignment) {
                recordReferences(assignment.left(),role==SignalTraceRole::Port ? role : SignalTraceRole::Write);
                recordReferences(assignment.right(),role==SignalTraceRole::Port ? role : SignalTraceRole::Read);
                if (assignment.isCompound()) recordReferences(assignment.left(),SignalTraceRole::Read);
            },
            [&](auto& v,const NamedValueExpression& value) { consume(v,value); },
            [&](auto& v,const HierarchicalValueExpression& value) { consume(v,value); },
            [&](auto& v,const MemberAccessExpression& value) { consume(v,value); },
            [&](auto& v,const ElementSelectExpression& value) { consume(v,value); },
            [&](auto& v,const RangeSelectExpression& value) { consume(v,value); },
            [&](auto&,const ConditionalExpression& value) {
                for (const auto& condition:value.conditions) recordReferences(*condition.expr,SignalTraceRole::Control);
                recordReferences(value.left(),role); recordReferences(value.right(),role);
            });
        expr.visit(visitor);
    }
    void recordReferences(const Statement& stmt) {
        auto visitor=makeVisitor(
            [&](auto&,const Expression& expr) { recordReferences(expr,SignalTraceRole::Read); },
            [&](auto& v,const ConditionalStatement& conditional) {
                for (const auto& item:conditional.conditions) recordReferences(*item.expr,SignalTraceRole::Control);
                conditional.ifTrue.visit(v);
                if (conditional.ifFalse) conditional.ifFalse->visit(v);
            },
            [&](auto& v,const CaseStatement& cases) {
                recordReferences(cases.expr,SignalTraceRole::Control);
                for (const auto& item:cases.items) {
                    for (const auto* expr:item.expressions) recordReferences(*expr,SignalTraceRole::Control);
                    item.stmt->visit(v);
                }
                if (cases.defaultCase) cases.defaultCase->visit(v);
            },
            [&](auto&,const SignalEventControl& event) { recordReferences(event.expr,SignalTraceRole::Clock); });
        stmt.visit(visitor);
    }
    void assignment(const AssignmentExpression& expr, Flow& flow, const QList<Origin>& controls) {
        const auto target = access(expr.left());
        if (!target.symbol) {
            processIssues << QStringLiteral("Composite assignment target is unsupported.");
            return;
        }
        auto origins = reads(expr.right(), flow);
        if (expr.isCompound()) for (const auto& origin : read(target,flow,SignalDependencyKind::Data)) append(origins,origin);
        for (const auto& origin : controls) append(origins,origin);
        QStringList limitations;
        if (target.dynamic) limitations << QStringLiteral("Dynamic assignment target.");
        if (expr.timingControl) limitations << QStringLiteral("Intra-assignment timing control.");
        // Deferred assignments read the pre-update value. A later read must not use this substitution.
        if (expr.isNonBlocking()) limitations << QStringLiteral("Nonblocking assignment.");
        const auto previous=flow.constFind(target.key());
        if (previous!=flow.cend() && previous->limitations.contains(QStringLiteral("Nonblocking assignment.")) != expr.isNonBlocking())
            processIssues << QStringLiteral("Mixed blocking and nonblocking updates to one object.");
        // A later slice write replaces precisely that part of an earlier write.
        QList<WrittenValue> remaining;
        for(auto it=flow.begin();it!=flow.end();){
            const auto prior=it.value();
            if(prior.target.object!=target.object || !prior.target.bits.overlaps(target.bits) || it.key()==target.key()){++it;continue;}
            if(!target.bits.known || !prior.target.bits.known){
                processIssues<<QStringLiteral("Overlapping procedural updates have unresolved bounds.");++it;continue;
            }
            it=flow.erase(it);
            for(const auto range:{SignalBitRange{true,prior.target.bits.low,qMin(prior.target.bits.high,target.bits.low-1)},
                                  SignalBitRange{true,qMax(prior.target.bits.low,target.bits.high+1),prior.target.bits.high}}){
                if(range.low>range.high)continue;
                auto part=prior;part.target.bits=range;
                for(auto& origin:part.origins){
                    if(origin.kind!=SignalDependencyKind::Data)continue;
                    if(prior.direct && origin.aligned && origin.access.bits.known
                        && origin.access.bits.high-origin.access.bits.low==prior.target.bits.high-prior.target.bits.low){
                        const auto offset=origin.access.bits.low-prior.target.bits.low;
                        origin.access.bits={true,range.low+offset,range.high+offset};
                    }else{
                        part.direct=false;
                        part.limitations<<QStringLiteral("Partial overwrite of an expression without exact bit mapping.");
                    }
                }
                remaining.append(part);
            }
        }
        for(const auto& part:remaining)flow.insert(part.target.key(),part);
        const bool aligned=std::all_of(origins.cbegin(),origins.cend(),[](const Origin& origin){return origin.kind!=SignalDependencyKind::Data || origin.aligned;});
        flow.insert(target.key(), {target,origins,relationshipEvidenceRange(sm,expr.sourceRange),true,limitations,
            bitwiseExpression(expr.right()) && aligned && !expr.isCompound()});
    }
    void statement(const Statement& stmt, Flow& flow, const QList<Origin>& controls = {}) {
        if ((cancelled && cancelled()) || ++steps > 4096) {
            processIssues << QStringLiteral("Process analysis cancelled or statement limit reached (4096).");
            return;
        }
        if (const auto* list = stmt.as_if<StatementList>()) {
            for (const auto* child : list->list) statement(*child,flow,controls);
        } else if (const auto* block = stmt.as_if<BlockStatement>()) {
            if (block->blockKind != StatementBlockKind::Sequential)
                processIssues << QStringLiteral("Parallel process block.");
            statement(block->body,flow,controls);
        } else if (const auto* expr = stmt.as_if<ExpressionStatement>()) {
            if (const auto* assign = expr->expr.as_if<AssignmentExpression>()) assignment(*assign,flow,controls);
            else processIssues << QStringLiteral("Statement effects are unresolved.");
        } else if (const auto* condition = stmt.as_if<ConditionalStatement>()) {
            auto branchesControls = controls;
            for (const auto& item : condition->conditions)
                for (const auto& origin : reads(*item.expr,flow,SignalDependencyKind::Control)) append(branchesControls,origin);
            auto yes=flow, no=flow;
            statement(condition->ifTrue,yes,branchesControls);
            if (condition->ifFalse) statement(*condition->ifFalse,no,branchesControls);
            merge(flow,{yes,no});
        } else if (const auto* cases = stmt.as_if<CaseStatement>()) {
            auto branchControls=controls;
            for (const auto& origin : reads(cases->expr,flow,SignalDependencyKind::Control)) append(branchControls,origin);
            QList<Flow> branches;
            for (const auto& item : cases->items) {
                auto branch=flow;
                statement(*item.stmt,branch,branchControls); branches.append(branch);
            }
            auto fallback=flow;
            if (cases->defaultCase) statement(*cases->defaultCase,fallback,branchControls);
            branches.append(fallback); merge(flow,branches);
        } else if (stmt.kind != StatementKind::Empty) {
            processIssues << QStringLiteral("Unsupported statement/process flow; dependencies are conservative.");
            auto visitor=makeVisitor([&](auto&,const AssignmentExpression& expr) { assignment(expr,flow,controls); });
            stmt.visit(visitor);
        }
    }
    void merge(Flow& base, const QList<Flow>& branches) {
        QSet<QString> keys;
        for (const auto& branch : branches) for (auto it=branch.cbegin();it!=branch.cend();++it) keys.insert(it.key());
        Flow merged;
        for (const auto& key : keys) {
            WrittenValue value; value.definite=true; value.direct=true;
            for (const auto& branch : branches) {
                const auto found=branch.constFind(key);
                if (found==branch.cend()) { value.definite=false; continue; }
                value.target=found->target; value.evidence=found->evidence;
                value.definite &= found->definite; value.limitations += found->limitations;
                value.direct &= found->direct;
                for (const auto& origin : found->origins) append(value.origins,origin);
            }
            if (!value.definite) append(value.origins,{value.target,SignalDependencyKind::Data});
            merged.insert(key,value);
        }
        base=merged;
    }
    void emitFact(const Access& from,const WrittenValue& to,SignalDependencyKind kind,
                  SignalStorageBoundary boundary,const QString& process,const QString& processKind,
                  SemanticSourceRange boundaryRange,QStringList limitations) {
        if (!from.binding.isValid() || !to.target.binding.isValid()) return;
        limitations += to.limitations;
        limitations.removeAll(QStringLiteral("Nonblocking assignment."));
        limitations.removeDuplicates();
        auto& fact=facts.emplaceBack();
        fact.from=from.binding; fact.to=to.target.binding; fact.evidence=to.evidence;
        fact.dependency={kind,boundary,from.instance,to.target.instance,from.object,to.target.object,
                         from.bits,to.target.bits,process,processKind,boundaryRange,limitations,limitations.isEmpty()};
        fact.dependency.bitwiseForward=to.direct && from.bits.known && to.target.bits.known
            && from.bits.high-from.bits.low==to.target.bits.high-to.target.bits.low;
        if (!limitations.isEmpty()) fact.dependency.boundary=SignalStorageBoundary::Unknown;
    }
    void publish(const Flow& flow,SignalStorageBoundary boundary,const QString& process,
                 const QString& kind,const SemanticSourceRange& boundaryRange) {
        for (const auto& value : flow) {
            auto actualBoundary=boundary;
            if (!value.definite && boundary==SignalStorageBoundary::Combinational) actualBoundary=SignalStorageBoundary::Latch;
            auto issues=processIssues;
            if (boundary!=SignalStorageBoundary::EdgeTriggered && value.limitations.contains(QStringLiteral("Nonblocking assignment.")))
                issues << QStringLiteral("Nonblocking assignment without a proven edge-triggered boundary.");
            emitFact(value.target,value,SignalDependencyKind::Coverage,actualBoundary,process,kind,boundaryRange,issues);
            for (const auto& origin : value.origins) {
                const auto before=facts.size();
                emitFact(origin.access,value,origin.kind,actualBoundary,process,kind,boundaryRange,issues);
                if (facts.size()>before) {
                    auto& dependency=facts.last().dependency;
                    dependency.sourceTrail=origin.trail;
                    dependency.bitwiseForward=value.direct && origin.kind==SignalDependencyKind::Data
                        && origin.access.bits.known && value.target.bits.known
                        && origin.access.bits.high-origin.access.bits.low == value.target.bits.high-value.target.bits.low;
                }
            }
        }
    }
};
}

QVector<SignalDependencyInfo> collectSlangSignalDependencies(
    Compilation& compilation,const std::function<bool()>& cancelled) {
    Collector c{compilation.getSourceManager(),cancelled};
    auto processKey=[&](const Symbol& symbol) {
        const auto range=relationshipEvidenceRange(c.sm,{symbol.location,symbol.location});
        const auto scope=symbol.kind==SymbolKind::Instance
            ? QString::fromStdString(symbol.getHierarchicalPath()) : instanceOf(symbol);
        return scope+QStringLiteral("|%1:%2:%3").arg(range.fileName).arg(range.line).arg(range.column);
    };
    auto visitor=makeVisitor(
        [&](auto& v,const GenerateBlockSymbol& block) { if (!block.isUninstantiated && !(cancelled && cancelled())) v.visitDefault(block); },
        [&](auto&,const ValueSymbol& symbol) {
            if (symbol.kind==SymbolKind::Parameter || symbol.kind==SymbolKind::EnumValue) return;
            const auto value=c.access(symbol);
            const auto range=relationshipEvidenceRange(c.sm,{symbol.location,symbol.location});
            c.emitFact(value,{value,{},range,true,{}},SignalDependencyKind::Identity,
                SignalStorageBoundary::Unknown,{},QStringLiteral("Declaration"),{},{});
        },
        [&](auto&,const UninstantiatedDefSymbol& instance) {
            auto unknown=makeVisitor([&](auto&,const Expression& expr) {
                c.recordReferences(expr,SignalTraceRole::Unknown);
                c.processIssues.clear();
                for (const auto& origin:c.reads(expr,{})) {
                    const auto range=relationshipEvidenceRange(c.sm,expr.sourceRange);
                    c.emitFact(origin.access,{origin.access,{},range,true,{}},SignalDependencyKind::Coverage,
                        SignalStorageBoundary::Unknown,processKey(instance),QStringLiteral("Unresolved instance"),range,
                        {QStringLiteral("Unresolved instance or black box; port direction is unknown.")});
                }
            });
            for (const auto* expr:instance.getPortConnections()) expr->visit(unknown);
        },
        [&](auto&,const NetAliasSymbol& alias) {
            for(const auto* expr:alias.getNetReferences()){
                c.recordReferences(*expr,SignalTraceRole::Unknown);c.processIssues.clear();
                for(const auto& origin:c.reads(*expr,{})){
                    const auto range=relationshipEvidenceRange(c.sm,expr->sourceRange);
                    c.emitFact(origin.access,{origin.access,{},range,true,{}},SignalDependencyKind::Coverage,
                        SignalStorageBoundary::Unknown,processKey(alias),QStringLiteral("Net alias"),range,
                        {QStringLiteral("Net alias equivalence is not canonicalized; no directional edge is inferred.")});
                }
            }
        },
        [&](auto& v,const InstanceSymbol& instance) {
            if (cancelled && cancelled()) return;
            if (instance.instanceDepth>0) for (const auto* symbol : instance.body.getPortList()) {
                const auto* port=symbol->as_if<PortSymbol>();
                if (!port || !port->internalSymbol || !ValueSymbol::isKind(port->internalSymbol->kind)) continue;
                const auto* connection=instance.getPortConnection(*port);
                const auto* expression=connection ? connection->getExpression() : nullptr;
                if (!expression) continue;
                const auto internal=c.access(port->internalSymbol->as<ValueSymbol>());
                c.processIssues.clear();
                const auto evidence=relationshipEvidenceRange(c.sm,expression->sourceRange);
                c.recordReferences(*expression,SignalTraceRole::Port);
                if (port->direction==ArgumentDirection::In) {
                    auto origins=c.reads(*expression,{});
                    WrittenValue target{internal,origins,evidence,true,{},c.access(*expression).symbol!=nullptr};
                    for (const auto& origin:origins) c.emitFact(origin.access,target,SignalDependencyKind::Port,
                        SignalStorageBoundary::Combinational,processKey(instance),QStringLiteral("Input port"),evidence,c.processIssues);
                } else if(port->direction==ArgumentDirection::Out) {
                    const Expression* destination=expression;
                    if (const auto* assignment=expression->as_if<AssignmentExpression>()) destination=&assignment->left();
                    const auto external=c.access(*destination);
                    WrittenValue target{external,{},evidence,true,{},true};
                    c.emitFact(internal,target,SignalDependencyKind::Port,SignalStorageBoundary::Combinational,
                        processKey(instance),QStringLiteral("Output port"),evidence,c.processIssues);
                } else {
                    auto endpoints=c.reads(*expression,{});
                    endpoints.append({internal,SignalDependencyKind::Data});
                    for(const auto& endpoint:endpoints)c.emitFact(endpoint.access,{endpoint.access,{},evidence,true,{}},
                        SignalDependencyKind::Coverage,SignalStorageBoundary::Unknown,processKey(instance),QStringLiteral("Bidirectional/ref port"),evidence,
                        {QStringLiteral("Bidirectional/ref port connectivity is unresolved; no directional edge is inferred.")});
                }
            }
            v.visitDefault(instance);
        },
        [&](auto&,const ContinuousAssignSymbol& symbol) {
            if (cancelled && cancelled()) return;
            c.processIssues.clear(); c.steps=0;
            Flow flow;
            c.recordReferences(symbol.getAssignment(),SignalTraceRole::Read);
            if (const auto* assignment=symbol.getAssignment().as_if<AssignmentExpression>()) c.assignment(*assignment,flow,{});
            c.publish(flow,SignalStorageBoundary::Combinational,processKey(symbol),QStringLiteral("Continuous assignment"),
                relationshipEvidenceRange(c.sm,symbol.getAssignment().sourceRange));
        },
        [&](auto&,const ProceduralBlockSymbol& symbol) {
            if (cancelled && cancelled()) return;
            c.processIssues.clear(); c.steps=0;
            const Statement* body=&symbol.getBody();
            c.recordReferences(*body);
            auto boundary=SignalStorageBoundary::Unknown;
            if (symbol.procedureKind==ProceduralBlockKind::AlwaysComb) boundary=SignalStorageBoundary::Combinational;
            else if (symbol.procedureKind==ProceduralBlockKind::AlwaysLatch) boundary=SignalStorageBoundary::Latch;
            if (const auto* timed=body->as_if<TimedStatement>()) {
                int edgeEvents=0,levelEvents=0;
                auto events=makeVisitor([&](auto&,const SignalEventControl& event) { event.edge==EdgeKind::None ? ++levelEvents : ++edgeEvents; });
                timed->timing.visit(events);
                const bool recurring=symbol.procedureKind==ProceduralBlockKind::Always || symbol.procedureKind==ProceduralBlockKind::AlwaysFF;
                if(recurring){
                    if (edgeEvents>0 && !levelEvents) boundary=SignalStorageBoundary::EdgeTriggered;
                    else if (!edgeEvents && (levelEvents>0 || timed->timing.kind==TimingControlKind::ImplicitEvent)) boundary=SignalStorageBoundary::Combinational;
                }else c.processIssues<<QStringLiteral("One-time or unsupported timed process.");
                body=&timed->stmt;
            }
            Flow flow; c.statement(*body,flow);
            c.publish(flow,boundary,processKey(symbol),text(toString(symbol.procedureKind)),
                relationshipEvidenceRange(c.sm,symbol.getBody().sourceRange));
            if (const auto* timed=symbol.getBody().as_if<TimedStatement>()) {
                auto events=makeVisitor([&](auto&,const SignalEventControl& event) {
                    for (const auto& origin:c.reads(event.expr,{})) for (const auto& target:flow)
                        c.emitFact(origin.access,target,SignalDependencyKind::Clock,boundary,processKey(symbol),
                            QStringLiteral("Event control"),relationshipEvidenceRange(c.sm,event.expr.sourceRange),{});
                });
                timed->timing.visit(events);
            }
        });
    compilation.getRoot().visit(visitor);
    if (cancelled && cancelled()) return {};
    return c.facts;
}
