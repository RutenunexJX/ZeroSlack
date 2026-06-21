#ifndef TEST_SV_SEMANTIC_FIXTURE_RECORDS_H
#define TEST_SV_SEMANTIC_FIXTURE_RECORDS_H

#include "semanticindex.h"
#include "syminfo.h"

#include <QHash>
#include <QList>
#include <QSet>

static SymbolTaxonomy::CollectorKind semanticFixtureCollectorKindForDeclaration(
    SymbolTaxonomy::DeclarationKind kind)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;

    switch (kind) {
    case DeclarationKind::Module:
        return CollectorKind::Module;
    case DeclarationKind::Interface:
        return CollectorKind::Interface;
    case DeclarationKind::Package:
        return CollectorKind::Package;
    case DeclarationKind::Typedef:
        return CollectorKind::Typedef;
    case DeclarationKind::Enum:
        return CollectorKind::Enum;
    case DeclarationKind::Parameter:
        return CollectorKind::Parameter;
    case DeclarationKind::Localparam:
        return CollectorKind::Localparam;
    case DeclarationKind::Port:
        return CollectorKind::PortInput;
    case DeclarationKind::Signal:
        return CollectorKind::Logic;
    case DeclarationKind::Struct:
        return CollectorKind::PackedStruct;
    case DeclarationKind::StructVariable:
        return CollectorKind::PackedStructVariable;
    case DeclarationKind::StructMember:
        return CollectorKind::StructMember;
    case DeclarationKind::Instance:
        return CollectorKind::Inst;
    case DeclarationKind::Modport:
        return CollectorKind::InterfaceModport;
    case DeclarationKind::Task:
        return CollectorKind::Task;
    case DeclarationKind::Function:
        return CollectorKind::Function;
    case DeclarationKind::Macro:
        return CollectorKind::DefDefine;
    case DeclarationKind::Process:
        return CollectorKind::Always;
    case DeclarationKind::Generate:
        return CollectorKind::GenerateIf;
    case DeclarationKind::Constraint:
        return CollectorKind::XilinxConstraint;
    case DeclarationKind::Unknown:
    case DeclarationKind::User:
        return CollectorKind::User;
    }
    return CollectorKind::User;
}

static SymbolTaxonomy::SymbolUsageRole semanticFixtureUsageRoleForDeclaration(
    SymbolTaxonomy::DeclarationKind kind)
{
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using SymbolUsageRole = SymbolTaxonomy::SymbolUsageRole;

    if (kind == DeclarationKind::Unknown)
        return SymbolUsageRole::Unknown;
    if (kind == DeclarationKind::Process || kind == DeclarationKind::Generate)
        return SymbolUsageRole::Process;
    return SymbolUsageRole::Declaration;
}

static SymbolTaxonomy::SymbolOwnerScope semanticFixtureOwnerScopeForDeclaration(
    SymbolTaxonomy::DeclarationKind kind)
{
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;

    switch (kind) {
    case DeclarationKind::Module:
    case DeclarationKind::Interface:
    case DeclarationKind::Package:
        return SymbolOwnerScope::Global;
    case DeclarationKind::Modport:
        return SymbolOwnerScope::Interface;
    case DeclarationKind::StructMember:
        return SymbolOwnerScope::Struct;
    default:
        return SymbolOwnerScope::Unknown;
    }
}

static SymbolTaxonomy::SymbolVisibility semanticFixtureVisibilityForOwner(
    SymbolTaxonomy::SymbolOwnerScope ownerScope)
{
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;
    using SymbolVisibility = SymbolTaxonomy::SymbolVisibility;

    switch (ownerScope) {
    case SymbolOwnerScope::Global:
        return SymbolVisibility::Global;
    case SymbolOwnerScope::Module:
        return SymbolVisibility::ScopeLocal;
    case SymbolOwnerScope::Interface:
    case SymbolOwnerScope::Struct:
        return SymbolVisibility::Member;
    case SymbolOwnerScope::Package:
        return SymbolVisibility::PackageVisible;
    case SymbolOwnerScope::Unknown:
        return SymbolVisibility::Unknown;
    }
    return SymbolVisibility::Unknown;
}

static SymbolStableKey semanticFixtureStableKey(
    const QString& fileName,
    const QString& symbolName,
    SymbolTaxonomy::DeclarationKind declarationKind,
    const QString& ownerName = QString())
{
    SymbolStableKey key;
    key.fileName = fileName;
    key.symbolName = symbolName;
    key.declarationKind = declarationKind;
    key.ownerScope = ownerName;
    return key;
}

