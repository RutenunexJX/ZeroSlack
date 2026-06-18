#include "fsmgraphservice.h"

#include "symboltaxonomy.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>

std::unique_ptr<FsmGraphService> FsmGraphService::instance = nullptr;

namespace {
sym_list::SymbolInfo missingFsmSymbol()
{
    sym_list::SymbolInfo symbol;
    symbol.symbolId = -1;
    return symbol;
}

sym_list::SymbolInfo stateSymbolByName(
    const QList<sym_list::SymbolInfo>& states,
    const QString& stateName)
{
    for (const sym_list::SymbolInfo& state : states) {
        if (state.symbolName == stateName)
            return state;
    }
    return missingFsmSymbol();
}

RtlInsightCodeLink codeLinkForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (record.isValid()) {
        const QString fileName = fallback.fileName.isEmpty()
            ? record.location.fileName
            : fallback.fileName;
        return RtlInsightLink::fromFileLine(fileName,
                                            record.location.startLine,
                                            record.location.startColumn);
    }
    return RtlInsightLink::fromSymbol(fallback);
}

QString displayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback,
    const QString& defaultName = QString())
{
    if (!record.name.isEmpty())
        return record.name;
    if (!fallback.symbolName.isEmpty())
        return fallback.symbolName;
    return defaultName;
}

SymbolTaxonomy::SemanticMetadata metadataForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(fallback);
    if (!record.isValid())
        return metadata;

    metadata.declarationKind = record.declarationKind;
    metadata.usageRole = record.usageRole;
    metadata.visibility = record.visibility;
    metadata.sourceRole = record.sourceRole;
    metadata.rawCollectorKind = record.rawCollectorKind;
    metadata.interfaceLikeOwner = record.owner.interfaceLike;
    return metadata;
}

QString typeDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    return SymbolTaxonomy::symbolTypeLabel(
        metadataForRecord(record, fallback));
}

QString sourceRoleDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    return SymbolTaxonomy::sourceRoleDisplayName(
        metadataForRecord(record, fallback).sourceRole);
}

QString moduleDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;
    const SemanticSymbolRecord fallbackRecord =
        semanticSymbolRecordForSymbol(fallback);
    if (!fallbackRecord.owner.name.isEmpty())
        return fallbackRecord.owner.name;
    if (record.owner.kind == SymbolTaxonomy::SymbolOwnerScope::Global)
        return QStringLiteral("global");
    return QStringLiteral("global");
}

QString rawTypeTextForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.type.rawTypeText.isEmpty())
        return record.type.rawTypeText;
    return semanticSymbolRecordForSymbol(fallback).type.rawTypeText;
}

QString rawTypeTextForSymbol(const sym_list::SymbolInfo& symbol)
{
    return rawTypeTextForRecord(semanticSymbolRecordForSymbol(symbol), symbol);
}
}

FsmGraphService* FsmGraphService::getInstance()
{
    if (!instance)
        instance = std::make_unique<FsmGraphService>();
    return instance.get();
}

FsmGraphService::FsmGraphService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

FsmGraphService::~FsmGraphService() = default;

void FsmGraphService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

FsmGraphReport FsmGraphService::buildFsmGraph(const FsmGraphQuery& query) const
{
    FsmGraphReport report;
    report.groupDisplayName = QStringLiteral("FSM Graphs");
    const sym_list::SymbolInfo moduleSymbol =
        resolveModule(query, &report.notFoundReason);
    if (moduleSymbol.symbolId < 0) {
        report.notFoundReasonDisplayName =
            notFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    const QList<sym_list::SymbolInfo> moduleSymbols = symbolsInModule(moduleSymbol);
    const QList<sym_list::SymbolInfo> allSymbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& stateRegister
         : stateRegisters(moduleSymbols, allSymbols)) {
        FsmGraph graph;
        graph.moduleSymbol = moduleSymbol;
        graph.stateRegister = stateRegister;
        graph.nextStateSignal = nextStateSignal(moduleSymbols, stateRegister);
        graph.states = stateValues(moduleSymbols, allSymbols, stateRegister);
        graph.transitions = parseTransitions(moduleSymbol,
                                             stateRegister,
                                             graph.nextStateSignal,
                                             graph.states);
        if (graph.states.isEmpty() && graph.transitions.isEmpty())
            continue;
        fillDisplayMetadata(graph);
        report.graphs.append(graph);
    }

    report.found = !report.graphs.isEmpty();
    if (!report.found) {
        report.notFoundReason = FsmGraphNotFoundReason::NoFsmGraph;
        report.notFoundReasonDisplayName =
            notFoundReasonDisplayName(report.notFoundReason);
    } else {
        report.notFoundReason = FsmGraphNotFoundReason::None;
    }
    return report;
}

