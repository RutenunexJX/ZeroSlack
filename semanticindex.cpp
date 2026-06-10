#include "semanticindex.h"

#include "completionservice.h"
#include "scope_tree.h"
#include "semanticindexsnapshot.h"
#include "smartrelationshipbuilder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <limits>
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

bool commandSymbolTypeMatches(sym_list::sym_type_e symbolType,
                              sym_list::sym_type_e commandType,
                              const QString& dataType = QString())
{
    if (symbolType == commandType)
        return true;
    return commandType == sym_list::sym_enum
        && symbolType == sym_list::sym_typedef
        && dataType == QLatin1String("enum");
}

bool semanticCompletionNameMatches(const QString& name, const QString& prefix)
{
    if (prefix.isEmpty())
        return true;
    if (name.isEmpty())
        return false;

    const QString lowerName = name.toLower();
    const QString lowerPrefix = prefix.toLower();
    if (lowerName.startsWith(lowerPrefix))
        return true;

    int namePos = 0;
    int prefixPos = 0;
    while (prefixPos < lowerPrefix.length() && namePos < lowerName.length()) {
        if (lowerPrefix.at(prefixPos) == lowerName.at(namePos))
            ++prefixPos;
        ++namePos;
    }
    return prefixPos == lowerPrefix.length();
}

bool isModuleRangeSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_packed_struct_var
        || type == sym_list::sym_unpacked_struct_var;
}

bool symbolSearchTypeMatches(sym_list::sym_type_e type,
                             const QList<sym_list::sym_type_e>& types)
{
    return types.isEmpty() || types.contains(type);
}

bool semanticDefinitionSymbolMatches(const sym_list::SymbolInfo& symbol,
                                     const QString& searchWord)
{
    if (symbol.symbolName != searchWord)
        return false;

    switch (symbol.symbolType) {
    case sym_list::sym_module:
    case sym_list::sym_interface:
    case sym_list::sym_package:
    case sym_list::sym_task:
    case sym_list::sym_function:
    case sym_list::sym_port_input:
    case sym_list::sym_port_output:
    case sym_list::sym_port_inout:
    case sym_list::sym_port_ref:
    case sym_list::sym_port_interface:
    case sym_list::sym_port_interface_modport:
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
    case sym_list::sym_packed_struct_var:
    case sym_list::sym_unpacked_struct_var:
    case sym_list::sym_struct_member:
    case sym_list::sym_typedef:
    case sym_list::sym_enum_var:
    case sym_list::sym_enum_value:
        return true;
    default:
        return false;
    }
}

int semanticDefinitionTypePriority(sym_list::sym_type_e type)
{
    switch (type) {
    case sym_list::sym_module: return 0;
    case sym_list::sym_interface: return 1;
    case sym_list::sym_package: return 2;
    case sym_list::sym_port_input:
    case sym_list::sym_port_output:
    case sym_list::sym_port_inout:
    case sym_list::sym_port_ref:
    case sym_list::sym_port_interface:
    case sym_list::sym_port_interface_modport: return 3;
    case sym_list::sym_task:
    case sym_list::sym_function: return 4;
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:
    case sym_list::sym_packed_struct_var:
    case sym_list::sym_unpacked_struct_var:
    case sym_list::sym_enum_var: return 5;
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
    case sym_list::sym_typedef: return 6;
    case sym_list::sym_struct_member:
    case sym_list::sym_enum_value: return 7;
    default: return 10;
    }
}

bool semanticDefinitionInScope(const sym_list::SymbolInfo& symbol,
                               const SemanticDefinitionQuery& query)
{
    if (query.moduleName.isEmpty())
        return true;
    return symbol.moduleScope == query.moduleName;
}

bool semanticDefinitionSkipForStructMemberType(
    const sym_list::SymbolInfo& symbol,
    const SemanticDefinitionQuery& query)
{
    if (query.structTypeNameForMember.isEmpty())
        return false;
    return symbol.symbolType == sym_list::sym_struct_member
        && symbol.moduleScope != query.structTypeNameForMember;
}

int symbolSearchMatchScore(const QString& symbolName,
                           const SemanticSymbolSearchQuery& query)
{
    if (symbolName.isEmpty())
        return 0;
    if (query.text.isEmpty())
        return 1;

    const Qt::CaseSensitivity sensitivity =
        query.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    if (QString::compare(symbolName, query.text, sensitivity) == 0)
        return 100;
    if (query.exactMatch)
        return 0;
    if (symbolName.startsWith(query.text, sensitivity))
        return 75;
    if (symbolName.contains(query.text, sensitivity))
        return 50;
    return 0;
}

