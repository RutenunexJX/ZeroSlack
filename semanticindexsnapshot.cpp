#include "semanticindexsnapshot.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <utility>

namespace {
QString normalizedSnapshotFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QList<SymbolRelationshipEngine::RelationType> snapshotRelationshipTypes()
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

sym_list::SymbolInfo missingSnapshotSymbol()
{
    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    return missing;
}
}

SemanticIndexSnapshot::SemanticIndexSnapshot(
    QList<sym_list::SymbolInfo> symbols,
    QList<SemanticRelationship> relationships,
    QList<SemanticDiagnostic> diagnostics)
    : m_symbols(std::move(symbols)),
      m_relationships(std::move(relationships)),
      m_diagnostics(std::move(diagnostics))
{
}

SemanticIndexSnapshot SemanticIndexSnapshot::fromSymbolDatabase(sym_list* symbolDatabase)
{
    if (!symbolDatabase)
        return SemanticIndexSnapshot();

    const QList<sym_list::SymbolInfo> symbols = symbolDatabase->getAllSymbols();
    QList<SemanticRelationship> relationships;
    SymbolRelationshipEngine* engine = symbolDatabase->getRelationshipEngine();
    if (engine) {
        QSet<QString> seen;
        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (symbol.symbolId < 0)
                continue;
            for (SymbolRelationshipEngine::RelationType type : snapshotRelationshipTypes()) {
                const QList<int> related = engine->getRelatedSymbols(symbol.symbolId, type, true);
                for (int relatedId : related) {
                    SemanticRelationship relationship;
                    relationship.fromId = symbol.symbolId;
                    relationship.toId = relatedId;
                    relationship.type = type;

                    const QString key = QStringLiteral("%1:%2:%3")
                                            .arg(relationship.fromId)
                                            .arg(relationship.toId)
                                            .arg(static_cast<int>(relationship.type));
                    if (seen.contains(key))
                        continue;
                    seen.insert(key);
                    relationships.append(relationship);
                }
            }
        }
    }

    return SemanticIndexSnapshot(symbols, relationships);
}

QList<sym_list::SymbolInfo> SemanticIndexSnapshot::getSymbols(const QString& fileName) const
{
    if (fileName.isEmpty())
        return m_symbols;

    QList<sym_list::SymbolInfo> result;
    const QString normalizedTarget = normalizedSnapshotFileName(fileName);
    for (const sym_list::SymbolInfo& symbol : m_symbols) {
        if (symbol.fileName == fileName
            || normalizedSnapshotFileName(symbol.fileName) == normalizedTarget) {
            result.append(symbol);
        }
    }
    return result;
}

QList<sym_list::SymbolInfo> SemanticIndexSnapshot::getSymbolsByType(
    sym_list::sym_type_e type) const
{
    QList<sym_list::SymbolInfo> result;
    for (const sym_list::SymbolInfo& symbol : m_symbols) {
        if (symbol.symbolType == type)
            result.append(symbol);
    }
    return result;
}

sym_list::SymbolInfo SemanticIndexSnapshot::getSymbolById(int symbolId) const
{
    if (symbolId < 0)
        return missingSnapshotSymbol();

    for (const sym_list::SymbolInfo& symbol : m_symbols) {
        if (symbol.symbolId == symbolId)
            return symbol;
    }
    return missingSnapshotSymbol();
}

QList<sym_list::SymbolInfo> SemanticIndexSnapshot::findDefinitions(
    const QString& name,
    const SemanticQueryContext& context) const
{
    if (name.isEmpty())
        return {};

    QList<sym_list::SymbolInfo> result;
    for (const sym_list::SymbolInfo& symbol : m_symbols) {
        if (symbol.symbolName == name)
            result.append(symbol);
    }
    return sortedDefinitions(result, context);
}

int SemanticIndexSnapshot::findSymbolId(const QString& name,
                                        const SemanticQueryContext& context) const
{
    const QList<sym_list::SymbolInfo> symbols = findDefinitions(name, context);
    if (symbols.isEmpty())
        return -1;
    return symbols.first().symbolId;
}

QList<SemanticRelationship> SemanticIndexSnapshot::getRelationships(int symbolId,
                                                                    bool outgoing) const
{
    QList<SemanticRelationship> result;
    if (symbolId < 0)
        return result;

    for (const SemanticRelationship& relationship : m_relationships) {
        if ((outgoing && relationship.fromId == symbolId)
            || (!outgoing && relationship.toId == symbolId)) {
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
    const QString normalizedTarget = normalizedSnapshotFileName(fileName);
    for (const SemanticDiagnostic& diagnostic : m_diagnostics) {
        if (diagnostic.fileName == fileName
            || normalizedSnapshotFileName(diagnostic.fileName) == normalizedTarget) {
            result.append(diagnostic);
        }
    }
    return result;
}

QList<sym_list::SymbolInfo> SemanticIndexSnapshot::sortedDefinitions(
    const QList<sym_list::SymbolInfo>& symbols,
    const SemanticQueryContext& context) const
{
    QList<sym_list::SymbolInfo> sorted = symbols;
    const QString normalizedContextFile = normalizedSnapshotFileName(context.fileName);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&context, &normalizedContextFile](const sym_list::SymbolInfo& a,
                                                        const sym_list::SymbolInfo& b) {
        auto score = [&context, &normalizedContextFile](const sym_list::SymbolInfo& s) {
            int value = 0;
            if (!normalizedContextFile.isEmpty()
                && normalizedSnapshotFileName(s.fileName) == normalizedContextFile)
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
