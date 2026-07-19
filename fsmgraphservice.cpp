#include "fsmgraphservice.h"

#include "svtokenutils.h"
#include "symboltaxonomy.h"

#include <QSet>
#include <algorithm>

std::unique_ptr<FsmGraphService> FsmGraphService::instance = nullptr;

namespace {
struct FsmPairCandidate {
    SemanticSymbolRecord currentState;
    SemanticSymbolRecord nextState;
    QList<SemanticSymbolRecord> states;
};

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

QString stateNameFromCondition(const QString& condition,
                               const QString& currentStateName,
                               const QSet<QString>& stateNames)
{
    if (!SvTokenUtils::containsWord(condition, currentStateName, true))
        return QString();
    for (const QString& token : identifiersInText(condition)) {
        if (token == currentStateName)
            continue;
        if (stateNames.contains(token))
            return token;
    }
    return QString();
}

QString assignmentRhs(const QString& code)
{
    int operatorLength = 0;
    const int assignment = findAssignmentOperator(code, &operatorLength);
    QString rhs = assignment >= 0 ? code.mid(assignment + operatorLength).trimmed()
                                  : QString();
    if (!rhs.startsWith(QLatin1Char('#')))
        return rhs;

    int pos = skipSpaces(rhs, 1);
    if (pos < rhs.size() && rhs.at(pos) == QLatin1Char('(')) {
        const int parenEnd = matchingParenEnd(rhs, pos);
        return parenEnd >= 0 ? rhs.mid(parenEnd + 1).trimmed() : rhs;
    }

    if (pos < rhs.size() && rhs.at(pos) == QLatin1Char('`'))
        ++pos;
    while (pos < rhs.size()) {
        const QChar ch = rhs.at(pos);
        if (ch.isSpace())
            break;
        if (ch == QLatin1Char('(')
            || ch == QLatin1Char(';')
            || ch == QLatin1Char(','))
            break;
        ++pos;
    }
    return rhs.mid(pos).trimmed();
}

QString assignmentTargetInCode(const QString& code)
{
    const int assignment = findAssignmentOperator(code);
    if (assignment < 0)
        return QString();
    QString target;
    return readIdentifierEndingAt(code, assignment, target)
        ? target
        : QString();
}

QString stripLineCommentText(const QString& line)
{
    const int commentIndex = line.indexOf(QStringLiteral("//"));
    return commentIndex < 0 ? line : line.left(commentIndex);
}

bool isNonblockingAssignment(const QString& code)
{
    int operatorLength = 0;
    const int assignment = findAssignmentOperator(code, &operatorLength);
    return assignment >= 0 && operatorLength == 2;
}

QString firstIdentifier(const QString& text)
{
    const QList<QString> identifiers = identifiersInText(text);
    return identifiers.isEmpty() ? QString() : identifiers.first();
}

bool isInsideModule(const SemanticSymbolRecord& record,
                    const SemanticSymbolRecord& moduleRecord);
bool isStateCarrierRecord(const SemanticSymbolRecord& record);
SemanticSymbolRecord stateCarrierByName(
    const QList<SemanticSymbolRecord>& moduleRecords,
    const QString& name);
SemanticSymbolRecord stateValueRecordByName(
    const QList<SemanticSymbolRecord>& moduleRecords,
    const QList<SemanticSymbolRecord>& allRecords,
    const SemanticSymbolRecord& stateRegister,
    const QString& name);
QList<FsmPairCandidate> discoverStructuralFsmPairs(
    const SemanticSymbolRecord& moduleRecord,
    const QList<SemanticSymbolRecord>& moduleRecords,
    const QList<SemanticSymbolRecord>& allRecords,
    const QString& content);
QList<SemanticSymbolRecord> structuralStateValues(
    const SemanticSymbolRecord& moduleRecord,
    const QList<SemanticSymbolRecord>& moduleRecords,
    const QList<SemanticSymbolRecord>& allRecords,
    const SemanticSymbolRecord& stateRegister,
    const SemanticSymbolRecord& nextStateSignal,
    const QString& content);
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
    const QString content =
        semanticIndex()->getCachedFileContent(moduleRecord.location.fileName);
    const QList<FsmPairCandidate> candidates =
        discoverStructuralFsmPairs(moduleRecord,
                                   moduleRecords,
                                   allRecords,
                                   content);
    for (const FsmPairCandidate& candidate : candidates) {
        FsmGraph graph;
        const SemanticSymbolRecord& stateRegister = candidate.currentState;
        const SemanticSymbolRecord& nextState = candidate.nextState;
        const QList<SemanticSymbolRecord>& states = candidate.states;
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

FsmSymbolRoleReport FsmGraphService::roleForSymbol(
    const FsmGraphQuery& query,
    const QString& symbolName) const
{
    FsmSymbolRoleReport roleReport;
    if (symbolName.isEmpty()) {
        roleReport.reasonDisplayName =
            QStringLiteral("no selected symbol for state transition graph");
        return roleReport;
    }

    const FsmGraphReport graphReport = buildFsmGraph(query);
    if (!graphReport.found) {
        roleReport.reasonDisplayName =
            graphReport.notFoundReasonDisplayName.isEmpty()
                ? QStringLiteral("symbol is not part of a discovered FSM")
                : graphReport.notFoundReasonDisplayName;
        return roleReport;
    }

    for (const FsmGraph& graph : graphReport.graphs) {
        if (graph.nextStateSignalRecord.isValid()
            && graph.nextStateSignalRecord.name == symbolName) {
            roleReport.inFsm = true;
            roleReport.role = FsmSymbolRole::NextState;
            roleReport.graph = graph;
            return roleReport;
        }
        if (graph.stateRegisterRecord.isValid()
            && graph.stateRegisterRecord.name == symbolName) {
            roleReport.inFsm = true;
            roleReport.role = FsmSymbolRole::CurrentState;
            roleReport.graph = graph;
            roleReport.reasonDisplayName =
                QStringLiteral("Please select the next-state signal for this FSM");
            return roleReport;
        }
    }

    roleReport.reasonDisplayName =
        QStringLiteral("symbol is not part of a discovered FSM");
    return roleReport;
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
    QString pendingIfState;
    QString pendingIfCondition;
    QSet<QString> seenTransitions;
    for (int i = startIndex; i <= endIndex; ++i) {
        const QString code = stripLineComment(lines.at(i));
        if (!inCase) {
            if (caseSelector(code) == stateRegister.name) {
                inCase = true;
                pendingIfState.clear();
                pendingIfCondition.clear();
                continue;
            }

            const QString condition = ifCondition(code);
            const QString ifState =
                stateNameFromCondition(condition, stateRegister.name, stateNames);
            if (!ifState.isEmpty()) {
                pendingIfState = ifState;
                pendingIfCondition = condition;
            }

            const QString target = assignmentTarget(code);
            if (target.isEmpty())
                continue;
            if (nextStateSignal.localHandle < 0
                || target != nextStateSignal.name
                || pendingIfState.isEmpty()) {
                continue;
            }
            const QList<QString> toStates = assignedStateValues(code, stateNames);
            for (const QString& toState : toStates) {
                appendTransition(transitions,
                                 seenTransitions,
                                 pendingIfState,
                                 toState,
                                 target,
                                 transitionConditionForState(code,
                                                             pendingIfCondition,
                                                             toState),
                                 moduleRecord.location.fileName,
                                 i + 1);
            }
            if (!toStates.isEmpty()) {
                pendingIfState.clear();
                pendingIfCondition.clear();
            }
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
        if (nextStateSignal.localHandle < 0
            || target != nextStateSignal.name) {
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

bool isStateCarrierRecord(const SemanticSymbolRecord& record)
{
    return SymbolTaxonomy::isFsmStateRegisterDeclaration(
        metadataForRecord(record));
}

SemanticSymbolRecord stateCarrierByName(
    const QList<SemanticSymbolRecord>& moduleRecords,
    const QString& name)
{
    for (const SemanticSymbolRecord& record : moduleRecords) {
        if (record.name == name && isStateCarrierRecord(record))
            return record;
    }
    return missingFsmRecord();
}

bool isStateValueLikeRecord(const SemanticSymbolRecord& record)
{
    const SymbolTaxonomy::SemanticMetadata metadata = metadataForRecord(record);
    return SymbolTaxonomy::isFsmStateValueDeclaration(metadata)
        || metadata.collectorKind == SymbolTaxonomy::CollectorKind::Parameter
        || metadata.collectorKind == SymbolTaxonomy::CollectorKind::Localparam
        || metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Parameter
        || metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Localparam;
}

SemanticSymbolRecord syntheticStateRecord(const SemanticSymbolRecord& moduleRecord,
                                          const QString& name)
{
    SemanticSymbolRecord record;
    record.name = name;
    record.location.fileName = moduleRecord.location.fileName;
    record.location.startLine = moduleRecord.location.startLine;
    record.location.startColumn = moduleRecord.location.startColumn;
    record.declarationKind = SymbolTaxonomy::DeclarationKind::User;
    record.usageRole = SymbolTaxonomy::SymbolUsageRole::Declaration;
    record.sourceRole =
        SymbolTaxonomy::sourceRoleForFileName(record.location.fileName);
    record.owner.kind = SymbolTaxonomy::SymbolOwnerScope::Module;
    record.owner.name = moduleRecord.name;
    record.owner.stableKey = moduleRecord.stableKey;
    record.stableKey.fileName = record.location.fileName;
    record.stableKey.symbolName = record.name;
    record.stableKey.declarationKind = record.declarationKind;
    record.stableKey.ownerScope = moduleRecord.name;
    record.stableKey.sourcePosition = record.location.position;
    record.stableKey.sourceLength = record.location.length;
    return record;
}

SemanticSymbolRecord stateValueRecordByName(
    const QList<SemanticSymbolRecord>& moduleRecords,
    const QList<SemanticSymbolRecord>& allRecords,
    const SemanticSymbolRecord& stateRegister,
    const QString& name)
{
    const QString stateRegisterType = rawTypeTextForRecord(stateRegister);
    for (const SemanticSymbolRecord& record : moduleRecords) {
        if (record.name == name && isStateValueLikeRecord(record))
            return record;
    }
    if (!stateRegisterType.isEmpty()) {
        for (const SemanticSymbolRecord& record : allRecords) {
            if (record.name == name
                && rawTypeTextForRecord(record) == stateRegisterType
                && isStateValueLikeRecord(record)) {
                return record;
            }
        }
    }
    for (const SemanticSymbolRecord& record : allRecords) {
        if (record.name == name
            && isStateValueLikeRecord(record)
            && !isStateCarrierRecord(record)) {
            return record;
        }
    }
    return missingFsmRecord();
}

int wordCount(const QString& code, const QString& word)
{
    int count = 0;
    int pos = 0;
    while ((pos = SvTokenUtils::indexOfWord(code, word, pos)) >= 0) {
        ++count;
        pos += word.size();
    }
    return count;
}

bool isClockedProcessStart(const QString& code)
{
    if (SvTokenUtils::containsWord(code, QStringLiteral("always_ff")))
        return true;
    return SvTokenUtils::containsWord(code, QStringLiteral("always"))
        && (SvTokenUtils::containsWord(code, QStringLiteral("posedge"))
            || SvTokenUtils::containsWord(code, QStringLiteral("negedge")));
}

bool isIgnoredStateToken(const QString& token)
{
    static const QSet<QString> ignored{
        QStringLiteral("if"),
        QStringLiteral("else"),
        QStringLiteral("case"),
        QStringLiteral("casez"),
        QStringLiteral("casex"),
        QStringLiteral("default"),
        QStringLiteral("begin"),
        QStringLiteral("end"),
        QStringLiteral("endcase"),
        QStringLiteral("logic"),
        QStringLiteral("reg"),
        QStringLiteral("wire"),
    };
    return ignored.contains(token);
}

bool addStateName(QSet<QString>& stateNames,
                  const QList<SemanticSymbolRecord>& moduleRecords,
                  const SemanticSymbolRecord& currentState,
                  const SemanticSymbolRecord& nextState,
                  const QString& token)
{
    if (token.isEmpty()
        || token == currentState.name
        || token == nextState.name
        || isIgnoredStateToken(token)
        || stateCarrierByName(moduleRecords, token).isValid()) {
        return false;
    }
    stateNames.insert(token);
    return true;
}

bool addAssignedStateName(QSet<QString>& stateNames,
                          const QList<SemanticSymbolRecord>& moduleRecords,
                          const QList<SemanticSymbolRecord>& allRecords,
                          const SemanticSymbolRecord& currentState,
                          const SemanticSymbolRecord& nextState,
                          const QString& token)
{
    if (stateNames.contains(token)) {
        return addStateName(stateNames,
                            moduleRecords,
                            currentState,
                            nextState,
                            token);
    }
    const SemanticSymbolRecord record =
        stateValueRecordByName(moduleRecords, allRecords, currentState, token);
    if (!record.isValid())
        return false;
    return addStateName(stateNames,
                        moduleRecords,
                        currentState,
                        nextState,
                        token);
}

QList<SemanticSymbolRecord> structuralStateValues(
    const SemanticSymbolRecord& moduleRecord,
    const QList<SemanticSymbolRecord>& moduleRecords,
    const QList<SemanticSymbolRecord>& allRecords,
    const SemanticSymbolRecord& stateRegister,
    const SemanticSymbolRecord& nextStateSignal,
    const QString& content)
{
    QSet<QString> stateNames;
    const QString stateRegisterType = rawTypeTextForRecord(stateRegister);
    if (!stateRegisterType.isEmpty()) {
        for (const SemanticSymbolRecord& record : allRecords) {
            if (rawTypeTextForRecord(record) == stateRegisterType
                && isStateValueLikeRecord(record)) {
                stateNames.insert(record.name);
            }
        }
    }

    const QStringList lines = content.split(QLatin1Char('\n'));
    const int lineCount = lines.size();
    int startIndex = moduleRecord.location.startLine > 0
        ? moduleRecord.location.startLine - 1
        : 0;
    int endIndex = moduleRecord.location.endLine > 0
        ? moduleRecord.location.endLine - 1
        : lineCount - 1;
    startIndex = qBound(0, startIndex, lineCount - 1);
    endIndex = qBound(startIndex, endIndex, lineCount - 1);

    bool inCase = false;
    for (int i = startIndex; i <= endIndex; ++i) {
        const QString code = stripLineCommentText(lines.at(i));
        if (!inCase && caseSelector(code) == stateRegister.name)
            inCase = true;
        if (inCase) {
            const QString label = caseLabel(code);
            if (!label.isEmpty() && label != QStringLiteral("default"))
                addStateName(stateNames,
                             moduleRecords,
                             stateRegister,
                             nextStateSignal,
                             label);
            if (SvTokenUtils::containsWord(code, QStringLiteral("endcase")))
                inCase = false;
        }

        const QString condition = ifCondition(code);
        if (!condition.isEmpty()
            && SvTokenUtils::containsWord(condition, stateRegister.name, true)) {
            for (const QString& token : identifiersInText(condition)) {
                if (token != stateRegister.name)
                    addStateName(stateNames,
                                 moduleRecords,
                                 stateRegister,
                                 nextStateSignal,
                                 token);
            }
        }

        const QString target = assignmentTargetInCode(code);
        if (target != nextStateSignal.name && target != stateRegister.name)
            continue;
        for (const QString& token : identifiersInText(assignmentRhs(code))) {
            addAssignedStateName(stateNames,
                                 moduleRecords,
                                 allRecords,
                                 stateRegister,
                                 nextStateSignal,
                                 token);
        }
    }

    QList<SemanticSymbolRecord> states;
    QSet<QString> seen;
    for (const QString& name : std::as_const(stateNames)) {
        if (seen.contains(name))
            continue;
        SemanticSymbolRecord record =
            stateValueRecordByName(moduleRecords,
                                   allRecords,
                                   stateRegister,
                                   name);
        if (!record.isValid())
            record = syntheticStateRecord(moduleRecord, name);
        states.append(record);
        seen.insert(name);
    }
    sortRecords(states);
    return states;
}

QList<FsmPairCandidate> discoverStructuralFsmPairs(
    const SemanticSymbolRecord& moduleRecord,
    const QList<SemanticSymbolRecord>& moduleRecords,
    const QList<SemanticSymbolRecord>& allRecords,
    const QString& content)
{
    QList<FsmPairCandidate> candidates;
    if (content.isEmpty())
        return candidates;

    const QStringList lines = content.split(QLatin1Char('\n'));
    const int lineCount = lines.size();
    int startIndex = moduleRecord.location.startLine > 0
        ? moduleRecord.location.startLine - 1
        : 0;
    int endIndex = moduleRecord.location.endLine > 0
        ? moduleRecord.location.endLine - 1
        : lineCount - 1;
    startIndex = qBound(0, startIndex, lineCount - 1);
    endIndex = qBound(startIndex, endIndex, lineCount - 1);

    bool inClockedProcess = false;
    bool sawBegin = false;
    int depth = 0;
    int processStartLine = -1;
    QSet<QString> seenPairs;
    for (int i = startIndex; i <= endIndex; ++i) {
        const QString code = stripLineCommentText(lines.at(i));
        if (!inClockedProcess && isClockedProcessStart(code)) {
            inClockedProcess = true;
            sawBegin = false;
            depth = 0;
            processStartLine = i;
        }

        if (inClockedProcess && isNonblockingAssignment(code)) {
            const QString lhs = assignmentTargetInCode(code);
            const QString rhs = firstIdentifier(assignmentRhs(code));
            const SemanticSymbolRecord currentState =
                stateCarrierByName(moduleRecords, lhs);
            const SemanticSymbolRecord nextState =
                stateCarrierByName(moduleRecords, rhs);
            if (currentState.isValid()
                && nextState.isValid()
                && currentState.localHandle != nextState.localHandle) {
                const QString pairKey =
                    QStringLiteral("%1:%2")
                        .arg(currentState.localHandle)
                        .arg(nextState.localHandle);
                if (!seenPairs.contains(pairKey)) {
                    FsmPairCandidate candidate;
                    candidate.currentState = currentState;
                    candidate.nextState = nextState;
                    candidate.states = structuralStateValues(moduleRecord,
                                                             moduleRecords,
                                                             allRecords,
                                                             currentState,
                                                             nextState,
                                                             content);
                    candidates.append(candidate);
                    seenPairs.insert(pairKey);
                }
            }
        }

        if (!inClockedProcess)
            continue;
        const int begins = wordCount(code, QStringLiteral("begin"));
        const int ends = wordCount(code, QStringLiteral("end"));
        if (begins > 0)
            sawBegin = true;
        depth += begins;
        depth -= ends;
        if (sawBegin && i > processStartLine && depth <= 0)
            inClockedProcess = false;
        else if (!sawBegin && code.contains(QLatin1Char(';')))
            inClockedProcess = false;
    }

    std::sort(candidates.begin(),
              candidates.end(),
              [](const FsmPairCandidate& lhs,
                 const FsmPairCandidate& rhs) {
                  if (lhs.currentState.location.startLine
                      != rhs.currentState.location.startLine) {
                      return lhs.currentState.location.startLine
                          < rhs.currentState.location.startLine;
                  }
                  return lhs.nextState.location.startLine
                      < rhs.nextState.location.startLine;
              });
    return candidates;
}

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
    const QList<SemanticSymbolRecord>& states,
    const QList<FsmTransition>& transitions)
{
    QList<FsmStateRow> rows;
    rows.reserve(states.size());
    QSet<QString> selfLoopStates;
    QSet<QString> statesWithExternalExit;
    QSet<QString> knownStateNames;
    for (const SemanticSymbolRecord& state : states)
        knownStateNames.insert(state.name);
    for (const FsmTransition& transition : transitions) {
        if (!knownStateNames.contains(transition.fromState))
            continue;
        if (transition.fromState == transition.toState) {
            selfLoopStates.insert(transition.fromState);
        } else {
            statesWithExternalExit.insert(transition.fromState);
        }
    }
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
        row.deadEndState = selfLoopStates.contains(state.name)
            && !statesWithExternalExit.contains(state.name);
        row.statusDisplayName = row.deadEndState
            ? QStringLiteral("dead/end state")
            : QString();
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
    for (FsmTransition& transition : transitions)
        fillDisplayMetadata(transition);
    graph.stateRows = stateRows(states, transitions);
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
