#include "semanticindex.h"

#include "semanticcollectoradapter.h"
#include "semanticindexsnapshot.h"
#include "smartrelationshipbuilder.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace {
QString normalizedStoreFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool isSemanticModuleName(const QString& name)
{
    if (name.isEmpty())
        return false;
    static const QRegularExpression svIdentifier(
        QStringLiteral("^[a-zA-Z_][a-zA-Z0-9_]*$"));
    return svIdentifier.match(name).hasMatch();
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
    static const QRegularExpression moduleWord(QStringLiteral("\\bmodule\\b"));
    static const QRegularExpression endmoduleWord(QStringLiteral("\\bendmodule\\b"));
    for (int i = scanStart; i < lines.size(); ++i) {
        const QString code = stripCommentsFromLine(lines.at(i), inBlockComment);
        if (code.contains(moduleWord))
            ++moduleDepth;
        if (code.contains(endmoduleWord)) {
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

}

void SemanticIndex::replaceNativeSymbolRecordsForFile(
    const QString& fileName,
    const QList<SemanticSymbolRecord>& records,
    const QString& content)
{
    const QString normalizedTarget = normalizedStoreFileName(fileName);
    if (normalizedTarget.isEmpty())
        return;

    for (int i = m_nativeSymbolRecords.size() - 1; i >= 0; --i) {
        const QString normalized =
            normalizedStoreFileName(m_nativeSymbolRecords.at(i).location.fileName);
        if (normalized == normalizedTarget)
            m_nativeSymbolRecords.removeAt(i);
    }

    m_nativeFileContents.insert(fileName, content);
    if (fileName != normalizedTarget)
        m_nativeFileContents.insert(normalizedTarget, content);

    for (SemanticSymbolRecord record : records) {
        if (!record.isValid())
            continue;
        record.location.fileName = fileName;
        if (record.localHandle <= 0)
            record.localHandle = m_nextNativeLocalHandle++;
        record.stableKey.fileName = normalizedTarget;
        record.stableKey.symbolName = record.name;
        record.stableKey.declarationKind = record.declarationKind;
        record.stableKey.ownerScope = record.owner.name;
        m_nativeSymbolRecords.append(record);
    }

    rebuildNativeStoreIndexes();
}

void SemanticIndex::rebuildNativeStoreIndexes()
{
    m_nativeRecordIndexesByFile.clear();
    m_nativeStableKeyIndexes.clear();
    for (int i = 0; i < m_nativeSymbolRecords.size(); ++i) {
        const SemanticSymbolRecord& record = m_nativeSymbolRecords.at(i);
        const QString normalized =
            normalizedStoreFileName(record.location.fileName);
        if (!normalized.isEmpty())
            m_nativeRecordIndexesByFile[normalized].append(i);

        const QString stableKeyText = symbolStableKeyText(record.stableKey);
        if (!stableKeyText.isEmpty())
            m_nativeStableKeyIndexes.insert(stableKeyText, i);
    }
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

QList<SemanticSymbolRecord> SemanticIndex::nativeSymbolRecordsExcludingFiles(
    const QSet<QString>& normalizedFileNames) const
{
    QList<SemanticSymbolRecord> records;
    records.reserve(m_nativeSymbolRecords.size());
    for (const SemanticSymbolRecord& record : m_nativeSymbolRecords) {
        const QString normalized =
            normalizedStoreFileName(record.location.fileName);
        if (!normalized.isEmpty() && normalizedFileNames.contains(normalized))
            continue;
        records.append(record);
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

void SemanticIndex::updateSymbolRecordsForFile(
    const QString& fileName,
    const QList<SemanticSymbolRecord>& records,
    const QString& content)
{
    replaceNativeSymbolRecordsForFile(fileName, records, content);

    updateSymbolDatabaseRecordsForFile(
        symbolDatabase(),
        fileName,
        records,
        content);
}

QList<SemanticSymbolRecord> SemanticIndex::getSymbolRecords(
    const QString& fileName) const
{
    sym_list* db = symbolDatabase();
    if (m_snapshot) {
        QList<SemanticSymbolRecord> records =
            m_snapshot->getSymbolRecords(fileName);
        if (!fileName.isEmpty()) {
            if (!records.isEmpty())
                return records;
            records = nativeSymbolRecords(fileName);
            if (!records.isEmpty())
                return records;
        } else {
            QSet<QString> snapshotFiles;
            for (const SemanticSymbolRecord& record : std::as_const(records)) {
                const QString normalized =
                    normalizedStoreFileName(record.location.fileName);
                if (!normalized.isEmpty())
                    snapshotFiles.insert(normalized);
            }
            const QList<SemanticSymbolRecord> nativeRecords =
                nativeSymbolRecordsExcludingFiles(snapshotFiles);
            if (!nativeRecords.isEmpty()) {
                records.append(nativeRecords);
                return records;
            }
            records.append(
                semanticSymbolRecordsForDatabaseExcludingFiles(
                    db,
                    snapshotFiles));
            return records;
        }
    }

    const QList<SemanticSymbolRecord> records = nativeSymbolRecords(fileName);
    if (!records.isEmpty())
        return records;

    return semanticSymbolRecordsForDatabase(db, fileName);
}

SemanticSymbolRecord SemanticIndex::getSymbolRecordByStableKey(
    const SymbolStableKey& key) const
{
    if (!key.isValid())
        return {};

    if (m_snapshot) {
        const SemanticSymbolRecord record =
            m_snapshot->getSymbolRecordByStableKey(key);
        if (record.isValid())
            return record;
    }

    const SemanticSymbolRecord nativeRecord =
        nativeSymbolRecordByStableKey(key);
    if (nativeRecord.isValid())
        return nativeRecord;

    for (const SemanticSymbolRecord& record : getSymbolRecords()) {
        if (record.stableKey == key)
            return record;
    }
    return {};
}

QString SemanticIndex::getCachedFileContent(const QString& fileName) const
{
    if (m_snapshot) {
        const QString content = m_snapshot->getCachedFileContent(fileName);
        if (!content.isEmpty())
            return content;
    }

    if (hasNativeCachedFileContent(fileName))
        return nativeCachedFileContent(fileName);

    return symbolDatabase()->getCachedFileContent(fileName);
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
    return symbolDatabase()->contentAffectsSymbols(fileName, content);
}

void SemanticIndex::refreshStructTypedefEnumForFile(const QString& fileName,
                                                    const QString& content)
{
    symbolDatabase()->refreshStructTypedefEnumForFile(fileName, content);
}

void SemanticIndex::attachRelationshipEngine(SymbolRelationshipEngine* engine) const
{
    symbolDatabase()->setRelationshipEngine(engine);
}

std::unique_ptr<SmartRelationshipBuilder> SemanticIndex::createRelationshipBuilder(
    SymbolRelationshipEngine* engine,
    SlangManager* slangManager,
    QObject* parent) const
{
    return std::make_unique<SmartRelationshipBuilder>(
        engine, symbolDatabase(), slangManager, parent);
}

QList<SemanticDiagnostic> SemanticIndex::getDiagnostics(const QString& fileName) const
{
    if (m_snapshot)
        return m_snapshot->getDiagnostics(fileName);

    Q_UNUSED(fileName)
    return {};
}
