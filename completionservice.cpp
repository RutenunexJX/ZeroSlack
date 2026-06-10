#include "completionservice.h"

#include "relationshipservice.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <limits>

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

QVector<QPair<QString, int>> CompletionService::findScoredAllSymbolCompletions(
    const QString& prefix,
    int maxResults) const
{
    QSet<QString> uniqueNames;
    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!symbol.symbolName.isEmpty())
            uniqueNames.insert(symbol.symbolName);
    }

    QStringList names(uniqueNames.begin(), uniqueNames.end());
    names.sort(Qt::CaseInsensitive);

    QVector<QPair<QString, int>> scored;
    scored.reserve(qMin(names.size(), maxResults > 0 ? maxResults : names.size()));
    for (const QString& name : std::as_const(names)) {
        const int score = calculateContextMatchScore(name, prefix);
        if (score > 0)
            scored.append(qMakePair(name, score));
    }

    std::sort(scored.begin(), scored.end(),
              [](const QPair<QString, int>& left,
                 const QPair<QString, int>& right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first < right.first;
              });

    if (maxResults > 0 && scored.size() > maxResults)
        scored = scored.mid(0, maxResults);

    return scored;
}

QStringList CompletionService::findAllSymbolCompletions(
    const QString& prefix,
    int maxResults) const
{
    const QVector<QPair<QString, int>> scored =
        findScoredAllSymbolCompletions(prefix, maxResults);

    QStringList result;
    result.reserve(scored.size());
    for (const auto& match : scored)
        result.append(match.first);

    if (maxResults > 0 && result.size() > maxResults)
        result = result.mid(0, maxResults);

    return result;
}

QVector<QPair<sym_list::SymbolInfo, int>>
CompletionService::findScoredSymbolCompletionsByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    int maxResults) const
{
    QVector<QPair<sym_list::SymbolInfo, int>> result;
    QList<sym_list::SymbolInfo> symbols;
    QSet<int> seenIds;

    const QList<sym_list::SymbolInfo> allSymbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (symbol.symbolType != symbolType)
            continue;
        if (seenIds.contains(symbol.symbolId))
            continue;
        seenIds.insert(symbol.symbolId);
        symbols.append(symbol);
    }

    if (symbolType == sym_list::sym_enum) {
        for (const sym_list::SymbolInfo& symbol : allSymbols) {
            if (symbol.symbolType != sym_list::sym_typedef
                || symbol.dataType != QLatin1String("enum")
                || seenIds.contains(symbol.symbolId)) {
                continue;
            }
            seenIds.insert(symbol.symbolId);
            symbols.append(symbol);
        }
    }

    result.reserve(qMin(symbols.size(), maxResults > 0 ? maxResults : symbols.size()));
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const int score =
            calculateSymbolTypeCompletionScore(symbol.symbolName, prefix);
        if (score > 0)
            result.append(qMakePair(symbol, score));
    }

    std::sort(result.begin(), result.end(),
              [](const QPair<sym_list::SymbolInfo, int>& left,
                 const QPair<sym_list::SymbolInfo, int>& right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first.symbolName < right.first.symbolName;
              });

    if (maxResults > 0 && result.size() > maxResults)
        result = result.mid(0, maxResults);

    return result;
}

QStringList CompletionService::findSymbolCompletionsByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    int maxResults) const
{
    const QVector<QPair<sym_list::SymbolInfo, int>> scored =
        findScoredSymbolCompletionsByType(symbolType, prefix, maxResults);

    QStringList result;
    result.reserve(scored.size());
    for (const auto& match : scored) {
        if (!result.contains(match.first.symbolName))
            result.append(match.first.symbolName);
    }

    if (maxResults > 0 && result.size() > maxResults)
        result = result.mid(0, maxResults);

    return result;
}

bool CompletionService::matchesCompletionAbbreviation(
    const QString& text,
    const QString& abbreviation) const
{
    if (abbreviation.isEmpty() || text.isEmpty())
        return false;

    if (text.toLower().startsWith(abbreviation.toLower()))
        return true;

    return isValidContextAbbreviationMatch(text, abbreviation);
}

int CompletionService::calculateCompletionMatchScore(
    const QString& text,
    const QString& abbreviation) const
{
    return calculateContextMatchScore(text, abbreviation);
}