static SymbolTaxonomy::SemanticMetadata semanticFixtureMetadata(
    SymbolTaxonomy::DeclarationKind declarationKind,
    SymbolTaxonomy::SymbolOwnerScope ownerScope =
        SymbolTaxonomy::SymbolOwnerScope::Unknown,
    SymbolTaxonomy::CollectorKind collectorKind =
        SymbolTaxonomy::CollectorKind::User,
    SymbolTaxonomy::SourceRole sourceRole =
        SymbolTaxonomy::SourceRole::DesignSource)
{
    SymbolTaxonomy::SemanticMetadata metadata;
    metadata.declarationKind = declarationKind;
    metadata.usageRole = semanticFixtureUsageRoleForDeclaration(declarationKind);
    metadata.ownerScope = ownerScope;
    metadata.visibility = semanticFixtureVisibilityForOwner(ownerScope);
    metadata.sourceRole = sourceRole;
    metadata.collectorKind = collectorKind;
    metadata.interfaceLikeOwner =
        ownerScope == SymbolTaxonomy::SymbolOwnerScope::Interface;
    return metadata;
}

class SemanticFixtureRecordBuilder
{
public:
    explicit SemanticFixtureRecordBuilder(
        const QString& name,
        SymbolTaxonomy::DeclarationKind declarationKind =
            SymbolTaxonomy::DeclarationKind::Unknown)
    {
        m_record.name = name;
        m_record.location.fileName = QStringLiteral("fixture.sv");
        m_record.location.startLine = 1;
        m_record.location.startColumn = 1;
        m_record.location.endLine = 1;
        m_record.location.endColumn = 1;
        m_record.location.length = name.size();
        m_record.declarationKind = declarationKind;
        m_record.usageRole =
            semanticFixtureUsageRoleForDeclaration(declarationKind);
        m_record.owner.kind =
            semanticFixtureOwnerScopeForDeclaration(declarationKind);
        m_record.visibility =
            semanticFixtureVisibilityForOwner(m_record.owner.kind);
        m_record.sourceRole =
            SymbolTaxonomy::sourceRoleForFileName(m_record.location.fileName);
        m_record.collectorKind =
            semanticFixtureCollectorKindForDeclaration(declarationKind);
        updateStableKey();
    }

    SemanticFixtureRecordBuilder& withFile(const QString& fileName)
    {
        m_record.location.fileName = fileName;
        m_record.sourceRole = SymbolTaxonomy::sourceRoleForFileName(fileName);
        updateStableKey();
        return *this;
    }

    SemanticFixtureRecordBuilder& withLocalHandle(int localHandle)
    {
        m_record.localHandle = localHandle;
        return *this;
    }

    SemanticFixtureRecordBuilder& withLine(int line, int column = 1)
    {
        m_record.location.startLine = line;
        m_record.location.startColumn = column;
        m_record.location.endLine = line;
        m_record.location.endColumn = column;
        return *this;
    }

    SemanticFixtureRecordBuilder& withRange(int startLine,
                                            int startColumn,
                                            int endLine,
                                            int endColumn)
    {
        m_record.location.startLine = startLine;
        m_record.location.startColumn = startColumn;
        m_record.location.endLine = endLine;
        m_record.location.endColumn = endColumn;
        return *this;
    }

    SemanticFixtureRecordBuilder& withTextSpan(int position, int length)
    {
        m_record.location.position = position;
        m_record.location.length = length;
        return *this;
    }

    SemanticFixtureRecordBuilder& withMetadata(
        const SymbolTaxonomy::SemanticMetadata& metadata)
    {
        m_record.declarationKind = metadata.declarationKind;
        m_record.usageRole = metadata.usageRole;
        m_record.visibility = metadata.visibility;
        m_record.sourceRole = metadata.sourceRole;
        m_record.collectorKind = metadata.collectorKind;
        m_record.owner.kind = metadata.ownerScope;
        m_record.owner.interfaceLike = metadata.interfaceLikeOwner;
        updateStableKey();
        return *this;
    }

    SemanticFixtureRecordBuilder& withCollectorKind(
        SymbolTaxonomy::CollectorKind collectorKind)
    {
        m_record.collectorKind = collectorKind;
        return *this;
    }

    SemanticFixtureRecordBuilder& withUsageRole(
        SymbolTaxonomy::SymbolUsageRole usageRole)
    {
        m_record.usageRole = usageRole;
        return *this;
    }

