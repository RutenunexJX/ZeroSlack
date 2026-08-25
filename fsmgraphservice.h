#ifndef FSMGRAPHSERVICE_H
#define FSMGRAPHSERVICE_H

#include "rtlinsightlink.h"
#include "semanticindex.h"
#include "symboltaxonomy.h"

#include <QList>
#include <QString>
#include <memory>

struct FsmGraphQuery {
    SymbolStableKey moduleStableKey;
    QString moduleName;
    QString fileName;
};

enum class FsmGraphNotFoundReason {
    None,
    EmptyModuleName,
    NoMatchingModule,
    UnsupportedSymbolKind,
    NoFsmGraph
};

struct FsmTransition {
    QString fromState;
    QString toState;
    QString condition;
    QString assignmentTarget;
    int line = 0;
    RtlInsightCodeLink codeLink;
    QString sectionDisplayName;
    QString detailDisplayName;
};

struct FsmStateRow {
    SemanticSymbolRecord stateRecord;
    SymbolStableKey stateStableKey;
    RtlInsightCodeLink codeLink;
    QString sectionDisplayName;
    QString stateDisplayName;
    QString detailDisplayName;
    QString typeDisplayName;
    QString sourceRoleDisplayName;
    QString moduleDisplayName;
    QString statusDisplayName;
    bool deadEndState = false;
};

struct FsmTransitionRow {
    SemanticSymbolRecord moduleSymbolRecord;
    SemanticSymbolRecord fromStateRecord;
    SemanticSymbolRecord toStateRecord;
    SymbolStableKey fromStateStableKey;
    SymbolStableKey toStateStableKey;
    RtlInsightCodeLink codeLink;
    RtlInsightCodeLink fromStateCodeLink;
    RtlInsightCodeLink toStateCodeLink;
    QString assignmentTargetDisplayName;
    QString sectionDisplayName;
    QString fromStateDisplayName;
    QString toStateDisplayName;
    QString conditionDisplayName;
    QString detailDisplayName;
    QString sourceLineDisplayName;
    QString sourceRoleDisplayName;
};

struct FsmGraph {
    SemanticSymbolRecord moduleSymbolRecord;
    SemanticSymbolRecord stateRegisterRecord;
    SemanticSymbolRecord nextStateSignalRecord;
    SymbolStableKey moduleStableKey;
    SymbolStableKey stateRegisterStableKey;
    SymbolStableKey nextStateSignalStableKey;
    RtlInsightCodeLink stateRegisterCodeLink;
    RtlInsightCodeLink nextStateSignalCodeLink;
    QList<FsmStateRow> stateRows;
    QList<FsmTransitionRow> transitionRows;
    QString moduleDisplayName;
    int stateCount = 0;
    int transitionCount = 0;
    QString stateRegisterSectionDisplayName;
    QString stateRegisterDisplayName;
    QString stateRegisterDetailDisplayName;
    QString stateRegisterTypeDisplayName;
    QString stateRegisterSourceRoleDisplayName;
    QString nextStateSignalDisplayName;
    QString initialStateDisplayName;
    QString nextStateSignalTypeDisplayName;
    QString nextStateSignalSourceRoleDisplayName;
    QString statesGroupDisplayName;
    QString transitionsGroupDisplayName;
};

struct FsmGraphReport {
    bool found = false;
    FsmGraphNotFoundReason notFoundReason =
        FsmGraphNotFoundReason::None;
    QString groupDisplayName;
    QString notFoundReasonDisplayName;
    QList<FsmGraph> graphs;
};

enum class FsmSymbolRole {
    None,
    CurrentState,
    NextState
};

struct FsmSymbolRoleReport {
    bool inFsm = false;
    FsmSymbolRole role = FsmSymbolRole::None;
    QString reasonDisplayName;
    FsmGraph graph;
};

class FsmGraphService
{
public:
    static FsmGraphService* getInstance();

    explicit FsmGraphService(SemanticIndex* semanticIndex = nullptr);
    ~FsmGraphService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    FsmGraphReport buildFsmGraph(const FsmGraphQuery& query) const;
    FsmSymbolRoleReport roleForSymbol(const FsmGraphQuery& query,
                                      const QString& symbolName) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<FsmGraphService> instance;

    SemanticIndex* semanticIndex() const;
    SemanticSymbolRecord resolveModule(
        const FsmGraphQuery& query,
        FsmGraphNotFoundReason* reason) const;
    QList<SemanticSymbolRecord> symbolsInModule(
        const SemanticSymbolRecord& moduleRecord) const;
    QList<FsmTransition> parseTransitions(
        const SemanticSymbolRecord& moduleRecord,
        const SemanticSymbolRecord& stateRegister,
        const SemanticSymbolRecord& nextStateSignal,
        const QList<SemanticSymbolRecord>& states) const;

    static QString assignmentTarget(const QString& code);
    static QList<QString> assignedStateValues(const QString& code,
                                              const QSet<QString>& stateNames);
    static QString transitionConditionForState(const QString& code,
                                               const QString& pendingCondition,
                                               const QString& stateName);
    static void appendTransition(QList<FsmTransition>& transitions,
                                 QSet<QString>& seenTransitions,
                                 const QString& currentState,
                                 const QString& toState,
                                 const QString& assignmentTarget,
                                 const QString& condition,
                                 const QString& fileName,
                                 int line);
    static QString stripLineComment(const QString& line);
    static QList<FsmStateRow> stateRows(
        const QList<SemanticSymbolRecord>& states,
        const QList<FsmTransition>& transitions);
    static QList<FsmTransitionRow> transitionRows(
        const SemanticSymbolRecord& moduleRecord,
        const QList<FsmTransition>& transitions,
        const QList<SemanticSymbolRecord>& states);
    static QString stateRegisterDetailDisplayName(
        bool hasNextStateSignal,
        const QString& nextStateSignalDisplayName);
    static QString transitionDetailDisplayName(const FsmTransition& transition);
    static QString transitionConditionDisplayName(const FsmTransition& transition);
    static QString transitionSourceLineDisplayName(const FsmTransition& transition);
    static QString notFoundReasonDisplayName(FsmGraphNotFoundReason reason);
    static void fillDisplayMetadata(FsmGraph& graph,
                                    const SemanticSymbolRecord& moduleRecord,
                                    const SemanticSymbolRecord& stateRegister,
                                    const SemanticSymbolRecord& nextStateSignal,
                                    const QList<SemanticSymbolRecord>& states,
                                    QList<FsmTransition>& transitions);
    static void fillDisplayMetadata(FsmTransition& transition);
    static void sortTransitions(QList<FsmTransition>& transitions);
};

#endif // FSMGRAPHSERVICE_H
