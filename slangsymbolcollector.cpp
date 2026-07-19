#include "slangsymbolcollector.h"
#include "slangsymbolpresentation.h"
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
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxNode.h>
#include <slang/text/SourceManager.h>

#include <QHash>
#include <QSet>
#include <functional>
#include <string>

using namespace slang::ast;
using namespace slang_symbols::detail;
using CollectorKind = SymbolTaxonomy::CollectorKind;

namespace {

bool isDeclaredLocalparam(const ParameterSymbol& parameter)
{
    const slang::syntax::SyntaxNode* syntax = parameter.getSyntax();
    if (syntax
        && syntax->kind == slang::syntax::SyntaxKind::Declarator
        && syntax->parent
        && syntax->parent->kind
            == slang::syntax::SyntaxKind::ParameterDeclaration) {
        const auto& declaration =
            syntax->parent->as<slang::syntax::ParameterDeclarationSyntax>();
        return declaration.keyword.kind
            == slang::parsing::TokenKind::LocalParamKeyword;
    }
    return parameter.isLocalParam();
}

bool fillLocationFromSource(
    const slang::SourceManager* sm,
    slang::SourceLocation location,
    const QString& name,
    SemanticSymbolRecord& record)
{
    if (!sm || !location.valid() || name.isEmpty())
        return false;

    const QTextDocumentSourcePosition start =
        qTextDocumentSourcePosition(sm, location);
    const QTextDocumentSourcePosition end =
        qTextDocumentSourcePosition(sm, location + name.toUtf8().size());
    if (!start.isValid())
        return false;
    record.name = name;
    record.location.fileName = start.fileName;
    record.location.startLine = start.line;
    record.location.startColumn = start.column;
    record.location.endLine = end.isValid() ? end.line : start.line;
    record.location.endColumn = end.isValid()
        ? end.column : start.column + name.size();
    record.location.position = start.position;
    record.location.length = end.isValid()
        ? qMax(0, end.position - start.position) : name.size();
    record.localHandle = -1;
    return true;
}

void emitInstancePinRecords(const slang::SourceManager* sm,
                            const InstanceSymbol& inst,
                            const QString& moduleScope,
                            QList<SemanticSymbolRecord>& outList)
{
    const auto* syntax = inst.getSyntax();
    if (!syntax || syntax->kind != slang::syntax::SyntaxKind::HierarchicalInstance)
        return;

    const auto& hierarchical =
        syntax->as<slang::syntax::HierarchicalInstanceSyntax>();
    QHash<QString, slang::SourceLocation> namedConnectionLocations;
    for (const slang::syntax::PortConnectionSyntax* portSyntax :
         hierarchical.connections) {
        if (!portSyntax
            || portSyntax->kind != slang::syntax::SyntaxKind::NamedPortConnection) {
            continue;
        }

        const auto& named =
            portSyntax->as<slang::syntax::NamedPortConnectionSyntax>();
        const QString portName =
            QString::fromStdString(std::string(named.name.valueText()));
        if (!portName.isEmpty())
            namedConnectionLocations.insert(portName, named.name.location());
    }

    if (namedConnectionLocations.isEmpty())
        return;

    const QString instantiatedModule =
        QString::fromStdString(std::string(inst.getDefinition().name));
    for (const PortConnection* connection : inst.getPortConnections()) {
        if (!connection)
            continue;
        const QString portName =
            QString::fromStdString(std::string(connection->port.name));
        if (!namedConnectionLocations.contains(portName))
            continue;

        SemanticSymbolRecord record;
        if (!fillLocationFromSource(
                sm,
                namedConnectionLocations.value(portName),
                portName,
                record)) {
            continue;
        }

        applyCollectorKind(&record, CollectorKind::InstPin);
        record.owner.name = moduleScope;
        record.type.rawTypeText = instantiatedModule;
        record.type.resolvedTypeName = instantiatedModule;
        record.type.resolvedTypeKind =
            SymbolTaxonomy::DeclarationKind::Module;
        outList.append(record);
    }
}

void collectNativeRecords(slang::ast::Compilation& compilation,
                          QList<SemanticSymbolRecord>& outList,
                          const std::function<bool()>& isCancelled,
                          QList<EffectiveValueFact>* effectiveValueFacts)
{
    bool cancellationReached = false;
    auto cancelled = [&]() {
        if (cancellationReached)
            return true;
        if (isCancelled && isCancelled()) {
            cancellationReached = true;
            return true;
        }
        return false;
    };
    if (cancelled()) {
        outList.clear();
        return;
    }

    const slang::SourceManager* sm = compilation.getSourceManager();
    if (!sm)
        return;
    resetQTextDocumentSourcePositionCache(sm);
    struct SourcePositionCacheReset {
        ~SourcePositionCacheReset()
        {
            resetQTextDocumentSourcePositionCache(nullptr);
        }
    } sourcePositionCacheReset;
    const slang::ast::RootSymbol& root = compilation.getRoot();

    // (1) Module / interface / program definitions. root.visit() walks the elaborated instance
    //     tree, NOT definitions, so definitions must be emitted explicitly here - one entry per
    //     definition, always present even for uninstantiated modules.
    for (const slang::ast::Symbol* defSym : compilation.getDefinitions()) {
        if (cancelled()) {
            outList.clear();
            return;
        }
        const auto* def = defSym ? defSym->as_if<DefinitionSymbol>() : nullptr;
        if (!def)
            continue;
        SemanticSymbolRecord record;
        if (!fillSymbolRecord(sm, *def, record, nullptr))
            continue;
        if (def->definitionKind == DefinitionKind::Module)
            applyCollectorKind(&record, CollectorKind::Module);
        else if (def->definitionKind == DefinitionKind::Interface)
            applyCollectorKind(&record, CollectorKind::Interface);
        else if (def->definitionKind == DefinitionKind::Program)
            applyCollectorKind(&record, CollectorKind::Module);
        else
            continue;
        record.owner.name.clear();
        outList.append(record);
    }

    // (2) Top-level auto-instances (uninstantiated/top modules) should not appear as instance
    //     symbols; collect them so we can skip emitting spurious sym_inst rows for them.
    QSet<const void*> topInstances;
    for (const slang::ast::InstanceSymbol* ti : root.topInstances) {
        if (cancelled()) {
            outList.clear();
            return;
        }
        topInstances.insert(ti);
    }

    // (3) Pass A: collect the net/variable that backs each port, so the main pass can skip it
    //     (otherwise every ANSI port is emitted twice: once as a port, once as a net/var).
    QSet<const void*> portInternals;
    {
        auto portCollector = makeVisitor([&](auto& v, const PortSymbol& port) {
            if (cancelled())
                return;
            if (port.internalSymbol)
                portInternals.insert(port.internalSymbol);
            v.visitDefault(port);
        });
        root.visit(portCollector);
    }
    if (cancellationReached) {
        outList.clear();
        return;
    }

    // (4) Pass B: emit instances and all body members. Each definition's body is walked only
    //     once (visitedDefs) so a module instantiated N times doesn't duplicate its members.
    QSet<const void*> visitedDefs;
    auto visitor = makeVisitor(
        [&](auto& v, const InstanceSymbol& inst) {
            if (cancelled())
                return;
            if (!topInstances.contains(&inst)) {
                SemanticSymbolRecord record;
                QString moduleScope;
                if (fillSymbolRecord(sm, inst, record, &moduleScope)) {
                    applyCollectorKind(&record, CollectorKind::Inst);
                    record.type.rawTypeText =
                        QString::fromStdString(std::string(inst.getDefinition().name));
                    record.type.resolvedTypeName = record.type.rawTypeText;
                    record.type.resolvedTypeKind =
                        SymbolTaxonomy::DeclarationKind::Module;
                    record.owner.name = moduleScope;
                    outList.append(record);
                    emitInstancePinRecords(sm, inst, moduleScope, outList);
                }
            }
            const void* defKey = &inst.getDefinition();
            if (visitedDefs.contains(defKey))
                return;  // members of this definition already captured
            visitedDefs.insert(defKey);
            if (cancelled())
                return;
            v.visitDefault(inst);
        },
        [&](auto& v, const VariableSymbol& var) {
            if (cancelled())
                return;
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
            applyCollectorKind(&record, variableOrNetCollectorKind(var.getType()));
            record.owner.name = moduleScope;
            // For enum/struct variables, record the type key in dataType so var.member /
            // enum-value completion can resolve the type (consumed by get{Struct,Enum}TypeForVariable).
            // Typedef'd types use the alias name (members/values already emitted at the typedef site).
            // Inline anonymous types (no alias) have no typedef site, so key members/values by the
            // variable name here and emit them now.
            if (record.collectorKind == CollectorKind::EnumVariable
                || record.collectorKind == CollectorKind::PackedStructVariable
                || record.collectorKind == CollectorKind::UnpackedStructVariable) {
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
            if (cancelled())
                return;
            if (var.kind != SymbolKind::FormalArgument)
                v.visitDefault(var);
        },
        [&](auto& v, const NetSymbol& net) {
            if (cancelled())
                return;
            if (portInternals.contains(&net))
                return;  // backing net of a port; emitted as the port itself
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, net, record, &moduleScope))
                return;
            applyCollectorKind(&record, CollectorKind::Wire);
            record.owner.name = moduleScope;
            outList.append(record);
            if (cancelled())
                return;
            v.visitDefault(net);
        },
        [&](auto& v, const SubroutineSymbol& sub) {
            if (cancelled())
                return;
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, sub, record, &moduleScope))
                return;
            applyCollectorKind(&record,
                (sub.subroutineKind == SubroutineKind::Task)
                    ? CollectorKind::Task
                    : CollectorKind::Function);
            record.owner.name = moduleScope;
            outList.append(record);
            if (cancelled())
                return;
            v.visitDefault(sub);
        },
        [&](auto& v, const PortSymbol& port) {
            if (cancelled())
                return;
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, port, record, &moduleScope))
                return;
            applyCollectorKind(&record, portDirectionCollectorKind(port.direction));
            record.owner.name = moduleScope;
            outList.append(record);
            if (cancelled())
                return;
            v.visitDefault(port);
        },
        [&](auto& v, const InterfacePortSymbol& port) {
            if (cancelled())
                return;
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, port, record, &moduleScope))
                return;
            applyCollectorKind(&record,
                port.modport.empty()
                    ? CollectorKind::PortInterface
                    : CollectorKind::PortInterfaceModport);
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
            if (cancelled())
                return;
            v.visitDefault(port);
        },
        [&](auto& v, const ModportSymbol& modport) {
            if (cancelled())
                return;
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, modport, record, &moduleScope))
                return;
            applyCollectorKind(&record, CollectorKind::InterfaceModport);
            record.owner.name = moduleScope;
            outList.append(record);
            if (cancelled())
                return;
            v.visitDefault(modport);
        },
        [&](auto& v, const ParameterSymbol& param) {
            if (cancelled())
                return;
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, param, record, &moduleScope))
                return;
            applyCollectorKind(&record,
                isDeclaredLocalparam(param)
                    ? CollectorKind::Localparam
                    : CollectorKind::Parameter);
            record.owner.name = moduleScope;
            outList.append(record);
            if (cancelled())
                return;
            v.visitDefault(param);
        },
        [&](auto& v, const TypeAliasType& typeAlias) {
            if (cancelled())
                return;
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, typeAlias, record, &moduleScope))
                return;
            applyCollectorKind(&record, CollectorKind::Typedef);
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
                        ? CollectorKind::PackedStruct
                        : CollectorKind::UnpackedStruct);
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
            if (cancelled())
                return;
            v.visitDefault(typeAlias);
        },
        [&](auto& v, const EnumType& enumType) {
            if (cancelled())
                return;
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, enumType, record, &moduleScope))
                return;
            applyCollectorKind(&record, CollectorKind::Enum);
            record.owner.name = moduleScope;
            outList.append(record);
            if (cancelled())
                return;
            v.visitDefault(enumType);
        },
        [&](auto& v, const PackageSymbol& pkg) {
            if (cancelled())
                return;
            SemanticSymbolRecord record;
            QString moduleScope;
            if (!fillSymbolRecord(sm, pkg, record, &moduleScope))
                return;
            applyCollectorKind(&record, CollectorKind::Package);
            record.owner.name = QString::fromStdString(std::string(pkg.name));
            outList.append(record);
            if (cancelled())
                return;
            v.visitDefault(pkg);
        }
    );

    root.visit(visitor);
    if (cancellationReached || cancelled()) {
        outList.clear();
        return;
    }
    finalizeCollectedSymbolRecords(&outList);
    slang_symbols::populateSymbolPresentations(compilation,
                                               outList,
                                               effectiveValueFacts,
                                               cancelled);
    if (cancellationReached || cancelled()) {
        outList.clear();
        if (effectiveValueFacts)
            effectiveValueFacts->clear();
    }
}

}

void slang_symbols::collectSymbolRecords(slang::ast::Compilation& compilation,
                                         QList<SemanticSymbolRecord>& outList,
                                         std::function<bool()> isCancelled,
                                         QList<EffectiveValueFact>* effectiveValueFacts)
{
    collectNativeRecords(compilation,
                         outList,
                         isCancelled,
                         effectiveValueFacts);
}
