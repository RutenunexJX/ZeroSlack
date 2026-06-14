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
    const sym_list::SymbolInfo moduleSymbol = resolveModule(query);
    if (moduleSymbol.symbolId < 0)
        return report;

    const QList<sym_list::SymbolInfo> symbols = symbolsInModule(moduleSymbol);
    for (const sym_list::SymbolInfo& stateRegister : stateRegisters(symbols)) {
        FsmGraph graph;
        graph.moduleSymbol = moduleSymbol;
        graph.stateRegister = stateRegister;
        graph.nextStateSignal = nextStateSignal(symbols, stateRegister);
        graph.states = stateValues(symbols, stateRegister);
        graph.transitions = parseTransitions(moduleSymbol, stateRegister, graph.states);
        if (graph.states.isEmpty() && graph.transitions.isEmpty())
            continue;
        report.graphs.append(graph);
    }

    report.found = !report.graphs.isEmpty();
    return report;
}

SemanticIndex* FsmGraphService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

sym_list::SymbolInfo FsmGraphService::resolveModule(
    const FsmGraphQuery& query) const
{
    if (query.moduleSymbolId >= 0) {
        const sym_list::SymbolInfo symbol =
            semanticIndex()->getSymbolById(query.moduleSymbolId);
        return SymbolTaxonomy::isModuleDeclaration(symbol.symbolType)
            ? symbol
            : missingFsmSymbol();
    }
    if (query.moduleName.isEmpty())
        return missingFsmSymbol();

    SemanticDefinitionQuery definitionQuery;
    definitionQuery.symbolName = query.moduleName;
    definitionQuery.fileName = query.fileName;
    const SemanticDefinitionResult definition =
        semanticIndex()->resolveDefinition(definitionQuery);
    return definition.found
            && SymbolTaxonomy::isModuleDeclaration(definition.symbol.symbolType)
        ? definition.symbol
        : missingFsmSymbol();
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
    const QList<sym_list::SymbolInfo>& symbols) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<int> seen;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!SymbolTaxonomy::isFsmStateRegisterDeclaration(symbol.symbolType))
            continue;
        if (!symbol.symbolName.contains(QStringLiteral("state"), Qt::CaseInsensitive))
            continue;
        if (symbol.symbolName.contains(QStringLiteral("next"), Qt::CaseInsensitive))
            continue;
        if (symbol.symbolName.endsWith(QStringLiteral("_d"), Qt::CaseInsensitive))
            continue;
        if (seen.contains(symbol.symbolId))
            continue;
        seen.insert(symbol.symbolId);
        result.append(symbol);
    }
    sortSymbols(result);
    return result;
}

QList<sym_list::SymbolInfo> FsmGraphService::stateValues(
    const QList<sym_list::SymbolInfo>& symbols,
    const sym_list::SymbolInfo& stateRegister) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<int> seen;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!SymbolTaxonomy::isFsmStateValueDeclaration(symbol.symbolType))
            continue;
        if (!stateRegister.dataType.isEmpty()
            && !symbol.dataType.isEmpty()
            && symbol.dataType != stateRegister.dataType) {
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

sym_list::SymbolInfo FsmGraphService::nextStateSignal(
    const QList<sym_list::SymbolInfo>& symbols,
    const sym_list::SymbolInfo& stateRegister) const
{
    QList<sym_list::SymbolInfo> candidates;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!SymbolTaxonomy::isFsmStateRegisterDeclaration(symbol.symbolType))
            continue;
        if (!symbol.symbolName.contains(QStringLiteral("state"), Qt::CaseInsensitive))
            continue;
        if (symbol.symbolId == stateRegister.symbolId)
            continue;
        const bool looksNext =
            symbol.symbolName.contains(QStringLiteral("next"), Qt::CaseInsensitive)
            || symbol.symbolName.endsWith(QStringLiteral("_d"), Qt::CaseInsensitive);
        if (!looksNext)
            continue;
        if (!stateRegister.dataType.isEmpty()
            && !symbol.dataType.isEmpty()
            && symbol.dataType != stateRegister.dataType) {
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
    const QRegularExpression assignmentExpression(
        QStringLiteral("\\b([A-Za-z_][A-Za-z0-9_$]*)\\s*(?:<=|=)\\s*"
                       "([A-Za-z_][A-Za-z0-9_$]*)\\b"));
    const QRegularExpression conditionExpression(
        QStringLiteral("\\bif\\s*\\(([^)]*)\\)"));
    const QRegularExpression endcaseExpression(QStringLiteral("\\bendcase\\b"));

    bool inCase = false;
    QString currentState;
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
            continue;
        }

        const QRegularExpressionMatch labelMatch = labelExpression.match(code);
        if (labelMatch.hasMatch()) {
            const QString label = labelMatch.captured(1);
            currentState = label == QStringLiteral("default") || stateNames.contains(label)
                ? label
                : QString();
        }
        if (currentState.isEmpty())
            continue;

        const QRegularExpressionMatch assignmentMatch = assignmentExpression.match(code);
        if (!assignmentMatch.hasMatch())
            continue;

        const QString target = assignmentMatch.captured(1);
        const QString rhs = assignmentMatch.captured(2);
        if (!stateNames.contains(rhs))
            continue;
        if (!target.contains(QStringLiteral("state"), Qt::CaseInsensitive))
            continue;

        const QString key = QStringLiteral("%1:%2:%3:%4")
                                .arg(currentState)
                                .arg(rhs)
                                .arg(target)
                                .arg(i + 1);
        if (seenTransitions.contains(key))
            continue;
        seenTransitions.insert(key);

        FsmTransition transition;
        transition.fromState = currentState;
        transition.toState = rhs;
        transition.assignmentTarget = target;
        transition.line = i + 1;
        const QRegularExpressionMatch conditionMatch = conditionExpression.match(code);
        if (conditionMatch.hasMatch())
            transition.condition = conditionMatch.captured(1).trimmed();
        transitions.append(transition);
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
    if (!moduleSymbol.symbolName.isEmpty()
        && symbol.moduleScope == moduleSymbol.symbolName) {
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

QString FsmGraphService::stripLineComment(const QString& line)
{
    const int commentIndex = line.indexOf(QStringLiteral("//"));
    if (commentIndex < 0)
        return line;
    return line.left(commentIndex);
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
