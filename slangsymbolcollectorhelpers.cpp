#include "slangsymbolcollectorhelpers.h"

#include <slang/ast/Scope.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/SubroutineSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/syntax/SyntaxNode.h>
#include <slang/text/SourceManager.h>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <string>
#include <utility>

namespace slang_symbols::detail {

namespace {
struct SourcePositionMap {
    QVector<size_t> adjustmentOffsets;
    QVector<int> cumulativeAdjustments;
    QVector<size_t> lineStartOffsets{0};
    QVector<int> lineStartPositions{0};

    int positionAt(size_t byteOffset) const
    {
        const auto adjustment = std::upper_bound(
            adjustmentOffsets.cbegin(),
            adjustmentOffsets.cend(),
            byteOffset);
        const qsizetype index = std::distance(
            adjustmentOffsets.cbegin(), adjustment);
        const int delta = index > 0
            ? cumulativeAdjustments.at(index - 1) : 0;
        return static_cast<int>(byteOffset) + delta;
    }

    int lineStartAt(size_t byteOffset) const
    {
        const auto line = std::upper_bound(
            lineStartOffsets.cbegin(),
            lineStartOffsets.cend(),
            byteOffset);
        const qsizetype index = std::distance(
            lineStartOffsets.cbegin(), line);
        return lineStartPositions.at(qMax<qsizetype>(0, index - 1));
    }
};

struct SourcePositionCache {
    const slang::SourceManager* sourceManager = nullptr;
    QHash<uint32_t, SourcePositionMap> maps;
};

SourcePositionCache*& sourcePositionCacheSlot()
{
    // MinGW destroys C++ thread_local objects after Qt has already torn down
    // parts of its TLS state. A thread_local QHash therefore dereferenced
    // 0xfeeefeee when a QtConcurrent analysis thread exited. Keep only a
    // trivially destructible pointer in TLS and destroy the Qt container while
    // the worker function and Qt runtime are still alive.
    thread_local SourcePositionCache* cache = nullptr;
    return cache;
}

SourcePositionCache& sourcePositionCache()
{
    SourcePositionCache*& cache = sourcePositionCacheSlot();
    if (!cache)
        cache = new SourcePositionCache;
    return *cache;
}

bool continuationByte(unsigned char byte)
{
    return (byte & 0xc0u) == 0x80u;
}

SourcePositionMap buildSourcePositionMap(std::string_view source)
{
    SourcePositionMap result;
    size_t byte = 0;
    int cumulativeAdjustment = 0;
    while (byte < source.size()) {
        const unsigned char lead =
            static_cast<unsigned char>(source[byte]);

        if (lead == '\r') {
            if (byte + 1 < source.size() && source[byte + 1] == '\n') {
                byte += 2;
                --cumulativeAdjustment;
                result.adjustmentOffsets.append(byte);
                result.cumulativeAdjustments.append(
                    cumulativeAdjustment);
            } else {
                ++byte;
            }
            result.lineStartOffsets.append(byte);
            result.lineStartPositions.append(
                static_cast<int>(byte) + cumulativeAdjustment);
            continue;
        }
        if (lead == '\n') {
            ++byte;
            result.lineStartOffsets.append(byte);
            result.lineStartPositions.append(
                static_cast<int>(byte) + cumulativeAdjustment);
            continue;
        }

        size_t byteCount = 1;
        int utf16Units = 1;
        if (lead >= 0xc2u && lead <= 0xdfu
            && byte + 1 < source.size()
            && continuationByte(
                static_cast<unsigned char>(source[byte + 1]))) {
            byteCount = 2;
        } else if (lead >= 0xe0u && lead <= 0xefu
                   && byte + 2 < source.size()
                   && continuationByte(static_cast<unsigned char>(
                       source[byte + 1]))
                   && continuationByte(static_cast<unsigned char>(
                       source[byte + 2]))) {
            byteCount = 3;
        } else if (lead >= 0xf0u && lead <= 0xf4u
                   && byte + 3 < source.size()
                   && continuationByte(static_cast<unsigned char>(
                       source[byte + 1]))
                   && continuationByte(static_cast<unsigned char>(
                       source[byte + 2]))
                   && continuationByte(static_cast<unsigned char>(
                       source[byte + 3]))) {
            byteCount = 4;
            utf16Units = 2;
        }

        byte += byteCount;
        const int adjustment = utf16Units
            - static_cast<int>(byteCount);
        if (adjustment != 0) {
            cumulativeAdjustment += adjustment;
            result.adjustmentOffsets.append(byte);
            result.cumulativeAdjustments.append(cumulativeAdjustment);
        }
    }
    return result;
}
}

void resetQTextDocumentSourcePositionCache(
    const slang::SourceManager* sourceManager)
{
    SourcePositionCache*& slot = sourcePositionCacheSlot();
    if (!sourceManager) {
        delete slot;
        slot = nullptr;
        return;
    }

    SourcePositionCache& cache = sourcePositionCache();
    cache.sourceManager = sourceManager;
    cache.maps.clear();
}

QString sourceIdentityFileName(
    const slang::SourceManager* sourceManager,
    slang::SourceLocation location)
{
    if (!sourceManager || !location.valid())
        return QString();

    const slang::SourceLocation fileLocation =
        sourceManager->getFullyExpandedLoc(location);
    if (!fileLocation.valid())
        return QString();

    const std::filesystem::path& physicalPath =
        sourceManager->getFullPath(fileLocation.buffer());
    QString fileName;
    // assignText() gives synthetic buffers names such as
    // <unnamed_buffer0>; these are not physical identities. In that case the
    // logical name supplied to Slang (often the editor document path) is the
    // only stable source identity.
    if (!physicalPath.empty() && physicalPath.is_absolute()) {
#ifdef Q_OS_WIN
        fileName = QString::fromStdWString(physicalPath.wstring());
#else
        fileName = QString::fromStdString(physicalPath.string());
#endif
    } else {
        fileName = QString::fromStdString(std::string(
            sourceManager->getFileName(fileLocation)));
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(fileName));
}

QTextDocumentSourcePosition qTextDocumentSourcePosition(
    const slang::SourceManager* sourceManager,
    slang::SourceLocation location)
{
    QTextDocumentSourcePosition result;
    if (!sourceManager || !location.valid())
        return result;

    const slang::SourceLocation fileLocation =
        sourceManager->getFullyExpandedLoc(location);
    if (!fileLocation.valid())
        return result;

    SourcePositionCache& cache = sourcePositionCache();
    if (cache.sourceManager != sourceManager)
        resetQTextDocumentSourcePositionCache(sourceManager);
    const std::string_view source =
        sourceManager->getSourceText(fileLocation.buffer());
    const size_t byteOffset = std::min(fileLocation.offset(), source.size());
    const uint32_t bufferId = fileLocation.buffer().getId();
    auto map = cache.maps.find(bufferId);
    if (map == cache.maps.end()) {
        map = cache.maps.insert(
            bufferId, buildSourcePositionMap(source));
    }

    result.fileName = sourceIdentityFileName(sourceManager, fileLocation);
    result.position = map->positionAt(byteOffset);
    const size_t line = sourceManager->getLineNumber(fileLocation);
    result.line = line == 0 ? 1 : static_cast<int>(line);
    result.column = result.position - map->lineStartAt(byteOffset) + 1;
    return result;
}

namespace {

QString normalizedStableKeyFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

SymbolTaxonomy::DeclarationKind declarationKindForRawKind(
    SymbolTaxonomy::CollectorKind rawKind)
{
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    switch (rawKind) {
    case CollectorKind::Module:
        return DeclarationKind::Module;
    case CollectorKind::Interface:
    case CollectorKind::InterfaceAssocStruct:
        return DeclarationKind::Interface;
    case CollectorKind::Package:
        return DeclarationKind::Package;
    case CollectorKind::Typedef:
        return DeclarationKind::Typedef;
    case CollectorKind::Enum:
    case CollectorKind::EnumVariable:
    case CollectorKind::EnumValue:
        return DeclarationKind::Enum;
    case CollectorKind::Parameter:
    case CollectorKind::ModuleParameter:
    case CollectorKind::InterfaceParameter:
    case CollectorKind::DefParameter:
        return DeclarationKind::Parameter;
    case CollectorKind::Localparam:
        return DeclarationKind::Localparam;
    case CollectorKind::PortInput:
    case CollectorKind::PortOutput:
    case CollectorKind::PortInout:
    case CollectorKind::PortRef:
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
        return DeclarationKind::Port;
    case CollectorKind::Reg:
    case CollectorKind::Wire:
    case CollectorKind::Logic:
        return DeclarationKind::Signal;
    case CollectorKind::PackedStruct:
    case CollectorKind::UnpackedStruct:
        return DeclarationKind::Struct;
    case CollectorKind::PackedStructVariable:
    case CollectorKind::UnpackedStructVariable:
        return DeclarationKind::StructVariable;
    case CollectorKind::StructMember:
        return DeclarationKind::StructMember;
    case CollectorKind::Inst:
    case CollectorKind::InstPin:
        return DeclarationKind::Instance;
    case CollectorKind::InterfaceModport:
        return DeclarationKind::Modport;
    case CollectorKind::Task:
        return DeclarationKind::Task;
    case CollectorKind::Function:
        return DeclarationKind::Function;
    case CollectorKind::DefDefine:
    case CollectorKind::DefIfdef:
    case CollectorKind::DefIfndef:
    case CollectorKind::DefElse:
    case CollectorKind::DefElsif:
    case CollectorKind::DefEndif:
        return DeclarationKind::Macro;
    case CollectorKind::Always:
    case CollectorKind::AlwaysFf:
    case CollectorKind::AlwaysComb:
    case CollectorKind::AlwaysLatch:
    case CollectorKind::Assign:
    case CollectorKind::Initial:
    case CollectorKind::Case:
    case CollectorKind::Casex:
    case CollectorKind::Casez:
    case CollectorKind::Endcase:
    case CollectorKind::CaseDefault:
    case CollectorKind::FsmState:
        return DeclarationKind::Process;
    case CollectorKind::GenerateIf:
    case CollectorKind::GenerateFor:
    case CollectorKind::GenerateCase:
        return DeclarationKind::Generate;
    case CollectorKind::XilinxConstraint:
        return DeclarationKind::Constraint;
    case CollectorKind::User:
        return DeclarationKind::User;
    }
    return DeclarationKind::Unknown;
}

SymbolTaxonomy::SymbolUsageRole usageRoleForRawKind(
    SymbolTaxonomy::CollectorKind rawKind)
{
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using SymbolUsageRole = SymbolTaxonomy::SymbolUsageRole;

    const DeclarationKind declarationKind = declarationKindForRawKind(rawKind);
    if (declarationKind == DeclarationKind::Process
        || declarationKind == DeclarationKind::Generate) {
        return SymbolUsageRole::Process;
    }
    if (declarationKind == DeclarationKind::Instance
        && rawKind == CollectorKind::InstPin) {
        return SymbolUsageRole::Reference;
    }
    if (declarationKind == DeclarationKind::Unknown)
        return SymbolUsageRole::Unknown;
    return SymbolUsageRole::Declaration;
}

bool isGlobalDefinition(const SemanticSymbolRecord& record)
{
    return record.declarationKind == SymbolTaxonomy::DeclarationKind::Module
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Interface
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Package;
}

bool isPackageVisibleDefinition(const SemanticSymbolRecord& record)
{
    return record.declarationKind == SymbolTaxonomy::DeclarationKind::Parameter
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Localparam
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Typedef
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Enum
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Struct;
}

bool hasInterfaceLikeOwner(SymbolTaxonomy::CollectorKind rawKind)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    return rawKind == CollectorKind::Interface
        || rawKind == CollectorKind::Inst
        || rawKind == CollectorKind::PortInterface
        || rawKind == CollectorKind::PortInterfaceModport;
}

SymbolTaxonomy::SymbolOwnerScope ownerScopeForRecord(
    const SemanticSymbolRecord& record,
    const QSet<QString>& packageScopes)
{
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;
    if (isGlobalDefinition(record))
        return SymbolOwnerScope::Global;
    if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Modport)
        return SymbolOwnerScope::Interface;
    if (record.declarationKind == SymbolTaxonomy::DeclarationKind::StructMember)
        return SymbolOwnerScope::Struct;
    if (!record.owner.name.isEmpty()) {
        if (packageScopes.contains(record.owner.name)
            && isPackageVisibleDefinition(record)) {
            return SymbolOwnerScope::Package;
        }
        return SymbolOwnerScope::Module;
    }
    return SymbolOwnerScope::Unknown;
}