    SemanticFixtureRecordBuilder& withVisibility(
        SymbolTaxonomy::SymbolVisibility visibility)
    {
        m_record.visibility = visibility;
        return *this;
    }

    SemanticFixtureRecordBuilder& withSourceRole(
        SymbolTaxonomy::SourceRole sourceRole)
    {
        m_record.sourceRole = sourceRole;
        return *this;
    }

    SemanticFixtureRecordBuilder& withOwner(
        SymbolTaxonomy::SymbolOwnerScope ownerScope,
        const QString& ownerName = QString(),
        const SymbolStableKey& ownerStableKey = {},
        bool interfaceLike = false)
    {
        m_record.owner.kind = ownerScope;
        m_record.owner.name = ownerName;
        m_record.owner.stableKey = ownerStableKey;
        m_record.owner.interfaceLike = interfaceLike;
        m_record.visibility = semanticFixtureVisibilityForOwner(ownerScope);
        updateStableKey();
        return *this;
    }

    SemanticFixtureRecordBuilder& inModule(const QString& moduleName)
    {
        return withOwner(SymbolTaxonomy::SymbolOwnerScope::Module, moduleName);
    }

    SemanticFixtureRecordBuilder& inPackage(const QString& packageName)
    {
        return withOwner(SymbolTaxonomy::SymbolOwnerScope::Package,
                         packageName);
    }

    SemanticFixtureRecordBuilder& inInterface(const QString& interfaceName)
    {
        return withOwner(SymbolTaxonomy::SymbolOwnerScope::Interface,
                         interfaceName,
                         {},
                         true);
    }

    SemanticFixtureRecordBuilder& inStruct(const QString& structName)
    {
        return withOwner(SymbolTaxonomy::SymbolOwnerScope::Struct, structName);
    }

    SemanticFixtureRecordBuilder& withType(
        const QString& rawTypeText,
        const QString& resolvedTypeName = QString(),
        SymbolTaxonomy::DeclarationKind resolvedTypeKind =
            SymbolTaxonomy::DeclarationKind::Unknown,
        const QString& modportName = QString(),
        const SymbolStableKey& typeStableKey = {})
    {
        m_record.type.rawTypeText = rawTypeText;
        m_record.type.resolvedTypeName = resolvedTypeName;
        m_record.type.resolvedTypeKind = resolvedTypeKind;
        m_record.type.modportName = modportName;
        m_record.type.stableKey = typeStableKey;
        return *this;
    }

    SemanticSymbolRecord record() const
    {
        return m_record;
    }

private:
    void updateStableKey()
    {
        m_record.stableKey = semanticFixtureStableKey(
            m_record.location.fileName,
            m_record.name,
            m_record.declarationKind,
            m_record.owner.name);
    }

    SemanticSymbolRecord m_record;
};

static SemanticRelationship semanticFixtureRelationship(
    const SemanticSymbolRecord& fromRecord,
    const SemanticSymbolRecord& toRecord,
    SymbolRelationshipEngine::RelationType type,
    RelationshipProvenance provenance = RelationshipProvenance::Inferred,
    int confidence = 100,
    const QString& evidenceText = QString())
{
    SemanticRelationship relationship;
    relationship.fromId = fromRecord.localHandle;
    relationship.toId = toRecord.localHandle;
    relationship.type = type;
    relationship.fromStableKey = fromRecord.stableKey;
    relationship.toStableKey = toRecord.stableKey;
    relationship.provenance = provenance;
    relationship.confidence = confidence;
    relationship.evidenceText = evidenceText;
    return relationship;
}

static SymbolTaxonomy::CollectorKind collectorKindForFixtureType(sym_list::sym_type_e type)
{
    return static_cast<SymbolTaxonomy::CollectorKind>(type);
}