bool internalCompletionSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_reg
        || type == sym_list::sym_wire
        || type == sym_list::sym_logic
        || type == sym_list::sym_localparam
        || type == sym_list::sym_parameter;
}

bool globalCompletionSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_task
        || type == sym_list::sym_function
        || type == sym_list::sym_interface
        || type == sym_list::sym_package;
}

bool commandGlobalCompletionSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_task
        || type == sym_list::sym_function
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_typedef
        || type == sym_list::sym_def_define
        || type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_enum;
}

bool globalSymbolInfoType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_task
        || type == sym_list::sym_function
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_typedef
        || type == sym_list::sym_def_define
        || type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_packed_struct_var
        || type == sym_list::sym_unpacked_struct_var;
}

bool alwaysGlobalSymbolInfoType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct;
}

bool alwaysGlobalCommandSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_def_define;
}

void sortSymbolsByName(QList<sym_list::SymbolInfo>& symbols)
{
    std::sort(symbols.begin(), symbols.end(),
              [](const sym_list::SymbolInfo& left,
                 const sym_list::SymbolInfo& right) {
                  return QString::compare(left.symbolName,
                                          right.symbolName,
                                          Qt::CaseInsensitive) < 0;
              });
}

QStringList uniqueSortedSymbolNames(const QList<sym_list::SymbolInfo>& symbols)
{
    QStringList result;
    QSet<QString> seenNames;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol.symbolName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

int endModulePositionInContent(const QString& fileContent,
                               const sym_list::SymbolInfo& moduleSymbol)
{
    int searchStart = moduleSymbol.position;
    int moduleDepth = 0;
    bool foundModule = false;

    static const QRegularExpression moduleStartPattern(QStringLiteral("\\bmodule\\s+"));
    static const QRegularExpression moduleEndPattern(QStringLiteral("\\bendmodule\\b"));

    int pos = searchStart;
    while (pos < fileContent.length()) {
        const QRegularExpressionMatch startMatch = moduleStartPattern.match(fileContent, pos);
        const QRegularExpressionMatch endMatch = moduleEndPattern.match(fileContent, pos);
        const int nextModuleStart = startMatch.hasMatch() ? startMatch.capturedStart(0) : -1;
        const int nextModuleEnd = endMatch.hasMatch() ? endMatch.capturedStart(0) : -1;

        if (nextModuleStart != -1
            && (nextModuleEnd == -1 || nextModuleStart < nextModuleEnd)) {
            if (foundModule || nextModuleStart == moduleSymbol.position) {
                ++moduleDepth;
                foundModule = true;
            }
            pos = nextModuleStart + startMatch.capturedLength(0);
        } else if (nextModuleEnd != -1) {
            if (foundModule) {
                --moduleDepth;
                if (moduleDepth == 0)
                    return nextModuleEnd + endMatch.capturedLength(0);
            }
            pos = nextModuleEnd + endMatch.capturedLength(0);
        } else {
            break;
        }
    }

    return -1;
}

QString moduleNameAtPositionInContent(const QList<sym_list::SymbolInfo>& modules,
                                      int cursorPosition,
                                      const QString& fileContent)
{
    if (fileContent.isEmpty())
        return QString();

    int cursorLine = 0;
    int pos = 0;
    while (pos < cursorPosition && pos < fileContent.length()) {
        if (fileContent.at(pos) == QLatin1Char('\n'))
            ++cursorLine;
        ++pos;
    }

    for (const sym_list::SymbolInfo& module : modules) {
        if (cursorPosition < module.position)
            continue;
        if (!sym_list::isValidModuleName(module.symbolName))
            continue;

        if (module.endLine > 0) {
            if (cursorLine >= module.startLine && cursorLine <= module.endLine)
                return module.symbolName;
            continue;
        }

        const int moduleEndPosition = endModulePositionInContent(fileContent, module);
        if (moduleEndPosition >= 0 && cursorPosition < moduleEndPosition)
            return module.symbolName;
    }

    return QString();
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

QList<SemanticSymbolSearchResult> SemanticIndex::searchSymbols(
    const SemanticSymbolSearchQuery& query) const
{
    QList<SemanticSymbolSearchResult> result;
    const QList<sym_list::SymbolInfo> symbols = getSymbols(query.fileName);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!symbolSearchTypeMatches(symbol.symbolType, query.types))
            continue;

        const int score = symbolSearchMatchScore(symbol.symbolName, query);
        if (score <= 0)
            continue;

        SemanticSymbolSearchResult item;
        item.symbol = symbol;
        item.score = score;
        result.append(item);
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const SemanticSymbolSearchResult& a,
                        const SemanticSymbolSearchResult& b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.symbol.fileName != b.symbol.fileName)
            return a.symbol.fileName < b.symbol.fileName;
        if (a.symbol.startLine != b.symbol.startLine)
            return a.symbol.startLine < b.symbol.startLine;
        return a.symbol.symbolName < b.symbol.symbolName;
    });

    if (query.maxResults >= 0 && result.size() > query.maxResults)
        result = result.mid(0, query.maxResults);
    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getModuleCompletionSymbols(
    const QString& moduleName,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    if (moduleName.isEmpty())
        return result;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.moduleScope != moduleName
            || !internalCompletionSymbolType(symbol.symbolType)
            || !semanticCompletionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    sortSymbolsByName(result);
    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getGlobalCompletionSymbols(
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!globalCompletionSymbolType(symbol.symbolType)
            || !semanticCompletionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    sortSymbolsByName(result);
    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getCommandCompletionSymbols(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    if (moduleName.isEmpty() && !commandGlobalCompletionSymbolType(symbolType))
        return result;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const bool useGlobalScope = moduleName.isEmpty()
            || alwaysGlobalCommandSymbolType(symbolType);
        const bool scopeMatches = useGlobalScope
            ? symbol.moduleScope.isEmpty()
            : symbol.moduleScope == moduleName;
        if (!scopeMatches
            || !commandSymbolTypeMatches(symbol.symbolType,
                                        symbolType,
                                        symbol.dataType)
            || !semanticCompletionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    sortSymbolsByName(result);
    return result;
}

QStringList SemanticIndex::getCompletionSymbolNames() const
{
    QSet<QString> uniqueNames;
    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!symbol.symbolName.isEmpty())
            uniqueNames.insert(symbol.symbolName);
    }

    QStringList names(uniqueNames.begin(), uniqueNames.end());
    names.sort(Qt::CaseInsensitive);
    return names;
}

QList<sym_list::SymbolInfo> SemanticIndex::getTypedCompletionSymbols(
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<int> seenIds;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    auto appendIfMatches = [&](const sym_list::SymbolInfo& symbol) {
        if (seenIds.contains(symbol.symbolId))
            return;
        if (!semanticCompletionNameMatches(symbol.symbolName, prefix))
            return;
        seenIds.insert(symbol.symbolId);
        result.append(symbol);
    };

    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.symbolType == symbolType)
            appendIfMatches(symbol);
    }

    if (symbolType == sym_list::sym_enum) {
        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (symbol.symbolType == sym_list::sym_typedef
                && symbol.dataType == QLatin1String("enum")) {
                appendIfMatches(symbol);
            }
        }
    }

    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getGlobalSymbolInfosByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    if (!globalSymbolInfoType(symbolType))
        return result;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!commandSymbolTypeMatches(symbol.symbolType,
                                      symbolType,
                                      symbol.dataType)
            || !semanticCompletionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const bool global = alwaysGlobalSymbolInfoType(symbolType)
            || symbol.moduleScope.isEmpty();
        if (global)
            result.append(symbol);
    }

    return result;
}

