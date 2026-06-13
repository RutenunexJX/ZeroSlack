#include "completionservice.h"

#include "completionmatcher.h"
#include "relationshipservice.h"

#include <QRegularExpression>
#include <QSet>
#include <Qt>
#include <QVector>
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
    return findCompletionResult(query).names;
}

CompletionResult CompletionService::findCompletionResult(
    const CompletionQuery& query) const
{
    CompletionResult result;
    result.symbols = findCompletionSymbols(query);
    result.names = completionNamesFromSymbols(result.symbols);
    return result;
}

QList<sym_list::SymbolInfo> CompletionService::findCompletionSymbols(
    const CompletionQuery& query) const
{
    if (!query.structTypeNameForMember.isEmpty()) {
        return findStructMemberSymbols(query);
    }

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
    const QStringList names = semanticIndex()->getCompletionSymbolNames();

    QVector<QPair<QString, int>> scored;
    scored.reserve(qMin(names.size(), maxResults > 0 ? maxResults : names.size()));
    for (const QString& name : std::as_const(names)) {
        const int score = CompletionMatcher::calculateContextMatchScore(name, prefix);
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
    const QList<sym_list::SymbolInfo> symbols =
        semanticIndex()->getTypedCompletionSymbols(symbolType);

    result.reserve(qMin(symbols.size(), maxResults > 0 ? maxResults : symbols.size()));
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const int score =
            CompletionMatcher::calculateSymbolTypeScore(symbol.symbolName, prefix);
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
    return CompletionMatcher::matchesAbbreviation(text, abbreviation);
}

int CompletionService::calculateCompletionMatchScore(
    const QString& text,
    const QString& abbreviation) const
{
    return CompletionMatcher::calculateContextMatchScore(text, abbreviation);
}

int CompletionService::completionItemScore(const QString& text, const QString& prefix) const
{
    return CompletionMatcher::completionItemScore(text, prefix);
}

QString CompletionService::symbolTypeDescription(sym_list::sym_type_e symbolType) const
{
    switch (symbolType) {
    case sym_list::sym_module:
        return QStringLiteral("module");
    case sym_list::sym_reg:
        return QStringLiteral("reg");
    case sym_list::sym_wire:
        return QStringLiteral("wire");
    case sym_list::sym_logic:
        return QStringLiteral("logic");
    case sym_list::sym_task:
        return QStringLiteral("task");
    case sym_list::sym_function:
        return QStringLiteral("function");
    case sym_list::sym_parameter:
        return QStringLiteral("parameter");
    case sym_list::sym_localparam:
        return QStringLiteral("localparam");
    case sym_list::sym_struct_member:
        return QStringLiteral("member");
    default:
        return QStringLiteral("symbol");
    }
}

QList<CommandModeCommand> CompletionService::commandModeCommands() const
{
    return {
        {QStringLiteral("r "), sym_list::sym_reg, QStringLiteral("reg variables"), QStringLiteral("reg")},
        {QStringLiteral("w "), sym_list::sym_wire, QStringLiteral("wire variables"), QStringLiteral("wire")},
        {QStringLiteral("l "), sym_list::sym_logic, QStringLiteral("logic variables"), QStringLiteral("logic")},
        {QStringLiteral("m "), sym_list::sym_module, QStringLiteral("modules"), QStringLiteral("module")},
        {QStringLiteral("t "), sym_list::sym_task, QStringLiteral("tasks"), QStringLiteral("task")},
        {QStringLiteral("f "), sym_list::sym_function, QStringLiteral("functions"), QStringLiteral("function")},
        {QStringLiteral("i "), sym_list::sym_interface, QStringLiteral("interfaces"), QStringLiteral("interface")},
        {QStringLiteral("d "), sym_list::sym_def_define, QStringLiteral("macro definitions"), QStringLiteral("`define")},
        {QStringLiteral("lp "), sym_list::sym_localparam, QStringLiteral("localparam declarations"), QStringLiteral("localparam")},
        {QStringLiteral("p "), sym_list::sym_parameter, QStringLiteral("parameter declarations"), QStringLiteral("parameter")},
        {QStringLiteral("a "), sym_list::sym_always, QStringLiteral("always blocks"), QStringLiteral("always")},
        {QStringLiteral("c "), sym_list::sym_assign, QStringLiteral("continuous assignments"), QStringLiteral("assign")},
        {QStringLiteral("u "), sym_list::sym_typedef, QStringLiteral("type definitions"), QStringLiteral("typedef")},
        {QStringLiteral("ee "), sym_list::sym_enum_value, QStringLiteral("enum values"), QStringLiteral("enum_value")},
        {QStringLiteral("ne "), sym_list::sym_enum, QStringLiteral("enum types"), QStringLiteral("enum")},
        {QStringLiteral("e "), sym_list::sym_enum_var, QStringLiteral("enum variables"), QStringLiteral("enum_var")},
        {QStringLiteral("sm "), sym_list::sym_struct_member, QStringLiteral("struct members"), QStringLiteral("member")},
        {QStringLiteral("nsp "), sym_list::sym_packed_struct, QStringLiteral("packed struct types"), QStringLiteral("struct")},
        {QStringLiteral("ns "), sym_list::sym_unpacked_struct, QStringLiteral("unpacked struct types"), QStringLiteral("struct")},
        {QStringLiteral("sp "), sym_list::sym_packed_struct_var, QStringLiteral("packed struct variables"), QStringLiteral("struct")},
        {QStringLiteral("s "), sym_list::sym_unpacked_struct_var, QStringLiteral("unpacked struct variables"), QStringLiteral("struct")},
    };
}

CommandModeMatch CompletionService::matchCommandMode(const QString& lineUpToCursor) const
{
    CommandModeMatch result;
    for (const CommandModeCommand& command : commandModeCommands()) {
        const int prefixPosition = lineUpToCursor.lastIndexOf(command.prefix);
        if (prefixPosition < 0)
            continue;

        const QString beforePrefix = lineUpToCursor.left(prefixPosition).trimmed();
        if (!beforePrefix.isEmpty())
            continue;

        result.matched = true;
        result.prefixPosition = prefixPosition;
        result.command = command;
        result.input = lineUpToCursor.mid(prefixPosition + command.prefix.length());
        return result;
    }
    return result;
}

CommandModeInputState CompletionService::commandModeInputState(
    const QString& lineUpToCursor) const
{
    const CommandModeMatch match = matchCommandMode(lineUpToCursor);
    CommandModeInputState state;
    if (!match.matched)
        return state;

    state.matched = true;
    state.prefixPosition = match.prefixPosition;
    state.input = match.input;
    state.command = match.command;
    state.exitRequested =
        lineUpToCursor.size() >= 2
        && lineUpToCursor.right(2) == QStringLiteral("  ");
    return state;
}

CommandModeCompletionState CompletionService::commandModeCompletionState(
    const CommandModeCompletionQuery& query) const
{
    const CommandModeInputState inputState =
        commandModeInputState(query.lineUpToCursor);

    CommandModeCompletionState state;
    if (!inputState.matched)
        return state;

    state.matched = true;
    state.exitRequested = inputState.exitRequested;
    state.prefixPosition = inputState.prefixPosition;
    state.input = inputState.input;
    state.completionPrefix = inputState.input.trimmed();
    state.command = inputState.command;

    if (state.exitRequested)
        return state;

    CommandCompletionQuery completionQuery;
    completionQuery.prefix = state.completionPrefix;
    completionQuery.fileName = query.fileName;
    completionQuery.moduleName = query.moduleName;
    completionQuery.documentText = query.documentText;
    completionQuery.symbolType = state.command.symbolType;

    state.symbols = findCommandCompletionSymbols(completionQuery);
    if (state.symbols.isEmpty()
        && isModuleRangeSymbolType(state.command.symbolType)
        && completionQuery.moduleName.isEmpty()) {
        state.hidePopup = true;
        return state;
    }

    state.showCompletions = true;
    return state;
}

EditorCompletionState CompletionService::editorCompletionState(
    const EditorCompletionQuery& query) const
{
    EditorCompletionState state;

    QString variableName;
    QString memberPrefix;
    const QString lineForParse = query.lineUpToCursor.trimmed();
    if (tryParseStructMemberContext(lineForParse, variableName, memberPrefix)) {
        const QString structTypeName =
            getStructTypeForVariable(variableName, query.moduleName);
        if (!structTypeName.isEmpty()) {
            CompletionQuery completionQuery;
            completionQuery.prefix = memberPrefix;
            completionQuery.fileName = query.fileName;
            completionQuery.moduleName = query.moduleName;
            completionQuery.structTypeNameForMember = structTypeName;
            completionQuery.cursorLine = query.cursorLine;
            completionQuery.cursorPosition = query.cursorPosition;

            state.available = true;
            state.prefix = memberPrefix;
            state.replacementStartColumn =
                query.lineUpToCursor.lastIndexOf(QLatin1Char('.')) + 1;
            state.completion = findCompletionResult(completionQuery);
            return state;
        }
    }

    if (query.wordPrefix.length() < 1)
        return state;

    CompletionQuery completionQuery;
    completionQuery.prefix = query.wordPrefix;
    completionQuery.fileName = query.fileName;
    completionQuery.moduleName = query.moduleName;
    completionQuery.cursorLine = query.cursorLine;
    completionQuery.cursorPosition = query.cursorPosition;

    state.available = true;
    state.prefix = query.wordPrefix;
    state.replacementStartColumn =
        qMax(0, query.lineUpToCursor.size() - query.wordPrefix.size());
    state.completion = findCompletionResult(completionQuery);
    return state;
}

CompletionTriggerState CompletionService::completionTriggerState(
    const CompletionTriggerQuery& query) const
{
    CompletionTriggerState state;

    if (query.lineUpToCursor.isEmpty()) {
        state.hidePopup = !query.commandModeActive;
        return state;
    }

    const QChar lastChar = query.lineUpToCursor.back();
    if (query.commandModeActive) {
        state.continueCompletion =
            lastChar.isLetterOrNumber()
            || lastChar == QLatin1Char('_')
            || lastChar == QLatin1Char(' ');
        state.hidePopup = false;
        return state;
    }

    if (lastChar.isLetterOrNumber()
        || lastChar == QLatin1Char('_')
        || lastChar == QLatin1Char('.')) {
        state.continueCompletion = true;
        return state;
    }

    if (lastChar != QLatin1Char(' ')) {
        state.hidePopup = true;
        return state;
    }

    const QString lineBeforeSpace =
        query.lineUpToCursor.left(query.lineUpToCursor.size() - 1).trimmed();
    QString variableName;
    QString memberPrefix;
    if (!tryParseStructMemberContext(lineBeforeSpace, variableName, memberPrefix)) {
        state.hidePopup = true;
        return state;
    }

    state.continueCompletion =
        !getStructTypeForVariable(variableName, query.moduleName).isEmpty();
    state.hidePopup = !state.continueCompletion;
    return state;
}

bool CompletionService::shouldContinueCompletion(
    const CompletionTriggerQuery& query) const
{
    return completionTriggerState(query).continueCompletion;
}

CompletionActivationState CompletionService::completionActivationState(
    const CompletionActivationQuery& query) const
{
    CompletionActivationState state;
    if (!query.selectable)
        return state;

    switch (query.mode) {
    case CompletionActivationMode::AlternateMode:
        state.action = CompletionActivationAction::ExecuteAlternateCommand;
        state.text = query.itemText;
        return state;
    case CompletionActivationMode::CommandMode:
        state.action = CompletionActivationAction::ReplaceLine;
        state.text = query.defaultValue.isEmpty()
            ? query.itemText
            : query.defaultValue;
        state.clearCommandMode = true;
        state.hidePopup = true;
        return state;
    case CompletionActivationMode::EditorWord:
        state.action = CompletionActivationAction::ReplaceWord;
        state.text = query.itemText;
        state.hidePopup = true;
        return state;
    }

    return state;
}

CompletionPopupKeyState CompletionService::completionPopupKeyState(
    const CompletionPopupKeyQuery& query) const
{
    CompletionPopupKeyState state;

    switch (query.key) {
    case Qt::Key_Down:
    case Qt::Key_Up:
        state.action = CompletionPopupKeyAction::ForwardToPopup;
        return state;
    case Qt::Key_Escape:
        state.action = query.mode == CompletionActivationMode::AlternateMode
            ? CompletionPopupKeyAction::HidePopupAndClearAlternate
            : CompletionPopupKeyAction::HidePopup;
        return state;
    case Qt::Key_Backspace:
        if (query.mode == CompletionActivationMode::AlternateMode) {
            state.action = query.alternateBufferEmpty
                ? CompletionPopupKeyAction::HidePopup
                : CompletionPopupKeyAction::BackspaceAlternateInput;
        }
        return state;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (query.mode == CompletionActivationMode::AlternateMode) {
            state.action = query.currentIndexValid
                ? CompletionPopupKeyAction::ActivateCurrent
                : CompletionPopupKeyAction::Consume;
        } else {
            state.action = query.hasRows
                ? CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable
                : CompletionPopupKeyAction::Consume;
        }
        return state;
    case Qt::Key_Tab:
        if (query.mode != CompletionActivationMode::AlternateMode) {
            state.action = query.hasRows
                ? CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable
                : CompletionPopupKeyAction::Consume;
        }
        return state;
    default:
        return state;
    }
}

CommandSymbolPresentation CompletionService::commandSymbolPresentation(
    sym_list::sym_type_e symbolType) const
{
    for (const CommandModeCommand& command : commandModeCommands()) {
        if (command.symbolType == symbolType) {
            CommandSymbolPresentation presentation;
            presentation.defaultValue = command.defaultValue;
            presentation.typeDescription = command.description;
            return presentation;
        }
    }

    CommandSymbolPresentation presentation;
    presentation.defaultValue = QStringLiteral("symbol");
    presentation.typeDescription = QStringLiteral("symbols");
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
    return CompletionMatcher::abbreviationPositions(text, abbreviation);
}

QVector<QPair<QString, int>> CompletionService::findScoredKeywordCompletions(
    const QString& prefix) const
{
    return CompletionMatcher::scoredKeywordCompletions(prefix);
}

QStringList CompletionService::findKeywordCompletions(
    const QString& prefix,
    int maxResults) const
{
    return CompletionMatcher::keywordCompletions(prefix, maxResults);
}

QStringList CompletionService::findKeywordAbbreviationMatches(
    const QStringList& candidates,
    const QString& abbreviation) const
{
    return CompletionMatcher::keywordAbbreviationMatches(candidates, abbreviation);
}

bool CompletionService::relationshipCompletionsAvailable() const
{
    return semanticIndex()->hasRelationshipFacts();
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
        const int baseScore = CompletionMatcher::calculateContextMatchScore(completion, prefix);
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
    return semanticIndex()->getRelationshipCompletionNames(
        moduleName,
        {SymbolRelationshipEngine::CONTAINS},
        true,
        prefix);
}

QStringList CompletionService::findRelatedSymbolCompletions(
    const QString& symbolName,
    const QString& prefix) const
{
    return semanticIndex()->getBidirectionalRelationshipCompletionNames(
        symbolName,
        {SymbolRelationshipEngine::REFERENCES},
        prefix);
}

QStringList CompletionService::findSymbolReferenceCompletions(
    const QString& symbolName,
    const QString& prefix) const
{
    return semanticIndex()->getRelationshipCompletionNames(
        symbolName,
        {SymbolRelationshipEngine::REFERENCES},
        false,
        prefix);
}

QStringList CompletionService::findClockDomainCompletions(const QString& prefix) const
{
    return semanticIndex()->getSymbolsWithOutgoingRelationshipCompletionNames(
        SymbolRelationshipEngine::CLOCKS,
        prefix);
}

QStringList CompletionService::findResetSignalCompletions(const QString& prefix) const
{
    return semanticIndex()->getSymbolsWithOutgoingRelationshipCompletionNames(
        SymbolRelationshipEngine::RESETS,
        prefix);
}

QStringList CompletionService::findModuleInternalVariableCompletions(
    const QString& moduleName,
    const QString& prefix) const
{
    return completionNamesFromSymbols(
        semanticIndex()->getModuleCompletionSymbols(moduleName, prefix));
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
    return completionNamesFromSymbols(semanticIndex()->getGlobalCompletionSymbols(prefix));
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
        return completionNamesFromSymbols(
            semanticIndex()->getTypedCompletionSymbols(variableType, prefix));
    }
    return findModuleSymbolsByType(moduleName, variableType, prefix);
}