SemanticIndex* FsmGraphService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

sym_list::SymbolInfo FsmGraphService::resolveModule(
    const FsmGraphQuery& query,
    FsmGraphNotFoundReason* reason) const
{
    if (reason)
        *reason = FsmGraphNotFoundReason::None;

    if (query.moduleStableKey.isValid()) {
        const sym_list::SymbolInfo symbol =
            semanticIndex()->getSymbolByStableKey(query.moduleStableKey);
        if (symbol.symbolId < 0) {
            if (reason)
                *reason = FsmGraphNotFoundReason::NoMatchingModule;
            return missingFsmSymbol();
        }
        if (!SymbolTaxonomy::isModuleDeclaration(symbol)) {
            if (reason)
                *reason = FsmGraphNotFoundReason::UnsupportedSymbolKind;
            return missingFsmSymbol();
        }
        return symbol;
    }

    if (query.moduleSymbolId >= 0) {
        const sym_list::SymbolInfo symbol =
            semanticIndex()->getSymbolById(query.moduleSymbolId);
        if (symbol.symbolId < 0) {
            if (reason)
                *reason = FsmGraphNotFoundReason::NoMatchingModule;
            return missingFsmSymbol();
        }
        if (!SymbolTaxonomy::isModuleDeclaration(symbol)) {
            if (reason)
                *reason = FsmGraphNotFoundReason::UnsupportedSymbolKind;
            return missingFsmSymbol();
        }
        return symbol;
    }
    if (query.moduleName.isEmpty()) {
        if (reason)
            *reason = FsmGraphNotFoundReason::EmptyModuleName;
        return missingFsmSymbol();
    }

    SemanticDefinitionQuery definitionQuery;
    definitionQuery.symbolName = query.moduleName;
    definitionQuery.fileName = query.fileName;
    const SemanticDefinitionResult definition =
        semanticIndex()->resolveDefinition(definitionQuery);
    if (!definition.found) {
        if (reason)
            *reason = FsmGraphNotFoundReason::NoMatchingModule;
        return missingFsmSymbol();
    }
    if (!SymbolTaxonomy::isModuleDeclaration(definition.symbol)) {
        if (reason)
            *reason = FsmGraphNotFoundReason::UnsupportedSymbolKind;
        return missingFsmSymbol();
    }
    return definition.symbol;
}

QList<sym_list::SymbolInfo> FsmGraphService::symbolsInModule(
    const sym_list::SymbolInfo& moduleSymbol) const
{
    QList<sym_list::SymbolInfo> result;
    const QList<sym_list::SymbolInfo> symbols =
        semanticIndex()->getSymbols(moduleSymbol.fileName);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (isInsideModule(symbol, moduleSymbol))
            result.append(symbol);
    }
    sortSymbols(result);
    return result;
}