QStringList SemanticIndex::getEnumValueCompletionNames(
    const QString& prefix,
    const QString& enumTypeName) const
{
    QList<sym_list::SymbolInfo> result;
    const QList<sym_list::SymbolInfo> symbols =
        getSymbolsByType(sym_list::sym_enum_value);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!enumTypeName.isEmpty() && symbol.moduleScope != enumTypeName)
            continue;
        if (!semanticCompletionNameMatches(symbol.symbolName, prefix))
            continue;
        result.append(symbol);
    }
    return uniqueSortedSymbolNames(result);
}

QString SemanticIndex::enumTypeForVariable(
    const QString& variableName,
    const QString& moduleName) const
{
    if (!moduleName.isEmpty()) {
        const QList<sym_list::SymbolInfo> moduleSymbols =
            getModuleInternalSymbolsByType(moduleName, sym_list::sym_enum_var);
        for (const sym_list::SymbolInfo& symbol : moduleSymbols) {
            if (symbol.symbolName == variableName)
                return symbol.moduleScope;
        }
    }

    const QList<sym_list::SymbolInfo> symbols =
        getSymbolsByType(sym_list::sym_enum_var);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.symbolName == variableName)
            return symbol.moduleScope;
    }

    return QString();
}

QStringList SemanticIndex::getModulePortCompletionNames(
    const QString& prefix,
    const QString& moduleTypeName) const
{
    if (moduleTypeName.isEmpty())
        return {};

    bool moduleExists = false;
    const QList<sym_list::SymbolInfo> modules =
        getSymbolsByType(sym_list::sym_module);
    for (const sym_list::SymbolInfo& symbol : modules) {
        if (symbol.symbolName == moduleTypeName) {
            moduleExists = true;
            break;
        }
    }
    if (!moduleExists)
        return {};

    QList<sym_list::SymbolInfo> portSymbols;
    portSymbols.append(getCommandCompletionSymbols(moduleTypeName,
                                                   sym_list::sym_wire,
                                                   prefix));
    portSymbols.append(getCommandCompletionSymbols(moduleTypeName,
                                                   sym_list::sym_reg,
                                                   prefix));
    portSymbols.append(getCommandCompletionSymbols(moduleTypeName,
                                                   sym_list::sym_logic,
                                                   prefix));
    return uniqueSortedSymbolNames(portSymbols);
}

