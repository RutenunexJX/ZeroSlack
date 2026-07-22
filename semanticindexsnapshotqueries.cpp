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
    QString normalized = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
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
    const QList<int> indexes =
        m_symbolRecordIndexesByFile.value(normalizedTarget);
    result.reserve(indexes.size());
    for (int index : indexes) {
        if (index >= 0 && index < m_symbolRecords.size())
            result.append(m_symbolRecords.at(index));
    }
    return result;
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::getSymbolRecordsByName(
    const QString& name) const
{
    if (name.isEmpty())
        return {};

    QList<SemanticSymbolRecord> result;
    const QList<int> indexes = m_symbolRecordIndexesByName.value(name);
    result.reserve(indexes.size());
    for (int index : indexes) {
        if (index >= 0 && index < m_symbolRecords.size())
            result.append(m_symbolRecords.at(index));
    }
    return result;
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::getSymbolRecordsByOwner(
    const QString& ownerName) const
{
    QList<SemanticSymbolRecord> result;
    const QList<int> indexes = m_symbolRecordIndexesByOwner.value(ownerName);
    result.reserve(indexes.size());
    for (int index : indexes) {
        if (index >= 0 && index < m_symbolRecords.size())
            result.append(m_symbolRecords.at(index));
    }
    return result;
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::getSymbolRecordsByDeclarationKind(
    SymbolTaxonomy::DeclarationKind declarationKind) const
{
    QList<SemanticSymbolRecord> result;
    const QList<int> indexes =
        m_symbolRecordIndexesByDeclarationKind.value(
            static_cast<int>(declarationKind));
    result.reserve(indexes.size());
    for (int index : indexes) {
        if (index >= 0 && index < m_symbolRecords.size())
            result.append(m_symbolRecords.at(index));
    }
    return result;
}

SemanticAnalysisBandReport SemanticIndexSnapshot::analysisBandReport(
    const QString& fileName) const
{
    return semanticAnalysisBandReportForRecords(getSymbolRecords(fileName));
}

SemanticSymbolRecord SemanticIndexSnapshot::getSymbolRecordByStableKey(
    const SymbolStableKey& key) const
{
    if (!key.isValid())
        return {};

    const int index =
        m_symbolRecordIndexByStableKey.value(symbolStableKeyText(key), -1);
    if (index >= 0 && index < m_symbolRecords.size())
        return m_symbolRecords.at(index);
    return {};
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::findDefinitionRecords(
    const QString& name,
    const SemanticQueryContext& context) const
{
    if (name.isEmpty())
        return {};

    QList<SemanticSymbolRecord> result;
    const QList<int> indexes = m_symbolRecordIndexesByName.value(name);
    result.reserve(indexes.size());
    for (int index : indexes) {
        if (index >= 0 && index < m_symbolRecords.size())
            result.append(m_symbolRecords.at(index));
    }
    return sortedDefinitionRecords(result, context);
}

SemanticRelationship SemanticIndexSnapshot::rebindRelationship(
    const SemanticRelationship& relationship) const
{
    SemanticRelationship rebound = relationship;

    if (!rebound.fromStableKey.isValid()) {
        const int index =
            m_symbolRecordIndexByLocalHandle.value(rebound.fromId, -1);
        if (index >= 0 && index < m_symbolRecords.size())
            rebound.fromStableKey = m_symbolRecords.at(index).stableKey;
    }
    if (!rebound.toStableKey.isValid()) {
        const int index =
            m_symbolRecordIndexByLocalHandle.value(rebound.toId, -1);
        if (index >= 0 && index < m_symbolRecords.size())
            rebound.toStableKey = m_symbolRecords.at(index).stableKey;
    }

    int index =
        m_symbolRecordIndexByStableKey.value(
            symbolStableKeyText(rebound.fromStableKey),
            -1);
    if (index >= 0 && index < m_symbolRecords.size())
        rebound.fromId = m_symbolRecords.at(index).localHandle;

    index =
        m_symbolRecordIndexByStableKey.value(
            symbolStableKeyText(rebound.toStableKey),
            -1);
    if (index >= 0 && index < m_symbolRecords.size())
        rebound.toId = m_symbolRecords.at(index).localHandle;

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

QList<SemanticRelationship> SemanticIndexSnapshot::relationshipsForStableKey(
    const SymbolStableKey& key,
    bool outgoing) const
{
    QList<SemanticRelationship> result;
    if (!key.isValid())
        return result;

    const QString keyText = symbolStableKeyText(key);
    if (keyText.isEmpty())
        return result;

    const QList<int> indexes = outgoing
        ? m_relationshipIndexesByFromStableKey.value(keyText)
        : m_relationshipIndexesByToStableKey.value(keyText);
    result.reserve(indexes.size());
    for (int index : indexes) {
        if (index >= 0 && index < m_relationships.size())
            result.append(m_relationships.at(index));
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

const QHash<QString, QString>& SemanticIndexSnapshot::fileContentsView() const
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
        const int aBandPriority =
            semanticSymbolAnalysisBandSortPriority(a);
        const int bBandPriority =
            semanticSymbolAnalysisBandSortPriority(b);
        if (aBandPriority != bBandPriority)
            return aBandPriority < bBandPriority;
        if (a.location.fileName != b.location.fileName)
            return a.location.fileName < b.location.fileName;
        if (a.location.startLine != b.location.startLine)
            return a.location.startLine < b.location.startLine;
        return a.localHandle < b.localHandle;
    });
    return sorted;
}
