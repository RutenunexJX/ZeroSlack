#include "fsmgraphservice.h"

#include "svtokenutils.h"
#include "symboltaxonomy.h"

#include <QSet>
#include <algorithm>

std::unique_ptr<FsmGraphService> FsmGraphService::instance = nullptr;

namespace {
SemanticSymbolRecord missingFsmRecord()
{
    return {};
}

SemanticSymbolRecord stateRecordByName(
    const QList<SemanticSymbolRecord>& states,
    const QString& stateName)
{
    for (const SemanticSymbolRecord& state : states) {
        if (state.name == stateName)
            return state;
    }
    return missingFsmRecord();
}

RtlInsightCodeLink codeLinkForRecord(const SemanticSymbolRecord& record)
{
    return RtlInsightLink::fromFileLine(record.location.fileName,
                                        record.location.startLine,
                                        record.location.startColumn);
}

QString displayNameForRecord(
    const SemanticSymbolRecord& record,
    const QString& defaultName = QString())
{
    if (!record.name.isEmpty())
        return record.name;
    return defaultName;
}

SymbolTaxonomy::SemanticMetadata metadataForRecord(
    const SemanticSymbolRecord& record)
{
    return semanticMetadataForSymbolRecord(record);
}

QString typeDisplayNameForRecord(const SemanticSymbolRecord& record)
{
    return SymbolTaxonomy::symbolTypeLabel(metadataForRecord(record));
}

QString sourceRoleDisplayNameForRecord(const SemanticSymbolRecord& record)
{
    return SymbolTaxonomy::sourceRoleDisplayName(
        metadataForRecord(record).sourceRole);
}

QString moduleDisplayNameForRecord(const SemanticSymbolRecord& record)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;
    if (record.owner.kind == SymbolTaxonomy::SymbolOwnerScope::Global)
        return QStringLiteral("global");
    return QStringLiteral("global");
}

QString rawTypeTextForRecord(const SemanticSymbolRecord& record)
{
    return record.type.rawTypeText;
}

int skipSpaces(const QString& text, int pos)
{
    while (pos < text.size() && text.at(pos).isSpace())
        ++pos;
    return pos;
}

int skipSpacesBackward(const QString& text, int endExclusive)
{
    int pos = qMin(endExclusive, text.size());
    while (pos > 0 && text.at(pos - 1).isSpace())
        --pos;
    return pos;
}

bool readIdentifierAt(
    const QString& text,
    int pos,
    QString& identifier,
    int& end)
{
    if (pos < 0 || pos >= text.size()
        || !SvTokenUtils::isIdentifierStart(text.at(pos))) {
        return false;
    }
    end = pos + 1;
    while (end < text.size()
           && SvTokenUtils::isIdentifierContinue(text.at(end), true)) {
        ++end;
    }
    identifier = text.mid(pos, end - pos);
    return true;
}

bool readIdentifierEndingAt(
    const QString& text,
    int endExclusive,
    QString& identifier)
{
    const int end = skipSpacesBackward(text, endExclusive);
    int start = end;
    while (start > 0
           && SvTokenUtils::isIdentifierContinue(text.at(start - 1), true)) {
        --start;
    }
    if (start == end || !SvTokenUtils::isIdentifierStart(text.at(start)))
        return false;
    identifier = text.mid(start, end - start);
    return true;
}

int findAssignmentOperator(const QString& code, int* operatorLength = nullptr)
{
    for (int i = 0; i < code.size(); ++i) {
        const QChar ch = code.at(i);
        const QChar next = i + 1 < code.size() ? code.at(i + 1) : QChar();
        const QChar prev = i > 0 ? code.at(i - 1) : QChar();
        if (ch == QLatin1Char('<') && next == QLatin1Char('=')) {
            if (operatorLength)
                *operatorLength = 2;
            return i;
        }
        if (ch != QLatin1Char('='))
            continue;
        if (prev == QLatin1Char('=')
            || prev == QLatin1Char('!')
            || prev == QLatin1Char('<')
            || prev == QLatin1Char('>')
            || next == QLatin1Char('=')) {
            continue;
        }
        if (operatorLength)
            *operatorLength = 1;
        return i;
    }
    return -1;
}

