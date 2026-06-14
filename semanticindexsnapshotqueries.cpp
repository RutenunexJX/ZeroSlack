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
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

sym_list::SymbolInfo missingSnapshotSymbol()
{
    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    return missing;
}
}

QList<sym_list::SymbolInfo> SemanticIndexSnapshot::getSymbols(const QString& fileName) const
{
    if (fileName.isEmpty())
        return m_symbols;

    QList<sym_list::SymbolInfo> result;
    const QString normalizedTarget = normalizedSnapshotQueryFileName(fileName);
    for (const sym_list::SymbolInfo& symbol : m_symbols) {
        if (symbol.fileName == fileName
            || normalizedSnapshotQueryFileName(symbol.fileName) == normalizedTarget) {
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

    const QList<sym_list::SymbolInfo> fileSymbols = getSymbols(fileName);
    QString containingModule;
    int containingModuleStart = -1;
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType != sym_list::sym_module)
            continue;
        if (symbol.startLine <= cursorLine
            && (symbol.endLine <= 0 || symbol.endLine >= cursorLine)
            && symbol.startLine > containingModuleStart) {
            containingModule = symbol.symbolName;
            containingModuleStart = symbol.startLine;
        }
    }

    QSet<QString> seen;
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        bool inScope = symbol.moduleScope.isEmpty();
        if (!containingModule.isEmpty()) {
            inScope = inScope
                || symbol.moduleScope == containingModule
                || (symbol.startLine <= cursorLine
                    && (symbol.endLine <= 0 || symbol.endLine >= cursorLine));
        }
        if (!inScope || symbol.symbolName.isEmpty() || seen.contains(symbol.symbolName))
            continue;
        seen.insert(symbol.symbolName);
        result.append(symbol.symbolName);
    }
    return result;
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

QList<sym_list::SymbolInfo> SemanticIndexSnapshot::sortedDefinitions(
    const QList<sym_list::SymbolInfo>& symbols,
    const SemanticQueryContext& context) const
{
    QList<sym_list::SymbolInfo> sorted = symbols;
    const QString normalizedContextFile = normalizedSnapshotQueryFileName(context.fileName);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&context, &normalizedContextFile](const sym_list::SymbolInfo& a,
                                                        const sym_list::SymbolInfo& b) {
        auto score = [&context, &normalizedContextFile](const sym_list::SymbolInfo& s) {
            int value = 0;
            if (!normalizedContextFile.isEmpty()
                && normalizedSnapshotQueryFileName(s.fileName) == normalizedContextFile)
                value += 100;
            if (!context.moduleName.isEmpty() && s.moduleScope == context.moduleName)
                value += 50;
            if (SymbolTaxonomy::isGlobalDefinition(s.symbolType))
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