QList<sym_list::SymbolInfo> FsmGraphService::stateRegisters(
    const QList<sym_list::SymbolInfo>& moduleSymbols,
    const QList<sym_list::SymbolInfo>& allSymbols) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<int> seen;
    for (const sym_list::SymbolInfo& symbol : moduleSymbols) {
        if (!SymbolTaxonomy::isFsmStateRegisterDeclaration(
                SymbolTaxonomy::semanticMetadata(symbol))) {
            continue;
        }
        if (looksLikeNextStateName(symbol.symbolName))
            continue;
        const bool hasNextStatePair =
            hasPairedNextStateSignal(moduleSymbols, symbol);
        if (!looksLikeCurrentStateName(symbol.symbolName)
            && !hasNextStatePair) {
            continue;
        }
        const QString symbolType = rawTypeTextForSymbol(symbol);
        if (!symbolType.isEmpty()
            && !hasStateValuesForType(allSymbols, symbolType)
            && !hasNextStatePair
            && !symbol.symbolName.contains(QStringLiteral("state"),
                                           Qt::CaseInsensitive)) {
            continue;
        }
        if (seen.contains(symbol.symbolId))
            continue;
        seen.insert(symbol.symbolId);
        result.append(symbol);
    }
    sortSymbols(result);
    return result;
}

QList<sym_list::SymbolInfo> FsmGraphService::stateValues(
    const QList<sym_list::SymbolInfo>& moduleSymbols,
    const QList<sym_list::SymbolInfo>& allSymbols,
    const sym_list::SymbolInfo& stateRegister) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<int> seen;
    const QString stateRegisterType = rawTypeTextForSymbol(stateRegister);
    const QList<sym_list::SymbolInfo>& source =
        stateRegisterType.isEmpty() ? moduleSymbols : allSymbols;
    for (const sym_list::SymbolInfo& symbol : source) {
        if (!SymbolTaxonomy::isFsmStateValueDeclaration(
                SymbolTaxonomy::semanticMetadata(symbol))) {
            continue;
        }
        if (!stateRegisterType.isEmpty()) {
            if (rawTypeTextForSymbol(symbol) != stateRegisterType)
                continue;
        }
        if (seen.contains(symbol.symbolId))
            continue;
        seen.insert(symbol.symbolId);
        result.append(symbol);
    }
    if (!stateRegisterType.isEmpty() && result.isEmpty()) {
        for (const sym_list::SymbolInfo& symbol : moduleSymbols) {
            if (!SymbolTaxonomy::isFsmStateValueDeclaration(
                    SymbolTaxonomy::semanticMetadata(symbol))) {
                continue;
            }
            const QString symbolType = rawTypeTextForSymbol(symbol);
            if (!symbolType.isEmpty()
                && symbolType != stateRegisterType) {
                continue;
            }
            if (seen.contains(symbol.symbolId))
                continue;
            seen.insert(symbol.symbolId);
            result.append(symbol);
        }
    }
    sortSymbols(result);
    return result;
}

sym_list::SymbolInfo FsmGraphService::nextStateSignal(
    const QList<sym_list::SymbolInfo>& moduleSymbols,
    const sym_list::SymbolInfo& stateRegister) const
{
    QList<sym_list::SymbolInfo> candidates;
    const QString stateRegisterType = rawTypeTextForSymbol(stateRegister);
    for (const sym_list::SymbolInfo& symbol : moduleSymbols) {
        if (!SymbolTaxonomy::isFsmStateRegisterDeclaration(
                SymbolTaxonomy::semanticMetadata(symbol))) {
            continue;
        }
        if (symbol.symbolId == stateRegister.symbolId)
            continue;
        const bool looksNext =
            looksLikeNextStateName(symbol.symbolName)
            || isPairedNextStateName(stateRegister.symbolName, symbol.symbolName);
        if (!looksNext)
            continue;
        const QString symbolType = rawTypeTextForSymbol(symbol);
        if (!stateRegisterType.isEmpty()
            && !symbolType.isEmpty()
            && symbolType != stateRegisterType) {
            continue;
        }
        candidates.append(symbol);
    }
    sortSymbols(candidates);
    return candidates.isEmpty() ? missingFsmSymbol() : candidates.first();
}

