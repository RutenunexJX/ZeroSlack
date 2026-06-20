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
using RawCollectorKind = SymbolTaxonomy::RawCollectorKind;

namespace {

void collectNativeRecords(slang::ast::Compilation& compilation,
                          QList<SemanticSymbolRecord>& outList)
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
        SemanticSymbolRecord record;
        if (!fillSymbolRecord(sm, *def, record, nullptr))
            continue;
        if (def->definitionKind == DefinitionKind::Module)
            applyCollectorKind(&record, RawCollectorKind::Module);
        else if (def->definitionKind == DefinitionKind::Interface)
            applyCollectorKind(&record, RawCollectorKind::Interface);
        else if (def->definitionKind == DefinitionKind::Program)
            applyCollectorKind(&record, RawCollectorKind::Module);
        else
            continue;
        record.owner.name.clear();
        outList.append(record);
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
                SemanticSymbolRecord record;
                QString moduleScope;
                if (fillSymbolRecord(sm, inst, record, &moduleScope)) {
                    applyCollectorKind(&record, RawCollectorKind::Inst);
                    record.type.rawTypeText =
                        QString::fromStdString(std::string(inst.getDefinition().name));
                    record.owner.name = moduleScope;
                    outList.append(record);
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
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, var, record, &moduleScope))
                return;
            applyCollectorKind(&record, variableOrNetRawCollectorKind(var.getType()));
            record.owner.name = moduleScope;
            // For enum/struct variables, record the type key in dataType so var.member /
            // enum-value completion can resolve the type (consumed by get{Struct,Enum}TypeForVariable).
            // Typedef'd types use the alias name (members/values already emitted at the typedef site).
            // Inline anonymous types (no alias) have no typedef site, so key members/values by the
            // variable name here and emit them now.
            if (record.rawCollectorKind == RawCollectorKind::EnumVariable
                || record.rawCollectorKind == RawCollectorKind::PackedStructVariable
                || record.rawCollectorKind == RawCollectorKind::UnpackedStructVariable) {
                QString typeName = QString::fromStdString(std::string(var.getType().name));
                if (!typeName.isEmpty()) {
                    record.type.rawTypeText = typeName;  // typedef'd: emitted at typedef site
                } else {
                    const QString key = record.name;  // anonymous: key by variable name
                    record.type.rawTypeText = key;
                    const slang::ast::Type& canon = var.getType().getCanonicalType();
                    if (canon.kind == SymbolKind::EnumType) {
                        emitEnumValueRecords(sm, canon.as<EnumType>(), key, outList);
                    } else if (canon.kind == SymbolKind::PackedStructType) {
                        emitStructMemberRecords(sm, static_cast<const slang::ast::Scope&>(canon.as<PackedStructType>()), key, outList);
                    } else if (canon.kind == SymbolKind::UnpackedStructType) {
                        emitStructMemberRecords(sm, static_cast<const slang::ast::Scope&>(canon.as<UnpackedStructType>()), key, outList);
                    }
                }
            }
            outList.append(record);
            if (var.kind != SymbolKind::FormalArgument)
                v.visitDefault(var);
        },
        [&](auto& v, const NetSymbol& net) {
            if (portInternals.contains(&net))
                return;  // backing net of a port; emitted as the port itself
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, net, record, &moduleScope))
                return;
            applyCollectorKind(&record, RawCollectorKind::Wire);
            record.owner.name = moduleScope;
            outList.append(record);
            v.visitDefault(net);
        },
        [&](auto& v, const SubroutineSymbol& sub) {
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, sub, record, &moduleScope))
                return;
            applyCollectorKind(&record,
                (sub.subroutineKind == SubroutineKind::Task)
                    ? RawCollectorKind::Task
                    : RawCollectorKind::Function);
            record.owner.name = moduleScope;
            outList.append(record);
            v.visitDefault(sub);
        },
        [&](auto& v, const PortSymbol& port) {
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, port, record, &moduleScope))
                return;
            applyCollectorKind(&record, portDirectionRawCollectorKind(port.direction));
            record.owner.name = moduleScope;
            outList.append(record);
            v.visitDefault(port);
        },
        [&](auto& v, const InterfacePortSymbol& port) {
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, port, record, &moduleScope))
                return;
            applyCollectorKind(&record,
                port.modport.empty()
                    ? RawCollectorKind::PortInterface
                    : RawCollectorKind::PortInterfaceModport);
            record.owner.name = moduleScope;
            if (port.interfaceDef) {
                record.type.rawTypeText = QString::fromStdString(
                    std::string(port.interfaceDef->name));
                if (!port.modport.empty()) {
                    record.type.rawTypeText += QLatin1Char('.');
                    record.type.rawTypeText += QString::fromStdString(std::string(port.modport));
                }
            }
            outList.append(record);
            v.visitDefault(port);
        },
        [&](auto& v, const ModportSymbol& modport) {
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, modport, record, &moduleScope))
                return;
            applyCollectorKind(&record, RawCollectorKind::InterfaceModport);
            record.owner.name = moduleScope;
            outList.append(record);
            v.visitDefault(modport);
        },
        [&](auto& v, const ParameterSymbol& param) {
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, param, record, &moduleScope))
                return;
            applyCollectorKind(&record,
                param.isLocalParam()
                    ? RawCollectorKind::Localparam
                    : RawCollectorKind::Parameter);
            record.owner.name = moduleScope;
            outList.append(record);
            v.visitDefault(param);
        },
        [&](auto& v, const TypeAliasType& typeAlias) {
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, typeAlias, record, &moduleScope))
                return;
            applyCollectorKind(&record, RawCollectorKind::Typedef);
            record.owner.name = moduleScope;
            const QString aliasName = record.name;
            const slang::ast::Type& target = typeAlias.getCanonicalType();
            if (target.kind == SymbolKind::EnumType) {
                record.type.rawTypeText = QLatin1String("enum");
                outList.append(record);
                emitEnumValueRecords(sm, target.as<EnumType>(), aliasName, outList);
            }
            else if (target.kind == SymbolKind::PackedStructType
                     || target.kind == SymbolKind::UnpackedStructType) {
                const bool packed = (target.kind == SymbolKind::PackedStructType);
                record.type.rawTypeText = QLatin1String("struct");
                outList.append(record);
                // Emit the struct *type* symbol too, so ns/nsp completion and type-name jump
                // (which look for sym_packed_struct / sym_unpacked_struct) resolve.
                SemanticSymbolRecord typeRecord = record;
                applyCollectorKind(&typeRecord,
                    packed
                        ? RawCollectorKind::PackedStruct
                        : RawCollectorKind::UnpackedStruct);
                typeRecord.type.rawTypeText.clear();
                outList.append(typeRecord);
                const slang::ast::Scope& structScope = packed
                    ? static_cast<const slang::ast::Scope&>(target.as<PackedStructType>())
                    : static_cast<const slang::ast::Scope&>(target.as<UnpackedStructType>());
                emitStructMemberRecords(sm, structScope, aliasName, outList);
            }
            else {
                outList.append(record);
            }
            v.visitDefault(typeAlias);
        },
        [&](auto& v, const EnumType& enumType) {
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, enumType, record, &moduleScope))
                return;
            applyCollectorKind(&record, RawCollectorKind::Enum);
            record.owner.name = moduleScope;
            outList.append(record);
            v.visitDefault(enumType);
        },
        [&](auto& v, const PackageSymbol& pkg) {
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, pkg, record, &moduleScope))
                return;
            applyCollectorKind(&record, RawCollectorKind::Package);
            record.owner.name = QString::fromStdString(std::string(pkg.name));
            outList.append(record);
            v.visitDefault(pkg);
        }
    );

    root.visit(visitor);
    finalizeCollectedSymbolRecords(&outList);
}

}

void slang_symbols::collectSymbolRecords(slang::ast::Compilation& compilation,
                                         QList<SemanticSymbolRecord>& outList)
{
    collectNativeRecords(compilation, outList);
}
