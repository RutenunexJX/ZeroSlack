#ifndef DEFINITIONSERVICE_H
#define DEFINITIONSERVICE_H

#include "semanticindex.h"

#include <QList>
#include <QString>
#include <memory>

struct DefinitionQuery {
    QString symbolName;
    QString fileName;
    QString moduleName;
    QString structTypeNameForMember;
    QString linePrefixBeforeCursor;
};

struct DefinitionResult {
    bool found = false;
    bool localFile = false;
    sym_list::SymbolInfo symbol;
    SemanticSymbolRecord symbolRecord;
    SymbolStableKey symbolStableKey;
    int inspectedCandidateCount = 0;
    int matchingNameCandidateCount = 0;
    int typeCompatibleCandidateCount = 0;
    int visibleCandidateCount = 0;
    SemanticDefinitionMissReason missReason =
        SemanticDefinitionMissReason::NoCandidateSymbols;
};

class DefinitionService
{
public:
    static DefinitionService* getInstance();

    explicit DefinitionService(SemanticIndex* semanticIndex = nullptr);
    ~DefinitionService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    DefinitionResult resolveDefinition(const DefinitionQuery& query) const;
    QList<sym_list::SymbolInfo> findDefinitions(const DefinitionQuery& query) const;
    bool canResolveDefinition(const DefinitionQuery& query) const;
    bool isDefinition(const sym_list::SymbolInfo& symbol, const QString& searchWord) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<DefinitionService> instance;

    SemanticIndex* semanticIndex() const;
    DefinitionQuery withResolvedMemberContext(const DefinitionQuery& query) const;
};

#endif // DEFINITIONSERVICE_H
