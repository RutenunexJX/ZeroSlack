#include "slangsymbolcollectorhelpers.h"

#include <slang/ast/Scope.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/SubroutineSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/syntax/SyntaxNode.h>
#include <slang/text/SourceManager.h>

#include <string>

namespace slang_symbols::detail {

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
        QString scopeName;
        for (const slang::ast::Scope* scope = sym.getParentScope(); scope;) {
            const slang::ast::Symbol* scopeSym = &scope->asSymbol();
            if (const auto* sub = scopeSym->as_if<slang::ast::SubroutineSymbol>()) {
                scopeName = QString::fromStdString(std::string(sub->name));
                break;
            }
            if (const auto* pkg = scopeSym->as_if<slang::ast::PackageSymbol>()) {
                scopeName = QString::fromStdString(std::string(pkg->name));
                break;
            }
            scope = scopeSym->getParentScope();
        }
        if (scopeName.isEmpty()) {
            if (const slang::ast::DefinitionSymbol* def = sym.getDeclaringDefinition())
                scopeName = QString::fromStdString(std::string(def->name));
        }
        *outModuleScope = scopeName;
    }
    return true;
}

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
        const auto& st = canon.as<slang::ast::ScalarType>();
        if (st.scalarKind == slang::ast::ScalarType::Reg)
            return sym_list::sym_reg;
        return sym_list::sym_logic;
    }
    if (k == SymbolKind::EnumType)
        return sym_list::sym_enum_var;
    if (k == SymbolKind::PackedStructType)
        return sym_list::sym_packed_struct_var;
    if (k == SymbolKind::UnpackedStructType)
        return sym_list::sym_unpacked_struct_var;
    if (const slang::ast::IntegralType* it = canon.as_if<slang::ast::IntegralType>()) {
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

} // namespace slang_symbols::detail