QStringList SemanticIndex::getRelationshipCompletionNames(
    const QString& symbolName,
    const QList<SymbolRelationshipEngine::RelationType>& types,
    bool outgoing,
    const QString& prefix) const
{
    if (symbolName.isEmpty())
        return {};

    const int symbolId = findSymbolId(symbolName);
    if (symbolId < 0)
        return {};

    QList<sym_list::SymbolInfo> symbols;
    const QList<SemanticRelationship> relationships = getRelationships(symbolId, outgoing);
    for (const SemanticRelationship& relationship : relationships) {
        if (!types.isEmpty() && !types.contains(relationship.type))
            continue;

        const int peerId = outgoing ? relationship.toId : relationship.fromId;
        const sym_list::SymbolInfo symbol = getSymbolById(peerId);
        if (symbol.symbolId < 0)
            continue;
        if (!semanticCompletionNameMatches(symbol.symbolName, prefix))
            continue;
        symbols.append(symbol);
    }

    return uniqueSortedSymbolNames(symbols);
}

QStringList SemanticIndex::getBidirectionalRelationshipCompletionNames(
    const QString& symbolName,
    const QList<SymbolRelationshipEngine::RelationType>& types,
    const QString& prefix) const
{
    QStringList result = getRelationshipCompletionNames(symbolName, types, true, prefix);
    result.append(getRelationshipCompletionNames(symbolName, types, false, prefix));
    result.removeDuplicates();
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList SemanticIndex::getSymbolsWithOutgoingRelationshipCompletionNames(
    SymbolRelationshipEngine::RelationType type,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!semanticCompletionNameMatches(symbol.symbolName, prefix))
            continue;

        const QList<SemanticRelationship> relationships =
            getRelationships(symbol.symbolId, true);
        for (const SemanticRelationship& relationship : relationships) {
            if (relationship.type == type) {
                result.append(symbol);
                break;
            }
        }
    }

    return uniqueSortedSymbolNames(result);
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

SemanticDefinitionResult SemanticIndex::resolveDefinition(
    const SemanticDefinitionQuery& query) const
{
    SemanticDefinitionResult empty;
    if (query.symbolName.isEmpty())
        return empty;

    SemanticDefinitionResult local = bestDefinitionFromCandidates(
        getSymbols(query.fileName),
        query,
        true);
    if (local.found)
        return local;

    QList<sym_list::SymbolInfo> globalCandidates = findDefinitions(query.symbolName);
    const QString queryFile = normalizedFileName(query.fileName);
    globalCandidates.erase(
        std::remove_if(globalCandidates.begin(), globalCandidates.end(),
                       [&queryFile](const sym_list::SymbolInfo& symbol) {
                           return normalizedFileName(symbol.fileName) == queryFile;
                       }),
        globalCandidates.end());
    return bestDefinitionFromCandidates(globalCandidates, query, false);
}

