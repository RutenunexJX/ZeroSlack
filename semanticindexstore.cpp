#include "semanticindex.h"

#include "scope_tree.h"
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
                               const sym_list::SymbolInfo& moduleSymbol)
{
    const QStringList lines = content.split('\n');
    int moduleDepth = 0;
    int scanStart = moduleSymbol.startLine - 1;
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

}

void SemanticIndex::updateSymbolsForFile(const QString& fileName,
                                         const QList<sym_list::SymbolInfo>& symbols,
                                         const QString& content)
{
    symbolDatabase()->setSymbolsForFile(fileName, symbols, content);
}

QList<sym_list::SymbolInfo> SemanticIndex::getSymbols(const QString& fileName) const
{
    sym_list* db = symbolDatabase();
    if (m_snapshot) {
        QList<sym_list::SymbolInfo> symbols = m_snapshot->getSymbols(fileName);
        if (!fileName.isEmpty()) {
            if (!symbols.isEmpty())
                return symbols;
        } else {
            QSet<QString> snapshotFiles;
            for (const sym_list::SymbolInfo& symbol : std::as_const(symbols)) {
                const QString normalized = normalizedStoreFileName(symbol.fileName);
                if (!normalized.isEmpty())
                    snapshotFiles.insert(normalized);
            }
            for (const sym_list::SymbolInfo& symbol : db->getAllSymbols()) {
                const QString normalized = normalizedStoreFileName(symbol.fileName);
                if (!normalized.isEmpty() && !snapshotFiles.contains(normalized))
                    symbols.append(symbol);
            }
            return symbols;
        }
    }

    if (fileName.isEmpty())
        return db->getAllSymbols();

    QList<sym_list::SymbolInfo> symbols = db->findSymbolsByFileName(fileName);
    if (!symbols.isEmpty())
        return symbols;

    const QString normalizedTarget = normalizedStoreFileName(fileName);
    const QList<sym_list::SymbolInfo> allSymbols = db->getAllSymbols();
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (normalizedStoreFileName(symbol.fileName) == normalizedTarget)
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

QString SemanticIndex::getCachedFileContent(const QString& fileName) const
{
    if (m_snapshot) {
        const QString content = m_snapshot->getCachedFileContent(fileName);
        if (!content.isEmpty())
            return content;
    }

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

bool SemanticIndex::isValidModuleName(const QString& name) const
{
    return sym_list::isValidModuleName(name);
}

int SemanticIndex::findEndModuleLine(const QString& fileName,
                                     const sym_list::SymbolInfo& moduleSymbol) const
{
    if (moduleSymbol.symbolType != sym_list::sym_module)
        return -1;

    if (moduleSymbol.endLine >= moduleSymbol.startLine && moduleSymbol.endLine > 0)
        return moduleSymbol.endLine - 1;

    const QString content = getCachedFileContent(fileName);
    if (!content.isEmpty())
        return findEndModuleLineInContent(content, moduleSymbol);

    if (m_snapshot)
        return -1;

    return symbolDatabase()->findEndModuleLine(fileName, moduleSymbol);
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
