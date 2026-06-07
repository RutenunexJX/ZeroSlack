#include "semanticindex.h"

#include "completionmanager.h"
#include "scope_tree.h"
#include "semanticindexsnapshot.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <utility>

std::unique_ptr<SemanticIndex> SemanticIndex::instance = nullptr;

namespace {
QString normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}
}

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

std::shared_ptr<const SemanticIndexSnapshot>
SemanticIndex::captureSnapshotPreservingDiagnostics() const
{
    const QList<SemanticDiagnostic> diagnostics =
        m_snapshot ? m_snapshot->diagnostics() : QList<SemanticDiagnostic>();
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolDatabase(symbolDatabase(), diagnostics));
}

QList<sym_list::SymbolInfo> SemanticIndex::getSymbols(const QString& fileName) const
{
    if (m_snapshot)
        return m_snapshot->getSymbols(fileName);

    sym_list* db = symbolDatabase();
    if (fileName.isEmpty())
        return db->getAllSymbols();

    QList<sym_list::SymbolInfo> symbols = db->findSymbolsByFileName(fileName);
    if (!symbols.isEmpty())
        return symbols;

    const QString normalizedTarget = normalizedFileName(fileName);
    const QList<sym_list::SymbolInfo> allSymbols = db->getAllSymbols();
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (normalizedFileName(symbol.fileName) == normalizedTarget)
            symbols.append(symbol);
    }
    return symbols;
}

QList<sym_list::SymbolInfo> SemanticIndex::getSymbolsByType(sym_list::sym_type_e type) const
{
    if (m_snapshot)
        return m_snapshot->getSymbolsByType(type);

    return symbolDatabase()->findSymbolsByType(type);
}

sym_list::SymbolInfo SemanticIndex::getSymbolById(int symbolId) const
{
    if (m_snapshot)
        return m_snapshot->getSymbolById(symbolId);

    if (symbolId < 0) {
        sym_list::SymbolInfo missing;
        missing.symbolId = -1;
        return missing;
    }

    sym_list::SymbolInfo symbol = symbolDatabase()->getSymbolById(symbolId);
    if (symbol.symbolId != -1)
        return symbol;

    const QList<sym_list::SymbolInfo> allSymbols = getSymbols();
    for (const sym_list::SymbolInfo& candidate : allSymbols) {
        if (candidate.symbolId == symbolId)
            return candidate;
    }

    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    return missing;
}

int SemanticIndex::findSymbolId(const QString& name,
                                const SemanticQueryContext& context) const
{
    if (m_snapshot)
        return m_snapshot->findSymbolId(name, context);

    const QList<sym_list::SymbolInfo> symbols = findDefinitions(name, context);
    if (symbols.isEmpty())
        return -1;
    return symbols.first().symbolId;
}

QString SemanticIndex::getCachedFileContent(const QString& fileName) const
{
    if (m_snapshot)
        return m_snapshot->getCachedFileContent(fileName);

    return symbolDatabase()->getCachedFileContent(fileName);
}

QStringList SemanticIndex::getScopeSymbolNames(const QString& fileName, int cursorLine) const
{
    if (m_snapshot)
        return m_snapshot->getScopeSymbolNames(fileName, cursorLine);

    QStringList result;
    if (fileName.isEmpty() || cursorLine < 0)
        return result;

    ScopeManager* scopeManager = symbolDatabase()->getScopeManager();
    if (!scopeManager)
        return result;

    ScopeNode* scope = scopeManager->findScopeAt(fileName, cursorLine);
    QSet<QString> seen;
    while (scope) {
        for (auto it = scope->symbols.constBegin(); it != scope->symbols.constEnd(); ++it) {
            const QString& name = it.key();
            if (seen.contains(name))
                continue;
            seen.insert(name);
            result.append(name);
        }
        scope = scope->parent;
    }
    return result;
}

void SemanticIndex::refreshStructTypedefEnumForFile(const QString& fileName,
                                                    const QString& content)
{
    symbolDatabase()->refreshStructTypedefEnumForFile(fileName, content);
}

QList<sym_list::SymbolInfo> SemanticIndex::findDefinitions(
    const QString& name,
    const SemanticQueryContext& context) const
{
    if (name.isEmpty())
        return {};

    if (m_snapshot)
        return m_snapshot->findDefinitions(name, context);

    QList<sym_list::SymbolInfo> symbols = symbolDatabase()->findSymbolsByName(name);
    if (symbols.isEmpty())
        return symbols;

    return sortedDefinitions(symbols, context);
}

