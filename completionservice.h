#ifndef COMPLETIONSERVICE_H
#define COMPLETIONSERVICE_H

#include "semanticindex.h"

#include <QString>
#include <QStringList>
#include <memory>

struct RelationshipResult;

struct CompletionQuery {
    QString prefix;
    QString fileName;
    QString moduleName;
    QString structTypeNameForMember;
    int cursorLine = -1;
    int cursorPosition = -1;
};

struct CommandCompletionQuery {
    QString prefix;
    QString fileName;
    QString moduleName;
    QString documentText;
    sym_list::sym_type_e symbolType = sym_list::sym_user;
};

class CompletionService
{
public:
    static CompletionService* getInstance();

    explicit CompletionService(SemanticIndex* semanticIndex = nullptr);
    ~CompletionService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QStringList findCompletions(const CompletionQuery& query) const;
    QList<sym_list::SymbolInfo> findCompletionSymbols(const CompletionQuery& query) const;
    QStringList findScopeCompletions(const CompletionQuery& query) const;
    QStringList findCommandCompletions(const CommandCompletionQuery& query) const;
    QList<sym_list::SymbolInfo> findCommandCompletionSymbols(const CommandCompletionQuery& query) const;
    QStringList findModuleChildCompletions(const QString& moduleName,
                                           const QString& prefix = QString()) const;
    QStringList findRelatedSymbolCompletions(const QString& symbolName,
                                             const QString& prefix = QString()) const;
    QStringList findSymbolReferenceCompletions(const QString& symbolName,
                                               const QString& prefix = QString()) const;
    QStringList findClockDomainCompletions(const QString& prefix = QString()) const;
    QStringList findResetSignalCompletions(const QString& prefix = QString()) const;
    QString currentModuleAt(const QString& fileName, int cursorPosition) const;
    QString getStructTypeForVariable(const QString& variableName, const QString& moduleName) const;
    bool tryParseStructMemberContext(const QString& line,
                                     QString& outVariableName,
                                     QString& outMemberPrefix) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<CompletionService> instance;

    SemanticIndex* semanticIndex() const;
    QList<sym_list::SymbolInfo> findStructMemberSymbols(
        const CompletionQuery& query) const;
    QList<sym_list::SymbolInfo> findModuleCompletionSymbols(
        const CompletionQuery& query) const;
    QList<sym_list::SymbolInfo> findGlobalCompletionSymbols(
        const CompletionQuery& query) const;
    QList<sym_list::SymbolInfo> findCommandSymbolsFromIndex(
        const CommandCompletionQuery& query) const;
    QStringList completionNamesFromSymbols(
        const QList<sym_list::SymbolInfo>& symbols) const;
    bool completionNameMatches(const QString& name, const QString& prefix) const;
    QString moduleNameAtPosition(const QList<sym_list::SymbolInfo>& modules,
                                 int cursorPosition,
                                 const QString& fileName,
                                 const QString& fileContent) const;
    int endModulePosition(const QString& fileContent,
                          const sym_list::SymbolInfo& moduleSymbol) const;
    QStringList completionNamesFromRelationshipResults(
        const QList<RelationshipResult>& relationships,
        bool outgoing) const;
    bool isInternalCompletionType(sym_list::sym_type_e type) const;
    bool isGlobalCompletionType(sym_list::sym_type_e type) const;
    bool isCommandGlobalCompletionType(sym_list::sym_type_e type) const;
    bool isAlwaysGlobalCommandType(sym_list::sym_type_e type) const;
    bool commandSymbolTypeMatches(sym_list::sym_type_e symbolType,
                                  const QString& dataType,
                                  sym_list::sym_type_e requestedType) const;
};

#endif // COMPLETIONSERVICE_H
