#ifndef MODULEBRIEFSERVICE_H
#define MODULEBRIEFSERVICE_H

#include "semanticindex.h"

#include <QList>
#include <QMap>
#include <QString>
#include <memory>

struct ModuleBriefQuery {
    int moduleSymbolId = -1;
    QString moduleName;
    QString fileName;
};

struct ModuleBriefRelationshipSummary {
    int outgoingCount = 0;
    int incomingCount = 0;
    int totalCount = 0;
    QMap<SymbolRelationshipEngine::RelationType, int> outgoingTypeCounts;
    QMap<SymbolRelationshipEngine::RelationType, int> incomingTypeCounts;
};

struct ModuleBriefReport {
    bool found = false;
    sym_list::SymbolInfo moduleSymbol = {};
    QList<sym_list::SymbolInfo> ports;
    QList<sym_list::SymbolInfo> parameters;
    QList<sym_list::SymbolInfo> instances;
    QList<sym_list::SymbolInfo> imports;
    QList<SemanticDiagnostic> diagnostics;
    ModuleBriefRelationshipSummary relationshipSummary;
};

class ModuleBriefService
{
public:
    static ModuleBriefService* getInstance();

    explicit ModuleBriefService(SemanticIndex* semanticIndex = nullptr);
    ~ModuleBriefService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    ModuleBriefReport buildModuleBrief(const ModuleBriefQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<ModuleBriefService> instance;

    SemanticIndex* semanticIndex() const;
    sym_list::SymbolInfo resolveModule(const ModuleBriefQuery& query) const;
    QList<sym_list::SymbolInfo> symbolsInModule(
        const sym_list::SymbolInfo& moduleSymbol,
        const QList<sym_list::SymbolInfo>& symbols,
        bool (*matchesType)(sym_list::sym_type_e)) const;
    QList<sym_list::SymbolInfo> importSymbols(
        const sym_list::SymbolInfo& moduleSymbol) const;
    QList<SemanticDiagnostic> diagnosticsForModule(
        const sym_list::SymbolInfo& moduleSymbol) const;
    ModuleBriefRelationshipSummary relationshipSummary(
        const sym_list::SymbolInfo& moduleSymbol) const;

    static bool isInsideModule(const sym_list::SymbolInfo& symbol,
                               const sym_list::SymbolInfo& moduleSymbol);
    static void sortSymbols(QList<sym_list::SymbolInfo>& symbols);
};

#endif // MODULEBRIEFSERVICE_H