SymbolTaxonomy::SymbolVisibility visibilityForOwnerScope(
    SymbolTaxonomy::SymbolOwnerScope ownerScope)
{
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;
    using SymbolVisibility = SymbolTaxonomy::SymbolVisibility;
    switch (ownerScope) {
    case SymbolOwnerScope::Global:
        return SymbolVisibility::Global;
    case SymbolOwnerScope::Package:
        return SymbolVisibility::PackageVisible;
    case SymbolOwnerScope::Interface:
    case SymbolOwnerScope::Struct:
        return SymbolVisibility::Member;
    case SymbolOwnerScope::Module:
        return SymbolVisibility::ScopeLocal;
    case SymbolOwnerScope::Unknown:
        break;
    }
    return SymbolVisibility::Unknown;
}

void updateStableKey(SemanticSymbolRecord* record)
{
    if (!record)
        return;
    record->stableKey.fileName =
        normalizedStableKeyFileName(record->location.fileName);
    record->stableKey.symbolName = record->name;
    record->stableKey.declarationKind = record->declarationKind;
    record->stableKey.ownerScope = record->owner.name;
    record->stableKey.sourcePosition = record->location.position;
    record->stableKey.sourceLength = record->location.length;
}

} // namespace

bool fillSymbolRecord(const slang::SourceManager* sm,
                      const slang::ast::Symbol& sym,
                      SemanticSymbolRecord& out,
                      QString* outOwnerName)
{
    if (!sm || !sym.location.valid())
        return false;

    const std::string nameStr(sym.name);
    out.name = QString::fromStdString(nameStr);
    const QTextDocumentSourcePosition start =
        qTextDocumentSourcePosition(sm, sym.location);
    if (!start.isValid())
        return false;
    const QTextDocumentSourcePosition nameEnd =
        qTextDocumentSourcePosition(sm, sym.location + nameStr.size());
    out.location.fileName = start.fileName;
    out.location.startLine = start.line;
    out.location.startColumn = start.column;
    out.location.position = start.position;
    out.location.length = nameEnd.isValid()
        ? qMax(0, nameEnd.position - start.position)
        : out.name.size();
    out.localHandle = -1;
    out.type.rawTypeText.clear();

    if (const slang::syntax::SyntaxNode* syntax = sym.getSyntax()) {
        slang::SourceRange range = syntax->sourceRange();
        if (range.end().valid()) {
            const QTextDocumentSourcePosition end =
                qTextDocumentSourcePosition(sm, range.end());
            out.location.endLine = end.isValid()
                ? end.line : out.location.startLine;
            out.location.endColumn = end.isValid()
                ? end.column : out.location.startColumn;
        } else {
            out.location.endLine = out.location.startLine;
            out.location.endColumn = out.location.startColumn;
        }
    } else {
        out.location.endLine = out.location.startLine;
        out.location.endColumn = out.location.startColumn;
    }

    if (outOwnerName) {
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
        *outOwnerName = scopeName;
    }
    return true;
}