static SymbolTaxonomy::DeclarationKind declarationKindForFixtureType(sym_list::sym_type_e type)
{
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;

    switch (type) {
    case sym_list::sym_module:
        return DeclarationKind::Module;
    case sym_list::sym_interface:
    case sym_list::sym_interface_assco_struct:
        return DeclarationKind::Interface;
    case sym_list::sym_package:
        return DeclarationKind::Package;
    case sym_list::sym_typedef:
        return DeclarationKind::Typedef;
    case sym_list::sym_enum:
    case sym_list::sym_enum_var:
    case sym_list::sym_enum_value:
        return DeclarationKind::Enum;
    case sym_list::sym_parameter:
    case sym_list::sym_module_parameter:
    case sym_list::sym_interface_parameter:
    case sym_list::sym_def_parameter:
        return DeclarationKind::Parameter;
    case sym_list::sym_localparam:
        return DeclarationKind::Localparam;
    case sym_list::sym_port_input:
    case sym_list::sym_port_output:
    case sym_list::sym_port_inout:
    case sym_list::sym_port_ref:
    case sym_list::sym_port_interface:
    case sym_list::sym_port_interface_modport:
        return DeclarationKind::Port;
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:
        return DeclarationKind::Signal;
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
        return DeclarationKind::Struct;
    case sym_list::sym_packed_struct_var:
    case sym_list::sym_unpacked_struct_var:
        return DeclarationKind::StructVariable;
    case sym_list::sym_struct_member:
        return DeclarationKind::StructMember;
    case sym_list::sym_inst:
    case sym_list::sym_inst_pin:
        return DeclarationKind::Instance;
    case sym_list::sym_interface_modport:
        return DeclarationKind::Modport;
    case sym_list::sym_task:
        return DeclarationKind::Task;
    case sym_list::sym_function:
        return DeclarationKind::Function;
    case sym_list::sym_def_define:
    case sym_list::sym_def_ifdef:
    case sym_list::sym_def_ifndef:
    case sym_list::sym_def_else:
    case sym_list::sym_def_elsif:
    case sym_list::sym_def_endif:
        return DeclarationKind::Macro;
    case sym_list::sym_always:
    case sym_list::sym_always_ff:
    case sym_list::sym_always_comb:
    case sym_list::sym_always_latch:
    case sym_list::sym_assign:
    case sym_list::sym_initial:
    case sym_list::sym_case:
    case sym_list::sym_casex:
    case sym_list::sym_casez:
    case sym_list::sym_endcase:
    case sym_list::sym_case_default:
    case sym_list::sym_fsm_state:
        return DeclarationKind::Process;
    case sym_list::sym_generate_if:
    case sym_list::sym_generate_for:
    case sym_list::sym_generate_case:
        return DeclarationKind::Generate;
    case sym_list::sym_xilinx_constraint:
        return DeclarationKind::Constraint;
    case sym_list::sym_user:
        return DeclarationKind::User;
    }
    return DeclarationKind::Unknown;
}

static SymbolTaxonomy::SymbolUsageRole usageRoleForFixtureType(sym_list::sym_type_e type)
{
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using SymbolUsageRole = SymbolTaxonomy::SymbolUsageRole;

    switch (declarationKindForFixtureType(type)) {
    case DeclarationKind::Unknown:
        return SymbolUsageRole::Unknown;
    case DeclarationKind::Process:
    case DeclarationKind::Generate:
        return SymbolUsageRole::Process;
    case DeclarationKind::Instance:
        return type == sym_list::sym_inst_pin
            ? SymbolUsageRole::Reference
            : SymbolUsageRole::Declaration;
    case DeclarationKind::Module:
    case DeclarationKind::Interface:
    case DeclarationKind::Package:
    case DeclarationKind::Typedef:
    case DeclarationKind::Enum:
    case DeclarationKind::Parameter:
    case DeclarationKind::Localparam:
    case DeclarationKind::Port:
    case DeclarationKind::Signal:
    case DeclarationKind::Struct:
    case DeclarationKind::StructVariable:
    case DeclarationKind::StructMember:
    case DeclarationKind::Modport:
    case DeclarationKind::Task:
    case DeclarationKind::Function:
    case DeclarationKind::Macro:
    case DeclarationKind::Constraint:
    case DeclarationKind::User:
        return SymbolUsageRole::Declaration;
    }
    return SymbolUsageRole::Unknown;
}

static bool fixtureTypeIsGlobalDefinition(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_interface
        || type == sym_list::sym_package;
}

static bool fixtureTypeIsPackageVisibleDefinition(sym_list::sym_type_e type)
{
    switch (type) {
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
    case sym_list::sym_typedef:
    case sym_list::sym_enum:
    case sym_list::sym_enum_value:
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
        return true;
    default:
        return false;
    }
}

static bool fixtureTypeHasInterfaceLikeOwner(sym_list::sym_type_e type)
{
    return type == sym_list::sym_interface
        || type == sym_list::sym_inst
        || type == sym_list::sym_port_interface
        || type == sym_list::sym_port_interface_modport;
}

