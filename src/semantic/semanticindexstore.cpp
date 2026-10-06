#include "semanticindex.h"
#include "semanticstableidentity.h"

#include "semanticindexsnapshot.h"
#include "smartrelationshipbuilder.h"
#include "svtokenutils.h"
#include "semanticchangeclassifier.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace {
QString normalizedStoreFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString result = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

bool isSemanticModuleName(const QString& name)
{
    return SvTokenUtils::isIdentifier(name);
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

QString containingModuleNameForRecords(
    const QList<SemanticSymbolRecord>& records,
    int cursorLine)
{
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
    return containingModule;
}

QString presentationIdentity(const SemanticSymbolRecord& record)
{
    // owner.name intentionally contains only the semantic owner (for
    // example, the module name), not every generate / named-block scope.
    // The exact declaration range is therefore required to distinguish
    // same-named constants in nested scopes of that owner.
    return QStringList{
               normalizedStoreFileName(record.location.fileName),
               QString::number(record.location.position),
               QString::number(record.location.length),
               QString::number(record.location.startLine),
               QString::number(record.location.startColumn),
               QString::number(record.location.endLine),
               QString::number(record.location.endColumn),
               record.name,
               QString::number(static_cast<int>(record.declarationKind)),
               QString::number(static_cast<int>(record.collectorKind)),
               record.owner.name,
           }
        .join(QLatin1Char('|'));
}

}

void SemanticIndex::setWorkspaceFileAnalysisBands(
    const QHash<QString, SemanticAnalysisBandMetadata>& bands)
{
    ++m_workspaceAnalysisBandRevision;
    m_workspaceFileAnalysisBands.clear();
    for (auto it = bands.constBegin(); it != bands.constEnd(); ++it) {
        const QString normalized = normalizedStoreFileName(it.key());
        if (!normalized.isEmpty() && it.value().isValid())
            m_workspaceFileAnalysisBands.insert(normalized, it.value());
    }
}

void SemanticIndex::clearWorkspaceFileAnalysisBands()
{
    ++m_workspaceAnalysisBandRevision;
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
    if (fileName.isEmpty() && m_preparedAnalysisBandReportValid)
        return m_preparedAnalysisBandReport;
    return semanticAnalysisBandReportForRecords(getSymbolRecords(fileName));
}

bool SemanticIndex::hasPreparedAnalysisBandReport() const
{
    return m_preparedAnalysisBandReportValid;
}

SemanticSymbolRecord SemanticIndex::recordWithAnalysisBand(
    SemanticSymbolRecord record) const
{
    const auto band = analysisBandForFile(record.location.fileName);
    if (band.isValid())
        record.analysisBand = band;
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
    const QString& fileName, const QList<SemanticSymbolRecord>& records,
    const QString& content)
{
    SemanticFileSymbolUpdate update;
    update.fileName = fileName;
    update.symbolRecords = records;
    update.content = content;
    updateSymbolRecordsForFiles({update});
}

