#include "semanticindex.h"

#include "semanticindexsnapshot.h"
#include "smartrelationshipbuilder.h"
#include "svtokenutils.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace {
QString normalizedStoreFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QString nativeContentHash(const QString& content)
{
    return QString::number(qHash(content));
}

QString nativeSymbolRelevantHash(const QString& content)
{
    QString joined;
    joined.reserve(content.size());
    QString line;
    line.reserve(256);
    bool inBlockComment = false;

    auto flushLine = [&]() {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QStringLiteral("//")))
            return;
        const QString collapsed = SvTokenUtils::collapseWhitespaceRuns(trimmed);
        if (collapsed.isEmpty())
            return;
        if (!joined.isEmpty())
            joined.append(QLatin1Char(' '));
        joined.append(collapsed);
    };

    for (int i = 0; i < content.size(); ++i) {
        const QChar ch = content.at(i);
        if (inBlockComment) {
            if (ch == QLatin1Char('*')
                && i + 1 < content.size()
                && content.at(i + 1) == QLatin1Char('/')) {
                inBlockComment = false;
                ++i;
            } else if (ch == QLatin1Char('\n')) {
                flushLine();
                line.clear();
            }
            continue;
        }

        if (ch == QLatin1Char('/')
            && i + 1 < content.size()
            && content.at(i + 1) == QLatin1Char('*')) {
            inBlockComment = true;
            line.append(QLatin1Char(' '));
            ++i;
            continue;
        }
        if (ch == QLatin1Char('\n')) {
            flushLine();
            line.clear();
            continue;
        }
        line.append(ch);
    }
    flushLine();
    return QString::number(qHash(joined));
}

bool isSemanticModuleName(const QString& name)
{
    return SvTokenUtils::isIdentifier(name);
}