static SemanticSymbolTypeReference fixtureTypeReferenceForSymbol(
    const sym_list::SymbolInfo& symbol)
{
    SemanticSymbolTypeReference type;
    type.rawTypeText = symbol.dataType;
    if (symbol.dataType.isEmpty())
        return type;

    const QStringList parts = symbol.dataType.split(QLatin1Char('.'));
    type.resolvedTypeName = parts.value(0);
    if (parts.size() > 1)
        type.modportName = parts.value(1);
    if (symbol.symbolType == sym_list::sym_inst
        || symbol.symbolType == sym_list::sym_port_interface
        || symbol.symbolType == sym_list::sym_port_interface_modport) {
        type.resolvedTypeKind = SymbolTaxonomy::DeclarationKind::Interface;
    }
    return type;
}

static SymbolTaxonomy::SymbolOwnerScope ownerScopeForFixtureSymbol(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {})
{
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;

    const SymbolTaxonomy::DeclarationKind kind =
        declarationKindForFixtureType(symbol.symbolType);
    if (fixtureTypeIsGlobalDefinition(symbol.symbolType))
        return SymbolOwnerScope::Global;
    if (kind == SymbolTaxonomy::DeclarationKind::Modport)
        return SymbolOwnerScope::Interface;
    if (kind == SymbolTaxonomy::DeclarationKind::StructMember)
        return SymbolOwnerScope::Struct;
    if (!symbol.moduleScope.isEmpty()) {
        if (packageScopes.contains(symbol.moduleScope)
            && fixtureTypeIsPackageVisibleDefinition(symbol.symbolType)) {
            return SymbolOwnerScope::Package;
        }
        return SymbolOwnerScope::Module;
    }
    return SymbolOwnerScope::Unknown;
}

static SymbolTaxonomy::SymbolVisibility visibilityForFixtureSymbol(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {})
{
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;
    using SymbolVisibility = SymbolTaxonomy::SymbolVisibility;

    const SymbolOwnerScope scope = ownerScopeForFixtureSymbol(symbol, packageScopes);
    if (scope == SymbolOwnerScope::Global)
        return SymbolVisibility::Global;
    if (scope == SymbolOwnerScope::Package
        && fixtureTypeIsPackageVisibleDefinition(symbol.symbolType)) {
        return SymbolVisibility::PackageVisible;
    }
    if (scope == SymbolOwnerScope::Interface
        || scope == SymbolOwnerScope::Struct) {
        return SymbolVisibility::Member;
    }
    if (scope == SymbolOwnerScope::Module)
        return SymbolVisibility::ScopeLocal;
    return SymbolVisibility::Unknown;
}

static SymbolTaxonomy::SemanticMetadata semanticMetadataForSymbolInfo(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {})
{
    if (symbol.hasSemanticMetadata) {
        SymbolTaxonomy::SemanticMetadata metadata;
        metadata.declarationKind = symbol.semanticDeclarationKind;
        metadata.usageRole = symbol.semanticUsageRole;
        metadata.ownerScope = symbol.semanticOwnerScope;
        metadata.visibility = symbol.semanticVisibility;
        metadata.sourceRole = symbol.semanticSourceRole;
        metadata.collectorKind = collectorKindForFixtureType(symbol.collectorKind);
        metadata.interfaceLikeOwner = symbol.interfaceLikeOwner;
        return metadata;
    }

    SymbolTaxonomy::SemanticMetadata metadata;
    metadata.declarationKind = declarationKindForFixtureType(symbol.symbolType);
    metadata.usageRole = usageRoleForFixtureType(symbol.symbolType);
    metadata.ownerScope = ownerScopeForFixtureSymbol(symbol, packageScopes);
    metadata.visibility = visibilityForFixtureSymbol(symbol, packageScopes);
    metadata.sourceRole = SymbolTaxonomy::sourceRoleForFileName(symbol.fileName);
    metadata.collectorKind = collectorKindForFixtureType(symbol.symbolType);
    metadata.interfaceLikeOwner = fixtureTypeHasInterfaceLikeOwner(symbol.symbolType);
    return metadata;
}

static SymbolTaxonomy::SemanticMetadata semanticMetadataForFixtureType(
    sym_list::sym_type_e type)
{
    sym_list::SymbolInfo symbol;
    symbol.symbolType = type;
    return semanticMetadataForSymbolInfo(symbol);
}