QList<FsmTransition> FsmGraphService::parseTransitions(
    const sym_list::SymbolInfo& moduleSymbol,
    const sym_list::SymbolInfo& stateRegister,
    const sym_list::SymbolInfo& nextStateSignal,
    const QList<sym_list::SymbolInfo>& states) const
{
    QList<FsmTransition> transitions;
    if (states.isEmpty())
        return transitions;

    QSet<QString> stateNames;
    for (const sym_list::SymbolInfo& state : states)
        stateNames.insert(state.symbolName);

    const QString content = semanticIndex()->getCachedFileContent(moduleSymbol.fileName);
    if (content.isEmpty())
        return transitions;

    const QStringList lines = content.split('\n');
    const int lineCount = static_cast<int>(lines.size());
    int startIndex = moduleSymbol.startLine > 0 ? moduleSymbol.startLine - 1 : 0;
    int endIndex = moduleSymbol.endLine > 0 ? moduleSymbol.endLine - 1 : lineCount - 1;
    startIndex = std::max(0, startIndex);
    endIndex = std::min(endIndex, lineCount - 1);
    if (startIndex > endIndex)
        return transitions;

    const QRegularExpression caseExpression(
        QStringLiteral("\\bcase[zx]?\\s*\\(\\s*%1\\s*\\)")
            .arg(QRegularExpression::escape(stateRegister.symbolName)));
    const QRegularExpression labelExpression(
        QStringLiteral("^\\s*([A-Za-z_][A-Za-z0-9_$]*|default)\\s*:"));
    const QRegularExpression conditionExpression(
        QStringLiteral("\\bif\\s*\\(([^)]*)\\)"));
    const QRegularExpression endcaseExpression(QStringLiteral("\\bendcase\\b"));

    bool inCase = false;
    QString currentState;
    QString pendingCondition;
    QSet<QString> seenTransitions;
    for (int i = startIndex; i <= endIndex; ++i) {
        const QString code = stripLineComment(lines.at(i));
        if (!inCase) {
            if (code.contains(caseExpression))
                inCase = true;
            continue;
        }

        if (code.contains(endcaseExpression)) {
            inCase = false;
            currentState.clear();
            pendingCondition.clear();
            continue;
        }

        const QRegularExpressionMatch labelMatch = labelExpression.match(code);
        if (labelMatch.hasMatch()) {
            const QString label = labelMatch.captured(1);
            currentState = label == QStringLiteral("default") || stateNames.contains(label)
                ? label
                : QString();
            pendingCondition.clear();
        }
        if (currentState.isEmpty())
            continue;

        const QRegularExpressionMatch conditionMatch = conditionExpression.match(code);
        if (conditionMatch.hasMatch())
            pendingCondition = conditionMatch.captured(1).trimmed();

        const QString target = assignmentTarget(code);
        if (target.isEmpty())
            continue;
        if (!target.contains(QStringLiteral("state"), Qt::CaseInsensitive)
            && (nextStateSignal.symbolId < 0
                || target != nextStateSignal.symbolName)) {
            continue;
        }

        const QList<QString> toStates = assignedStateValues(code, stateNames);
        for (const QString& toState : toStates) {
            appendTransition(transitions,
                             seenTransitions,
                             currentState,
                             toState,
                             target,
                             transitionConditionForState(code,
                                                         pendingCondition,
                                                         toState),
                             moduleSymbol.fileName,
                             i + 1);
        }
        if (!toStates.isEmpty())
            pendingCondition.clear();
    }

    sortTransitions(transitions);
    return transitions;
}

bool FsmGraphService::isInsideModule(
    const sym_list::SymbolInfo& symbol,
    const sym_list::SymbolInfo& moduleSymbol)
{
    if (symbol.symbolId == moduleSymbol.symbolId)
        return false;
    const SemanticSymbolRecord symbolRecord =
        semanticSymbolRecordForSymbol(symbol);
    const SemanticSymbolRecord moduleRecord =
        semanticSymbolRecordForSymbol(moduleSymbol);
    if (!moduleRecord.name.isEmpty()
        && symbolRecord.owner.name == moduleRecord.name) {
        return true;
    }
    if (symbol.fileName != moduleSymbol.fileName)
        return false;
    if (moduleSymbol.startLine <= 0 || symbol.startLine <= 0)
        return false;
    if (symbol.startLine < moduleSymbol.startLine)
        return false;
    return moduleSymbol.endLine <= 0 || symbol.startLine <= moduleSymbol.endLine;
}