QList<sym_list::SymbolInfo> SemanticIndex::findDefinitionSymbols(
    const SemanticDefinitionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    const SemanticDefinitionResult resolved = resolveDefinition(query);
    if (resolved.found)
        result.append(resolved.symbol);
    return result;
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

QString SemanticIndex::getStructTypeForVariable(const QString& variableName,
                                                const QString& moduleName) const
{
    if (variableName.isEmpty())
        return QString();

    QList<sym_list::SymbolInfo> structVariables =
        getSymbolsByType(sym_list::sym_packed_struct_var);
    structVariables.append(getSymbolsByType(sym_list::sym_unpacked_struct_var));

    if (!moduleName.isEmpty()) {
        for (const sym_list::SymbolInfo& symbol : std::as_const(structVariables)) {
            if (symbol.symbolName == variableName
                && symbol.moduleScope == moduleName
                && !symbol.dataType.isEmpty()) {
                return symbol.dataType;
            }
        }
    }

    for (const sym_list::SymbolInfo& symbol : std::as_const(structVariables)) {
        if (symbol.symbolName == variableName && !symbol.dataType.isEmpty())
            return symbol.dataType;
    }

    return QString();
}

QList<sym_list::SymbolInfo> SemanticIndex::getStructMembers(
    const QString& structTypeName) const
{
    QList<sym_list::SymbolInfo> result;
    const QList<sym_list::SymbolInfo> members =
        getSymbolsByType(sym_list::sym_struct_member);
    for (const sym_list::SymbolInfo& symbol : members) {
        if (!structTypeName.isEmpty() && symbol.moduleScope != structTypeName)
            continue;
        result.append(symbol);
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const sym_list::SymbolInfo& a,
                        const sym_list::SymbolInfo& b) {
        const int nameCompare = QString::compare(a.symbolName,
                                                 b.symbolName,
                                                 Qt::CaseInsensitive);
        if (nameCompare != 0)
            return nameCompare < 0;
        if (a.fileName != b.fileName)
            return a.fileName < b.fileName;
        if (a.startLine != b.startLine)
            return a.startLine < b.startLine;
        return a.symbolId < b.symbolId;
    });
    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getModuleInternalSymbolsByType(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    bool useRelationshipFallback) const
{
    QList<sym_list::SymbolInfo> result;
    if (moduleName.isEmpty())
        return result;

    const QList<sym_list::SymbolInfo> allSymbols = getSymbols();
    sym_list::SymbolInfo moduleSymbol;
    bool foundModule = false;
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (symbol.symbolType == sym_list::sym_module
            && symbol.symbolName == moduleName) {
            moduleSymbol = symbol;
            foundModule = true;
            break;
        }
    }

    int moduleEndLineExclusive = std::numeric_limits<int>::max();
    if (foundModule) {
        QList<sym_list::SymbolInfo> fileModules;
        const QList<sym_list::SymbolInfo> fileSymbols = getSymbols(moduleSymbol.fileName);
        for (const sym_list::SymbolInfo& symbol : fileSymbols) {
            if (symbol.symbolType == sym_list::sym_module
                && symbol.fileName == moduleSymbol.fileName) {
                fileModules.append(symbol);
            }
        }
        std::sort(fileModules.begin(), fileModules.end(),
                  [](const sym_list::SymbolInfo& left,
                     const sym_list::SymbolInfo& right) {
                      return left.startLine < right.startLine;
                  });
        for (int i = 0; i < fileModules.size(); ++i) {
            if (fileModules.at(i).symbolId == moduleSymbol.symbolId
                && i + 1 < fileModules.size()) {
                moduleEndLineExclusive = fileModules.at(i + 1).startLine;
                break;
            }
        }
    }

    auto appendIfMatches = [&](const sym_list::SymbolInfo& symbol, bool fuzzyPrefix) {
        if (!commandSymbolTypeMatches(symbol.symbolType, symbolType, symbol.dataType))
            return;
        const bool nameMatches = fuzzyPrefix
            ? semanticCompletionNameMatches(symbol.symbolName, prefix)
            : (prefix.isEmpty()
               || symbol.symbolName.startsWith(prefix, Qt::CaseInsensitive));
        if (nameMatches)
            result.append(symbol);
    };

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        bool correctModule = false;
        if (isModuleRangeSymbolType(symbolType)) {
            correctModule = foundModule
                && symbol.fileName == moduleSymbol.fileName
                && symbol.startLine > moduleSymbol.startLine
                && symbol.startLine < moduleEndLineExclusive;
        } else {
            correctModule = symbol.moduleScope == moduleName;
        }

        if (correctModule)
            appendIfMatches(symbol, true);
    }

    if (useRelationshipFallback && result.isEmpty()) {
        const int moduleId = findSymbolId(moduleName);
        const QList<SemanticRelationship> relationships = getRelationships(moduleId, true);
        for (const SemanticRelationship& relationship : relationships) {
            if (relationship.type != SymbolRelationshipEngine::CONTAINS)
                continue;
            const sym_list::SymbolInfo symbol = getSymbolById(relationship.toId);
            if (symbol.symbolId >= 0)
                appendIfMatches(symbol, false);
        }
    }

    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getModuleContextSymbolsByType(
    const QString& moduleName,
    const QString& fileName,
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    if (moduleName.isEmpty() || fileName.isEmpty())
        return result;

    const QString normalizedTargetFile = normalizedFileName(fileName);
    const QList<sym_list::SymbolInfo> fileSymbols = getSymbols(fileName);
    sym_list::SymbolInfo moduleSymbol;
    bool foundModule = false;
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType == sym_list::sym_module
            && symbol.symbolName == moduleName
            && normalizedFileName(symbol.fileName) == normalizedTargetFile) {
            moduleSymbol = symbol;
            foundModule = true;
            break;
        }
    }
    if (!foundModule)
        return result;

    int moduleEndLineExclusive = std::numeric_limits<int>::max();
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType != sym_list::sym_module)
            continue;
        if (symbol.symbolId == moduleSymbol.symbolId)
            continue;
        if (symbol.startLine > moduleSymbol.startLine
            && symbol.startLine < moduleEndLineExclusive) {
            moduleEndLineExclusive = symbol.startLine;
        }
    }

    auto inModuleRange = [&moduleSymbol, moduleEndLineExclusive](
                             const sym_list::SymbolInfo& symbol) {
        return symbol.fileName == moduleSymbol.fileName
            && symbol.startLine > moduleSymbol.startLine
            && symbol.startLine < moduleEndLineExclusive;
    };

    QSet<int> seenIds;
    auto appendSymbol = [&](const sym_list::SymbolInfo& symbol) {
        if (!commandSymbolTypeMatches(symbol.symbolType, symbolType, symbol.dataType))
            return;
        if (!semanticCompletionNameMatches(symbol.symbolName, prefix))
            return;
        if (seenIds.contains(symbol.symbolId))
            return;
        seenIds.insert(symbol.symbolId);
        result.append(symbol);
    };

    const QList<sym_list::SymbolInfo> allSymbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        bool isCorrectModule = false;
        if (symbolType == sym_list::sym_packed_struct
            || symbolType == sym_list::sym_unpacked_struct
            || symbolType == sym_list::sym_packed_struct_var
            || symbolType == sym_list::sym_unpacked_struct_var) {
            isCorrectModule = inModuleRange(symbol);
        } else {
            isCorrectModule = symbol.moduleScope == moduleName;
        }
        if (isCorrectModule)
            appendSymbol(symbol);
    }

    QString fileContent = getCachedFileContent(fileName);
    if (fileContent.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            fileContent = QString::fromUtf8(file.readAll());
    }

    if (!fileContent.isEmpty()) {
        const QString baseDir = QFileInfo(fileName).absolutePath();
        static const QRegularExpression includeRegex(
            QStringLiteral("`include\\s+\"([^\"]+)\""));
        static const QRegularExpression importStarRegex(
            QStringLiteral("import\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*::\\s*\\*\\s*;"));
        static const QRegularExpression importSymbolRegex(
            QStringLiteral("import\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*::\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*;"));

        QSet<QString> starPackages;
        QHash<QString, QSet<QString>> importedSymbolsByPackage;
        const QStringList lines = fileContent.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            const int lineNumber = i + 1;
            if (lineNumber < moduleSymbol.startLine
                || lineNumber >= moduleEndLineExclusive) {
                continue;
            }

            const QString line = lines.at(i);
            const QRegularExpressionMatch includeMatch = includeRegex.match(line);
            if (includeMatch.hasMatch()) {
                const QString includePath = includeMatch.captured(1).trimmed();
                const QString absoluteIncludePath =
                    QDir(baseDir).absoluteFilePath(includePath);
                const QList<sym_list::SymbolInfo> includeSymbols =
                    getSymbols(absoluteIncludePath);
                for (const sym_list::SymbolInfo& symbol : includeSymbols)
                    appendSymbol(symbol);
            }

            const QRegularExpressionMatch starMatch = importStarRegex.match(line);
            if (starMatch.hasMatch()) {
                starPackages.insert(starMatch.captured(1).trimmed());
                continue;
            }

            const QRegularExpressionMatch symbolMatch = importSymbolRegex.match(line);
            if (symbolMatch.hasMatch()) {
                importedSymbolsByPackage[symbolMatch.captured(1).trimmed()].insert(
                    symbolMatch.captured(2).trimmed());
            }
        }

        for (const sym_list::SymbolInfo& symbol : allSymbols) {
            bool imported = starPackages.contains(symbol.moduleScope);
            if (!imported) {
                auto it = importedSymbolsByPackage.constFind(symbol.moduleScope);
                imported = it != importedSymbolsByPackage.constEnd()
                    && it->contains(symbol.symbolName);
            }
            if (imported)
                appendSymbol(symbol);
        }
    }

    if (result.isEmpty()) {
        const int moduleId = findSymbolId(moduleName);
        const QList<SemanticRelationship> relationships =
            getRelationships(moduleId, true);
        for (const SemanticRelationship& relationship : relationships) {
            if (relationship.type != SymbolRelationshipEngine::CONTAINS)
                continue;
            appendSymbol(getSymbolById(relationship.toId));
        }
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const sym_list::SymbolInfo& a,
                        const sym_list::SymbolInfo& b) {
        const int nameCompare = QString::compare(a.symbolName,
                                                 b.symbolName,
                                                 Qt::CaseInsensitive);
        if (nameCompare != 0)
            return nameCompare < 0;
        if (a.startLine != b.startLine)
            return a.startLine < b.startLine;
        if (a.fileName != b.fileName)
            return a.fileName < b.fileName;
        return a.symbolId < b.symbolId;
    });
    return result;
}