void applyCollectorKind(
    SemanticSymbolRecord* record,
    SymbolTaxonomy::CollectorKind rawKind)
{
    if (!record)
        return;
    record->collectorKind = rawKind;
    record->declarationKind = declarationKindForRawKind(rawKind);
    record->usageRole = usageRoleForRawKind(rawKind);
    record->sourceRole =
        SymbolTaxonomy::sourceRoleForFileName(record->location.fileName);
    record->owner.interfaceLike = hasInterfaceLikeOwner(rawKind);
}

void finalizeCollectedSymbolRecords(QList<SemanticSymbolRecord>* records)
{
    if (!records)
        return;

    QSet<QString> packageScopes;
    for (const SemanticSymbolRecord& record : std::as_const(*records)) {
        if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Package
            && !record.name.isEmpty()) {
            packageScopes.insert(record.name);
        }
    }

    for (SemanticSymbolRecord& record : *records) {
        record.owner.kind = ownerScopeForRecord(record, packageScopes);
        record.visibility = visibilityForOwnerScope(record.owner.kind);
        if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Interface) {
            record.type.resolvedTypeName = record.name;
        } else if (record.owner.interfaceLike
                   || record.declarationKind == SymbolTaxonomy::DeclarationKind::Instance) {
            record.type.resolvedTypeName =
                SymbolTaxonomy::interfaceTypeName(record.type.rawTypeText);
        }
        record.type.modportName =
            SymbolTaxonomy::interfaceModportName(record.type.rawTypeText);
        if (!record.type.resolvedTypeName.isEmpty()) {
            record.type.resolvedTypeKind =
                SymbolTaxonomy::DeclarationKind::Interface;
        }
        updateStableKey(&record);
    }
}