bool FsmGraphService::hasStateValuesForType(
    const QList<sym_list::SymbolInfo>& symbols,
    const QString& rawTypeText)
{
    if (rawTypeText.isEmpty())
        return false;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (SymbolTaxonomy::isFsmStateValueDeclaration(
                SymbolTaxonomy::semanticMetadata(symbol))
            && rawTypeTextForSymbol(symbol) == rawTypeText) {
            return true;
        }
    }
    return false;
}

bool FsmGraphService::hasPairedNextStateSignal(
    const QList<sym_list::SymbolInfo>& moduleSymbols,
    const sym_list::SymbolInfo& stateRegister)
{
    const QString stateRegisterType = rawTypeTextForSymbol(stateRegister);
    for (const sym_list::SymbolInfo& symbol : moduleSymbols) {
        if (symbol.symbolId == stateRegister.symbolId)
            continue;
        if (!SymbolTaxonomy::isFsmStateRegisterDeclaration(
                SymbolTaxonomy::semanticMetadata(symbol))) {
            continue;
        }
        const QString symbolType = rawTypeTextForSymbol(symbol);
        if (!stateRegisterType.isEmpty()
            && !symbolType.isEmpty()
            && symbolType != stateRegisterType) {
            continue;
        }
        if (looksLikeNextStateName(symbol.symbolName)
            || isPairedNextStateName(stateRegister.symbolName,
                                     symbol.symbolName)) {
            return true;
        }
    }
    return false;
}

bool FsmGraphService::isPairedNextStateName(
    const QString& currentName,
    const QString& candidateName)
{
    const QString current = currentName.toLower();
    const QString candidate = candidateName.toLower();
    if (current.isEmpty() || candidate.isEmpty())
        return false;
    if (current.endsWith(QStringLiteral("_cs")))
        return candidate == current.left(current.size() - 3) + QStringLiteral("_ns");
    if (current.endsWith(QStringLiteral("cs")))
        return candidate == current.left(current.size() - 2) + QStringLiteral("ns");
    if (current.endsWith(QStringLiteral("_q")))
        return candidate == current.left(current.size() - 2) + QStringLiteral("_d");
    if (current.endsWith(QStringLiteral("_cur")))
        return candidate == current.left(current.size() - 4) + QStringLiteral("_nxt");
    return false;
}

bool FsmGraphService::looksLikeCurrentStateName(const QString& name)
{
    const QString lower = name.toLower();
    return lower.contains(QStringLiteral("state"))
        || lower == QStringLiteral("cs")
        || lower.endsWith(QStringLiteral("_cs"))
        || lower.endsWith(QStringLiteral("cs"))
        || lower.endsWith(QStringLiteral("_q"));
}

bool FsmGraphService::looksLikeNextStateName(const QString& name)
{
    const QString lower = name.toLower();
    return lower.contains(QStringLiteral("next"))
        || lower == QStringLiteral("ns")
        || lower.endsWith(QStringLiteral("_ns"))
        || lower.endsWith(QStringLiteral("ns"))
        || lower.endsWith(QStringLiteral("_d"))
        || lower.endsWith(QStringLiteral("_nxt"));
}

QString FsmGraphService::assignmentTarget(const QString& code)
{
    const QRegularExpression assignmentExpression(
        QStringLiteral("\\b([A-Za-z_][A-Za-z0-9_$]*)\\s*(?:<=|=)\\s*"));
    const QRegularExpressionMatch match = assignmentExpression.match(code);
    return match.hasMatch() ? match.captured(1) : QString();
}

QList<QString> FsmGraphService::assignedStateValues(
    const QString& code,
    const QSet<QString>& stateNames)
{
    QList<QString> states;
    QSet<QString> seen;
    const int assignment = code.indexOf(QRegularExpression(QStringLiteral("<=|=")));
    const QString rhs = assignment >= 0 ? code.mid(assignment + 1) : code;
    const QRegularExpression identifierExpression(
        QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_$]*\\b"));
    QRegularExpressionMatchIterator it = identifierExpression.globalMatch(rhs);
    while (it.hasNext()) {
        const QString token = it.next().captured(0);
        if (!stateNames.contains(token) || seen.contains(token))
            continue;
        seen.insert(token);
        states.append(token);
    }
    return states;
}