QStringList SemanticIndex::findCompletions(const SemanticQueryContext& context) const
{
    CompletionManager* completions = CompletionManager::getInstance();
    if (!context.fileName.isEmpty() && context.cursorLine > 0) {
        return completions->getCompletions(context.prefix, context.fileName, context.cursorLine);
    }
    return completions->getContextAwareCompletions(context.prefix, context.moduleName);
}

QList<SemanticRelationship> SemanticIndex::getRelationships(int symbolId, bool outgoing) const
{
    if (m_snapshot)
        return m_snapshot->getRelationships(symbolId, outgoing);

    QList<SemanticRelationship> result;
    if (symbolId < 0)
        return result;

    SymbolRelationshipEngine* engine = symbolDatabase()->getRelationshipEngine();
    if (!engine)
        return result;

    QSet<QString> seen;
    for (SymbolRelationshipEngine::RelationType type : relationshipTypes()) {
        const QList<int> related = engine->getRelatedSymbols(symbolId, type, outgoing);
        for (int otherId : related) {
            SemanticRelationship rel;
            rel.fromId = outgoing ? symbolId : otherId;
            rel.toId = outgoing ? otherId : symbolId;
            rel.type = type;

            const QString key = QStringLiteral("%1:%2:%3")
                                    .arg(rel.fromId)
                                    .arg(rel.toId)
                                    .arg(static_cast<int>(rel.type));
            if (seen.contains(key))
                continue;
            seen.insert(key);
            result.append(rel);
        }
    }

    return result;
}

QList<SemanticRelationship> SemanticIndex::getRelationships(const QString& scopeName,
                                                            bool outgoing) const
{
    const QList<sym_list::SymbolInfo> defs = findDefinitions(scopeName);
    if (defs.isEmpty())
        return {};
    return getRelationships(defs.first().symbolId, outgoing);
}

QList<SemanticDiagnostic> SemanticIndex::getDiagnostics(const QString& fileName) const
{
    if (m_snapshot)
        return m_snapshot->getDiagnostics(fileName);

    Q_UNUSED(fileName)
    return {};
}

QList<sym_list::SymbolInfo> SemanticIndex::sortedDefinitions(
    const QList<sym_list::SymbolInfo>& symbols,
    const SemanticQueryContext& context) const
{
    QList<sym_list::SymbolInfo> sorted = symbols;
    const QString normalizedContextFile = normalizedFileName(context.fileName);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&context, &normalizedContextFile](const sym_list::SymbolInfo& a,
                                                        const sym_list::SymbolInfo& b) {
        auto score = [&context, &normalizedContextFile](const sym_list::SymbolInfo& s) {
            int value = 0;
            if (!normalizedContextFile.isEmpty()
                && normalizedFileName(s.fileName) == normalizedContextFile)
                value += 100;
            if (!context.moduleName.isEmpty() && s.moduleScope == context.moduleName)
                value += 50;
            if (s.symbolType == sym_list::sym_module
                || s.symbolType == sym_list::sym_interface
                || s.symbolType == sym_list::sym_package)
                value += 10;
            return value;
        };

        const int aScore = score(a);
        const int bScore = score(b);
        if (aScore != bScore)
            return aScore > bScore;
        if (a.fileName != b.fileName)
            return a.fileName < b.fileName;
        if (a.startLine != b.startLine)
            return a.startLine < b.startLine;
        return a.symbolId < b.symbolId;
    });
    return sorted;
}

QList<SymbolRelationshipEngine::RelationType> SemanticIndex::relationshipTypes() const
{
    return {
        SymbolRelationshipEngine::CONTAINS,
        SymbolRelationshipEngine::REFERENCES,
        SymbolRelationshipEngine::INSTANTIATES,
        SymbolRelationshipEngine::CALLS,
        SymbolRelationshipEngine::INHERITS,
        SymbolRelationshipEngine::IMPLEMENTS,
        SymbolRelationshipEngine::ASSIGNS_TO,
        SymbolRelationshipEngine::READS_FROM,
        SymbolRelationshipEngine::CLOCKS,
        SymbolRelationshipEngine::RESETS,
        SymbolRelationshipEngine::GENERATES,
        SymbolRelationshipEngine::CONSTRAINS,
    };
}
