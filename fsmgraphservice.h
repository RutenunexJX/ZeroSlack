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
    sym_list::SymbolInfo state = {};
    SemanticSymbolRecord stateRecord;
    SymbolStableKey stateStableKey;
    RtlInsightCodeLink codeLink;
    QString sectionDisplayName;
    QString stateDisplayName;
    QString detailDisplayName;
    QString typeDisplayName;
    QString sourceRoleDisplayName;
    QString moduleDisplayName;
};

struct FsmTransitionRow {
    FsmTransition transition;
    SemanticSymbolRecord moduleSymbolRecord;
    SemanticSymbolRecord fromStateRecord;
    SemanticSymbolRecord toStateRecord;
    SymbolStableKey fromStateStableKey;
    SymbolStableKey toStateStableKey;
    RtlInsightCodeLink codeLink;
    RtlInsightCodeLink fromStateCodeLink;
    RtlInsightCodeLink toStateCodeLink;
    QString sectionDisplayName;
    QString fromStateDisplayName;
    QString toStateDisplayName;
    QString conditionDisplayName;
    QString detailDisplayName;
    QString sourceLineDisplayName;
    QString sourceRoleDisplayName;
};

struct FsmGraph {
    sym_list::SymbolInfo moduleSymbol = {};
    sym_list::SymbolInfo stateRegister = {};
    sym_list::SymbolInfo nextStateSignal = {};
    SemanticSymbolRecord moduleSymbolRecord;
    SemanticSymbolRecord stateRegisterRecord;
    SemanticSymbolRecord nextStateSignalRecord;
    SymbolStableKey moduleStableKey;
    SymbolStableKey stateRegisterStableKey;
    SymbolStableKey nextStateSignalStableKey;
    RtlInsightCodeLink stateRegisterCodeLink;
    RtlInsightCodeLink nextStateSignalCodeLink;
    QList<sym_list::SymbolInfo> states;
    QList<FsmTransition> transitions;
    QList<FsmStateRow> stateRows;
    QList<FsmTransitionRow> transitionRows;
    QString moduleDisplayName;
    int stateCount = 0;
    QString stateRegisterSectionDisplayName;
    QString stateRegisterDisplayName;
    QString stateRegisterDetailDisplayName;
    QString stateRegisterTypeDisplayName;
    QString stateRegisterSourceRoleDisplayName;
    QString nextStateSignalDisplayName;
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

class FsmGraphService
{
public:
    static FsmGraphService* getInstance();

    explicit FsmGraphService(SemanticIndex* semanticIndex = nullptr);
    ~FsmGraphService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    FsmGraphReport buildFsmGraph(const FsmGraphQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<FsmGraphService> instance;

    SemanticIndex* semanticIndex() const;
    sym_list::SymbolInfo resolveModule(
        const FsmGraphQuery& query,
        FsmGraphNotFoundReason* reason) const;
    QList<sym_list::SymbolInfo> symbolsInModule(
        const sym_list::SymbolInfo& moduleSymbol) const;
    QList<sym_list::SymbolInfo> stateRegisters(
        const QList<sym_list::SymbolInfo>& moduleSymbols,
        const QList<sym_list::SymbolInfo>& allSymbols) const;
    QList<sym_list::SymbolInfo> stateValues(
        const QList<sym_list::SymbolInfo>& moduleSymbols,
        const QList<sym_list::SymbolInfo>& allSymbols,
        const sym_list::SymbolInfo& stateRegister) const;
    sym_list::SymbolInfo nextStateSignal(
        const QList<sym_list::SymbolInfo>& moduleSymbols,
        const sym_list::SymbolInfo& stateRegister) const;
    QList<FsmTransition> parseTransitions(
        const sym_list::SymbolInfo& moduleSymbol,
        const sym_list::SymbolInfo& stateRegister,
        const sym_list::SymbolInfo& nextStateSignal,
        const QList<sym_list::SymbolInfo>& states) const;

    static bool isInsideModule(const sym_list::SymbolInfo& symbol,
                               const sym_list::SymbolInfo& moduleSymbol);
    static bool hasStateValuesForType(const QList<sym_list::SymbolInfo>& symbols,
                                      const QString& rawTypeText);
    static bool hasPairedNextStateSignal(
        const QList<sym_list::SymbolInfo>& moduleSymbols,
        const sym_list::SymbolInfo& stateRegister);
    static bool isPairedNextStateName(const QString& currentName,
                                      const QString& candidateName);
    static bool looksLikeCurrentStateName(const QString& name);
    static bool looksLikeNextStateName(const QString& name);
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
        const QList<sym_list::SymbolInfo>& states);
    static QList<FsmTransitionRow> transitionRows(
        const sym_list::SymbolInfo& moduleSymbol,
        const QList<FsmTransition>& transitions,
        const QList<sym_list::SymbolInfo>& states);
    static QString stateDetailDisplayName(const sym_list::SymbolInfo& state);
    static QString stateRegisterDetailDisplayName(
        const FsmGraph& graph,
        const QString& nextStateSignalDisplayName);
    static QString transitionDetailDisplayName(const FsmTransition& transition);
    static QString transitionConditionDisplayName(const FsmTransition& transition);
    static QString transitionSourceLineDisplayName(const FsmTransition& transition);
    static QString notFoundReasonDisplayName(FsmGraphNotFoundReason reason);
    static void fillDisplayMetadata(FsmGraph& graph);
    static void fillDisplayMetadata(FsmTransition& transition);
    static void sortSymbols(QList<sym_list::SymbolInfo>& symbols);
    static void sortTransitions(QList<FsmTransition>& transitions);
};

#endif // FSMGRAPHSERVICE_H