CommandSymbolPresentation CompletionService::commandSymbolPresentation(
    sym_list::sym_type_e symbolType) const
{
    CommandSymbolPresentation presentation;
    switch (symbolType) {
    case sym_list::sym_reg:
        presentation.defaultValue = QStringLiteral("reg");
        presentation.typeDescription = QStringLiteral("reg variables");
        break;
    case sym_list::sym_wire:
        presentation.defaultValue = QStringLiteral("wire");
        presentation.typeDescription = QStringLiteral("wire variables");
        break;
    case sym_list::sym_logic:
        presentation.defaultValue = QStringLiteral("logic");
        presentation.typeDescription = QStringLiteral("logic variables");
        break;
    case sym_list::sym_module:
        presentation.defaultValue = QStringLiteral("module");
        presentation.typeDescription = QStringLiteral("modules");
        break;
    case sym_list::sym_task:
        presentation.defaultValue = QStringLiteral("task");
        presentation.typeDescription = QStringLiteral("tasks");
        break;
    case sym_list::sym_function:
        presentation.defaultValue = QStringLiteral("function");
        presentation.typeDescription = QStringLiteral("functions");
        break;
    case sym_list::sym_packed_struct_var:
        presentation.defaultValue = QStringLiteral("struct");
        presentation.typeDescription = QStringLiteral("packed struct variables");
        break;
    case sym_list::sym_unpacked_struct_var:
        presentation.defaultValue = QStringLiteral("struct");
        presentation.typeDescription = QStringLiteral("unpacked struct variables");
        break;
    case sym_list::sym_packed_struct:
        presentation.defaultValue = QStringLiteral("struct");
        presentation.typeDescription = QStringLiteral("packed struct types");
        break;
    case sym_list::sym_unpacked_struct:
        presentation.defaultValue = QStringLiteral("struct");
        presentation.typeDescription = QStringLiteral("unpacked struct types");
        break;
    case sym_list::sym_parameter:
        presentation.defaultValue = QStringLiteral("parameter");
        presentation.typeDescription = QStringLiteral("parameter declarations");
        break;
    case sym_list::sym_localparam:
        presentation.defaultValue = QStringLiteral("localparam");
        presentation.typeDescription = QStringLiteral("localparam declarations");
        break;
    case sym_list::sym_enum_value:
        presentation.defaultValue = QStringLiteral("enum_value");
        presentation.typeDescription = QStringLiteral("enum values");
        break;
    case sym_list::sym_enum:
        presentation.defaultValue = QStringLiteral("enum");
        presentation.typeDescription = QStringLiteral("enum types");
        break;
    case sym_list::sym_enum_var:
        presentation.defaultValue = QStringLiteral("enum_var");
        presentation.typeDescription = QStringLiteral("enum variables");
        break;
    default:
        presentation.defaultValue = QStringLiteral("symbol");
        presentation.typeDescription = QStringLiteral("symbols");
        break;
    }
    return presentation;
}

CommandSymbolCompletionItem CompletionService::commandSymbolCompletionItem(
    const sym_list::SymbolInfo& symbol,
    sym_list::sym_type_e requestedType,
    const QString& prefix) const
{
    CommandSymbolCompletionItem item;
    item.defaultValue = symbol.symbolName;
    item.description = commandSymbolPresentation(requestedType)
        .typeDescription
        .split(' ')
        .value(0);

    if (requestedType == sym_list::sym_packed_struct_var
        || requestedType == sym_list::sym_unpacked_struct_var) {
        const QString structTypeName = symbol.moduleScope;
        item.text = structTypeName.isEmpty()
            ? symbol.symbolName
            : QStringLiteral("%1(%2)").arg(symbol.symbolName, structTypeName);
        item.uniqueKey = QStringLiteral("%1:%2").arg(symbol.symbolName, structTypeName);
    } else if (requestedType == sym_list::sym_enum_value) {
        item.text = symbol.symbolName;
        item.description = symbol.dataType.isEmpty() ? QStringLiteral("enum") : symbol.dataType;
        item.uniqueKey = symbol.symbolName;
    } else {
        item.text = symbol.symbolName;
        item.uniqueKey = symbol.symbolName;
    }

    item.score = prefix.isEmpty()
        ? 100
        : calculateCompletionMatchScore(symbol.symbolName, prefix);
    return item;
}

QList<int> CompletionService::findCompletionAbbreviationPositions(
    const QString& text,
    const QString& abbreviation) const
{
    return findContextAbbreviationPositions(text, abbreviation);
}

QVector<QPair<QString, int>> CompletionService::findScoredKeywordCompletions(
    const QString& prefix) const
{
    const QStringList keywords = publicKeywordCompletions();
    QVector<QPair<QString, int>> result;
    result.reserve(keywords.size());

    for (const QString& keyword : keywords) {
        const int score = calculateCompletionMatchScore(keyword, prefix);
        if (score > 0)
            result.append(qMakePair(keyword, score));
    }

    std::sort(result.begin(), result.end(),
              [](const QPair<QString, int>& left,
                 const QPair<QString, int>& right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first < right.first;
              });

    return result;
}

QStringList CompletionService::findKeywordCompletions(
    const QString& prefix,
    int maxResults) const
{
    const QVector<QPair<QString, int>> scored =
        findScoredKeywordCompletions(prefix);

    QStringList result;
    result.reserve(scored.size());
    for (const auto& match : scored)
        result.append(match.first);

    if (maxResults > 0 && result.size() > maxResults)
        result = result.mid(0, maxResults);

    return result;
}