void emitEnumValueRecords(const slang::SourceManager* sm,
                          const slang::ast::EnumType& et,
                          const QString& scopeKey,
                          QList<SemanticSymbolRecord>& outList)
{
    for (const auto& ev : et.values()) {
        SemanticSymbolRecord record;
        if (!fillSymbolRecord(sm, ev, record, nullptr))
            continue;
        applyCollectorKind(&record, SymbolTaxonomy::CollectorKind::EnumValue);
        record.owner.name = scopeKey;
        outList.append(record);
    }
}

void emitStructMemberRecords(const slang::SourceManager* sm,
                             const slang::ast::Scope& structScope,
                             const QString& scopeKey,
                             QList<SemanticSymbolRecord>& outList)
{
    for (const auto& member : structScope.members()) {
        if (member.kind != slang::ast::SymbolKind::Field)
            continue;
        SemanticSymbolRecord record;
        if (!fillSymbolRecord(sm, member, record, nullptr))
            continue;
        applyCollectorKind(&record, SymbolTaxonomy::CollectorKind::StructMember);
        record.owner.name = scopeKey;
        outList.append(record);
    }
}

SymbolTaxonomy::CollectorKind variableOrNetCollectorKind(
    const slang::ast::Type& type)
{
    const slang::ast::Type& canon = type.getCanonicalType();
    using slang::ast::SymbolKind;
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    SymbolKind k = canon.kind;

    if (k == SymbolKind::ScalarType) {
        const auto& st = canon.as<slang::ast::ScalarType>();
        if (st.scalarKind == slang::ast::ScalarType::Reg)
            return CollectorKind::Reg;
        return CollectorKind::Logic;
    }
    if (k == SymbolKind::EnumType)
        return CollectorKind::EnumVariable;
    if (k == SymbolKind::PackedStructType)
        return CollectorKind::PackedStructVariable;
    if (k == SymbolKind::UnpackedStructType)
        return CollectorKind::UnpackedStructVariable;
    if (const slang::ast::IntegralType* it = canon.as_if<slang::ast::IntegralType>()) {
        if (it->isDeclaredReg())
            return CollectorKind::Reg;
        return CollectorKind::Logic;
    }
    return CollectorKind::Logic;
}

SymbolTaxonomy::CollectorKind portDirectionCollectorKind(
    slang::ast::ArgumentDirection dir)
{
    using slang::ast::ArgumentDirection;
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    switch (dir) {
    case ArgumentDirection::In:    return CollectorKind::PortInput;
    case ArgumentDirection::Out:    return CollectorKind::PortOutput;
    case ArgumentDirection::InOut:  return CollectorKind::PortInout;
    case ArgumentDirection::Ref:    return CollectorKind::PortRef;
    default:                        return CollectorKind::PortInout;
    }
}

} // namespace slang_symbols::detail
