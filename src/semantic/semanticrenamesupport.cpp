#include "semanticrenamesupport.h"

#include "saferenameservice.h"

namespace {
using DeclarationKind = SymbolTaxonomy::DeclarationKind;
using CollectorKind = SymbolTaxonomy::CollectorKind;

bool supportedSignalCollector(CollectorKind kind)
{
    switch (kind) {
    case CollectorKind::Wire:
    case CollectorKind::Reg:
    case CollectorKind::Logic:
    case CollectorKind::EnumVariable:
    case CollectorKind::PackedStructVariable:
    case CollectorKind::UnpackedStructVariable:
    case CollectorKind::User:
        return true;
    default:
        return false;
    }
}

QString collectorLabel(CollectorKind kind)
{
    switch (kind) {
    case CollectorKind::Wire:
        return QStringLiteral("wire");
    case CollectorKind::Reg:
        return QStringLiteral("reg");
    case CollectorKind::Logic:
        return QStringLiteral("logic");
    case CollectorKind::Enum:
        return QStringLiteral("enum type");
    case CollectorKind::EnumVariable:
        return QStringLiteral("enum variable");
    case CollectorKind::EnumValue:
        return QStringLiteral("enum value");
    case CollectorKind::PackedStruct:
        return QStringLiteral("packed struct type");
    case CollectorKind::UnpackedStruct:
        return QStringLiteral("struct type");
    case CollectorKind::PackedStructVariable:
        return QStringLiteral("packed struct variable");
    case CollectorKind::UnpackedStructVariable:
        return QStringLiteral("struct variable");
    case CollectorKind::StructMember:
        return QStringLiteral("struct member");
    case CollectorKind::Typedef:
        return QStringLiteral("typedef type");
    default:
        return QString();
    }
}
}

bool isSupportedSemanticRenameSubject(
    const SemanticSymbolRecord& record)
{
    if (!record.isValid() || !record.stableKey.isValid())
        return false;
    switch (record.declarationKind) {
    case DeclarationKind::Port:
    case DeclarationKind::Parameter:
    case DeclarationKind::Localparam:
    case DeclarationKind::Typedef:
    case DeclarationKind::Enum:
    case DeclarationKind::Struct:
    case DeclarationKind::StructVariable:
    case DeclarationKind::StructMember:
        return true;
    case DeclarationKind::Signal:
        return supportedSignalCollector(record.collectorKind);
    default:
        return false;
    }
}

bool isStructuralSemanticRenameSubject(
    const SemanticSymbolRecord& record)
{
    const bool structuredKind =
        record.declarationKind == DeclarationKind::Port
        || record.declarationKind == DeclarationKind::Parameter
        || record.declarationKind == DeclarationKind::Localparam;
    const bool structuredOwner =
        record.owner.kind
            == SymbolTaxonomy::SymbolOwnerScope::Module
        || record.owner.kind
            == SymbolTaxonomy::SymbolOwnerScope::Interface;
    return structuredKind && structuredOwner;
}

QString semanticRenameKindLabel(
    const SemanticSymbolRecord& record)
{
    const QString specific = collectorLabel(record.collectorKind);
    if (!specific.isEmpty())
        return specific;
    switch (record.declarationKind) {
    case DeclarationKind::Port:
        return QStringLiteral("port");
    case DeclarationKind::Parameter:
        return QStringLiteral("parameter");
    case DeclarationKind::Localparam:
        return QStringLiteral("localparam");
    case DeclarationKind::Typedef:
        return QStringLiteral("typedef type");
    case DeclarationKind::Enum:
        return QStringLiteral("enum");
    case DeclarationKind::Struct:
        return QStringLiteral("struct type");
    case DeclarationKind::StructVariable:
        return QStringLiteral("struct variable");
    case DeclarationKind::StructMember:
        return QStringLiteral("struct member");
    case DeclarationKind::Signal:
        return QStringLiteral("signal");
    default:
        return QStringLiteral("symbol");
    }
}

QString semanticRenameOwnerLabel(
    const SemanticSymbolRecord& record)
{
    return record.owner.name.isEmpty()
        ? QStringLiteral("global")
        : record.owner.name;
}

QString semanticRenameInlineValidation(
    SemanticIndex* index,
    const SemanticSymbolRecord& subject,
    const QString& newName)
{
    const QString trimmed = newName.trimmed();
    if (!SafeRenameService::isValidIdentifier(trimmed)) {
        return QStringLiteral(
            "Enter a valid SystemVerilog identifier.");
    }
    if (trimmed == subject.name)
        return QStringLiteral("The name is unchanged.");
    if (!index)
        return QStringLiteral("The semantic snapshot is unavailable.");

    for (const SemanticSymbolRecord& record :
         index->getSymbolRecordsByName(trimmed)) {
        if (!record.stableKey.isValid()
            || record.stableKey == subject.stableKey) {
            continue;
        }
        if (record.owner.kind == subject.owner.kind
            && record.owner.name == subject.owner.name
            && SymbolTaxonomy::isDefinitionCandidate(
                semanticMetadataForSymbolRecord(record))) {
            return QStringLiteral(
                "The name conflicts with a declaration in this scope.");
        }
    }
    return QString();
}
