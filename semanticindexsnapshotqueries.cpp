#include "semanticindexsnapshot.h"

#include "symboltaxonomy.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace {
QString normalizedSnapshotQueryFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

sym_list::SymbolInfo missingSnapshotSymbol()
{
    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    return missing;
}

QString snapshotRecordDisplayName(const SemanticSymbolRecord& record,
                                  const sym_list::SymbolInfo& fallback)
{
    if (!record.name.isEmpty())
        return record.name;
    return fallback.symbolName;
}

QString snapshotRecordOwnerName(const SemanticSymbolRecord& record,
                                const sym_list::SymbolInfo& fallback)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;
    return semanticSymbolRecordForSymbol(fallback).owner.name;
}

SymbolStableKey stableKeyFromSnapshotSymbol(const sym_list::SymbolInfo& symbol)
{
    const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
    return record.stableKey.isValid()
        ? record.stableKey
        : symbolStableKeyForSymbol(symbol);
}

sym_list::SymbolInfo snapshotSymbolByLocalHandle(
    const SemanticIndexSnapshot& snapshot,
    int symbolId)
{
    if (symbolId >= 0) {
        for (const sym_list::SymbolInfo& symbol : snapshot.getSymbols()) {
            if (symbol.symbolId == symbolId)
                return symbol;
        }
    }

    return missingSnapshotSymbol();
}

SymbolStableKey relationshipEndpointStableKey(
    const SemanticIndexSnapshot& snapshot,
    const SemanticRelationship& relationship,
    bool fromEndpoint)
{
    const SymbolStableKey stableKey = fromEndpoint
        ? relationship.fromStableKey
        : relationship.toStableKey;
    if (stableKey.isValid())
        return stableKey;

    return stableKeyFromSnapshotSymbol(
        snapshotSymbolByLocalHandle(snapshot,
                                    fromEndpoint
                                        ? relationship.fromId
                                        : relationship.toId));
}
}

QList<sym_list::SymbolInfo> SemanticIndexSnapshot::getSymbols(const QString& fileName) const
{
    if (fileName.isEmpty())
        return m_symbols;

    QList<sym_list::SymbolInfo> result;
    const QString normalizedTarget = normalizedSnapshotQueryFileName(fileName);
    for (const sym_list::SymbolInfo& symbol : m_symbols) {
        if (symbol.fileName == fileName
            || normalizedSnapshotQueryFileName(symbol.fileName) == normalizedTarget) {
            result.append(symbol);
        }
    }
    return result;
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::getSymbolRecords(
    const QString& fileName) const
{
    return semanticSymbolRecordsForSymbols(getSymbols(fileName));
}

SemanticSymbolRecord SemanticIndexSnapshot::getSymbolRecordByStableKey(
    const SymbolStableKey& key) const
{
    if (!key.isValid())
        return {};

    for (const sym_list::SymbolInfo& symbol : m_symbols) {
        const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
        if (record.stableKey == key)
            return record;
    }
    return {};
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::findDefinitionRecords(
    const QString& name,
    const SemanticQueryContext& context) const
{
    if (name.isEmpty())
        return {};

    QList<SemanticSymbolRecord> result;
    for (const sym_list::SymbolInfo& symbol : m_symbols) {
        if (symbol.symbolName == name)
            result.append(semanticSymbolRecordForSymbol(symbol));
    }
    return sortedDefinitionRecords(result, context);
}

SemanticRelationship SemanticIndexSnapshot::rebindRelationship(
    const SemanticRelationship& relationship) const
{
    SemanticRelationship rebound = relationship;

    rebound.fromStableKey =
        relationshipEndpointStableKey(*this, rebound, true);
    rebound.toStableKey =
        relationshipEndpointStableKey(*this, rebound, false);

    const SemanticSymbolRecord fromRecord =
        getSymbolRecordByStableKey(rebound.fromStableKey);
    if (fromRecord.localHandle >= 0)
        rebound.fromId = fromRecord.localHandle;

    const SemanticSymbolRecord toRecord =
        getSymbolRecordByStableKey(rebound.toStableKey);
    if (toRecord.localHandle >= 0)
        rebound.toId = toRecord.localHandle;

    return rebound;
}

QString SemanticIndexSnapshot::getCachedFileContent(const QString& fileName) const
{
    if (fileName.isEmpty())
        return QString();

    if (m_fileContents.contains(fileName))
        return m_fileContents.value(fileName);

    const QString normalizedTarget = normalizedSnapshotQueryFileName(fileName);
    for (auto it = m_fileContents.constBegin(); it != m_fileContents.constEnd(); ++it) {
        if (normalizedSnapshotQueryFileName(it.key()) == normalizedTarget)
            return it.value();
    }
    return QString();
}

QStringList SemanticIndexSnapshot::getScopeSymbolNames(const QString& fileName,
                                                       int cursorLine) const
{
    QStringList result;
    if (fileName.isEmpty() || cursorLine < 0)
        return result;

    const QList<sym_list::SymbolInfo> fileSymbols = getSymbols(fileName);
    QString containingModule;
    int containingModuleStart = -1;
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
        if (record.declarationKind != SymbolTaxonomy::DeclarationKind::Module)
            continue;
        if (symbol.startLine <= cursorLine
            && (symbol.endLine <= 0 || symbol.endLine >= cursorLine)
            && symbol.startLine > containingModuleStart) {
            containingModule = snapshotRecordDisplayName(record, symbol);
            containingModuleStart = symbol.startLine;
        }
    }

    QSet<QString> seen;
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
        const QString displayName = snapshotRecordDisplayName(record, symbol);
        const QString ownerName = snapshotRecordOwnerName(record, symbol);
        bool inScope = ownerName.isEmpty();
        if (!containingModule.isEmpty()) {
            inScope = inScope
                || ownerName == containingModule
                || (symbol.startLine <= cursorLine
                    && (symbol.endLine <= 0 || symbol.endLine >= cursorLine));
        }
        if (!inScope || displayName.isEmpty() || seen.contains(displayName))
            continue;
        seen.insert(displayName);
        result.append(displayName);
    }
    return result;
}