int matchingParenEnd(const QString& text, int openParen)
{
    if (openParen < 0 || openParen >= text.size()
        || text.at(openParen) != QLatin1Char('(')) {
        return -1;
    }
    int depth = 1;
    for (int i = openParen + 1; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('('))
            ++depth;
        else if (text.at(i) == QLatin1Char(')')) {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return -1;
}

QString caseSelector(const QString& code)
{
    for (const QString& keyword :
         {QStringLiteral("case"), QStringLiteral("casez"), QStringLiteral("casex")}) {
        const int keywordPos = SvTokenUtils::indexOfWord(code, keyword);
        if (keywordPos < 0)
            continue;
        const int parenStart = skipSpaces(code, keywordPos + keyword.size());
        const int parenEnd = matchingParenEnd(code, parenStart);
        if (parenEnd > parenStart)
            return code.mid(parenStart + 1, parenEnd - parenStart - 1).trimmed();
    }
    return QString();
}

QString caseLabel(const QString& code)
{
    int pos = skipSpaces(code, 0);
    QString label;
    int end = pos;
    if (SvTokenUtils::isWordAt(code, QStringLiteral("default"), pos)) {
        label = QStringLiteral("default");
        end = pos + QStringLiteral("default").size();
    } else if (!readIdentifierAt(code, pos, label, end)) {
        return QString();
    }
    end = skipSpaces(code, end);
    return end < code.size() && code.at(end) == QLatin1Char(':')
        ? label
        : QString();
}

QString ifCondition(const QString& code)
{
    const int ifPos = SvTokenUtils::indexOfWord(code, QStringLiteral("if"));
    if (ifPos < 0)
        return QString();
    const int parenStart = skipSpaces(code, ifPos + QStringLiteral("if").size());
    const int parenEnd = matchingParenEnd(code, parenStart);
    if (parenEnd <= parenStart)
        return QString();
    return code.mid(parenStart + 1, parenEnd - parenStart - 1).trimmed();
}

QList<QString> identifiersInText(const QString& text)
{
    QList<QString> identifiers;
    int pos = 0;
    while (pos < text.size()) {
        QString identifier;
        int end = pos;
        if (readIdentifierAt(text, pos, identifier, end)) {
            identifiers.append(identifier);
            pos = end;
        } else {
            ++pos;
        }
    }
    return identifiers;
}

bool isInsideModule(const SemanticSymbolRecord& record,
                    const SemanticSymbolRecord& moduleRecord);
bool hasStateValuesForType(const QList<SemanticSymbolRecord>& records,
                           const QString& rawTypeText);
bool looksLikeTextualStateValue(const SemanticSymbolRecord& record,
                                const SemanticSymbolRecord& stateRegister);
QString stateDetailDisplayName(const SemanticSymbolRecord& state);
void sortRecords(QList<SemanticSymbolRecord>& records);
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
    const SemanticSymbolRecord moduleRecord =
        resolveModule(query, &report.notFoundReason);
    if (moduleRecord.localHandle < 0) {
        report.notFoundReasonDisplayName =
            notFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    const QList<SemanticSymbolRecord> moduleRecords =
        symbolsInModule(moduleRecord);
    const QList<SemanticSymbolRecord> allRecords =
        semanticIndex()->getSymbolRecords();
    for (const SemanticSymbolRecord& stateRegister
         : stateRegisters(moduleRecords, allRecords)) {
        FsmGraph graph;
        const SemanticSymbolRecord nextState =
            nextStateSignal(moduleRecords, stateRegister);
        const QList<SemanticSymbolRecord> states =
            stateValues(moduleRecords, allRecords, stateRegister);
        QList<FsmTransition> transitions = parseTransitions(moduleRecord,
                                                            stateRegister,
                                                            nextState,
                                                            states);
        if (states.isEmpty() || transitions.isEmpty())
            continue;
        fillDisplayMetadata(graph,
                            moduleRecord,
                            stateRegister,
                            nextState,
                            states,
                            transitions);
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

SemanticSymbolRecord FsmGraphService::resolveModule(
    const FsmGraphQuery& query,
    FsmGraphNotFoundReason* reason) const
{
    if (reason)
        *reason = FsmGraphNotFoundReason::None;

    if (query.moduleStableKey.isValid()) {
        const SemanticSymbolRecord record =
            semanticIndex()->getSymbolRecordByStableKey(query.moduleStableKey);
        if (!record.isValid()) {
            if (reason)
                *reason = FsmGraphNotFoundReason::NoMatchingModule;
            return missingFsmRecord();
        }
        if (!SymbolTaxonomy::isModuleDeclaration(metadataForRecord(record))) {
            if (reason)
                *reason = FsmGraphNotFoundReason::UnsupportedSymbolKind;
            return missingFsmRecord();
        }
        return record;
    }

    if (query.moduleName.isEmpty()) {
        if (reason)
            *reason = FsmGraphNotFoundReason::EmptyModuleName;
        return missingFsmRecord();
    }

    SemanticDefinitionQuery definitionQuery;
    definitionQuery.symbolName = query.moduleName;
    definitionQuery.fileName = query.fileName;
    const SemanticDefinitionResult definition =
        semanticIndex()->resolveDefinition(definitionQuery);
    if (!definition.found) {
        if (reason)
            *reason = FsmGraphNotFoundReason::NoMatchingModule;
        return missingFsmRecord();
    }
    if (!SymbolTaxonomy::isModuleDeclaration(
            metadataForRecord(definition.symbolRecord))) {
        if (reason)
            *reason = FsmGraphNotFoundReason::UnsupportedSymbolKind;
        return missingFsmRecord();
    }
    const SemanticSymbolRecord record = definition.symbolRecord;
    if (record.localHandle < 0)
        return missingFsmRecord();
    return record;
}

QList<SemanticSymbolRecord> FsmGraphService::symbolsInModule(
    const SemanticSymbolRecord& moduleRecord) const
{
    QList<SemanticSymbolRecord> result;
    const QList<SemanticSymbolRecord> records =
        semanticIndex()->getSymbolRecords(moduleRecord.location.fileName);
    for (const SemanticSymbolRecord& record : records) {
        if (isInsideModule(record, moduleRecord))
            result.append(record);
    }
    sortRecords(result);
    return result;
}

QList<SemanticSymbolRecord> FsmGraphService::stateRegisters(
    const QList<SemanticSymbolRecord>& moduleRecords,
    const QList<SemanticSymbolRecord>& allRecords) const
{
    QList<SemanticSymbolRecord> result;
    QSet<int> seen;
    for (const SemanticSymbolRecord& record : moduleRecords) {
        if (!SymbolTaxonomy::isFsmStateRegisterDeclaration(
                metadataForRecord(record))) {
            continue;
        }
        if (looksLikeNextStateName(record.name))
            continue;
        const bool hasNextStatePair =
            hasPairedNextStateSignal(moduleRecords, record);
        if (!looksLikeCurrentStateName(record.name)
            && !hasNextStatePair) {
            continue;
        }
        const QString symbolType = rawTypeTextForRecord(record);
        if (!symbolType.isEmpty()
            && !hasStateValuesForType(allRecords, symbolType)
            && !hasNextStatePair
            && !record.name.contains(QStringLiteral("state"),
                                     Qt::CaseInsensitive)) {
            continue;
        }
        if (seen.contains(record.localHandle))
            continue;
        seen.insert(record.localHandle);
        result.append(record);
    }
    sortRecords(result);
    return result;
}

QList<SemanticSymbolRecord> FsmGraphService::stateValues(
    const QList<SemanticSymbolRecord>& moduleRecords,
    const QList<SemanticSymbolRecord>& allRecords,
    const SemanticSymbolRecord& stateRegister) const
{
    QList<SemanticSymbolRecord> result;
    QSet<int> seen;
    const QString stateRegisterType = rawTypeTextForRecord(stateRegister);
    const QList<SemanticSymbolRecord>& source =
        stateRegisterType.isEmpty() ? moduleRecords : allRecords;
    for (const SemanticSymbolRecord& record : source) {
        if (!SymbolTaxonomy::isFsmStateValueDeclaration(
                metadataForRecord(record))) {
            continue;
        }
        if (!stateRegisterType.isEmpty()) {
            if (rawTypeTextForRecord(record) != stateRegisterType)
                continue;
        }
        if (seen.contains(record.localHandle))
            continue;
        seen.insert(record.localHandle);
        result.append(record);
    }
    if (!stateRegisterType.isEmpty() && result.isEmpty()) {
        for (const SemanticSymbolRecord& record : moduleRecords) {
            if (!SymbolTaxonomy::isFsmStateValueDeclaration(
                    metadataForRecord(record))) {
                continue;
            }
            const QString symbolType = rawTypeTextForRecord(record);
            if (!symbolType.isEmpty()
                && symbolType != stateRegisterType) {
                continue;
            }
            if (seen.contains(record.localHandle))
                continue;
            seen.insert(record.localHandle);
            result.append(record);
        }
    }
    if (result.isEmpty()) {
        for (const SemanticSymbolRecord& record : moduleRecords) {
            if (!looksLikeTextualStateValue(record, stateRegister))
                continue;
            if (seen.contains(record.localHandle))
                continue;
            seen.insert(record.localHandle);
            result.append(record);
        }
    }
    sortRecords(result);
    return result;
}

SemanticSymbolRecord FsmGraphService::nextStateSignal(
    const QList<SemanticSymbolRecord>& moduleRecords,
    const SemanticSymbolRecord& stateRegister) const
{
    QList<SemanticSymbolRecord> pairedCandidates;
    QList<SemanticSymbolRecord> fallbackCandidates;
    const QString stateRegisterType = rawTypeTextForRecord(stateRegister);
    for (const SemanticSymbolRecord& record : moduleRecords) {
        if (!SymbolTaxonomy::isFsmStateRegisterDeclaration(
                metadataForRecord(record))) {
            continue;
        }
        if (record.localHandle == stateRegister.localHandle) {
            continue;
        }
        const bool paired =
            isPairedNextStateName(stateRegister.name, record.name);
        if (!paired && !looksLikeNextStateName(record.name))
            continue;
        const QString symbolType = rawTypeTextForRecord(record);
        if (!stateRegisterType.isEmpty()
            && !symbolType.isEmpty()
            && symbolType != stateRegisterType) {
            continue;
        }
        if (paired)
            pairedCandidates.append(record);
        else
            fallbackCandidates.append(record);
    }
    sortRecords(pairedCandidates);
    sortRecords(fallbackCandidates);
    if (!pairedCandidates.isEmpty())
        return pairedCandidates.first();
    return fallbackCandidates.isEmpty()
        ? missingFsmRecord()
        : fallbackCandidates.first();
}

QList<FsmTransition> FsmGraphService::parseTransitions(
    const SemanticSymbolRecord& moduleRecord,
    const SemanticSymbolRecord& stateRegister,
    const SemanticSymbolRecord& nextStateSignal,
    const QList<SemanticSymbolRecord>& states) const
{
    QList<FsmTransition> transitions;
    if (states.isEmpty())
        return transitions;

    QSet<QString> stateNames;
    for (const SemanticSymbolRecord& state : states)
        stateNames.insert(state.name);

    const QString content =
        semanticIndex()->getCachedFileContent(moduleRecord.location.fileName);
    if (content.isEmpty())
        return transitions;

    const QStringList lines = content.split('\n');
    const int lineCount = static_cast<int>(lines.size());
    int startIndex = moduleRecord.location.startLine > 0
        ? moduleRecord.location.startLine - 1
        : 0;
    int endIndex = moduleRecord.location.endLine > 0
        ? moduleRecord.location.endLine - 1
        : lineCount - 1;
    startIndex = std::max(0, startIndex);
    endIndex = std::min(endIndex, lineCount - 1);
    if (startIndex > endIndex)
        return transitions;

    bool inCase = false;
    QString currentState;
    QString pendingCondition;
    QSet<QString> seenTransitions;
    for (int i = startIndex; i <= endIndex; ++i) {
        const QString code = stripLineComment(lines.at(i));
        if (!inCase) {
            if (caseSelector(code) == stateRegister.name)
                inCase = true;
            continue;
        }

        if (SvTokenUtils::containsWord(code, QStringLiteral("endcase"))) {
            inCase = false;
            currentState.clear();
            pendingCondition.clear();
            continue;
        }

        const QString label = caseLabel(code);
        if (!label.isEmpty()) {
            currentState = label == QStringLiteral("default") || stateNames.contains(label)
                ? label
                : QString();
            pendingCondition.clear();
        }
        if (currentState.isEmpty())
            continue;

        const QString condition = ifCondition(code);
        if (!condition.isEmpty())
            pendingCondition = condition;

        const QString target = assignmentTarget(code);
        if (target.isEmpty())
            continue;
        if (!target.contains(QStringLiteral("state"), Qt::CaseInsensitive)
            && (nextStateSignal.localHandle < 0
                || target != nextStateSignal.name)) {
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
                             moduleRecord.location.fileName,
                             i + 1);
        }
        if (!toStates.isEmpty())
            pendingCondition.clear();
    }

    sortTransitions(transitions);
    return transitions;
}

namespace {

bool isInsideModule(
    const SemanticSymbolRecord& record,
    const SemanticSymbolRecord& moduleRecord)
{
    if (record.localHandle == moduleRecord.localHandle) {
        return false;
    }
    if (!moduleRecord.name.isEmpty()
        && record.owner.name == moduleRecord.name) {
        return true;
    }
    if (record.location.fileName != moduleRecord.location.fileName)
        return false;
    if (moduleRecord.location.startLine <= 0
        || record.location.startLine <= 0)
        return false;
    if (record.location.startLine < moduleRecord.location.startLine)
        return false;
    return moduleRecord.location.endLine <= 0
        || record.location.startLine <= moduleRecord.location.endLine;
}

bool hasStateValuesForType(
    const QList<SemanticSymbolRecord>& records,
    const QString& rawTypeText)
{
    if (rawTypeText.isEmpty())
        return false;
    for (const SemanticSymbolRecord& record : records) {
        if (SymbolTaxonomy::isFsmStateValueDeclaration(
                metadataForRecord(record))
            && rawTypeTextForRecord(record) == rawTypeText) {
            return true;
        }
    }
    return false;
}

QString stateRegisterPrefix(const QString& name)
{
    QString lower = name.toLower();
    if (lower == QStringLiteral("current_state")
        || lower == QStringLiteral("state")
        || lower == QStringLiteral("cs")) {
        return QString();
    }
    for (const QString& suffix : {
             QStringLiteral("_current_state"),
             QStringLiteral("_state"),
             QStringLiteral("_cs"),
             QStringLiteral("_q"),
         }) {
        if (lower.endsWith(suffix)) {
            lower.chop(suffix.size());
            return lower;
        }
    }
    return QString();
}

bool stateValueNameMatchesPrefix(const QString& stateName,
                                 const QString& registerPrefix)
{
    if (registerPrefix.isEmpty())
        return true;
    const QString upper = stateName.toUpper();
    const QString prefix = registerPrefix.toUpper();
    return upper.startsWith(prefix + QLatin1Char('_'))
        || upper.contains(QStringLiteral("_") + prefix + QStringLiteral("_"));
}

bool looksLikeTextualStateValue(const SemanticSymbolRecord& record,
                                const SemanticSymbolRecord& stateRegister)
{
    const SymbolTaxonomy::SemanticMetadata metadata = metadataForRecord(record);
    const bool parameterLike =
        metadata.collectorKind == SymbolTaxonomy::CollectorKind::Parameter
        || metadata.collectorKind == SymbolTaxonomy::CollectorKind::Localparam
        || metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Parameter
        || metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Localparam;
    if (!parameterLike)
        return false;

    const QString upper = record.name.toUpper();
    const bool stateShaped =
        upper.startsWith(QStringLiteral("S_"))
        || upper.startsWith(QStringLiteral("ST_"))
        || upper.startsWith(QStringLiteral("SM_"))
        || upper.endsWith(QStringLiteral("_STATE"))
        || upper.contains(QStringLiteral("_STATE_"));
    if (!stateShaped)
        return false;

    return stateValueNameMatchesPrefix(record.name,
                                       stateRegisterPrefix(stateRegister.name));
}

}

bool FsmGraphService::hasPairedNextStateSignal(
    const QList<SemanticSymbolRecord>& moduleRecords,
    const SemanticSymbolRecord& stateRegister)
{
    const QString stateRegisterType = rawTypeTextForRecord(stateRegister);
    for (const SemanticSymbolRecord& record : moduleRecords) {
        if (record.localHandle == stateRegister.localHandle) {
            continue;
        }
        if (!SymbolTaxonomy::isFsmStateRegisterDeclaration(
                metadataForRecord(record))) {
            continue;
        }
        const QString symbolType = rawTypeTextForRecord(record);
        if (!stateRegisterType.isEmpty()
            && !symbolType.isEmpty()
            && symbolType != stateRegisterType) {
            continue;
        }
        if (isPairedNextStateName(stateRegister.name,
                                  record.name)) {
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
    if (current == QStringLiteral("current_state"))
        return candidate == QStringLiteral("next_state");
    if (current == QStringLiteral("state"))
        return candidate == QStringLiteral("next_state")
            || candidate == QStringLiteral("ns");
    if (current.endsWith(QStringLiteral("_current_state"))) {
        return candidate
            == current.left(current.size()
                            - QStringLiteral("_current_state").size())
                + QStringLiteral("_next_state");
    }
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
    const int assignment = findAssignmentOperator(code);
    if (assignment < 0)
        return QString();
    QString target;
    return readIdentifierEndingAt(code, assignment, target)
        ? target
        : QString();
}

QList<QString> FsmGraphService::assignedStateValues(
    const QString& code,
    const QSet<QString>& stateNames)
{
    QList<QString> states;
    QSet<QString> seen;
    int operatorLength = 0;
    const int assignment = findAssignmentOperator(code, &operatorLength);
    const QString rhs = assignment >= 0
        ? code.mid(assignment + operatorLength)
        : code;
    for (const QString& token : identifiersInText(rhs)) {
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
    int operatorLength = 0;
    const int assignment = findAssignmentOperator(condition, &operatorLength);
    if (assignment >= 0)
        condition = condition.mid(assignment + operatorLength).trimmed();
    if (condition.isEmpty())
        condition = pendingCondition;

    const QString trueBranch = code.mid(question + 1, colon - question - 1);
    const QString falseBranch = code.mid(colon + 1);
    if (SvTokenUtils::containsWord(trueBranch, stateName, true)) {
        return condition;
    }
    if (SvTokenUtils::containsWord(falseBranch, stateName, true)) {
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
    const QList<SemanticSymbolRecord>& states)
{
    QList<FsmStateRow> rows;
    rows.reserve(states.size());
    for (const SemanticSymbolRecord& state : states) {
        FsmStateRow row;
        row.stateRecord = state;
        row.stateStableKey = row.stateRecord.stableKey;
        row.codeLink = codeLinkForRecord(row.stateRecord);
        row.sectionDisplayName = QStringLiteral("State");
        row.stateDisplayName =
            displayNameForRecord(row.stateRecord, state.name);
        row.detailDisplayName = stateDetailDisplayName(state);
        row.typeDisplayName = typeDisplayNameForRecord(row.stateRecord);
        row.sourceRoleDisplayName =
            sourceRoleDisplayNameForRecord(row.stateRecord);
        row.moduleDisplayName = moduleDisplayNameForRecord(row.stateRecord);
        rows.append(row);
    }
    return rows;
}

QList<FsmTransitionRow> FsmGraphService::transitionRows(
    const SemanticSymbolRecord& moduleRecord,
    const QList<FsmTransition>& transitions,
    const QList<SemanticSymbolRecord>& states)
{
    QList<FsmTransitionRow> rows;
    rows.reserve(transitions.size());
    for (const FsmTransition& transition : transitions) {
        FsmTransitionRow row;
        const SemanticSymbolRecord fromStateRecord =
            stateRecordByName(states, transition.fromState);
        const SemanticSymbolRecord toStateRecord =
            stateRecordByName(states, transition.toState);
        row.moduleSymbolRecord = moduleRecord;
        row.fromStateRecord = fromStateRecord;
        row.toStateRecord = toStateRecord;
        row.fromStateStableKey = row.fromStateRecord.stableKey;
        row.toStateStableKey = row.toStateRecord.stableKey;
        row.codeLink = transition.codeLink;
        row.fromStateCodeLink = codeLinkForRecord(row.fromStateRecord);
        row.toStateCodeLink = codeLinkForRecord(row.toStateRecord);
        row.assignmentTargetDisplayName = transition.assignmentTarget;
        row.sectionDisplayName = transition.sectionDisplayName;
        row.fromStateDisplayName =
            displayNameForRecord(row.fromStateRecord,
                                 transition.fromState);
        row.toStateDisplayName =
            displayNameForRecord(row.toStateRecord,
                                 transition.toState);
        row.conditionDisplayName = transitionConditionDisplayName(transition);
        row.detailDisplayName = transition.detailDisplayName;
        row.sourceLineDisplayName = transitionSourceLineDisplayName(transition);
        row.sourceRoleDisplayName =
            sourceRoleDisplayNameForRecord(row.moduleSymbolRecord);
        rows.append(row);
    }
    return rows;
}

namespace {

QString stateDetailDisplayName(const SemanticSymbolRecord& state)
{
    return rawTypeTextForRecord(state);
}

}

QString FsmGraphService::stateRegisterDetailDisplayName(
    bool hasNextStateSignal,
    const QString& nextStateSignalDisplayName)
{
    if (hasNextStateSignal) {
        return QStringLiteral("next %1")
            .arg(nextStateSignalDisplayName);
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

void FsmGraphService::fillDisplayMetadata(
    FsmGraph& graph,
    const SemanticSymbolRecord& moduleRecord,
    const SemanticSymbolRecord& stateRegister,
    const SemanticSymbolRecord& nextStateSignal,
    const QList<SemanticSymbolRecord>& states,
    QList<FsmTransition>& transitions)
{
    graph.moduleSymbolRecord = moduleRecord;
    graph.stateRegisterRecord = stateRegister;
    graph.nextStateSignalRecord = nextStateSignal;
    graph.moduleStableKey = graph.moduleSymbolRecord.stableKey;
    graph.stateRegisterStableKey = graph.stateRegisterRecord.stableKey;
    graph.nextStateSignalStableKey = graph.nextStateSignalRecord.stableKey;
    graph.stateRegisterCodeLink =
        codeLinkForRecord(graph.stateRegisterRecord);
    graph.nextStateSignalCodeLink =
        codeLinkForRecord(graph.nextStateSignalRecord);
    graph.moduleDisplayName =
        displayNameForRecord(graph.moduleSymbolRecord,
                             QStringLiteral("<unknown>"));
    graph.stateCount = states.size();
    graph.transitionCount = transitions.size();
    graph.stateRegisterSectionDisplayName = QStringLiteral("State Register");
    graph.stateRegisterDisplayName =
        displayNameForRecord(graph.stateRegisterRecord,
                             QStringLiteral("<unknown>"));
    graph.stateRegisterTypeDisplayName =
        typeDisplayNameForRecord(graph.stateRegisterRecord);
    graph.stateRegisterSourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(graph.stateRegisterRecord);
    const bool hasNextStateSignal =
        nextStateSignal.localHandle >= 0;
    graph.nextStateSignalDisplayName = hasNextStateSignal
        ? displayNameForRecord(graph.nextStateSignalRecord)
        : QString();
    graph.stateRegisterDetailDisplayName =
        stateRegisterDetailDisplayName(hasNextStateSignal,
                                       graph.nextStateSignalDisplayName);
    graph.nextStateSignalTypeDisplayName = hasNextStateSignal
        ? typeDisplayNameForRecord(graph.nextStateSignalRecord)
        : QString();
    graph.nextStateSignalSourceRoleDisplayName = hasNextStateSignal
        ? sourceRoleDisplayNameForRecord(graph.nextStateSignalRecord)
        : QString();
    graph.statesGroupDisplayName = QStringLiteral("States");
    graph.transitionsGroupDisplayName = QStringLiteral("Transitions");
    graph.stateRows = stateRows(states);
    for (FsmTransition& transition : transitions)
        fillDisplayMetadata(transition);
    graph.transitionRows = transitionRows(
        moduleRecord,
        transitions,
        states);
}

void FsmGraphService::fillDisplayMetadata(FsmTransition& transition)
{
    transition.sectionDisplayName = transition.fromState;
    transition.detailDisplayName = transitionDetailDisplayName(transition);
}

namespace {

void sortRecords(QList<SemanticSymbolRecord>& records)
{
    std::sort(records.begin(), records.end(),
              [](const SemanticSymbolRecord& lhs,
                 const SemanticSymbolRecord& rhs) {
                  if (lhs.location.fileName != rhs.location.fileName)
                      return lhs.location.fileName < rhs.location.fileName;
                  if (lhs.location.startLine != rhs.location.startLine)
                      return lhs.location.startLine < rhs.location.startLine;
                  if (lhs.location.startColumn != rhs.location.startColumn)
                      return lhs.location.startColumn < rhs.location.startColumn;
                  if (lhs.name != rhs.name)
                      return lhs.name < rhs.name;
                  return lhs.localHandle < rhs.localHandle;
              });
}

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
