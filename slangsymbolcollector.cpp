#include "slangsymbolcollector.h"
#include "slangsymbolcollectorhelpers.h"

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
#include <slang/ast/types/AllTypes.h>
#include <slang/syntax/SyntaxNode.h>
#include <slang/text/SourceManager.h>

#include <QSet>
#include <string>

using namespace slang::ast;
using namespace slang_symbols::detail;

void slang_symbols::collectSymbols(slang::ast::Compilation& compilation,
                                   QList<sym_list::SymbolInfo>& outList)
{
    const slang::SourceManager* sm = compilation.getSourceManager();
    if (!sm)
        return;
    const slang::ast::RootSymbol& root = compilation.getRoot();

    // (1) Module / interface / program definitions. root.visit() walks the elaborated instance
    //     tree, NOT definitions, so definitions must be emitted explicitly here - one entry per
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
