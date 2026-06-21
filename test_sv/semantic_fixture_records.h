#ifndef TEST_SV_SEMANTIC_FIXTURE_RECORDS_H
#define TEST_SV_SEMANTIC_FIXTURE_RECORDS_H

#include "semanticindex.h"

#include <QList>

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

#endif // TEST_SV_SEMANTIC_FIXTURE_RECORDS_H
