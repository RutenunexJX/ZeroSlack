#include "semanticindex.h"

#include "completionservice.h"
#include "scope_tree.h"
#include "semanticindexsnapshot.h"
#include "smartrelationshipbuilder.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <utility>

std::unique_ptr<SemanticIndex> SemanticIndex::instance = nullptr;

namespace {
QString normalizedFileName(const QString& fileName)
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

void SemanticIndex::updateSymbolsForFile(const QString& fileName,
                                         const QList<sym_list::SymbolInfo>& symbols,
                                         const QString& content)
{
    symbolDatabase()->setSymbolsForFile(fileName, symbols, content);
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

QList<SemanticDiagnostic> SemanticIndex::getDiagnostics(const QString& fileName) const
{
    if (m_snapshot)
        return m_snapshot->getDiagnostics(fileName);

    Q_UNUSED(fileName)
    return {};
}
