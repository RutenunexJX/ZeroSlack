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

SemanticSymbolRecord snapshotRecordByLocalHandle(
    const SemanticIndexSnapshot& snapshot,
    int localHandle)
{
    if (localHandle >= 0) {
        for (const SemanticSymbolRecord& record : snapshot.getSymbolRecords()) {
            if (record.localHandle == localHandle)
                return record;
        }
    }

    return {};
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

    const SemanticSymbolRecord record =
        snapshotRecordByLocalHandle(snapshot,
                                    fromEndpoint
                                        ? relationship.fromId
                                        : relationship.toId);
    return record.stableKey;
}
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::getSymbolRecords(
    const QString& fileName) const
{
    if (fileName.isEmpty())
        return m_symbolRecords;

    QList<SemanticSymbolRecord> result;
    const QString normalizedTarget = normalizedSnapshotQueryFileName(fileName);
    for (const SemanticSymbolRecord& record : m_symbolRecords) {
        if (record.location.fileName == fileName
            || normalizedSnapshotQueryFileName(record.location.fileName)
                   == normalizedTarget) {
            result.append(record);
        }
    }
    return result;
}

SemanticSymbolRecord SemanticIndexSnapshot::getSymbolRecordByStableKey(
    const SymbolStableKey& key) const
{
    if (!key.isValid())
        return {};

    for (const SemanticSymbolRecord& record : m_symbolRecords) {
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
    for (const SemanticSymbolRecord& record : m_symbolRecords) {
        if (record.name == name)
            result.append(record);
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

    const QList<SemanticSymbolRecord> fileRecords = getSymbolRecords(fileName);
    QString containingModule;
    int containingModuleStart = -1;
    for (const SemanticSymbolRecord& record : fileRecords) {
        if (record.declarationKind != SymbolTaxonomy::DeclarationKind::Module)
            continue;
        if (record.location.startLine <= cursorLine
            && (record.location.endLine <= 0
                || record.location.endLine >= cursorLine)
            && record.location.startLine > containingModuleStart) {
            containingModule = record.name;
            containingModuleStart = record.location.startLine;
        }
    }

    QSet<QString> seen;
    for (const SemanticSymbolRecord& record : fileRecords) {
        const QString displayName = record.name;
        const QString ownerName = record.owner.name;
        bool inScope = ownerName.isEmpty();
        if (!containingModule.isEmpty()) {
            inScope = inScope
                || ownerName == containingModule
                || (record.location.startLine <= cursorLine
                    && (record.location.endLine <= 0
                        || record.location.endLine >= cursorLine));
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
            const SymbolTaxonomy::SemanticMetadata metadata =
                semanticMetadataForSymbolRecord(s);
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