void SemanticIndex::updateSymbolRecordsForFiles(
    const QList<SemanticFileSymbolUpdate>& updates, bool buildRelationships)
{
    if (updates.isEmpty())
        return;
    const auto previous = m_snapshot ? *m_snapshot : SemanticIndexSnapshot{};
    QList<SemanticFileSymbolUpdate> normalized;
    QStringList changedInputs;
    for (auto update : updates) {
        if (update.fileName.isEmpty())
            continue;
        update.fileName = normalizedStoreFileName(update.fileName);
        if (!update.content.isEmpty() && update.content.front() == QChar(u'\ufeff'))
            update.content.remove(0, 1);
        update.content.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        update.content.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        const bool sameInput = !update.content.isNull()
            && previous.fileContentsView().contains(update.fileName)
            && previous.getCachedFileContent(update.fileName) == update.content;
        if (!sameInput)
            changedInputs.append(update.fileName);
        QHash<QString, SemanticSymbolPresentation> oldPresentations;
        for (const auto& old : previous.getSymbolRecords(update.fileName))
            oldPresentations.insert(presentationIdentity(old), old.presentation);
        QList<SemanticSymbolRecord> records;
        for (auto record : update.symbolRecords) {
            if (!record.isValid())
                continue;
            if (record.location.fileName.isEmpty())
                record.location.fileName = update.fileName;
            record = recordWithAnalysisBand(std::move(record));
            record.stableKey = semanticDeclarationKey(record);
            const auto old = oldPresentations.constFind(presentationIdentity(record));
            if (old != oldPresentations.cend()) {
                for (auto it = old->instanceInfoByPath.cbegin(); it != old->instanceInfoByPath.cend(); ++it) {
                    if (record.presentation.instanceInfoByPath.contains(it.key()))
                        continue;
                    auto value = sameInput ? it.value() : SemanticElaboratedSymbolInfo{};
                    if (!sameInput)
                        value.failureReason = QStringLiteral("Workspace elaboration is unavailable because the source document changed.");
                    record.presentation.instanceInfoByPath.insert(it.key(), value);
                }
                for (auto it = old->declaredTypeFactsByPath.cbegin(); it != old->declaredTypeFactsByPath.cend(); ++it) {
                    if (record.presentation.declaredTypeFactsByPath.contains(it.key()))
                        continue;
                    auto value = sameInput ? it.value() : SemanticDeclaredTypeFacts{};
                    if (!sameInput)
                        value.failureReason = QStringLiteral("Workspace declared-type facts are unavailable because the source document changed.");
                    record.presentation.declaredTypeFactsByPath.insert(it.key(), value);
                }
            }
            records.append(std::move(record));
        }
        update.symbolRecords = std::move(records);
        normalized.append(std::move(update));
    }
    // Compatibility writes are immutable shard transactions. All readers see
    // this publication immediately; no mutable overlay or parallel indexes.
    const auto replaced = previous.withReplacedFiles(normalized, {}, {}, changedInputs);
    // Synchronous compatibility callers can supply handles for their local
    // relationship builder. Keep those handles within the new snapshot; the
    // incremental production pipeline allocates its own handles directly.
    QHash<QString, int> requestedHandles;
    for (const auto& update : normalized)
        for (const auto& record : update.symbolRecords)
            if (record.localHandle >= 0)
                requestedHandles.insert(symbolStableKeyText(record.stableKey), record.localHandle);
    auto records = replaced.getSymbolRecords();
    for (auto& record : records) {
        const auto handle = requestedHandles.constFind(symbolStableKeyText(record.stableKey));
        if (handle != requestedHandles.cend())
            record.localHandle = handle.value();
    }
    setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(std::move(records),
            replaced.relationships(), replaced.rawDiagnostics(), replaced.fileContents())
            .withDiagnosticDisplayLimit(previous.diagnosticDisplayLimit())));
    if (buildRelationships && m_relationshipEngine) {
        for (const auto& update : normalized)
            m_relationshipEngine->buildFileRelationships(update.fileName);
        setSnapshot(captureSnapshotPreservingDiagnostics());
    }
}

QList<SemanticSymbolRecord> SemanticIndex::getSymbolRecords(const QString& fileName) const
{
    return m_snapshot ? recordsWithAnalysisBands(m_snapshot->getSymbolRecords(fileName))
                      : QList<SemanticSymbolRecord>{};
}

QList<SemanticSymbolRecord> SemanticIndex::getSymbolRecordsByName(const QString& name) const
{
    return m_snapshot ? recordsWithAnalysisBands(m_snapshot->getSymbolRecordsByName(name))
                      : QList<SemanticSymbolRecord>{};
}

QList<SemanticSymbolRecord> SemanticIndex::getSymbolRecordsByOwner(const QString& owner) const
{
    return m_snapshot ? recordsWithAnalysisBands(m_snapshot->getSymbolRecordsByOwner(owner))
                      : QList<SemanticSymbolRecord>{};
}