QStringList CompletionService::findKeywordAbbreviationMatches(
    const QStringList& candidates,
    const QString& abbreviation) const
{
    const QVector<QPair<QString, int>> scored =
        findScoredKeywordCompletions(abbreviation);

    QStringList result;
    result.reserve(scored.size());
    for (const auto& match : scored) {
        if (candidates.contains(match.first))
            result.append(match.first);
    }

    return result;
}

bool CompletionService::relationshipCompletionsAvailable() const
{
    if (semanticIndex()->snapshot())
        return true;

    return semanticIndex()->symbolDatabase()->getRelationshipEngine() != nullptr;
}

QVector<QPair<QString, int>> CompletionService::findSmartCompletions(
    const QString& prefix,
    const QString& fileName,
    int cursorPosition,
    bool relationshipCompletionsEnabled) const
{
    if (!relationshipCompletionsEnabled || !relationshipCompletionsAvailable())
        return findScoredAllSymbolCompletions(prefix);

    const QString currentModule = currentModuleAt(fileName, cursorPosition);
    const QString context = QStringLiteral("general");

    ContextCompletionQuery query;
    query.prefix = prefix;
    query.currentModule = currentModule;
    query.context = context;
    query.relationshipCompletionsEnabled = true;
    const QStringList completions = findContextAwareCompletions(query);

    QVector<QPair<QString, int>> result;
    result.reserve(completions.size());
    for (const QString& completion : completions) {
        const int baseScore = calculateContextMatchScore(completion, prefix);
        const int contextScore = calculateContextScore(completion, context);
        const int relationshipScore =
            calculateRelationshipScore(completion, currentModule);
        const int scopeScore = calculateScopeScore(completion, currentModule);

        const int finalScore = baseScore * 0.4
            + contextScore * 0.2
            + relationshipScore * 0.3
            + scopeScore * 0.1;
        result.append(qMakePair(completion, finalScore));
    }

    std::sort(result.begin(), result.end(),
              [](const QPair<QString, int>& left,
                 const QPair<QString, int>& right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first < right.first;
              });

    if (result.size() > 20)
        result = result.mid(0, 20);

    return result;
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

QStringList CompletionService::findModuleInternalVariableCompletions(
    const QString& moduleName,
    const QString& prefix) const
{
    QStringList result;
    QSet<QString> seenNames;
    if (moduleName.isEmpty())
        return result;

    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.moduleScope != moduleName
            || !isInternalCompletionType(symbol.symbolType)
            || !completionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol.symbolName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList CompletionService::findModuleSymbolsByType(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QStringList result;
    QSet<QString> seenNames;
    if (moduleName.isEmpty())
        return result;

    CommandCompletionQuery query;
    query.moduleName = moduleName;
    query.symbolType = symbolType;
    query.prefix = prefix;
    const QList<sym_list::SymbolInfo> symbols = findCommandSymbolsFromIndex(query);
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

QStringList CompletionService::findGlobalSymbolCompletions(const QString& prefix) const
{
    QStringList result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!isGlobalCompletionType(symbol.symbolType)
            || !completionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol.symbolName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList CompletionService::findGlobalSymbolsByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QStringList result;
    QSet<QString> seenNames;
    if (!isGlobalSymbolType(symbolType))
        return result;

    CommandCompletionQuery query;
    query.symbolType = symbolType;
    query.prefix = prefix;
    const QList<sym_list::SymbolInfo> symbols = findCommandSymbolsFromIndex(query);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        bool global = false;
        if (symbolType == sym_list::sym_module
            || symbolType == sym_list::sym_interface
            || symbolType == sym_list::sym_package
            || symbolType == sym_list::sym_packed_struct
            || symbolType == sym_list::sym_unpacked_struct) {
            global = true;
        } else {
            global = symbol.moduleScope.isEmpty();
        }
        if (!global)
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

QStringList CompletionService::findVariableCompletionsInScope(
    const QString& moduleName,
    sym_list::sym_type_e variableType,
    const QString& prefix) const
{
    if (moduleName.isEmpty()) {
        QStringList result;
        QSet<QString> seenNames;
        const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (!commandSymbolTypeMatches(symbol.symbolType, symbol.dataType, variableType)
                || !completionNameMatches(symbol.symbolName, prefix)) {
                continue;
            }
            const QString key = symbol.symbolName.toCaseFolded();
            if (seenNames.contains(key))
                continue;
            seenNames.insert(key);
            result.append(symbol.symbolName);
        }
        result.sort(Qt::CaseInsensitive);
        return result;
    }
    return findModuleSymbolsByType(moduleName, variableType, prefix);
}

QStringList CompletionService::findTaskFunctionCompletions(const QString& prefix) const
{
    QStringList result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if ((symbol.symbolType != sym_list::sym_task
             && symbol.symbolType != sym_list::sym_function)
            || !completionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }
        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol.symbolName);
    }
    result.removeDuplicates();
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList CompletionService::findInstantiableModuleCompletions(const QString& prefix) const
{
    return findGlobalSymbolsByType(sym_list::sym_module, prefix);
}

QStringList CompletionService::findContextAwareCompletions(
    const ContextCompletionQuery& query) const
{
    QStringList result;
    const bool relationshipsEnabled =
        query.relationshipCompletionsEnabled && relationshipCompletionsAvailable();

    if (query.context.contains(QLatin1Char('.'))
        || query.context.contains(QStringLiteral("->"))) {
        const QString structVariableName =
            extractStructVariableFromContext(query.context);
        if (!structVariableName.isEmpty()) {
            const QString structTypeName =
                getStructTypeForVariable(structVariableName, query.currentModule);
            if (!structTypeName.isEmpty()) {
                result.append(findStructMemberCompletions(query.prefix, structTypeName));
                if (!result.isEmpty())
                    return result;
            }
        }
    }

    if (query.context.contains(QLatin1Char('='))
        || query.context.contains(QStringLiteral("assign"))
        || query.context.contains(QStringLiteral("case"))
        || query.context.contains(QStringLiteral("if"))) {
        const QString enumVariableName =
            extractEnumVariableFromContext(query.context);
        if (!enumVariableName.isEmpty()) {
            const QString enumTypeName =
                findEnumTypeForVariable(enumVariableName, query.currentModule);
            if (!enumTypeName.isEmpty())
                result.append(findEnumValueCompletions(query.prefix, enumTypeName));
        }

        if (result.isEmpty())
            result.append(findEnumValueCompletions(query.prefix));
    }

    if (relationshipsEnabled
        && query.context.contains(QLatin1Char('('))
        && (query.context.contains(QStringLiteral("module"))
            || query.context.contains(QStringLiteral("instantiation")))) {
        const QString moduleTypeName = extractModuleTypeFromContext(query.context);
        if (!moduleTypeName.isEmpty())
            result.append(findModulePortCompletions(query.prefix, moduleTypeName));
    }

    if (query.context.contains(QStringLiteral("clk"), Qt::CaseInsensitive)
        || query.context.contains(QStringLiteral("clock"), Qt::CaseInsensitive)
        || query.context.contains(QStringLiteral("always_ff"))) {
        result.append(findClockDomainCompletions(query.prefix));
    }

    if (query.context.contains(QStringLiteral("rst"), Qt::CaseInsensitive)
        || query.context.contains(QStringLiteral("reset"), Qt::CaseInsensitive)
        || query.context.contains(QStringLiteral("negedge"))
        || query.context.contains(QStringLiteral("posedge"))) {
        result.append(findResetSignalCompletions(query.prefix));
    }

    if (!query.currentModule.isEmpty()) {
        result.append(findModuleChildCompletions(query.currentModule, query.prefix));
        if (relationshipsEnabled)
            result.append(findRelatedSymbolCompletions(query.currentModule, query.prefix));
    }

    if (query.context.contains(QStringLiteral("task"))
        || query.context.contains(QStringLiteral("function"))
        || query.context.contains(QStringLiteral("call"))) {
        result.append(findTaskFunctionCompletions(query.prefix));
    }

    if (query.context.contains(QStringLiteral("typedef"))
        || query.context.contains(QStringLiteral("type"))) {
        result.append(findGlobalSymbolsByType(sym_list::sym_typedef, query.prefix));
        result.append(findGlobalSymbolsByType(sym_list::sym_enum, query.prefix));
        result.append(findGlobalSymbolsByType(sym_list::sym_packed_struct, query.prefix));
        result.append(findGlobalSymbolsByType(sym_list::sym_unpacked_struct, query.prefix));
    }

    if (query.context.contains(QStringLiteral("reg"))
        || query.context.contains(QStringLiteral("wire"))
        || query.context.contains(QStringLiteral("logic"))
        || query.context.contains(QStringLiteral("var"))) {
        result.append(svKeywordCompletions(query.prefix));
        result.append(findGlobalSymbolsByType(sym_list::sym_enum, query.prefix));
        result.append(findGlobalSymbolsByType(sym_list::sym_packed_struct, query.prefix));
        result.append(findGlobalSymbolsByType(sym_list::sym_unpacked_struct, query.prefix));
    }

    if (result.isEmpty()
        || query.context == QLatin1String("general")
        || query.context.isEmpty()) {
        if (!query.currentModule.isEmpty()) {
            result.append(findModuleSymbolsByType(query.currentModule,
                                                  sym_list::sym_reg,
                                                  query.prefix));
            result.append(findModuleSymbolsByType(query.currentModule,
                                                  sym_list::sym_wire,
                                                  query.prefix));
            result.append(findModuleSymbolsByType(query.currentModule,
                                                  sym_list::sym_logic,
                                                  query.prefix));
        }

        result.append(findGlobalSymbolsByType(sym_list::sym_module, query.prefix));
        result.append(findGlobalSymbolsByType(sym_list::sym_enum, query.prefix));
        result.append(findGlobalSymbolsByType(sym_list::sym_packed_struct, query.prefix));
        result.append(findGlobalSymbolsByType(sym_list::sym_task, query.prefix));
        result.append(findGlobalSymbolsByType(sym_list::sym_function, query.prefix));
        result.append(svKeywordCompletions(query.prefix));
    }

    result.removeDuplicates();

    QVector<QPair<QString, int>> scoredResults;
    scoredResults.reserve(result.size());
    for (const QString& completion : std::as_const(result)) {
        int score = calculateContextMatchScore(completion, query.prefix);
        if (!query.context.isEmpty()
            && query.context != QLatin1String("general")) {
            score += calculateContextScore(completion, query.context);
        }
        if (!query.currentModule.isEmpty())
            score += calculateScopeScore(completion, query.currentModule);
        scoredResults.append(qMakePair(completion, score));
    }

    std::sort(scoredResults.begin(), scoredResults.end(),
              [](const QPair<QString, int>& left,
                 const QPair<QString, int>& right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first < right.first;
              });

    QStringList finalResults;
    finalResults.reserve(scoredResults.size());
    for (const auto& scored : std::as_const(scoredResults))
        finalResults.append(scored.first);

    if (finalResults.size() > 50)
        finalResults = finalResults.mid(0, 50);

    return finalResults;
}

QStringList CompletionService::findStructMemberCompletions(
    const QString& prefix,
    const QString& structTypeName) const
{
    CompletionQuery query;
    query.prefix = prefix;
    query.structTypeNameForMember = structTypeName;
    return completionNamesFromSymbols(findStructMemberSymbols(query));
}

QStringList CompletionService::findEnumValueCompletions(
    const QString& prefix,
    const QString& enumTypeName) const
{
    QStringList result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> symbols =
        semanticIndex()->getSymbolsByType(sym_list::sym_enum_value);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!enumTypeName.isEmpty() && symbol.moduleScope != enumTypeName)
            continue;
        if (!completionNameMatches(symbol.symbolName, prefix))
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

QString CompletionService::findEnumTypeForVariable(
    const QString& variableName,
    const QString& moduleName) const
{
    if (!moduleName.isEmpty()) {
        const QList<sym_list::SymbolInfo> moduleSymbols =
            findModuleInternalSymbolInfosByType(moduleName,
                                                sym_list::sym_enum_var);
        for (const sym_list::SymbolInfo& symbol : moduleSymbols) {
            if (symbol.symbolName == variableName)
                return symbol.moduleScope;
        }
    }

    const QList<sym_list::SymbolInfo> symbols =
        semanticIndex()->getSymbolsByType(sym_list::sym_enum_var);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.symbolName == variableName)
            return symbol.moduleScope;
    }

    return QString();
}