static SemanticSymbolRecord semanticSymbolRecordForSymbol(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {})
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolInfo(symbol, packageScopes);

    SemanticSymbolRecord record;
    record.localHandle = symbol.symbolId;
    record.name = symbol.symbolName;
    record.location.fileName = symbol.fileName;
    record.location.startLine = symbol.startLine;
    record.location.startColumn = symbol.startColumn;
    record.location.endLine = symbol.endLine;
    record.location.endColumn = symbol.endColumn;
    record.location.position = symbol.position;
    record.location.length = symbol.length;
    record.declarationKind = metadata.declarationKind;
    record.usageRole = metadata.usageRole;
    record.visibility = metadata.visibility;
    record.sourceRole = metadata.sourceRole;
    record.collectorKind = metadata.collectorKind;
    record.owner.kind = metadata.ownerScope;
    record.owner.name = symbol.moduleScope;
    record.owner.interfaceLike = metadata.interfaceLikeOwner;
    record.type = fixtureTypeReferenceForSymbol(symbol);

    record.stableKey.fileName = record.location.fileName;
    record.stableKey.symbolName = record.name;
    record.stableKey.declarationKind = record.declarationKind;
    record.stableKey.ownerScope = record.owner.name;
    return record;
}

static sym_list::SymbolInfo fixtureSymbolInfoForRecord(
    const SemanticSymbolRecord& record)
{
    sym_list::SymbolInfo symbol;
    symbol.fileName = record.location.fileName;
    symbol.symbolName = record.name;
    symbol.symbolType =
        static_cast<sym_list::sym_type_e>(record.collectorKind);
    symbol.startLine = record.location.startLine;
    symbol.startColumn = record.location.startColumn;
    symbol.endLine = record.location.endLine;
    symbol.endColumn = record.location.endColumn;
    symbol.position = record.location.position;
    symbol.length = record.location.length;
    symbol.symbolId = record.localHandle;
    symbol.moduleScope = record.owner.name;
    symbol.dataType = record.type.rawTypeText;
    symbol.hasSemanticMetadata = true;
    symbol.semanticDeclarationKind = record.declarationKind;
    symbol.semanticUsageRole = record.usageRole;
    symbol.semanticOwnerScope = record.owner.kind;
    symbol.semanticVisibility = record.visibility;
    symbol.semanticSourceRole = record.sourceRole;
    symbol.collectorKind =
        static_cast<sym_list::sym_type_e>(record.collectorKind);
    symbol.interfaceLikeOwner = record.owner.interfaceLike;
    return symbol;
}

static QList<sym_list::SymbolInfo> fixtureSymbolsForRecords(
    const QList<SemanticSymbolRecord>& records)
{
    QList<sym_list::SymbolInfo> symbols;
    symbols.reserve(records.size());
    for (const SemanticSymbolRecord& record : records) {
        if (record.isValid())
            symbols.append(fixtureSymbolInfoForRecord(record));
    }
    return symbols;
}

static QList<SemanticSymbolRecord> semanticSymbolRecordsForSymbols(
    const QList<sym_list::SymbolInfo>& symbols,
    const QSet<QString>& packageScopes = {})
{
    QList<SemanticSymbolRecord> records;
    records.reserve(symbols.size());
    for (const sym_list::SymbolInfo& symbol : symbols)
        records.append(semanticSymbolRecordForSymbol(symbol, packageScopes));
    return records;
}

static QList<SemanticSymbolRecord> semanticSymbolRecordsForDatabase(
    const sym_list* db,
    const QString& fileName = QString(),
    const QSet<QString>& packageScopes = {})
{
    if (!db)
        return {};
    return semanticSymbolRecordsForSymbols(db->getAllSymbols(fileName),
                                           packageScopes);
}

static void importFixtureSymbolsIntoSemanticIndex(SemanticIndex& index,
                                                  const sym_list* db)
{
    if (!db)
        return;

    QHash<QString, QList<sym_list::SymbolInfo>> symbolsByFile;
    for (const sym_list::SymbolInfo& symbol : db->getAllSymbols())
        symbolsByFile[symbol.fileName].append(symbol);

    for (auto it = symbolsByFile.cbegin(); it != symbolsByFile.cend(); ++it) {
        index.updateSymbolRecordsForFile(
            it.key(),
            semanticSymbolRecordsForSymbols(it.value()),
            db->getCachedFileContent(it.key()));
    }
}

static SemanticIndex semanticIndexFromFixtureDatabase(const sym_list* db)
{
    SemanticIndex index;
    importFixtureSymbolsIntoSemanticIndex(index, db);
    return index;
}

#endif // TEST_SV_SEMANTIC_FIXTURE_RECORDS_H