QStringList CompletionService::findTaskFunctionCompletions(const QString& prefix) const
{
    QStringList result;
    result.append(completionNamesFromSymbols(
        semanticIndex()->getTypedCompletionSymbols(sym_list::sym_task, prefix)));
    result.append(completionNamesFromSymbols(
        semanticIndex()->getTypedCompletionSymbols(sym_list::sym_function, prefix)));
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
        result.append(CompletionMatcher::svKeywordCompletions(query.prefix));
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
        result.append(CompletionMatcher::svKeywordCompletions(query.prefix));
    }

    result.removeDuplicates();

    QVector<QPair<QString, int>> scoredResults;
    scoredResults.reserve(result.size());
    for (const QString& completion : std::as_const(result)) {
        int score = CompletionMatcher::calculateContextMatchScore(completion, query.prefix);
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
    return semanticIndex()->getEnumValueCompletionNames(prefix, enumTypeName);
}

QString CompletionService::findEnumTypeForVariable(
    const QString& variableName,
    const QString& moduleName) const
{
    return semanticIndex()->enumTypeForVariable(variableName, moduleName);
}

QStringList CompletionService::findModulePortCompletions(
    const QString& prefix,
    const QString& moduleTypeName) const
{
    return semanticIndex()->getModulePortCompletionNames(prefix, moduleTypeName);
}