QStringList CompletionService::findModulePortCompletions(
    const QString& prefix,
    const QString& moduleTypeName) const
{
    QStringList result;
    if (moduleTypeName.isEmpty())
        return result;

    bool moduleExists = false;
    const QList<sym_list::SymbolInfo> modules =
        semanticIndex()->getSymbolsByType(sym_list::sym_module);
    for (const sym_list::SymbolInfo& symbol : modules) {
        if (symbol.symbolName == moduleTypeName) {
            moduleExists = true;
            break;
        }
    }
    if (!moduleExists)
        return result;

    result.append(findModuleSymbolsByType(moduleTypeName,
                                          sym_list::sym_wire,
                                          prefix));
    result.append(findModuleSymbolsByType(moduleTypeName,
                                          sym_list::sym_reg,
                                          prefix));
    result.append(findModuleSymbolsByType(moduleTypeName,
                                          sym_list::sym_logic,
                                          prefix));
    result.removeDuplicates();
    result.sort(Qt::CaseInsensitive);
    return result;
}

QList<sym_list::SymbolInfo> CompletionService::findModuleInternalSymbolInfosByType(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    bool useRelationshipFallback) const
{
    QList<sym_list::SymbolInfo> result;
    if (moduleName.isEmpty())
        return result;

    const QList<sym_list::SymbolInfo> allSymbols = semanticIndex()->getSymbols();
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
        const QList<sym_list::SymbolInfo> fileSymbols =
            semanticIndex()->getSymbols(moduleSymbol.fileName);
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

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (!commandSymbolTypeMatches(symbol.symbolType,
                                      symbol.dataType,
                                      symbolType)
            || !completionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

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
            result.append(symbol);
    }

    if (useRelationshipFallback && result.isEmpty()) {
        RelationshipQuery query;
        query.symbolName = moduleName;
        query.outgoing = true;
        query.types = {SymbolRelationshipEngine::CONTAINS};

        RelationshipService relationships(semanticIndex());
        const QList<RelationshipResult> related =
            relationships.findRelationships(query);
        for (const RelationshipResult& relationship : related) {
            const sym_list::SymbolInfo& symbol = relationship.toSymbol;
            if (symbol.symbolId < 0)
                continue;
            if (!commandSymbolTypeMatches(symbol.symbolType,
                                          symbol.dataType,
                                          symbolType)) {
                continue;
            }
            if (!prefix.isEmpty()
                && !symbol.symbolName.startsWith(prefix, Qt::CaseInsensitive)) {
                continue;
            }
            result.append(symbol);
        }
    }

    return result;
}

