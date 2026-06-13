#include "semanticindex.h"

#include "completionservice.h"
#include "semanticindexsnapshot.h"

#include <utility>

std::unique_ptr<SemanticIndex> SemanticIndex::instance = nullptr;

SemanticIndex* SemanticIndex::getInstance()
{
    if (!instance)
        instance = std::make_unique<SemanticIndex>();
    return instance.get();
}

SemanticIndex::SemanticIndex(sym_list* symbolDatabase)
    : m_symbolDatabase(symbolDatabase ? symbolDatabase : sym_list::getInstance())
{
}

SemanticIndex::~SemanticIndex() = default;

void SemanticIndex::setSymbolDatabase(sym_list* symbolDatabase)
{
    m_symbolDatabase = symbolDatabase ? symbolDatabase : sym_list::getInstance();
}

sym_list* SemanticIndex::symbolDatabase() const
{
    return m_symbolDatabase ? m_symbolDatabase : sym_list::getInstance();
}

void SemanticIndex::setSnapshot(std::shared_ptr<const SemanticIndexSnapshot> snapshot)
{
    m_snapshot = std::move(snapshot);
}

void SemanticIndex::clearSnapshot()
{
    m_snapshot.reset();
}

std::shared_ptr<const SemanticIndexSnapshot> SemanticIndex::snapshot() const
{
    return m_snapshot;
}

void SemanticIndex::publishCompleteSnapshot(QList<SemanticDiagnostic> diagnostics)
{
    setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolDatabase(symbolDatabase(), std::move(diagnostics))));
}

void SemanticIndex::publishSnapshotReplacingDiagnostics(
    const QStringList& fileNames,
    const QList<SemanticDiagnostic>& diagnostics)
{
    setSnapshot(captureSnapshotReplacingDiagnostics(fileNames, diagnostics));
}

std::shared_ptr<const SemanticIndexSnapshot>
SemanticIndex::captureSnapshotPreservingDiagnostics() const
{
    const QList<SemanticDiagnostic> diagnostics =
        m_snapshot ? m_snapshot->diagnostics() : QList<SemanticDiagnostic>();
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolDatabase(symbolDatabase(), diagnostics));
}

std::shared_ptr<const SemanticIndexSnapshot>
SemanticIndex::captureSnapshotReplacingDiagnostics(
    const QStringList& fileNames,
    const QList<SemanticDiagnostic>& diagnostics) const
{
    QList<SemanticDiagnostic> mergedDiagnostics = diagnostics;
    if (m_snapshot) {
        mergedDiagnostics =
            m_snapshot->withReplacedDiagnostics(fileNames, diagnostics).diagnostics();
    }
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolDatabase(symbolDatabase(), mergedDiagnostics));
}

std::shared_ptr<const SemanticIndexSnapshot>
SemanticIndex::beginRelationshipAnalysisSnapshot()
{
    std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot =
        captureSnapshotPreservingDiagnostics();
    setSnapshot(baseSnapshot);
    return baseSnapshot;
}

std::shared_ptr<const SemanticIndexSnapshot>
SemanticIndex::snapshotWithAdditionalRelationships(
    std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot,
    const QList<SemanticRelationship>& relationships) const
{
    if (!baseSnapshot)
        return baseSnapshot;
    return std::make_shared<const SemanticIndexSnapshot>(
        baseSnapshot->withAdditionalRelationships(relationships));
}

bool SemanticIndex::publishSnapshotIfCurrent(
    std::shared_ptr<const SemanticIndexSnapshot> expectedCurrentSnapshot,
    std::shared_ptr<const SemanticIndexSnapshot> nextSnapshot)
{
    if (expectedCurrentSnapshot && snapshot() != expectedCurrentSnapshot)
        return false;
    if (nextSnapshot)
        setSnapshot(std::move(nextSnapshot));
    return true;
}

QStringList SemanticIndex::findCompletions(const SemanticQueryContext& context) const
{
    CompletionQuery query;
    query.prefix = context.prefix;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.cursorLine = context.cursorLine;
    query.cursorPosition = context.cursorPosition;

    CompletionService completions(const_cast<SemanticIndex*>(this));
    if (!context.fileName.isEmpty() && context.cursorLine > 0) {
        return completions.findScopeCompletions(query);
    }
    return completions.findCompletions(query);
}
