#include "completionservice.h"

#include "relationshipservice.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

std::unique_ptr<CompletionService> CompletionService::instance = nullptr;

CompletionService* CompletionService::getInstance()
{
    if (!instance)
        instance = std::make_unique<CompletionService>();
    return instance.get();
}

CompletionService::CompletionService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

CompletionService::~CompletionService() = default;

void CompletionService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

QStringList CompletionService::findCompletions(const CompletionQuery& query) const
{
    if (!query.structTypeNameForMember.isEmpty()) {
        return completionNamesFromSymbols(findStructMemberSymbols(query));
    }

    if (query.prefix.isEmpty())
        return {};

    if (!query.moduleName.isEmpty())
        return completionNamesFromSymbols(findModuleCompletionSymbols(query));

    return completionNamesFromSymbols(findGlobalCompletionSymbols(query));
}

QList<sym_list::SymbolInfo> CompletionService::findCompletionSymbols(
    const CompletionQuery& query) const
{
    if (!query.structTypeNameForMember.isEmpty())
        return findStructMemberSymbols(query);

    if (query.prefix.isEmpty())
        return {};

    if (!query.moduleName.isEmpty())
        return findModuleCompletionSymbols(query);

    return findGlobalCompletionSymbols(query);
}

QStringList CompletionService::findScopeCompletions(const CompletionQuery& query) const
{
    QStringList result;
    if (query.fileName.isEmpty() || query.cursorLine < 0)
        return result;

    const QStringList scopeNames =
        semanticIndex()->getScopeSymbolNames(query.fileName, query.cursorLine);
    QSet<QString> seenNames;
    for (const QString& name : scopeNames) {
        if (!completionNameMatches(name, query.prefix))
            continue;
        const QString key = name.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(name);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList CompletionService::findCommandCompletions(const CommandCompletionQuery& query) const
{
    return completionNamesFromSymbols(findCommandSymbolsFromIndex(query));
}

QList<sym_list::SymbolInfo> CompletionService::findCommandCompletionSymbols(
    const CommandCompletionQuery& query) const
{
    const bool useSymbolInfoDirectly =
        query.symbolType == sym_list::sym_packed_struct_var
        || query.symbolType == sym_list::sym_unpacked_struct_var
        || query.symbolType == sym_list::sym_packed_struct
        || query.symbolType == sym_list::sym_unpacked_struct;

    if (useSymbolInfoDirectly) {
        if (query.moduleName.isEmpty())
            return {};

        semanticIndex()->refreshStructTypedefEnumForFile(query.fileName, query.documentText);

        return semanticIndex()->getModuleContextSymbolsByType(
            query.moduleName,
            query.fileName,
            query.symbolType,
            query.prefix);
    }

    return findCommandSymbolsFromIndex(query);
}

QStringList CompletionService::findModuleChildCompletions(
    const QString& moduleName,
    const QString& prefix) const
{
    if (moduleName.isEmpty())
        return {};

    RelationshipQuery query;
    query.symbolName = moduleName;
    query.outgoing = true;
    query.types = {SymbolRelationshipEngine::CONTAINS};

    RelationshipService relationships(semanticIndex());
    const QStringList names = completionNamesFromRelationshipResults(
        relationships.findRelationships(query),
        true);
    QStringList filtered;
    for (const QString& name : names) {
        if (completionNameMatches(name, prefix))
            filtered.append(name);
    }
    return filtered;
}

QStringList CompletionService::findRelatedSymbolCompletions(
    const QString& symbolName,
    const QString& prefix) const
{
    if (symbolName.isEmpty())
        return {};

    RelationshipService relationships(semanticIndex());
    RelationshipQuery outgoingQuery;
    outgoingQuery.symbolName = symbolName;
    outgoingQuery.outgoing = true;
    outgoingQuery.types = {SymbolRelationshipEngine::REFERENCES};

    RelationshipQuery incomingQuery = outgoingQuery;
    incomingQuery.outgoing = false;

    QStringList result = completionNamesFromRelationshipResults(
        relationships.findRelationships(outgoingQuery),
        true);
    result.append(completionNamesFromRelationshipResults(
        relationships.findRelationships(incomingQuery),
        false));
    result.removeDuplicates();

    QStringList filtered;
    for (const QString& name : result) {
        if (completionNameMatches(name, prefix))
            filtered.append(name);
    }
    filtered.sort(Qt::CaseInsensitive);
    return filtered;
}

QStringList CompletionService::findSymbolReferenceCompletions(
    const QString& symbolName,
    const QString& prefix) const
{
    if (symbolName.isEmpty())
        return {};

    RelationshipQuery query;
    query.symbolName = symbolName;
    query.outgoing = false;
    query.types = {SymbolRelationshipEngine::REFERENCES};

    RelationshipService relationships(semanticIndex());
    const QStringList names = completionNamesFromRelationshipResults(
        relationships.findRelationships(query),
        false);
    QStringList filtered;
    for (const QString& name : names) {
        if (completionNameMatches(name, prefix))
            filtered.append(name);
    }
    return filtered;
}

QStringList CompletionService::findClockDomainCompletions(const QString& prefix) const
{
    RelationshipService relationships(semanticIndex());
    QStringList result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!completionNameMatches(symbol.symbolName, prefix))
            continue;

        RelationshipQuery query;
        query.symbolId = symbol.symbolId;
        query.outgoing = true;
        query.types = {SymbolRelationshipEngine::CLOCKS};
        if (!relationships.hasRelationships(query))
            continue;

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol.symbolName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList CompletionService::findResetSignalCompletions(const QString& prefix) const
{
    RelationshipService relationships(semanticIndex());
    QStringList result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!completionNameMatches(symbol.symbolName, prefix))
            continue;

        RelationshipQuery query;
        query.symbolId = symbol.symbolId;
        query.outgoing = true;
        query.types = {SymbolRelationshipEngine::RESETS};
        if (!relationships.hasRelationships(query))
            continue;

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol.symbolName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QString CompletionService::currentModuleAt(const QString& fileName, int cursorPosition) const
{
    if (fileName.isEmpty() || cursorPosition < 0)
        return QString();

    QList<sym_list::SymbolInfo> modules;
    const QList<sym_list::SymbolInfo> fileSymbols = semanticIndex()->getSymbols(fileName);
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

    return moduleNameAtPosition(
        modules,
        cursorPosition,
        fileName,
        semanticIndex()->getCachedFileContent(fileName));
}

QString CompletionService::getStructTypeForVariable(const QString& variableName,
                                                    const QString& moduleName) const
{
    return semanticIndex()->getStructTypeForVariable(variableName, moduleName);
}

bool CompletionService::tryParseStructMemberContext(const QString& line,
                                                    QString& outVariableName,
                                                    QString& outMemberPrefix) const
{
    static const QRegularExpression memberPattern(
        QStringLiteral("([a-zA-Z_][a-zA-Z0-9_]*)\\.([a-zA-Z0-9_]*)\\s*$"));
    const QRegularExpressionMatch match = memberPattern.match(line);
    if (!match.hasMatch())
        return false;

    outVariableName = match.captured(1);
    outMemberPrefix = match.captured(2);
    return true;
}

SemanticIndex* CompletionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

QList<sym_list::SymbolInfo> CompletionService::findStructMemberSymbols(
    const CompletionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> members =
        semanticIndex()->getStructMembers(query.structTypeNameForMember);
    for (const sym_list::SymbolInfo& member : members) {
        if (!completionNameMatches(member.symbolName, query.prefix))
            continue;
        const QString key = member.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(member);
    }
    return result;
}

QList<sym_list::SymbolInfo> CompletionService::findModuleCompletionSymbols(
    const CompletionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    if (query.moduleName.isEmpty())
        return result;

    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.moduleScope != query.moduleName
            || !isInternalCompletionType(symbol.symbolType)
            || !completionNameMatches(symbol.symbolName, query.prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    std::sort(result.begin(), result.end(),
              [](const sym_list::SymbolInfo& left,
                 const sym_list::SymbolInfo& right) {
                  return QString::compare(left.symbolName,
                                          right.symbolName,
                                          Qt::CaseInsensitive) < 0;
              });
    return result;
}

QList<sym_list::SymbolInfo> CompletionService::findGlobalCompletionSymbols(
    const CompletionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!isGlobalCompletionType(symbol.symbolType)
            || !completionNameMatches(symbol.symbolName, query.prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    std::sort(result.begin(), result.end(),
              [](const sym_list::SymbolInfo& left,
                 const sym_list::SymbolInfo& right) {
                  return QString::compare(left.symbolName,
                                          right.symbolName,
                                          Qt::CaseInsensitive) < 0;
              });
    return result;
}

QList<sym_list::SymbolInfo> CompletionService::findCommandSymbolsFromIndex(
    const CommandCompletionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    if (query.moduleName.isEmpty()
        && !isCommandGlobalCompletionType(query.symbolType)) {
        return result;
    }

    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const bool useGlobalScope = query.moduleName.isEmpty()
            || isAlwaysGlobalCommandType(query.symbolType);
        const bool scopeMatches = useGlobalScope
            ? symbol.moduleScope.isEmpty()
            : symbol.moduleScope == query.moduleName;
        if (!scopeMatches
            || !commandSymbolTypeMatches(symbol.symbolType,
                                        symbol.dataType,
                                        query.symbolType)
            || !completionNameMatches(symbol.symbolName, query.prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    std::sort(result.begin(), result.end(),
              [](const sym_list::SymbolInfo& left,
                 const sym_list::SymbolInfo& right) {
                  return QString::compare(left.symbolName,
                                          right.symbolName,
                                          Qt::CaseInsensitive) < 0;
              });
    return result;
}

QStringList CompletionService::completionNamesFromSymbols(
    const QList<sym_list::SymbolInfo>& symbols) const
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

QStringList CompletionService::completionNamesFromRelationshipResults(
    const QList<RelationshipResult>& relationships,
    bool outgoing) const
{
    QStringList result;
    QSet<QString> seenNames;
    for (const RelationshipResult& relationship : relationships) {
        const sym_list::SymbolInfo& symbol =
            outgoing ? relationship.toSymbol : relationship.fromSymbol;
        if (symbol.symbolId < 0 || symbol.symbolName.isEmpty())
            continue;
        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol.symbolName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool CompletionService::completionNameMatches(const QString& name,
                                              const QString& prefix) const
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

QString CompletionService::moduleNameAtPosition(
    const QList<sym_list::SymbolInfo>& modules,
    int cursorPosition,
    const QString& fileName,
    const QString& fileContent) const
{
    QString content = fileContent;
    if (content.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            content = QString::fromUtf8(file.readAll());
    }
    if (content.isEmpty())
        return QString();

    int cursorLine = 0;
    int pos = 0;
    while (pos < cursorPosition && pos < content.length()) {
        if (content.at(pos) == QLatin1Char('\n'))
            ++cursorLine;
        ++pos;
    }

    for (const sym_list::SymbolInfo& module : modules) {
        if (cursorPosition < module.position)
            continue;
        if (!semanticIndex()->isValidModuleName(module.symbolName))
            continue;

        if (module.endLine > 0) {
            if (cursorLine >= module.startLine && cursorLine <= module.endLine)
                return module.symbolName;
            continue;
        }

        const int moduleEndPosition = endModulePosition(content, module);
        if (moduleEndPosition >= 0 && cursorPosition < moduleEndPosition)
            return module.symbolName;
    }

    return QString();
}

int CompletionService::endModulePosition(
    const QString& fileContent,
    const sym_list::SymbolInfo& moduleSymbol) const
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

bool CompletionService::isInternalCompletionType(sym_list::sym_type_e type) const
{
    return type == sym_list::sym_reg
        || type == sym_list::sym_wire
        || type == sym_list::sym_logic
        || type == sym_list::sym_localparam
        || type == sym_list::sym_parameter;
}

bool CompletionService::isGlobalCompletionType(sym_list::sym_type_e type) const
{
    return type == sym_list::sym_module
        || type == sym_list::sym_task
        || type == sym_list::sym_function
        || type == sym_list::sym_interface
        || type == sym_list::sym_package;
}

bool CompletionService::isCommandGlobalCompletionType(sym_list::sym_type_e type) const
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

bool CompletionService::isAlwaysGlobalCommandType(sym_list::sym_type_e type) const
{
    return type == sym_list::sym_module
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_def_define;
}

bool CompletionService::commandSymbolTypeMatches(
    sym_list::sym_type_e symbolType,
    const QString& dataType,
    sym_list::sym_type_e requestedType) const
{
    if (symbolType == requestedType)
        return true;
    return requestedType == sym_list::sym_enum
        && symbolType == sym_list::sym_typedef
        && dataType == QLatin1String("enum");
}