QList<sym_list::SymbolInfo> CompletionService::findModuleContextSymbolInfosByType(
    const QString& moduleName,
    const QString& fileName,
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    return semanticIndex()->getModuleContextSymbolsByType(
        moduleName,
        fileName,
        symbolType,
        prefix);
}

QList<sym_list::SymbolInfo> CompletionService::findGlobalSymbolInfosByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    if (!isGlobalSymbolInfoType(symbolType))
        return result;

    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!commandSymbolTypeMatches(symbol.symbolType,
                                      symbol.dataType,
                                      symbolType)
            || !completionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        bool global = false;
        if (symbolType == sym_list::sym_module
            || symbolType == sym_list::sym_interface
            || symbolType == sym_list::sym_package
            || symbolType == sym_list::sym_packed_struct
            || symbolType == sym_list::sym_unpacked_struct) {
            global = true;
        } else {
            global = symbol.moduleScope.isEmpty();
        }

        if (global)
            result.append(symbol);
    }

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

QString CompletionService::extractStructVariableFromContext(const QString& context) const
{
    static const QRegularExpression dotPattern(
        QStringLiteral("([a-zA-Z_][a-zA-Z0-9_]*)(?:\\s*\\[[^\\]]*\\])*\\s*\\.$"));
    QRegularExpressionMatch match = dotPattern.match(context);
    if (match.hasMatch())
        return match.captured(1);

    static const QRegularExpression arrowPattern(
        QStringLiteral("([a-zA-Z_][a-zA-Z0-9_]*)\\s*->$"));
    match = arrowPattern.match(context);
    if (match.hasMatch())
        return match.captured(1);

    return QString();
}

