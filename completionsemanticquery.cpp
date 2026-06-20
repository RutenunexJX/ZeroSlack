#include "completionsemanticquery.h"

#include "completionservice.h"
#include "symboltaxonomy.h"

namespace {
sym_list::sym_type_e rawCollectorKindForCommandKind(CompletionCommandKind kind)
{
    switch (kind) {
    case CompletionCommandKind::Reg:
        return sym_list::sym_reg;
    case CompletionCommandKind::Wire:
        return sym_list::sym_wire;
    case CompletionCommandKind::Logic:
        return sym_list::sym_logic;
    case CompletionCommandKind::Module:
        return sym_list::sym_module;
    case CompletionCommandKind::Task:
        return sym_list::sym_task;
    case CompletionCommandKind::Function:
        return sym_list::sym_function;
    case CompletionCommandKind::Interface:
        return sym_list::sym_interface;
    case CompletionCommandKind::Package:
        return sym_list::sym_package;
    case CompletionCommandKind::Macro:
        return sym_list::sym_def_define;
    case CompletionCommandKind::Localparam:
        return sym_list::sym_localparam;
    case CompletionCommandKind::Parameter:
        return sym_list::sym_parameter;
    case CompletionCommandKind::AlwaysProcess:
        return sym_list::sym_always;
    case CompletionCommandKind::ContinuousAssign:
        return sym_list::sym_assign;
    case CompletionCommandKind::Typedef:
        return sym_list::sym_typedef;
    case CompletionCommandKind::EnumValue:
        return sym_list::sym_enum_value;
    case CompletionCommandKind::EnumType:
        return sym_list::sym_enum;
    case CompletionCommandKind::EnumVariable:
        return sym_list::sym_enum_var;
    case CompletionCommandKind::StructMember:
        return sym_list::sym_struct_member;
    case CompletionCommandKind::PackedStructType:
        return sym_list::sym_packed_struct;
    case CompletionCommandKind::UnpackedStructType:
        return sym_list::sym_unpacked_struct;
    case CompletionCommandKind::PackedStructVariable:
        return sym_list::sym_packed_struct_var;
    case CompletionCommandKind::UnpackedStructVariable:
        return sym_list::sym_unpacked_struct_var;
    case CompletionCommandKind::User:
        return sym_list::sym_user;
    }
    return sym_list::sym_user;
}
}

QList<SemanticSymbolRecord> CompletionSemanticQuery::commandSymbolRecords(
    SemanticIndex* semanticIndex,
    const CommandCompletionQuery& query)
{
    if (!semanticIndex)
        return {};

    const sym_list::sym_type_e adapterRawCollectorKind =
        rawCollectorKindForCommandKind(query.commandKind);
    const bool directModuleContext =
        SymbolTaxonomy::isDirectModuleContextCompletionRequest(
            adapterRawCollectorKind);

    if (directModuleContext) {
        if (query.moduleName.isEmpty())
            return {};

        semanticIndex->refreshStructTypedefEnumForFile(
            query.fileName, query.documentText);

        return semanticSymbolRecordsForSymbols(
            semanticIndex->getModuleContextSymbolsByType(
                query.moduleName,
                query.fileName,
                adapterRawCollectorKind,
                query.prefix));
    }

    return semanticIndex->getCommandCompletionSymbolRecords(
        query.moduleName,
        adapterRawCollectorKind,
        query.prefix);
}

QList<SemanticSymbolRecord> CompletionSemanticQuery::typedSymbolRecords(
    SemanticIndex* semanticIndex,
    CompletionCommandKind commandKind,
    const QString& prefix)
{
    if (!semanticIndex)
        return {};

    return semanticSymbolRecordsForSymbols(
        semanticIndex->getTypedCompletionSymbols(
            rawCollectorKindForCommandKind(commandKind),
            prefix));
}

QStringList CompletionSemanticQuery::enumValueCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix,
    const QString& enumTypeName)
{
    return semanticIndex
        ? semanticIndex->getEnumValueCompletionNames(prefix, enumTypeName)
        : QStringList();
}

QString CompletionSemanticQuery::enumTypeForVariable(
    SemanticIndex* semanticIndex,
    const QString& variableName,
    const QString& moduleName)
{
    return semanticIndex
        ? semanticIndex->enumTypeForVariable(variableName, moduleName)
        : QString();
}

QStringList CompletionSemanticQuery::modulePortCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix,
    const QString& moduleTypeName)
{
    return semanticIndex
        ? semanticIndex->getModulePortCompletionNames(prefix, moduleTypeName)
        : QStringList();
}

QString CompletionSemanticQuery::currentModuleAt(
    SemanticIndex* semanticIndex,
    const QString& fileName,
    int cursorPosition)
{
    return semanticIndex
        ? semanticIndex->currentModuleAt(fileName, cursorPosition)
        : QString();
}

QString CompletionSemanticQuery::structTypeForVariable(
    SemanticIndex* semanticIndex,
    const QString& variableName,
    const QString& moduleName)
{
    return semanticIndex
        ? semanticIndex->getStructTypeForVariable(variableName, moduleName)
        : QString();
}
