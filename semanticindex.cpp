#include "semanticindex.h"

#include "completionmanager.h"

#include <QSet>
#include <algorithm>

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

QList<sym_list::SymbolInfo> SemanticIndex::getSymbols(const QString& fileName) const
{
    sym_list* db = symbolDatabase();
    return fileName.isEmpty() ? db->getAllSymbols() : db->findSymbolsByFileName(fileName);
}

QList<sym_list::SymbolInfo> SemanticIndex::getSymbolsByType(sym_list::sym_type_e type) const
{
    return symbolDatabase()->findSymbolsByType(type);
}

QList<sym_list::SymbolInfo> SemanticIndex::findDefinitions(
    const QString& name,
    const SemanticQueryContext& context) const
{
    if (name.isEmpty())
        return {};

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
    Q_UNUSED(fileName)
    return {};
}

QList<sym_list::SymbolInfo> SemanticIndex::sortedDefinitions(
    const QList<sym_list::SymbolInfo>& symbols,
    const SemanticQueryContext& context) const
{
    QList<sym_list::SymbolInfo> sorted = symbols;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&context](const sym_list::SymbolInfo& a, const sym_list::SymbolInfo& b) {
        auto score = [&context](const sym_list::SymbolInfo& s) {
            int value = 0;
            if (!context.fileName.isEmpty() && s.fileName == context.fileName)
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