QList<SemanticRelationship> SemanticIndexSnapshot::getRelationships(
    const SymbolStableKey& key,
    bool outgoing) const
{
    QList<SemanticRelationship> result;
    if (!key.isValid())
        return result;

    for (const SemanticRelationship& relationship : m_relationships) {
        const SymbolStableKey relationshipKey =
            relationshipEndpointStableKey(*this, relationship, outgoing);
        if (relationshipKey == key) {
            result.append(relationship);
        }
    }
    return result;
}

QList<SemanticDiagnostic> SemanticIndexSnapshot::getDiagnostics(const QString& fileName) const
{
    if (fileName.isEmpty())
        return m_diagnostics;

    QList<SemanticDiagnostic> result;
    const QString normalizedTarget = normalizedSnapshotQueryFileName(fileName);
    for (const SemanticDiagnostic& diagnostic : m_diagnostics) {
        if (diagnostic.fileName == fileName
            || normalizedSnapshotQueryFileName(diagnostic.fileName) == normalizedTarget) {
            result.append(diagnostic);
        }
    }
    return result;
}

QList<SemanticRelationship> SemanticIndexSnapshot::relationships() const
{
    return m_relationships;
}

QList<SemanticDiagnostic> SemanticIndexSnapshot::diagnostics() const
{
    return m_diagnostics;
}

QHash<QString, QString> SemanticIndexSnapshot::fileContents() const
{
    return m_fileContents;
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::sortedDefinitionRecords(
    const QList<SemanticSymbolRecord>& records,
    const SemanticQueryContext& context) const
{
    QList<SemanticSymbolRecord> sorted = records;
    const QString normalizedContextFile = normalizedSnapshotQueryFileName(context.fileName);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&context, &normalizedContextFile](const SemanticSymbolRecord& a,
                                                        const SemanticSymbolRecord& b) {
        auto score = [&context, &normalizedContextFile](const SemanticSymbolRecord& s) {
            int value = 0;
            if (!normalizedContextFile.isEmpty()
                && normalizedSnapshotQueryFileName(s.location.fileName)
                    == normalizedContextFile)
                value += 100;
            if (!context.moduleName.isEmpty()
                && s.owner.name == context.moduleName)
                value += 50;
            SymbolTaxonomy::SemanticMetadata metadata;
            metadata.declarationKind = s.declarationKind;
            metadata.usageRole = s.usageRole;
            metadata.ownerScope = s.owner.kind;
            metadata.visibility = s.visibility;
            metadata.sourceRole = s.sourceRole;
            metadata.rawCollectorKind = s.rawCollectorKind;
            metadata.interfaceLikeOwner = s.owner.interfaceLike;
            if (SymbolTaxonomy::isGlobalDefinition(metadata)) {
                value += 10;
            }
            return value;
        };

        const int aScore = score(a);
        const int bScore = score(b);
        if (aScore != bScore)
            return aScore > bScore;
        if (a.location.fileName != b.location.fileName)
            return a.location.fileName < b.location.fileName;
        if (a.location.startLine != b.location.startLine)
            return a.location.startLine < b.location.startLine;
        return a.localHandle < b.localHandle;
    });
    return sorted;
}
