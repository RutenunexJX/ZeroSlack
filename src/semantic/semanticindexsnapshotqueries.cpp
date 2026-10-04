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
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::getSymbolRecords(const QString& fileName) const
{
    if (fileName.isEmpty())
        return symbolRecordsView();
    const auto shard = m_symbolsByFile.value(normalizedSnapshotQueryFileName(fileName));
    return shard ? shard->records : QList<SemanticSymbolRecord>{};
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::getSymbolRecordsByName(const QString& name) const
{
    QList<SemanticSymbolRecord> result;
    if (name.isEmpty())
        return result;
    const auto files = m_filesByName.value(name);
    for (const QString& file : m_fileOrder) {
        if (!files.contains(file))
            continue;
        const auto shard = m_symbolsByFile.value(file);
        for (int index : shard->byName.value(name))
            result.append(shard->records.at(index));
    }
    return result;
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::getSymbolRecordsByOwner(const QString& owner) const
{
    QList<SemanticSymbolRecord> result;
    const auto files = m_filesByOwner.value(owner);
    for (const QString& file : m_fileOrder) {
        if (!files.contains(file))
            continue;
        const auto shard = m_symbolsByFile.value(file);
        for (int index : shard->byOwner.value(owner))
            result.append(shard->records.at(index));
    }
    return result;
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::getSymbolRecordsByDeclarationKind(
    SymbolTaxonomy::DeclarationKind kind) const
{
    QList<SemanticSymbolRecord> result;
    const auto files = m_filesByKind.value(int(kind));
    for (const QString& file : m_fileOrder) {
        if (!files.contains(file))
            continue;
        const auto shard = m_symbolsByFile.value(file);
        for (int index : shard->byKind.value(int(kind)))
            result.append(shard->records.at(index));
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
    const auto shard = m_symbolsByFile.value(normalizedSnapshotQueryFileName(key.fileName));
    const int index = shard ? shard->byStableKey.value(symbolStableKeyText(key), -1) : -1;
    return index >= 0 ? shard->records.at(index) : SemanticSymbolRecord{};
}

SemanticSymbolRecord SemanticIndexSnapshot::getSymbolRecordByLocalHandle(int handle) const
{
    const auto address = m_recordsByHandle.value(handle);
    const auto shard = m_symbolsByFile.value(address.file);
    return shard && address.index >= 0 && address.index < shard->records.size()
        ? shard->records.at(address.index) : SemanticSymbolRecord{};
}

QList<SemanticSymbolRecord> SemanticIndexSnapshot::findDefinitionRecords(
    const QString& name, const SemanticQueryContext& context) const
{
    auto result = getSymbolRecordsByName(name);
    result.removeIf([](const SemanticSymbolRecord& record) {
        return !SymbolTaxonomy::isDefinitionCandidate(semanticMetadataForSymbolRecord(record));
    });
    return sortedDefinitionRecords(result, context);
}

SemanticRelationship SemanticIndexSnapshot::rebindRelationship(const SemanticRelationship& relationship) const
{
    auto rebound = relationship;
    auto endpoint = [this](SymbolStableKey& key, int& handle) {
        auto record = getSymbolRecordByStableKey(key);
        if (!record.stableKey.isValid()) {
            const auto byHandle = getSymbolRecordByLocalHandle(handle);
            const auto& current = byHandle.stableKey;
            const bool sameIdentity = key.isValid() && current.isValid()
                && normalizedSnapshotQueryFileName(key.fileName) == normalizedSnapshotQueryFileName(current.fileName)
                && key.symbolName == current.symbolName && key.declarationKind == current.declarationKind
                && key.ownerScope == current.ownerScope;
            if (!key.isValid() || sameIdentity)
                record = byHandle;
        }
        if (record.stableKey.isValid()) {
            key = record.stableKey;
            handle = record.localHandle;
        } else {
            handle = -1;
        }
    };
    endpoint(rebound.fromStableKey, rebound.fromId);
    endpoint(rebound.toStableKey, rebound.toId);
    return rebound;
}

QString SemanticIndexSnapshot::getCachedFileContent(const QString& fileName) const
{
    return m_fileContents.value(normalizedSnapshotQueryFileName(fileName));
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
    const SymbolStableKey& key, bool outgoing) const
{
    QList<SemanticRelationship> result;
    if (!key.isValid())
        return result;
    const QString text = symbolStableKeyText(key);
    QStringList owners = m_relationshipOwnersByEndpoint.value(
        normalizedSnapshotQueryFileName(key.fileName)).values();
    owners.sort(Qt::CaseSensitive);
    for (const QString& owner : owners) {
        const auto shard = m_relationshipsByOwner.value(owner);
        const auto indexes = outgoing ? shard->outgoing.value(text) : shard->incoming.value(text);
        for (int index : indexes)
            result.append(shard->records.at(index));
    }
    return result;
}

QList<SemanticDiagnostic> SemanticIndexSnapshot::getDiagnostics(const QString& fileName) const
{
    if (m_diagnosticView)
        return fileName.isEmpty() ? m_diagnosticView->diagnostics
            : m_diagnosticView->byFile.value(normalizedSnapshotQueryFileName(fileName));
    return rawDiagnostics(fileName);
}

QList<SemanticDiagnostic> SemanticIndexSnapshot::rawDiagnostics(const QString& fileName) const
{
    if (!fileName.isEmpty())
        return m_rawDiagnosticsByFile.value(normalizedSnapshotQueryFileName(fileName));
    QList<SemanticDiagnostic> result;
    QStringList files = m_rawDiagnosticsByFile.keys();
    files.sort(Qt::CaseSensitive);
    for (const QString& file : files)
        result.append(m_rawDiagnosticsByFile.value(file));
    return result;
}

int SemanticIndexSnapshot::diagnosticDisplayLimit() const
{
    return m_diagnosticView ? m_diagnosticView->limit : 0;
}

int SemanticIndexSnapshot::rawDiagnosticCount() const
{
    if (m_diagnosticView)
        return m_diagnosticView->producedCount;
    int count = 0;
    for (auto it = m_rawDiagnosticsByFile.cbegin(); it != m_rawDiagnosticsByFile.cend(); ++it)
        count += it.value().size();
    return count;
}

int SemanticIndexSnapshot::suppressedDiagnosticCount() const
{
    return m_diagnosticView
        ? m_diagnosticView->producedCount - m_diagnosticView->diagnostics.size() : 0;
}

QList<SemanticRelationship> SemanticIndexSnapshot::relationships() const { return relationshipsView(); }
QList<SemanticDiagnostic> SemanticIndexSnapshot::diagnostics() const { return getDiagnostics(); }
QHash<QString, QString> SemanticIndexSnapshot::fileContents() const { return m_fileContents; }
const QHash<QString, QString>& SemanticIndexSnapshot::fileContentsView() const { return m_fileContents; }

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