QString FsmGraphService::transitionConditionForState(
    const QString& code,
    const QString& pendingCondition,
    const QString& stateName)
{
    const int question = code.indexOf(QLatin1Char('?'));
    const int colon = question >= 0 ? code.indexOf(QLatin1Char(':'), question + 1) : -1;
    if (question < 0 || colon < 0)
        return pendingCondition;

    QString condition = code.left(question).trimmed();
    const int assignment = condition.indexOf(QRegularExpression(QStringLiteral("<=|=")));
    if (assignment >= 0)
        condition = condition.mid(assignment + 1).trimmed();
    if (condition.isEmpty())
        condition = pendingCondition;

    const QString trueBranch = code.mid(question + 1, colon - question - 1);
    const QString falseBranch = code.mid(colon + 1);
    if (trueBranch.contains(QRegularExpression(
            QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(stateName))))) {
        return condition;
    }
    if (falseBranch.contains(QRegularExpression(
            QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(stateName))))) {
        return condition.isEmpty()
            ? QStringLiteral("else")
            : QStringLiteral("else %1").arg(condition);
    }
    return pendingCondition;
}

void FsmGraphService::appendTransition(
    QList<FsmTransition>& transitions,
    QSet<QString>& seenTransitions,
    const QString& currentState,
    const QString& toState,
    const QString& assignmentTarget,
    const QString& condition,
    const QString& fileName,
    int line)
{
    const QString key = QStringLiteral("%1:%2:%3:%4")
                            .arg(currentState)
                            .arg(toState)
                            .arg(assignmentTarget)
                            .arg(line);
    if (seenTransitions.contains(key))
        return;
    seenTransitions.insert(key);

    FsmTransition transition;
    transition.fromState = currentState;
    transition.toState = toState;
    transition.assignmentTarget = assignmentTarget;
    transition.line = line;
    transition.codeLink = RtlInsightLink::fromFileLine(fileName, transition.line, 1);
    transition.condition = condition;
    fillDisplayMetadata(transition);
    transitions.append(transition);
}

QString FsmGraphService::stripLineComment(const QString& line)
{
    const int commentIndex = line.indexOf(QStringLiteral("//"));
    if (commentIndex < 0)
        return line;
    return line.left(commentIndex);
}

QList<FsmStateRow> FsmGraphService::stateRows(
    const QList<sym_list::SymbolInfo>& states)
{
    QList<FsmStateRow> rows;
    rows.reserve(states.size());
    for (const sym_list::SymbolInfo& state : states) {
        FsmStateRow row;
        row.state = state;
        row.stateRecord = semanticSymbolRecordForSymbol(state);
        row.stateStableKey = row.stateRecord.isValid()
            ? row.stateRecord.stableKey
            : symbolStableKeyForSymbol(state);
        row.codeLink = codeLinkForRecord(row.stateRecord, state);
        row.sectionDisplayName = QStringLiteral("State");
        row.detailDisplayName = stateDetailDisplayName(state);
        row.typeDisplayName = typeDisplayNameForRecord(row.stateRecord, state);
        row.sourceRoleDisplayName =
            sourceRoleDisplayNameForRecord(row.stateRecord, state);
        row.moduleDisplayName = moduleDisplayNameForRecord(row.stateRecord, state);
        rows.append(row);
    }
    return rows;
}

