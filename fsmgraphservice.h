#ifndef FSMGRAPHSERVICE_H
#define FSMGRAPHSERVICE_H

#include "semanticindex.h"

#include <QList>
#include <QString>
#include <memory>

struct FsmGraphQuery {
    int moduleSymbolId = -1;
    QString moduleName;
    QString fileName;
};

struct FsmTransition {
    QString fromState;
    QString toState;
    QString condition;
    QString assignmentTarget;
    int line = 0;
    QString sectionDisplayName;
    QString detailDisplayName;
};

struct FsmStateRow {
    sym_list::SymbolInfo state = {};
    QString sectionDisplayName;
    QString detailDisplayName;
};

struct FsmGraph {
    sym_list::SymbolInfo moduleSymbol = {};
    sym_list::SymbolInfo stateRegister = {};
    sym_list::SymbolInfo nextStateSignal = {};
    QList<sym_list::SymbolInfo> states;
    QList<FsmTransition> transitions;
    QList<FsmStateRow> stateRows;
    QString stateRegisterSectionDisplayName;
    QString stateRegisterDetailDisplayName;
    QString statesGroupDisplayName;
    QString transitionsGroupDisplayName;
};

struct FsmGraphReport {
    bool found = false;
    QString groupDisplayName;
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
    sym_list::SymbolInfo resolveModule(const FsmGraphQuery& query) const;
    QList<sym_list::SymbolInfo> symbolsInModule(
        const sym_list::SymbolInfo& moduleSymbol) const;
    QList<sym_list::SymbolInfo> stateRegisters(
        const QList<sym_list::SymbolInfo>& symbols) const;
    QList<sym_list::SymbolInfo> stateValues(
        const QList<sym_list::SymbolInfo>& symbols,
        const sym_list::SymbolInfo& stateRegister) const;
    sym_list::SymbolInfo nextStateSignal(
        const QList<sym_list::SymbolInfo>& symbols,
        const sym_list::SymbolInfo& stateRegister) const;
    QList<FsmTransition> parseTransitions(
        const sym_list::SymbolInfo& moduleSymbol,
        const sym_list::SymbolInfo& stateRegister,
        const QList<sym_list::SymbolInfo>& states) const;

    static bool isInsideModule(const sym_list::SymbolInfo& symbol,
                               const sym_list::SymbolInfo& moduleSymbol);
    static QString stripLineComment(const QString& line);
    static QList<FsmStateRow> stateRows(
        const QList<sym_list::SymbolInfo>& states);
    static QString stateDetailDisplayName(const sym_list::SymbolInfo& state);
    static QString stateRegisterDetailDisplayName(const FsmGraph& graph);
    static QString transitionDetailDisplayName(const FsmTransition& transition);
    static void fillDisplayMetadata(FsmGraph& graph);
    static void fillDisplayMetadata(FsmTransition& transition);
    static void sortSymbols(QList<sym_list::SymbolInfo>& symbols);
    static void sortTransitions(QList<FsmTransition>& transitions);
};

#endif // FSMGRAPHSERVICE_H