QString CompletionService::extractEnumVariableFromContext(const QString& context) const
{
    static const QRegularExpression assignPattern(
        QStringLiteral("([a-zA-Z_][a-zA-Z0-9_]*)\\s*="));
    QRegularExpressionMatch match = assignPattern.match(context);
    if (match.hasMatch())
        return match.captured(1);

    static const QRegularExpression casePattern(
        QStringLiteral("case\\s*\\(\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\)"));
    match = casePattern.match(context);
    if (match.hasMatch())
        return match.captured(1);

    static const QRegularExpression ifPattern(
        QStringLiteral("if\\s*\\(\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*=="));
    match = ifPattern.match(context);
    if (match.hasMatch())
        return match.captured(1);

    return QString();
}

QString CompletionService::extractModuleTypeFromContext(const QString& context) const
{
    static const QRegularExpression instPattern(
        QStringLiteral("([a-zA-Z_][a-zA-Z0-9_]*)\\s+[a-zA-Z_][a-zA-Z0-9_]*\\s*\\("));
    const QRegularExpressionMatch match = instPattern.match(context);
    if (match.hasMatch())
        return match.captured(1);
    return QString();
}

QStringList CompletionService::publicKeywordCompletions() const
{
    return {
        QStringLiteral("always"), QStringLiteral("always_comb"),
        QStringLiteral("always_ff"), QStringLiteral("assign"),
        QStringLiteral("begin"), QStringLiteral("end"),
        QStringLiteral("module"), QStringLiteral("endmodule"),
        QStringLiteral("generate"), QStringLiteral("endgenerate"),
        QStringLiteral("if"), QStringLiteral("else"),
        QStringLiteral("for"), QStringLiteral("define"),
        QStringLiteral("ifdef"), QStringLiteral("ifndef"),
        QStringLiteral("task"), QStringLiteral("endtask"),
        QStringLiteral("initial"), QStringLiteral("reg"),
        QStringLiteral("wire"), QStringLiteral("logic"),
        QStringLiteral("enum"), QStringLiteral("localparam"),
        QStringLiteral("parameter"), QStringLiteral("struct"),
        QStringLiteral("package"), QStringLiteral("endpackage"),
        QStringLiteral("interface"), QStringLiteral("endinterface"),
        QStringLiteral("function"), QStringLiteral("endfunction"),
        QStringLiteral("case"), QStringLiteral("endcase"),
        QStringLiteral("default"), QStringLiteral("posedge"),
        QStringLiteral("negedge"), QStringLiteral("input"),
        QStringLiteral("output"), QStringLiteral("inout")
    };
}

