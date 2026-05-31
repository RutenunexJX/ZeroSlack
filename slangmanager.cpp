#include "slangmanager.h"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/Scope.h>
#include <slang/ast/SemanticFacts.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/SubroutineSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/CallExpression.h>
#include <slang/ast/expressions/ConversionExpression.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/expressions/SelectExpressions.h>
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/LoopStatements.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/syntax/SyntaxNode.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include <QFile>
#include <QSet>
#include <QTextStream>
#include <string>

using namespace slang::ast;

namespace {

// Map Slang symbol to our sym_type_e and fill SymbolInfo. Returns true if the symbol was emitted.
bool fillSymbolInfo(const slang::SourceManager* sm,
                    const slang::ast::Symbol& sym,
                    sym_list::SymbolInfo& out,
                    QString* outModuleScope)
{
    if (!sm || !sym.location.valid())
        return false;

    std::string nameStr(sym.name);
    out.symbolName = QString::fromStdString(nameStr);
    out.fileName = QString::fromStdString(std::string(sm->getFileName(sym.location)));
    size_t line = sm->getLineNumber(sym.location);
    out.startLine = (line == 0) ? 1 : static_cast<int>(line);
    size_t col = sm->getColumnNumber(sym.location);
    out.startColumn = (col == 0) ? 1 : static_cast<int>(col);
    out.position = static_cast<int>(sym.location.offset());
    out.length = 0;
    out.symbolId = 0;
    out.scopeLevel = 0;
    out.dataType.clear();

    if (const slang::syntax::SyntaxNode* syntax = sym.getSyntax()) {
        slang::SourceRange range = syntax->sourceRange();
        if (range.end().valid()) {
            size_t endLine = sm->getLineNumber(range.end());
            size_t endCol = sm->getColumnNumber(range.end());
            out.endLine = (endLine == 0) ? out.startLine : static_cast<int>(endLine);
            out.endColumn = (endCol == 0) ? out.startColumn : static_cast<int>(endCol);
        } else {
            out.endLine = out.startLine;
            out.endColumn = out.startColumn;
        }
    } else {
        out.endLine = out.startLine;
        out.endColumn = out.startColumn;
    }

    if (outModuleScope) {
        // Walk enclosing scopes. If the nearest named container is a task/function, scope the
        // symbol to that subroutine (not the module) so function/task locals (formal args,
        // return value, body vars) don't leak into module-level r/w/l completion, which filters
        // by moduleScope == module. The scope-tree path ignores moduleScope, so locals still
        // surface inside the subroutine. A package container yields the package name.
        QString scopeName;
        for (const Scope* scope = sym.getParentScope(); scope;) {
            const Symbol* scopeSym = &scope->asSymbol();
            if (const auto* sub = scopeSym->as_if<SubroutineSymbol>()) {
                scopeName = QString::fromStdString(std::string(sub->name));
                break;
            }
            if (const auto* pkg = scopeSym->as_if<PackageSymbol>()) {
                scopeName = QString::fromStdString(std::string(pkg->name));
                break;
            }
            scope = scopeSym->getParentScope();
        }
        if (scopeName.isEmpty()) {
            if (const DefinitionSymbol* def = sym.getDeclaringDefinition())
                scopeName = QString::fromStdString(std::string(def->name));
        }
        *outModuleScope = scopeName;
    }
    return true;
}

// Emit one sym_enum_value per enumerator, keyed (moduleScope) by scopeKey — the type alias name
// for typedef'd enums, or the variable name for inline anonymous enums. getEnumValueCompletions
// matches sym_enum_value whose moduleScope == that key.
void emitEnumValues(const slang::SourceManager* sm,
                    const slang::ast::EnumType& et,
                    const QString& scopeKey,
                    QList<sym_list::SymbolInfo>& outList)
{
    for (const auto& ev : et.values()) {
        sym_list::SymbolInfo m;
        if (!fillSymbolInfo(sm, ev, m, nullptr))
            continue;
        m.symbolType = sym_list::sym_enum_value;
        m.moduleScope = scopeKey;
        outList.append(m);
    }
}

// Emit one sym_struct_member per field, keyed (moduleScope) by scopeKey — the type alias name for
// typedef'd structs, or the variable name for inline anonymous structs. getStructMemberCompletions
// matches sym_struct_member whose moduleScope == that key.
void emitStructMembers(const slang::SourceManager* sm,
                       const slang::ast::Scope& structScope,
                       const QString& scopeKey,
                       QList<sym_list::SymbolInfo>& outList)
{
    for (const auto& member : structScope.members()) {
        if (member.kind != slang::ast::SymbolKind::Field)
            continue;
        sym_list::SymbolInfo m;
        if (!fillSymbolInfo(sm, member, m, nullptr))
            continue;
        m.symbolType = sym_list::sym_struct_member;
        m.moduleScope = scopeKey;
        outList.append(m);
    }
}

sym_list::sym_type_e variableOrNetTypeToSymType(const slang::ast::Type& type)
{
    const slang::ast::Type& canon = type.getCanonicalType();
    using slang::ast::SymbolKind;
    SymbolKind k = canon.kind;

    if (k == SymbolKind::ScalarType) {
        const auto& st = canon.as<ScalarType>();
        if (st.scalarKind == ScalarType::Reg)
            return sym_list::sym_reg;
        return sym_list::sym_logic;
    }
    if (k == SymbolKind::EnumType)
        return sym_list::sym_enum_var;
    if (k == SymbolKind::PackedStructType)
        return sym_list::sym_packed_struct_var;
    if (k == SymbolKind::UnpackedStructType)
        return sym_list::sym_unpacked_struct_var;
    if (const IntegralType* it = canon.as_if<IntegralType>()) {
        if (it->isDeclaredReg())
            return sym_list::sym_reg;
        return sym_list::sym_logic;
    }
    return sym_list::sym_logic;
}

sym_list::sym_type_e portDirectionToSymType(slang::ast::ArgumentDirection dir)
{
    using slang::ast::ArgumentDirection;
    switch (dir) {
    case ArgumentDirection::In:    return sym_list::sym_port_input;
    case ArgumentDirection::Out:    return sym_list::sym_port_output;
    case ArgumentDirection::InOut:  return sym_list::sym_port_inout;
    case ArgumentDirection::Ref:    return sym_list::sym_port_ref;
    default:                        return sym_list::sym_port_inout;
    }
}

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

void collectSymbols(slang::ast::Compilation& compilation,
                    QList<sym_list::SymbolInfo>& outList)
{
    const slang::SourceManager* sm = compilation.getSourceManager();
    if (!sm)
        return;
    const slang::ast::RootSymbol& root = compilation.getRoot();

    // (1) Module / interface / program definitions. root.visit() walks the elaborated instance
    //     tree, NOT definitions, so definitions must be emitted explicitly here — one entry per
    //     definition, always present even for uninstantiated modules.
    for (const slang::ast::Symbol* defSym : compilation.getDefinitions()) {
        const auto* def = defSym ? defSym->as_if<DefinitionSymbol>() : nullptr;
        if (!def)
            continue;
        sym_list::SymbolInfo info;
        if (!fillSymbolInfo(sm, *def, info, nullptr))
            continue;
        if (def->definitionKind == DefinitionKind::Module)
            info.symbolType = sym_list::sym_module;
        else if (def->definitionKind == DefinitionKind::Interface)
            info.symbolType = sym_list::sym_interface;
        else if (def->definitionKind == DefinitionKind::Program)
            info.symbolType = sym_list::sym_module;
        else
            continue;
        info.moduleScope.clear();
        outList.append(info);
    }

    // (2) Top-level auto-instances (uninstantiated/top modules) should not appear as instance
    //     symbols; collect them so we can skip emitting spurious sym_inst rows for them.
    QSet<const void*> topInstances;
    for (const slang::ast::InstanceSymbol* ti : root.topInstances)
        topInstances.insert(ti);

    // (3) Pass A: collect the net/variable that backs each port, so the main pass can skip it
    //     (otherwise every ANSI port is emitted twice: once as a port, once as a net/var).
    QSet<const void*> portInternals;
    {
        auto portCollector = makeVisitor([&](auto& v, const PortSymbol& port) {
            if (port.internalSymbol)
                portInternals.insert(port.internalSymbol);
            v.visitDefault(port);
        });
        root.visit(portCollector);
    }

    // (4) Pass B: emit instances and all body members. Each definition's body is walked only
    //     once (visitedDefs) so a module instantiated N times doesn't duplicate its members.
    QSet<const void*> visitedDefs;
    auto visitor = makeVisitor(
        [&](auto& v, const InstanceSymbol& inst) {
            if (!topInstances.contains(&inst)) {
                sym_list::SymbolInfo info;
                QString moduleScope;
                if (fillSymbolInfo(sm, inst, info, &moduleScope)) {
                    info.symbolType = sym_list::sym_inst;
                    info.dataType = QString::fromStdString(std::string(inst.getDefinition().name));
                    info.moduleScope = moduleScope;
                    outList.append(info);
                }
            }
            const void* defKey = &inst.getDefinition();
            if (visitedDefs.contains(defKey))
                return;  // members of this definition already captured
            visitedDefs.insert(defKey);
            v.visitDefault(inst);
        },
        [&](auto& v, const VariableSymbol& var) {
            if (portInternals.contains(&var))
                return;  // backing var of a port; emitted as the port itself
            // Struct/union fields are emitted at their typedef site (TypeAliasType visitor),
            // where the enclosing struct type name is known and used as moduleScope. Skipping
            // here avoids a wrong moduleScope (the module name) and duplicate members.
            if (var.kind == SymbolKind::Field)
                return;
            sym_list::SymbolInfo info;
            QString moduleScope;
            if (!fillSymbolInfo(sm, var, info, &moduleScope))
                return;
            info.symbolType = variableOrNetTypeToSymType(var.getType());
            info.moduleScope = moduleScope;
            // For enum/struct variables, record the type key in dataType so var.member /
            // enum-value completion can resolve the type (consumed by get{Struct,Enum}TypeForVariable).
            // Typedef'd types use the alias name (members/values already emitted at the typedef site).
            // Inline anonymous types (no alias) have no typedef site, so key members/values by the
            // variable name here and emit them now.
            if (info.symbolType == sym_list::sym_enum_var
                || info.symbolType == sym_list::sym_packed_struct_var
                || info.symbolType == sym_list::sym_unpacked_struct_var) {
                QString typeName = QString::fromStdString(std::string(var.getType().name));
                if (!typeName.isEmpty()) {
                    info.dataType = typeName;  // typedef'd: emitted at typedef site
                } else {
                    const QString key = info.symbolName;  // anonymous: key by variable name
                    info.dataType = key;
                    const slang::ast::Type& canon = var.getType().getCanonicalType();
                    if (canon.kind == SymbolKind::EnumType) {
                        emitEnumValues(sm, canon.as<EnumType>(), key, outList);
                    } else if (canon.kind == SymbolKind::PackedStructType) {
                        emitStructMembers(sm, static_cast<const slang::ast::Scope&>(canon.as<PackedStructType>()), key, outList);
                    } else if (canon.kind == SymbolKind::UnpackedStructType) {
                        emitStructMembers(sm, static_cast<const slang::ast::Scope&>(canon.as<UnpackedStructType>()), key, outList);
                    }
                }
            }
            outList.append(info);
            if (var.kind != SymbolKind::FormalArgument)
                v.visitDefault(var);
        },
        [&](auto& v, const NetSymbol& net) {
            if (portInternals.contains(&net))
                return;  // backing net of a port; emitted as the port itself
            sym_list::SymbolInfo info;
            QString moduleScope;
            if (!fillSymbolInfo(sm, net, info, &moduleScope))
                return;
            info.symbolType = sym_list::sym_wire;
            info.moduleScope = moduleScope;
            outList.append(info);
            v.visitDefault(net);
        },
        [&](auto& v, const SubroutineSymbol& sub) {
            sym_list::SymbolInfo info;
            QString moduleScope;
            if (!fillSymbolInfo(sm, sub, info, &moduleScope))
                return;
            info.symbolType = (sub.subroutineKind == SubroutineKind::Task)
                ? sym_list::sym_task
                : sym_list::sym_function;
            info.moduleScope = moduleScope;
            outList.append(info);
            v.visitDefault(sub);
        },
        [&](auto& v, const PortSymbol& port) {
            sym_list::SymbolInfo info;
            QString moduleScope;
            if (!fillSymbolInfo(sm, port, info, &moduleScope))
                return;
            info.symbolType = portDirectionToSymType(port.direction);
            info.moduleScope = moduleScope;
            outList.append(info);
            v.visitDefault(port);
        },
        [&](auto& v, const ParameterSymbol& param) {
            sym_list::SymbolInfo info;
            QString moduleScope;
            if (!fillSymbolInfo(sm, param, info, &moduleScope))
                return;
            info.symbolType = param.isLocalParam() ? sym_list::sym_localparam : sym_list::sym_parameter;
            info.moduleScope = moduleScope;
            outList.append(info);
            v.visitDefault(param);
        },
        [&](auto& v, const TypeAliasType& typeAlias) {
            sym_list::SymbolInfo info;
            QString moduleScope;
            if (!fillSymbolInfo(sm, typeAlias, info, &moduleScope))
                return;
            info.symbolType = sym_list::sym_typedef;
            info.moduleScope = moduleScope;
            const QString aliasName = info.symbolName;
            const slang::ast::Type& target = typeAlias.getCanonicalType();
            if (target.kind == SymbolKind::EnumType) {
                info.dataType = QLatin1String("enum");
                outList.append(info);
                emitEnumValues(sm, target.as<EnumType>(), aliasName, outList);
            }
            else if (target.kind == SymbolKind::PackedStructType
                     || target.kind == SymbolKind::UnpackedStructType) {
                const bool packed = (target.kind == SymbolKind::PackedStructType);
                info.dataType = QLatin1String("struct");
                outList.append(info);
                // Emit the struct *type* symbol too, so ns/nsp completion and type-name jump
                // (which look for sym_packed_struct / sym_unpacked_struct) resolve.
                sym_list::SymbolInfo typeSym = info;
                typeSym.symbolType = packed ? sym_list::sym_packed_struct
                                            : sym_list::sym_unpacked_struct;
                typeSym.dataType.clear();
                outList.append(typeSym);
                const slang::ast::Scope& structScope = packed
                    ? static_cast<const slang::ast::Scope&>(target.as<PackedStructType>())
                    : static_cast<const slang::ast::Scope&>(target.as<UnpackedStructType>());
                emitStructMembers(sm, structScope, aliasName, outList);
            }
            else {
                outList.append(info);
            }
            v.visitDefault(typeAlias);
        },
        [&](auto& v, const EnumType& enumType) {
            sym_list::SymbolInfo info;
            QString moduleScope;
            if (!fillSymbolInfo(sm, enumType, info, &moduleScope))
                return;
            info.symbolType = sym_list::sym_enum;
            info.moduleScope = moduleScope;
            outList.append(info);
            v.visitDefault(enumType);
        },
        [&](auto& v, const PackageSymbol& pkg) {
            sym_list::SymbolInfo info;
            QString moduleScope;
            if (!fillSymbolInfo(sm, pkg, info, &moduleScope))
                return;
            info.symbolType = sym_list::sym_package;
            info.moduleScope = QString::fromStdString(std::string(pkg.name));
            outList.append(info);
            v.visitDefault(pkg);
        }
    );

    root.visit(visitor);
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

        using namespace slang::ast;
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

        using namespace slang::ast;
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

        using namespace slang::ast;
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

        using namespace slang::ast;
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

        using namespace slang::ast;
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

        using namespace slang::ast;
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

QList<sym_list::SymbolInfo> SlangManager::extractSymbols(const QString& fileName, const QString& content)
{
    QList<sym_list::SymbolInfo> result;
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

        collectSymbols(compilation, result);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}

QList<sym_list::SymbolInfo> SlangManager::extractWorkspaceSymbols(const QStringList& filePaths)
{
    QList<sym_list::SymbolInfo> result;
    if (filePaths.isEmpty())
        return result;
    try {
        std::vector<std::string> pathStrs;
        pathStrs.reserve(filePaths.size());
        for (const QString& p : filePaths)
            pathStrs.push_back(p.toStdString());

        std::vector<std::string_view> pathViews;
        pathViews.reserve(pathStrs.size());
        for (const std::string& s : pathStrs)
            pathViews.push_back(s);

        auto treeOrErr = slang::syntax::SyntaxTree::fromFiles(pathViews);
        if (!treeOrErr)
            return result;

        std::shared_ptr<slang::syntax::SyntaxTree> tree = std::move(*treeOrErr);
        if (!tree)
            return result;

        slang::Bag bag;
        auto& opts = bag.insertOrGet<CompilationOptions>();
        opts.flags |= CompilationFlags::IgnoreUnknownModules;

        Compilation compilation(bag);
        compilation.addSyntaxTree(tree);

        collectSymbols(compilation, result);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}