QList<FsmTransitionRow> FsmGraphService::transitionRows(
    const sym_list::SymbolInfo& moduleSymbol,
    const QList<FsmTransition>& transitions,
    const QList<sym_list::SymbolInfo>& states)
{
    QList<FsmTransitionRow> rows;
    rows.reserve(transitions.size());
    for (const FsmTransition& transition : transitions) {
        FsmTransitionRow row;
        row.transition = transition;
        row.fromStateSymbol = stateSymbolByName(states, transition.fromState);
        row.toStateSymbol = stateSymbolByName(states, transition.toState);
        row.moduleSymbolRecord = semanticSymbolRecordForSymbol(moduleSymbol);
        row.fromStateRecord = semanticSymbolRecordForSymbol(row.fromStateSymbol);
        row.toStateRecord = semanticSymbolRecordForSymbol(row.toStateSymbol);
        row.fromStateStableKey = row.fromStateRecord.isValid()
            ? row.fromStateRecord.stableKey
            : symbolStableKeyForSymbol(row.fromStateSymbol);
        row.toStateStableKey = row.toStateRecord.isValid()
            ? row.toStateRecord.stableKey
            : symbolStableKeyForSymbol(row.toStateSymbol);
        row.codeLink = transition.codeLink;
        row.fromStateCodeLink =
            codeLinkForRecord(row.fromStateRecord, row.fromStateSymbol);
        row.toStateCodeLink =
            codeLinkForRecord(row.toStateRecord, row.toStateSymbol);
        row.sectionDisplayName = transition.sectionDisplayName;
        row.fromStateDisplayName =
            displayNameForRecord(row.fromStateRecord,
                                 row.fromStateSymbol,
                                 transition.fromState);
        row.toStateDisplayName =
            displayNameForRecord(row.toStateRecord,
                                 row.toStateSymbol,
                                 transition.toState);
        row.conditionDisplayName = transitionConditionDisplayName(transition);
        row.detailDisplayName = transition.detailDisplayName;
        row.sourceLineDisplayName = transitionSourceLineDisplayName(transition);
        row.sourceRoleDisplayName =
            sourceRoleDisplayNameForRecord(row.moduleSymbolRecord, moduleSymbol);
        rows.append(row);
    }
    return rows;
}

QString FsmGraphService::stateDetailDisplayName(
    const sym_list::SymbolInfo& state)
{
    return rawTypeTextForSymbol(state);
}

QString FsmGraphService::stateRegisterDetailDisplayName(
    const FsmGraph& graph)
{
    if (graph.nextStateSignal.symbolId >= 0) {
        return QStringLiteral("next %1")
            .arg(graph.nextStateSignal.symbolName);
    }
    return QStringLiteral("state register");
}

QString FsmGraphService::transitionDetailDisplayName(
    const FsmTransition& transition)
{
    if (transition.condition.isEmpty())
        return transition.assignmentTarget;
    return QStringLiteral("%1 when %2")
        .arg(transition.assignmentTarget, transition.condition);
}

QString FsmGraphService::transitionConditionDisplayName(
    const FsmTransition& transition)
{
    return transition.condition.isEmpty()
        ? QStringLiteral("unconditional")
        : transition.condition;
}

QString FsmGraphService::transitionSourceLineDisplayName(
    const FsmTransition& transition)
{
    return transition.line > 0
        ? QStringLiteral("line %1").arg(transition.line)
        : QStringLiteral("line unknown");
}

QString FsmGraphService::notFoundReasonDisplayName(
    FsmGraphNotFoundReason reason)
{
    switch (reason) {
    case FsmGraphNotFoundReason::None:
        return QString();
    case FsmGraphNotFoundReason::EmptyModuleName:
        return QStringLiteral("empty module name");
    case FsmGraphNotFoundReason::NoMatchingModule:
        return QStringLiteral("no matching module");
    case FsmGraphNotFoundReason::UnsupportedSymbolKind:
        return QStringLiteral("unsupported symbol kind");
    case FsmGraphNotFoundReason::NoFsmGraph:
        return QStringLiteral("no FSM graph");
    }
    return QStringLiteral("FSM graph unavailable");
}