QStringList CompletionService::svKeywordCompletions(const QString& prefix) const
{
    static const QStringList keywords = {
        QStringLiteral("module"), QStringLiteral("endmodule"),
        QStringLiteral("input"), QStringLiteral("output"),
        QStringLiteral("inout"), QStringLiteral("wire"),
        QStringLiteral("reg"), QStringLiteral("logic"),
        QStringLiteral("bit"), QStringLiteral("byte"),
        QStringLiteral("shortint"), QStringLiteral("int"),
        QStringLiteral("longint"), QStringLiteral("always"),
        QStringLiteral("always_ff"), QStringLiteral("always_comb"),
        QStringLiteral("initial"), QStringLiteral("assign"),
        QStringLiteral("case"), QStringLiteral("casex"),
        QStringLiteral("casez"), QStringLiteral("default"),
        QStringLiteral("endcase"), QStringLiteral("if"),
        QStringLiteral("else"), QStringLiteral("for"),
        QStringLiteral("while"), QStringLiteral("repeat"),
        QStringLiteral("forever"), QStringLiteral("task"),
        QStringLiteral("function"), QStringLiteral("endtask"),
        QStringLiteral("endfunction"), QStringLiteral("typedef"),
        QStringLiteral("enum"), QStringLiteral("struct"),
        QStringLiteral("packed"), QStringLiteral("unpacked"),
        QStringLiteral("interface"), QStringLiteral("endinterface"),
        QStringLiteral("modport"), QStringLiteral("generate"),
        QStringLiteral("endgenerate"), QStringLiteral("genvar"),
        QStringLiteral("parameter"), QStringLiteral("localparam"),
        QStringLiteral("`define"), QStringLiteral("`include"),
        QStringLiteral("posedge"), QStringLiteral("negedge"),
        QStringLiteral("and"), QStringLiteral("or"),
        QStringLiteral("not"), QStringLiteral("xor")
    };

    QStringList result;
    for (const QString& keyword : keywords) {
        if (completionNameMatches(keyword, prefix))
            result.append(keyword);
    }
    return result;
}

int CompletionService::calculateContextMatchScore(
    const QString& text,
    const QString& abbreviation) const
{
    if (abbreviation.isEmpty() || text.isEmpty())
        return 0;

    const QString lowerText = text.toLower();
    const QString lowerAbbreviation = abbreviation.toLower();

    if (lowerText == lowerAbbreviation)
        return 1000;
    if (lowerText.startsWith(lowerAbbreviation))
        return 800 + (100 - abbreviation.length());
    if (lowerText.contains(lowerAbbreviation))
        return 400 + (100 - text.length());
    if (!isValidContextAbbreviationMatch(text, abbreviation))
        return 0;

    const QList<int> positions =
        findContextAbbreviationPositions(text, abbreviation);
    int score = 500;
    int wordBoundaryMatches = 0;
    for (int position : positions) {
        if (position == 0
            || lowerText.at(position - 1) == QLatin1Char('_')
            || lowerText.at(position - 1) == QLatin1Char(' ')) {
            ++wordBoundaryMatches;
        }
        if (position > 0 && position < lowerText.length()) {
            const QChar previous = text.at(position - 1);
            const QChar current = text.at(position);
            if (previous.isLower() && current.isUpper())
                ++wordBoundaryMatches;
        }
    }

    score += wordBoundaryMatches * 50;
    score -= text.length();
    for (int i = 1; i < positions.size(); ++i) {
        if (positions.at(i) == positions.at(i - 1) + 1)
            score += 10;
    }
    return score;
}

int CompletionService::calculateSymbolTypeCompletionScore(
    const QString& text,
    const QString& abbreviation) const
{
    if (text.isEmpty())
        return 0;
    if (abbreviation.isEmpty())
        return 100;

    const QString lowerText = text.toLower();
    const QString lowerAbbreviation = abbreviation.toLower();
    if (lowerText == lowerAbbreviation)
        return 1000;
    if (lowerText.startsWith(lowerAbbreviation))
        return 800 + (100 - abbreviation.length());
    if (lowerText.contains(lowerAbbreviation))
        return 400 + (100 - text.length());
    if (isValidContextAbbreviationMatch(text, abbreviation))
        return 200;
    return 0;
}