QList<sym_list::SymbolInfo> CompletionService::findModuleInternalSymbolInfosByType(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    bool useRelationshipFallback) const
{
    return semanticIndex()->getModuleInternalSymbolsByType(
        moduleName,
        symbolType,
        prefix,
        useRelationshipFallback);
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
    return semanticIndex()->getGlobalSymbolInfosByType(symbolType, prefix);
}

QString CompletionService::currentModuleAt(const QString& fileName, int cursorPosition) const
{
    return semanticIndex()->currentModuleAt(fileName, cursorPosition);
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
    return semanticIndex()->getModuleCompletionSymbols(query.moduleName, query.prefix);
}

QList<sym_list::SymbolInfo> CompletionService::findGlobalCompletionSymbols(
    const CompletionQuery& query) const
{
    return semanticIndex()->getGlobalCompletionSymbols(query.prefix);
}

QList<sym_list::SymbolInfo> CompletionService::findCommandSymbolsFromIndex(
    const CommandCompletionQuery& query) const
{
    return semanticIndex()->getCommandCompletionSymbols(
        query.moduleName,
        query.symbolType,
        query.prefix);
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

    if (relationships.hasNamedRelationship(
            currentContext,
            symbol,
            SymbolRelationshipEngine::CONTAINS)) {
        return 40;
    }

    if (relationships.hasNamedRelationship(
            symbol,
            currentContext,
            SymbolRelationshipEngine::REFERENCES)
        || relationships.hasNamedRelationship(
            currentContext,
            symbol,
            SymbolRelationshipEngine::REFERENCES)) {
        return 30;
    }

    if (relationships.hasNamedRelationship(
            symbol,
            currentContext,
            SymbolRelationshipEngine::CALLS)
        || relationships.hasNamedRelationship(
            currentContext,
            symbol,
            SymbolRelationshipEngine::CALLS)) {
        return 25;
    }

    return 0;
}

int CompletionService::calculateScopeScore(
    const QString& symbol,
    const QString& currentModule) const
{
    return semanticIndex()->scopeScoreForSymbol(symbol, currentModule);
}

bool CompletionService::isModuleRangeSymbolType(sym_list::sym_type_e type) const
{
    return type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_packed_struct_var
        || type == sym_list::sym_unpacked_struct_var;
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