QString SemanticIndex::currentModuleAt(const QString& fileName, int cursorPosition) const
{
    if (fileName.isEmpty() || cursorPosition < 0)
        return QString();

    QList<sym_list::SymbolInfo> modules;
    const QList<sym_list::SymbolInfo> fileSymbols = getSymbols(fileName);
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType == sym_list::sym_module)
            modules.append(symbol);
    }

    if (modules.isEmpty())
        return QString();

    std::sort(modules.begin(), modules.end(),
              [](const sym_list::SymbolInfo& left,
                 const sym_list::SymbolInfo& right) {
                  return left.position < right.position;
              });

    QString content = getCachedFileContent(fileName);
    if (content.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            content = QString::fromUtf8(file.readAll());
    }

    return moduleNameAtPositionInContent(modules, cursorPosition, content);
}

bool SemanticIndex::hasRelationshipFacts() const
{
    if (m_snapshot)
        return true;
    return symbolDatabase()->getRelationshipEngine() != nullptr;
}

int SemanticIndex::scopeScoreForSymbol(const QString& symbolName,
                                       const QString& moduleName) const
{
    if (symbolName.isEmpty() || moduleName.isEmpty())
        return 0;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& candidate : symbols) {
        if (candidate.symbolName == symbolName
            && candidate.moduleScope == moduleName) {
            return 20;
        }
    }
    return 0;
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

SemanticDefinitionResult SemanticIndex::bestDefinitionFromCandidates(
    const QList<sym_list::SymbolInfo>& candidates,
    const SemanticDefinitionQuery& query,
    bool localFile) const
{
    SemanticDefinitionResult best;
    int bestPriority = 999;

    for (const sym_list::SymbolInfo& symbol : candidates) {
        if (!semanticDefinitionSymbolMatches(symbol, query.symbolName))
            continue;
        if (semanticDefinitionSkipForStructMemberType(symbol, query))
            continue;
        if (symbol.symbolType != sym_list::sym_struct_member
            && symbol.symbolType != sym_list::sym_enum_value
            && !semanticDefinitionInScope(symbol, query)) {
            continue;
        }

        int priority = semanticDefinitionTypePriority(symbol.symbolType);
        if (!query.moduleName.isEmpty() && symbol.moduleScope == query.moduleName)
            priority -= 100;

        if (!best.found || priority < bestPriority) {
            best.found = true;
            best.localFile = localFile;
            best.symbol = symbol;
            bestPriority = priority;
        }
    }

    return best;
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