QList<SemanticSymbolRecord> SemanticIndex::getSymbolRecordsByDeclarationKind(
    SymbolTaxonomy::DeclarationKind kind) const
{
    return m_snapshot ? recordsWithAnalysisBands(m_snapshot->getSymbolRecordsByDeclarationKind(kind))
                      : QList<SemanticSymbolRecord>{};
}

SemanticSymbolRecord SemanticIndex::getSymbolRecordByStableKey(const SymbolStableKey& key) const
{
    return m_snapshot ? recordWithAnalysisBand(m_snapshot->getSymbolRecordByStableKey(key))
                      : SemanticSymbolRecord{};
}

QString SemanticIndex::getCachedFileContent(const QString& fileName) const
{
    return m_snapshot ? m_snapshot->getCachedFileContent(fileName) : QString{};
}

QStringList SemanticIndex::getScopeSymbolNames(const QString& fileName, int cursorLine) const
{
    QStringList result;
    if (fileName.isEmpty() || cursorLine < 0)
        return result;

    const QList<SemanticSymbolRecord> fileRecords = getSymbolRecords(fileName);
    result = scopeSymbolNamesForRecords(fileRecords, cursorLine);
    QSet<QString> seenNames;
    for (const QString& name : result)
        seenNames.insert(name.toCaseFolded());

    SemanticQueryContext context;
    context.fileName = fileName;
    context.cursorLine = cursorLine;
    context.moduleName = containingModuleNameForRecords(fileRecords, cursorLine);

    QHash<QString, QList<SemanticSymbolRecord>> importedRecordsByName;
    for (const SemanticSymbolRecord& record :
         getVisibleImportedPackageRecords(context)) {
        if (!SymbolTaxonomy::isPackageVisibleDefinition(
                semanticMetadataForSymbolRecord(record))) {
            continue;
        }
        importedRecordsByName[record.name.toCaseFolded()].append(record);
    }

    for (auto it = importedRecordsByName.constBegin();
         it != importedRecordsByName.constEnd();
         ++it) {
        if (seenNames.contains(it.key()))
            continue;
        QSet<QString> owners;
        for (const SemanticSymbolRecord& record : it.value())
            owners.insert(record.owner.name);
        if (owners.size() != 1 || it.value().isEmpty())
            continue;
        seenNames.insert(it.key());
        result.append(it.value().first().name);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool SemanticIndex::isValidModuleName(const QString& name) const
{
    return isSemanticModuleName(name);
}

bool SemanticIndex::contentAffectsSymbols(const QString& fileName, const QString& content) const
{
    if (!m_snapshot || !m_snapshot->fileContentsView().contains(normalizedStoreFileName(fileName)))
        return true;
    const QString previous = m_snapshot->getCachedFileContent(fileName);
    if (previous == content)
        return false;
    // Position-changing trivia still requires a remap before locations can be
    // queried. Share the real classifier instead of keeping another text hash.
    return previous.count(QLatin1Char('\n')) != content.count(QLatin1Char('\n'))
        || SemanticChangeClassifier{}.classify(fileName, previous, content).impact
            != SemanticChangeImpact::TriviaOnly;
}

void SemanticIndex::attachRelationshipEngine(SymbolRelationshipEngine* engine)
{
    if (m_relationshipEngine && m_relationshipEngine != engine)
        m_relationshipEngine->setSymbolRecordProvider({});
    m_relationshipEngine = engine;
    if (engine) {
        engine->setSymbolRecordProvider([this](const QString& fileName) {
            return getSymbolRecords(fileName);
        });
        if (m_snapshot) {
            engine->replaceRelationshipsFromSnapshot(
                m_snapshot->getSymbolRecords(),
                m_snapshot->relationships());
        } else {
            engine->rebuildAllRelationships();
        }
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