void FsmGraphService::fillDisplayMetadata(FsmGraph& graph)
{
    graph.moduleSymbolRecord = semanticSymbolRecordForSymbol(graph.moduleSymbol);
    graph.stateRegisterRecord =
        semanticSymbolRecordForSymbol(graph.stateRegister);
    graph.nextStateSignalRecord =
        semanticSymbolRecordForSymbol(graph.nextStateSignal);
    graph.moduleStableKey = graph.moduleSymbolRecord.isValid()
        ? graph.moduleSymbolRecord.stableKey
        : symbolStableKeyForSymbol(graph.moduleSymbol);
    graph.stateRegisterStableKey = graph.stateRegisterRecord.isValid()
        ? graph.stateRegisterRecord.stableKey
        : symbolStableKeyForSymbol(graph.stateRegister);
    graph.nextStateSignalStableKey = graph.nextStateSignalRecord.isValid()
        ? graph.nextStateSignalRecord.stableKey
        : symbolStableKeyForSymbol(graph.nextStateSignal);
    graph.stateRegisterCodeLink =
        codeLinkForRecord(graph.stateRegisterRecord, graph.stateRegister);
    graph.nextStateSignalCodeLink =
        codeLinkForRecord(graph.nextStateSignalRecord, graph.nextStateSignal);
    graph.stateRegisterSectionDisplayName = QStringLiteral("State Register");
    graph.stateRegisterDetailDisplayName = stateRegisterDetailDisplayName(graph);
    graph.stateRegisterTypeDisplayName =
        typeDisplayNameForRecord(graph.stateRegisterRecord,
                                 graph.stateRegister);
    graph.stateRegisterSourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(graph.stateRegisterRecord,
                                       graph.stateRegister);
    graph.nextStateSignalDisplayName = graph.nextStateSignal.symbolId >= 0
        ? displayNameForRecord(graph.nextStateSignalRecord,
                               graph.nextStateSignal)
        : QString();
    graph.nextStateSignalTypeDisplayName = graph.nextStateSignal.symbolId >= 0
        ? typeDisplayNameForRecord(graph.nextStateSignalRecord,
                                   graph.nextStateSignal)
        : QString();
    graph.nextStateSignalSourceRoleDisplayName = graph.nextStateSignal.symbolId >= 0
        ? sourceRoleDisplayNameForRecord(graph.nextStateSignalRecord,
                                         graph.nextStateSignal)
        : QString();
    graph.statesGroupDisplayName = QStringLiteral("States");
    graph.transitionsGroupDisplayName = QStringLiteral("Transitions");
    graph.stateRows = stateRows(graph.states);
    for (FsmTransition& transition : graph.transitions)
        fillDisplayMetadata(transition);
    graph.transitionRows = transitionRows(
        graph.moduleSymbol,
        graph.transitions,
        graph.states);
}

void FsmGraphService::fillDisplayMetadata(FsmTransition& transition)
{
    transition.sectionDisplayName = transition.fromState;
    transition.detailDisplayName = transitionDetailDisplayName(transition);
}

void FsmGraphService::sortSymbols(QList<sym_list::SymbolInfo>& symbols)
{
    std::sort(symbols.begin(), symbols.end(),
              [](const sym_list::SymbolInfo& lhs,
                 const sym_list::SymbolInfo& rhs) {
                  if (lhs.fileName != rhs.fileName)
                      return lhs.fileName < rhs.fileName;
                  if (lhs.startLine != rhs.startLine)
                      return lhs.startLine < rhs.startLine;
                  if (lhs.startColumn != rhs.startColumn)
                      return lhs.startColumn < rhs.startColumn;
                  if (lhs.symbolName != rhs.symbolName)
                      return lhs.symbolName < rhs.symbolName;
                  return lhs.symbolId < rhs.symbolId;
              });
}

void FsmGraphService::sortTransitions(QList<FsmTransition>& transitions)
{
    std::sort(transitions.begin(), transitions.end(),
              [](const FsmTransition& lhs, const FsmTransition& rhs) {
                  if (lhs.line != rhs.line)
                      return lhs.line < rhs.line;
                  if (lhs.fromState != rhs.fromState)
                      return lhs.fromState < rhs.fromState;
                  if (lhs.toState != rhs.toState)
                      return lhs.toState < rhs.toState;
                  return lhs.assignmentTarget < rhs.assignmentTarget;
              });
}