bool CompletionService::isValidContextAbbreviationMatch(
    const QString& text,
    const QString& abbreviation) const
{
    if (abbreviation.length() > text.length())
        return false;

    const QString lowerText = text.toLower();
    const QString lowerAbbreviation = abbreviation.toLower();
    int textPosition = 0;
    int abbreviationPosition = 0;
    while (abbreviationPosition < lowerAbbreviation.length()
           && textPosition < text.length()) {
        const QChar abbreviationChar = lowerAbbreviation.at(abbreviationPosition);
        const QChar textChar = lowerText.at(textPosition);
        if (abbreviationChar == textChar) {
            ++abbreviationPosition;
            ++textPosition;
            continue;
        }

        bool separator = text.at(textPosition) == QLatin1Char('_')
            || text.at(textPosition) == QLatin1Char(' ');
        if (textPosition > 0) {
            const QChar previous = text.at(textPosition - 1);
            const QChar current = text.at(textPosition);
            if (previous.isLower() && current.isUpper())
                separator = true;
        }
        if (separator && textPosition + 1 < text.length()
            && abbreviationChar == lowerText.at(textPosition + 1)) {
            ++textPosition;
            continue;
        }
        ++textPosition;
    }
    return abbreviationPosition == lowerAbbreviation.length();
}

QList<int> CompletionService::findContextAbbreviationPositions(
    const QString& text,
    const QString& abbreviation) const
{
    QList<int> positions;
    if (!isValidContextAbbreviationMatch(text, abbreviation))
        return positions;

    const QString lowerText = text.toLower();
    const QString lowerAbbreviation = abbreviation.toLower();
    int textPosition = 0;
    int abbreviationPosition = 0;
    while (abbreviationPosition < lowerAbbreviation.length()
           && textPosition < lowerText.length()) {
        if (lowerAbbreviation.at(abbreviationPosition)
            == lowerText.at(textPosition)) {
            positions.append(textPosition);
            ++abbreviationPosition;
        } else {
            bool separator = text.at(textPosition) == QLatin1Char('_')
                || text.at(textPosition) == QLatin1Char(' ');
            if (textPosition > 0) {
                const QChar previous = text.at(textPosition - 1);
                const QChar current = text.at(textPosition);
                if (previous.isLower() && current.isUpper())
                    separator = true;
            }
            if (separator && textPosition + 1 < text.length()
                && lowerAbbreviation.at(abbreviationPosition)
                    == lowerText.at(textPosition + 1)) {
                ++textPosition;
                continue;
            }
        }
        ++textPosition;
    }
    return positions;
}

int CompletionService::calculateContextScore(
    const QString& symbol,
    const QString& context) const
{
    if (context == QLatin1String("clock")
        && symbol.contains(QStringLiteral("clk"), Qt::CaseInsensitive)) {
        return 50;
    }

    static const QRegularExpression resetPattern(
        QStringLiteral("rst|reset"),
        QRegularExpression::CaseInsensitiveOption);
    if (context == QLatin1String("reset") && symbol.contains(resetPattern))
        return 50;

    return 0;
}

int CompletionService::calculateRelationshipScore(
    const QString& symbol,
    const QString& currentContext) const
{
    if (currentContext.isEmpty())
        return 0;

    RelationshipService relationships(semanticIndex());
    auto hasNamedRelationship = [&relationships](const QString& fromName,
                                                 const QString& toName,
                                                 SymbolRelationshipEngine::RelationType type) {
        RelationshipQuery query;
        query.symbolName = fromName;
        query.outgoing = true;
        query.types = {type};
        const QList<RelationshipResult> results =
            relationships.findRelationships(query);
        for (const RelationshipResult& result : results) {
            if (result.toSymbol.symbolName == toName)
                return true;
        }
        return false;
    };

    if (hasNamedRelationship(currentContext, symbol, SymbolRelationshipEngine::CONTAINS))
        return 40;

    if (hasNamedRelationship(symbol, currentContext, SymbolRelationshipEngine::REFERENCES)
        || hasNamedRelationship(currentContext, symbol, SymbolRelationshipEngine::REFERENCES)) {
        return 30;
    }

    if (hasNamedRelationship(symbol, currentContext, SymbolRelationshipEngine::CALLS)
        || hasNamedRelationship(currentContext, symbol, SymbolRelationshipEngine::CALLS)) {
        return 25;
    }

    return 0;
}

int CompletionService::calculateScopeScore(
    const QString& symbol,
    const QString& currentModule) const
{
    if (currentModule.isEmpty())
        return 0;

    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& candidate : symbols) {
        if (candidate.symbolName == symbol
            && candidate.moduleScope == currentModule) {
            return 20;
        }
    }
    return 0;
}

bool CompletionService::isModuleRangeSymbolType(sym_list::sym_type_e type) const
{
    return type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_packed_struct_var
        || type == sym_list::sym_unpacked_struct_var;
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

bool CompletionService::isGlobalSymbolType(sym_list::sym_type_e type) const
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

bool CompletionService::isGlobalSymbolInfoType(sym_list::sym_type_e type) const
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
