#include "semanticindexsnapshot.h"

#include <QDir>
#include <QSet>
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

}

SemanticIndexSnapshot::SemanticIndexSnapshot(
    QList<sym_list::SymbolInfo> symbols,
    QList<SemanticRelationship> relationships,
    QList<SemanticDiagnostic> diagnostics,
    QHash<QString, QString> fileContents)
    : m_symbols(std::move(symbols)),
      m_relationships(std::move(relationships)),
      m_diagnostics(std::move(diagnostics)),
      m_fileContents(std::move(fileContents))
{
}

SemanticIndexSnapshot SemanticIndexSnapshot::fromSymbolDatabase(
    sym_list* symbolDatabase,
    QList<SemanticDiagnostic> diagnostics)
{
    if (!symbolDatabase)
        return SemanticIndexSnapshot({}, {}, std::move(diagnostics));

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

    QHash<QString, QString> fileContents;
    QSet<QString> seenFiles;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.fileName.isEmpty() || seenFiles.contains(symbol.fileName))
            continue;
        seenFiles.insert(symbol.fileName);
        fileContents.insert(symbol.fileName, symbolDatabase->getCachedFileContent(symbol.fileName));
    }

    return SemanticIndexSnapshot(symbols, relationships, std::move(diagnostics), fileContents);
}

SemanticIndexSnapshot SemanticIndexSnapshot::withAdditionalRelationships(
    const QList<SemanticRelationship>& relationships) const
{
    QList<SemanticRelationship> merged = m_relationships;
    QSet<QString> seen;
    for (const SemanticRelationship& relationship : std::as_const(merged)) {
        if (relationship.fromId < 0 || relationship.toId < 0)
            continue;
        seen.insert(QStringLiteral("%1:%2:%3")
                        .arg(relationship.fromId)
                        .arg(relationship.toId)
                        .arg(static_cast<int>(relationship.type)));
    }

    for (const SemanticRelationship& relationship : relationships) {
        if (relationship.fromId < 0 || relationship.toId < 0)
            continue;
        const QString key = QStringLiteral("%1:%2:%3")
                                .arg(relationship.fromId)
                                .arg(relationship.toId)
                                .arg(static_cast<int>(relationship.type));
        if (seen.contains(key))
            continue;
        seen.insert(key);
        merged.append(relationship);
    }

    return SemanticIndexSnapshot(m_symbols, merged, m_diagnostics, m_fileContents);
}

SemanticIndexSnapshot SemanticIndexSnapshot::withReplacedDiagnostics(
    const QStringList& fileNames,
    const QList<SemanticDiagnostic>& diagnostics) const
{
    if (fileNames.isEmpty())
        return SemanticIndexSnapshot(m_symbols, m_relationships, diagnostics, m_fileContents);

    QSet<QString> targetFiles;
    for (const QString& fileName : fileNames) {
        const QString normalized = normalizedSnapshotFileName(fileName);
        if (!normalized.isEmpty())
            targetFiles.insert(normalized);
    }

    QList<SemanticDiagnostic> merged;
    for (const SemanticDiagnostic& diagnostic : m_diagnostics) {
        const QString normalized = normalizedSnapshotFileName(diagnostic.fileName);
        if (!normalized.isEmpty() && targetFiles.contains(normalized))
            continue;
        merged.append(diagnostic);
    }
    merged.append(diagnostics);

    return SemanticIndexSnapshot(m_symbols, m_relationships, merged, m_fileContents);
}