QString stripCommentsFromLine(const QString& line, bool& inBlockComment)
{
    QString result;
    result.reserve(line.size());
    for (int i = 0; i < line.size(); ++i) {
        if (inBlockComment) {
            if (line.mid(i, 2) == QStringLiteral("*/")) {
                inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (line.mid(i, 2) == QStringLiteral("//"))
            break;
        if (line.mid(i, 2) == QStringLiteral("/*")) {
            inBlockComment = true;
            ++i;
            continue;
        }
        result.append(line.at(i));
    }
    return result;
}

int findEndModuleLineInContent(const QString& content,
                               const SemanticSymbolRecord& moduleRecord)
{
    const QStringList lines = content.split('\n');
    int moduleDepth = 0;
    int scanStart = moduleRecord.location.startLine - 1;
    if (scanStart < 0)
        scanStart = 0;

    bool inBlockComment = false;
    for (int i = scanStart; i < lines.size(); ++i) {
        const QString code = stripCommentsFromLine(lines.at(i), inBlockComment);
        if (SvTokenUtils::containsWord(code, QStringLiteral("module")))
            ++moduleDepth;
        if (SvTokenUtils::containsWord(code, QStringLiteral("endmodule"))) {
            --moduleDepth;
            if (moduleDepth == 0)
                return i;
        }
    }
    return -1;
}

QStringList scopeSymbolNamesForRecords(
    const QList<SemanticSymbolRecord>& records,
    int cursorLine)
{
    QStringList result;
    if (cursorLine < 0)
        return result;

    QString containingModule;
    int containingModuleStart = -1;
    for (const SemanticSymbolRecord& record : records) {
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
    for (const SemanticSymbolRecord& record : records) {
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

void removeRecordsCoveredByNativeFiles(
    QList<SemanticSymbolRecord>* records,
    const QSet<QString>& nativeFiles)
{
    if (!records || nativeFiles.isEmpty())
        return;
    records->erase(
        std::remove_if(records->begin(),
                       records->end(),
                       [&nativeFiles](const SemanticSymbolRecord& record) {
                           const QString normalized =
                               normalizedStoreFileName(record.location.fileName);
                           return !normalized.isEmpty()
                               && nativeFiles.contains(normalized);
                       }),
        records->end());
}

}

void SemanticIndex::replaceNativeSymbolRecordsForFile(
    const QString& fileName,
    const QList<SemanticSymbolRecord>& records,
    const QString& content,
    bool rebuildIndexes,
    bool updateIndexesIncrementally)
{
    const QString normalizedTarget = normalizedStoreFileName(fileName);
    if (normalizedTarget.isEmpty())
        return;

    const bool hadNativeCoverage =
        m_nativeCoveredFiles.contains(normalizedTarget);
    m_nativeCoveredFiles.insert(normalizedTarget);

    const QSet<int> previousHandles =
        m_nativeRecordHandlesByAnalysisFile.take(normalizedTarget);
    if (hadNativeCoverage || !previousHandles.isEmpty()) {
        for (int i = m_nativeSymbolRecords.size() - 1; i >= 0; --i) {
            const SemanticSymbolRecord& existingRecord =
                m_nativeSymbolRecords.at(i);
            const QString normalizedLocation =
                normalizedStoreFileName(existingRecord.location.fileName);
            const QString normalizedStableKey =
                normalizedStoreFileName(existingRecord.stableKey.fileName);
            if (previousHandles.contains(existingRecord.localHandle)
                || normalizedLocation == normalizedTarget
                || normalizedStableKey == normalizedTarget) {
                m_nativeSymbolRecords.removeAt(i);
            }
        }
    }

    QSet<int> nextHandles;
    const bool hasContent = !content.isNull();
    if (hasContent) {
        m_nativeFileContents.insert(fileName, content);
        if (fileName != normalizedTarget)
            m_nativeFileContents.insert(normalizedTarget, content);
    } else {
        m_nativeFileContents.remove(fileName);
        m_nativeFileContents.remove(normalizedTarget);
        m_nativeFileStates.remove(fileName);
        m_nativeFileStates.remove(normalizedTarget);
    }

    for (SemanticSymbolRecord record : records) {
        if (!record.isValid())
            continue;
        if (record.location.fileName.isEmpty())
            record.location.fileName = fileName;
        if (record.localHandle <= 0)
            record.localHandle = m_nextNativeLocalHandle++;
        const QString normalizedRecordFile =
            normalizedStoreFileName(record.location.fileName);
        record.stableKey.fileName = normalizedRecordFile.isEmpty()
            ? normalizedTarget
            : normalizedRecordFile;
        record.stableKey.symbolName = record.name;
        record.stableKey.declarationKind = record.declarationKind;
        record.stableKey.ownerScope = record.owner.name;
        nextHandles.insert(record.localHandle);
        const int recordIndex = m_nativeSymbolRecords.size();
        m_nativeSymbolRecords.append(record);
        if (!rebuildIndexes && updateIndexesIncrementally)
            appendNativeStoreIndexForRecord(recordIndex);
    }
    m_nativeRecordHandlesByAnalysisFile.insert(normalizedTarget, nextHandles);

    if (rebuildIndexes)
        rebuildNativeStoreIndexes();
    if (hasContent)
        updateNativeFileState(fileName, content);
}

void SemanticIndex::rebuildNativeStoreIndexes()
{
    m_nativeRecordIndexesByFile.clear();
    m_nativeRecordIndexesByName.clear();
    m_nativeRecordIndexesByOwner.clear();
    m_nativeRecordIndexesByDeclarationKind.clear();
    m_nativeStableKeyIndexes.clear();
    for (int i = 0; i < m_nativeSymbolRecords.size(); ++i) {
        appendNativeStoreIndexForRecord(i);
    }
}

void SemanticIndex::appendNativeStoreIndexForRecord(int index)
{
    if (index < 0 || index >= m_nativeSymbolRecords.size())
        return;

    const SemanticSymbolRecord& record = m_nativeSymbolRecords.at(index);
    const QString normalized =
        normalizedStoreFileName(record.location.fileName);
    if (!normalized.isEmpty())
        m_nativeRecordIndexesByFile[normalized].append(index);

    if (!record.name.isEmpty())
        m_nativeRecordIndexesByName[record.name].append(index);

    m_nativeRecordIndexesByOwner[record.owner.name].append(index);
    m_nativeRecordIndexesByDeclarationKind[
        static_cast<int>(record.declarationKind)].append(index);

    const QString stableKeyText = symbolStableKeyText(record.stableKey);
    if (!stableKeyText.isEmpty())
        m_nativeStableKeyIndexes.insert(stableKeyText, index);
}

QList<SemanticSymbolRecord> SemanticIndex::nativeSymbolRecords(
    const QString& fileName) const
{
    if (fileName.isEmpty())
        return m_nativeSymbolRecords;

    QList<SemanticSymbolRecord> records;
    const QString normalizedTarget = normalizedStoreFileName(fileName);
    const QList<int> indexes = m_nativeRecordIndexesByFile.value(normalizedTarget);
    records.reserve(indexes.size());
    for (int index : indexes) {
        if (index >= 0 && index < m_nativeSymbolRecords.size())
            records.append(m_nativeSymbolRecords.at(index));
    }
    return records;
}

QList<SemanticSymbolRecord> SemanticIndex::nativeSymbolRecordsByName(
    const QString& name) const
{
    if (name.isEmpty())
        return {};

    QList<SemanticSymbolRecord> records;
    const QList<int> indexes = m_nativeRecordIndexesByName.value(name);
    records.reserve(indexes.size());
    for (int index : indexes) {
        if (index >= 0 && index < m_nativeSymbolRecords.size())
            records.append(m_nativeSymbolRecords.at(index));
    }
    return records;
}

QList<SemanticSymbolRecord> SemanticIndex::nativeSymbolRecordsByOwner(
    const QString& ownerName) const
{
    QList<SemanticSymbolRecord> records;
    const QList<int> indexes = m_nativeRecordIndexesByOwner.value(ownerName);
    records.reserve(indexes.size());
    for (int index : indexes) {
        if (index >= 0 && index < m_nativeSymbolRecords.size())
            records.append(m_nativeSymbolRecords.at(index));
    }
    return records;
}

QList<SemanticSymbolRecord> SemanticIndex::nativeSymbolRecordsByDeclarationKind(
    SymbolTaxonomy::DeclarationKind declarationKind) const
{
    QList<SemanticSymbolRecord> records;
    const QList<int> indexes =
        m_nativeRecordIndexesByDeclarationKind.value(
            static_cast<int>(declarationKind));
    records.reserve(indexes.size());
    for (int index : indexes) {
        if (index >= 0 && index < m_nativeSymbolRecords.size())
            records.append(m_nativeSymbolRecords.at(index));
    }
    return records;
}

SemanticSymbolRecord SemanticIndex::nativeSymbolRecordByStableKey(
    const SymbolStableKey& key) const
{
    const QString stableKeyText = symbolStableKeyText(key);
    if (stableKeyText.isEmpty())
        return {};
    const int index = m_nativeStableKeyIndexes.value(stableKeyText, -1);
    if (index < 0 || index >= m_nativeSymbolRecords.size())
        return {};
    return m_nativeSymbolRecords.at(index);
}

bool SemanticIndex::hasNativeFileCoverage(const QString& fileName) const
{
    const QString normalizedTarget = normalizedStoreFileName(fileName);
    if (normalizedTarget.isEmpty())
        return false;
    return m_nativeCoveredFiles.contains(normalizedTarget);
}

bool SemanticIndex::hasNativeCachedFileContent(const QString& fileName) const
{
    if (m_nativeFileContents.contains(fileName))
        return true;
    const QString normalizedTarget = normalizedStoreFileName(fileName);
    if (normalizedTarget.isEmpty())
        return false;
    return m_nativeFileContents.contains(normalizedTarget);
}

QString SemanticIndex::nativeCachedFileContent(const QString& fileName) const
{
    if (m_nativeFileContents.contains(fileName))
        return m_nativeFileContents.value(fileName);
    const QString normalizedTarget = normalizedStoreFileName(fileName);
    if (normalizedTarget.isEmpty())
        return QString();
    return m_nativeFileContents.value(normalizedTarget);
}

void SemanticIndex::updateNativeFileState(
    const QString& fileName,
    const QString& content)
{
    const QString normalizedTarget = normalizedStoreFileName(fileName);
    if (normalizedTarget.isEmpty())
        return;

    NativeFileState state;
    state.contentHash = nativeContentHash(content);
    state.symbolRelevantHash = nativeSymbolRelevantHash(content);
    state.lastAnalyzedLineCount = content.count(QLatin1Char('\n')) + 1;
    m_nativeFileStates.insert(fileName, state);
    if (fileName != normalizedTarget)
        m_nativeFileStates.insert(normalizedTarget, state);
}

bool SemanticIndex::hasNativeFileState(const QString& fileName) const
{
    if (m_nativeFileStates.contains(fileName))
        return true;
    const QString normalizedTarget = normalizedStoreFileName(fileName);
    if (normalizedTarget.isEmpty())
        return false;
    return m_nativeFileStates.contains(normalizedTarget);
}

bool SemanticIndex::nativeContentAffectsSymbols(
    const QString& fileName,
    const QString& content) const
{
    NativeFileState state;
    if (m_nativeFileStates.contains(fileName)) {
        state = m_nativeFileStates.value(fileName);
    } else {
        const QString normalizedTarget = normalizedStoreFileName(fileName);
        state = m_nativeFileStates.value(normalizedTarget);
    }

    if (state.lastAnalyzedLineCount != content.count(QLatin1Char('\n')) + 1)
        return true;
    if (state.symbolRelevantHash.isEmpty())
        return true;
    return state.symbolRelevantHash != nativeSymbolRelevantHash(content);
}

void SemanticIndex::setWorkspaceFileAnalysisBands(
    const QHash<QString, SemanticAnalysisBandMetadata>& bands)
{
    m_workspaceFileAnalysisBands.clear();
    for (auto it = bands.constBegin(); it != bands.constEnd(); ++it) {
        const QString normalized = normalizedStoreFileName(it.key());
        if (!normalized.isEmpty() && it.value().isValid())
            m_workspaceFileAnalysisBands.insert(normalized, it.value());
    }
}

void SemanticIndex::clearWorkspaceFileAnalysisBands()
{
    m_workspaceFileAnalysisBands.clear();
}

SemanticAnalysisBandMetadata SemanticIndex::analysisBandForFile(
    const QString& fileName) const
{
    const QString normalized = normalizedStoreFileName(fileName);
    if (normalized.isEmpty())
        return {};
    return m_workspaceFileAnalysisBands.value(normalized);
}

SemanticAnalysisBandReport SemanticIndex::analysisBandReport(
    const QString& fileName) const
{
    return semanticAnalysisBandReportForRecords(getSymbolRecords(fileName));
}

SemanticSymbolRecord SemanticIndex::recordWithAnalysisBand(
    SemanticSymbolRecord record) const
{
    record.analysisBand = analysisBandForFile(record.location.fileName);
    return record;
}

QList<SemanticSymbolRecord> SemanticIndex::recordsWithAnalysisBands(
    QList<SemanticSymbolRecord> records) const
{
    for (SemanticSymbolRecord& record : records)
        record = recordWithAnalysisBand(record);
    return records;
}

void SemanticIndex::updateSymbolRecordsForFile(
    const QString& fileName,
    const QList<SemanticSymbolRecord>& records,
    const QString& content)
{
    replaceNativeSymbolRecordsForFile(fileName, records, content);

    if (m_relationshipEngine)
        m_relationshipEngine->buildFileRelationships(fileName);
}

void SemanticIndex::updateSymbolRecordsForFiles(
    const QList<SemanticFileSymbolUpdate>& updates,
    bool buildRelationships)
{
    if (updates.isEmpty())
        return;

    bool requiresIndexRebuild = false;
    for (const SemanticFileSymbolUpdate& update : updates) {
        const QString normalizedTarget =
            normalizedStoreFileName(update.fileName);
        if (!normalizedTarget.isEmpty()
            && (m_nativeCoveredFiles.contains(normalizedTarget)
                || m_nativeRecordHandlesByAnalysisFile.contains(normalizedTarget))) {
            requiresIndexRebuild = true;
            break;
        }
    }

    for (const SemanticFileSymbolUpdate& update : updates) {
        replaceNativeSymbolRecordsForFile(update.fileName,
                                          update.symbolRecords,
                                          update.content,
                                          false,
                                          !requiresIndexRebuild);
    }

    if (requiresIndexRebuild)
        rebuildNativeStoreIndexes();

    if (!buildRelationships || !m_relationshipEngine)
        return;
    for (const SemanticFileSymbolUpdate& update : updates)
        m_relationshipEngine->buildFileRelationships(update.fileName);
}

QList<SemanticSymbolRecord> SemanticIndex::getSymbolRecords(
    const QString& fileName) const
{
    const QList<SemanticSymbolRecord> nativeRecords = nativeSymbolRecords(fileName);
    if (!fileName.isEmpty() && hasNativeFileCoverage(fileName))
        return recordsWithAnalysisBands(nativeRecords);

    if (m_snapshot) {
        QList<SemanticSymbolRecord> records =
            recordsWithAnalysisBands(m_snapshot->getSymbolRecords(fileName));
        if (!fileName.isEmpty()) {
            if (!records.isEmpty())
                return records;
        } else {
            const QSet<QString> nativeFiles = m_nativeCoveredFiles;

            QList<SemanticSymbolRecord> mergedSnapshotRecords;
            for (const SemanticSymbolRecord& record : std::as_const(records)) {
                const QString normalized =
                    normalizedStoreFileName(record.location.fileName);
                if (!normalized.isEmpty() && nativeFiles.contains(normalized))
                    continue;
                mergedSnapshotRecords.append(record);
            }
            mergedSnapshotRecords.append(
                recordsWithAnalysisBands(m_nativeSymbolRecords));
            return mergedSnapshotRecords;
        }

        return records;
    }

    if (fileName.isEmpty())
        return recordsWithAnalysisBands(m_nativeSymbolRecords);
    return {};
}

QList<SemanticSymbolRecord> SemanticIndex::getSymbolRecordsByName(
    const QString& name) const
{
    if (name.isEmpty())
        return {};

    QList<SemanticSymbolRecord> records;
    if (m_snapshot) {
        records = recordsWithAnalysisBands(
            m_snapshot->getSymbolRecordsByName(name));
        removeRecordsCoveredByNativeFiles(&records, m_nativeCoveredFiles);
    }

    QList<SemanticSymbolRecord> nativeRecords =
        recordsWithAnalysisBands(nativeSymbolRecordsByName(name));
    if (!nativeRecords.isEmpty())
        records.append(nativeRecords);
    return records;
}

QList<SemanticSymbolRecord> SemanticIndex::getSymbolRecordsByOwner(
    const QString& ownerName) const
{
    QList<SemanticSymbolRecord> records;
    if (m_snapshot) {
        records = recordsWithAnalysisBands(
            m_snapshot->getSymbolRecordsByOwner(ownerName));
        removeRecordsCoveredByNativeFiles(&records, m_nativeCoveredFiles);
    }

    QList<SemanticSymbolRecord> nativeRecords =
        recordsWithAnalysisBands(nativeSymbolRecordsByOwner(ownerName));
    if (!nativeRecords.isEmpty())
        records.append(nativeRecords);
    return records;
}

QList<SemanticSymbolRecord> SemanticIndex::getSymbolRecordsByDeclarationKind(
    SymbolTaxonomy::DeclarationKind declarationKind) const
{
    QList<SemanticSymbolRecord> records;
    if (m_snapshot) {
        records = recordsWithAnalysisBands(
            m_snapshot->getSymbolRecordsByDeclarationKind(declarationKind));
        removeRecordsCoveredByNativeFiles(&records, m_nativeCoveredFiles);
    }

    QList<SemanticSymbolRecord> nativeRecords =
        recordsWithAnalysisBands(
            nativeSymbolRecordsByDeclarationKind(declarationKind));
    if (!nativeRecords.isEmpty())
        records.append(nativeRecords);
    return records;
}

SemanticSymbolRecord SemanticIndex::getSymbolRecordByStableKey(
    const SymbolStableKey& key) const
{
    if (!key.isValid())
        return {};

    const SemanticSymbolRecord nativeRecord =
        nativeSymbolRecordByStableKey(key);
    if (nativeRecord.isValid())
        return recordWithAnalysisBand(nativeRecord);
    if (hasNativeFileCoverage(key.fileName))
        return {};

    if (m_snapshot) {
        const SemanticSymbolRecord record =
            m_snapshot->getSymbolRecordByStableKey(key);
        if (record.isValid())
            return recordWithAnalysisBand(record);
    }

    for (const SemanticSymbolRecord& record : getSymbolRecords()) {
        if (record.stableKey == key)
            return record;
    }
    return {};
}

QString SemanticIndex::getCachedFileContent(const QString& fileName) const
{
    if (hasNativeCachedFileContent(fileName))
        return nativeCachedFileContent(fileName);

    if (m_snapshot) {
        const QString content = m_snapshot->getCachedFileContent(fileName);
        if (!content.isEmpty())
            return content;
    }

    return {};
}

QStringList SemanticIndex::getScopeSymbolNames(const QString& fileName, int cursorLine) const
{
    if (m_snapshot) {
        const QStringList snapshotNames =
            m_snapshot->getScopeSymbolNames(fileName, cursorLine);
        if (!snapshotNames.isEmpty())
            return snapshotNames;
    }

    QStringList result;
    if (fileName.isEmpty() || cursorLine < 0)
        return result;

    return scopeSymbolNamesForRecords(getSymbolRecords(fileName), cursorLine);
}

bool SemanticIndex::isValidModuleName(const QString& name) const
{
    return isSemanticModuleName(name);
}

int SemanticIndex::findEndModuleLine(const QString& fileName,
                                     const SemanticSymbolRecord& moduleRecord) const
{
    if (moduleRecord.declarationKind != SymbolTaxonomy::DeclarationKind::Module)
        return -1;

    if (moduleRecord.location.endLine >= moduleRecord.location.startLine
        && moduleRecord.location.endLine > 0) {
        return moduleRecord.location.endLine - 1;
    }

    const QString content = getCachedFileContent(fileName);
    if (!content.isEmpty())
        return findEndModuleLineInContent(content, moduleRecord);

    return -1;
}

bool SemanticIndex::contentAffectsSymbols(const QString& fileName,
                                          const QString& content) const
{
    if (hasNativeFileState(fileName))
        return nativeContentAffectsSymbols(fileName, content);

    return true;
}

void SemanticIndex::refreshStructTypedefEnumForFile(const QString& fileName,
                                                    const QString& content)
{
    updateNativeFileState(fileName, content);
    m_nativeFileContents.insert(fileName, content);
    const QString normalizedTarget = normalizedStoreFileName(fileName);
    if (!normalizedTarget.isEmpty() && normalizedTarget != fileName)
        m_nativeFileContents.insert(normalizedTarget, content);
}

void SemanticIndex::attachRelationshipEngine(SymbolRelationshipEngine* engine)
{
    m_relationshipEngine = engine;
    if (engine) {
        engine->setSymbolRecordProvider([this](const QString& fileName) {
            return getSymbolRecords(fileName);
        });
        engine->rebuildAllRelationships();
    }
}

SymbolRelationshipEngine* SemanticIndex::relationshipEngine() const
{
    return m_relationshipEngine;
}

std::unique_ptr<SmartRelationshipBuilder> SemanticIndex::createRelationshipBuilder(
    SymbolRelationshipEngine* engine,
    SlangManager* slangManager,
    QObject* parent) const
{
    return std::make_unique<SmartRelationshipBuilder>(
        engine,
        slangManager,
        [this](const QString& fileName) {
            return getSymbolRecords(fileName);
        },
        parent);
}

QList<SemanticDiagnostic> SemanticIndex::getDiagnostics(const QString& fileName) const
{
    if (m_snapshot)
        return m_snapshot->getDiagnostics(fileName);

    Q_UNUSED(fileName)
    return {};
}
