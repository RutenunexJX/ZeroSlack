#ifndef COMPLETIONSERVICE_H
#define COMPLETIONSERVICE_H

#include "semanticindex.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>
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

struct CompletionResult {
    QStringList names;
    QList<sym_list::SymbolInfo> symbols;
};

struct CommandCompletionQuery {
    QString prefix;
    QString fileName;
    QString moduleName;
    QString documentText;
    sym_list::sym_type_e symbolType = sym_list::sym_user;
};

struct ContextCompletionQuery {
    QString prefix;
    QString currentModule;
    QString context;
    bool relationshipCompletionsEnabled = true;
};

struct CommandSymbolPresentation {
    QString defaultValue;
    QString typeDescription;
};

struct CommandSymbolCompletionItem {
    QString text;
    QString defaultValue;
    QString description;
    QString uniqueKey;
    int score = 0;
};

struct CommandModeCommand {
    QString prefix;
    sym_list::sym_type_e symbolType = sym_list::sym_user;
    QString description;
    QString defaultValue;
};

struct CommandModeMatch {
    bool matched = false;
    int prefixPosition = -1;
    QString input;
    CommandModeCommand command;
};

class CompletionService
{
public:
    static CompletionService* getInstance();

    explicit CompletionService(SemanticIndex* semanticIndex = nullptr);
    ~CompletionService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QStringList findCompletions(const CompletionQuery& query) const;
    CompletionResult findCompletionResult(const CompletionQuery& query) const;
    QList<sym_list::SymbolInfo> findCompletionSymbols(const CompletionQuery& query) const;
    QVector<QPair<QString, int>> findScoredAllSymbolCompletions(
        const QString& prefix,
        int maxResults = 20) const;
    QStringList findAllSymbolCompletions(const QString& prefix,
                                         int maxResults = 15) const;
    QVector<QPair<sym_list::SymbolInfo, int>> findScoredSymbolCompletionsByType(
        sym_list::sym_type_e symbolType,
        const QString& prefix,
        int maxResults = 15) const;
    QStringList findSymbolCompletionsByType(
        sym_list::sym_type_e symbolType,
        const QString& prefix,
        int maxResults = 15) const;
    bool matchesCompletionAbbreviation(const QString& text,
                                       const QString& abbreviation) const;
    int calculateCompletionMatchScore(const QString& text,
                                      const QString& abbreviation) const;
    int completionItemScore(const QString& text, const QString& prefix) const;
    QString symbolTypeDescription(sym_list::sym_type_e symbolType) const;
    CommandSymbolPresentation commandSymbolPresentation(
        sym_list::sym_type_e symbolType) const;
    CommandSymbolCompletionItem commandSymbolCompletionItem(
        const sym_list::SymbolInfo& symbol,
        sym_list::sym_type_e requestedType,
        const QString& prefix = QString()) const;
    QList<CommandModeCommand> commandModeCommands() const;
    CommandModeMatch matchCommandMode(const QString& lineUpToCursor) const;
    QList<int> findCompletionAbbreviationPositions(
        const QString& text,
        const QString& abbreviation) const;
    QVector<QPair<QString, int>> findScoredKeywordCompletions(
        const QString& prefix) const;
    QStringList findKeywordCompletions(const QString& prefix,
                                       int maxResults = 10) const;
    QStringList findKeywordAbbreviationMatches(
        const QStringList& candidates,
        const QString& abbreviation) const;
    bool relationshipCompletionsAvailable() const;
    QVector<QPair<QString, int>> findSmartCompletions(
        const QString& prefix,
        const QString& fileName = QString(),
        int cursorPosition = -1,
        bool relationshipCompletionsEnabled = true) const;
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
    QStringList findModuleInternalVariableCompletions(const QString& moduleName,
                                                      const QString& prefix = QString()) const;
    QStringList findModuleSymbolsByType(const QString& moduleName,
                                        sym_list::sym_type_e symbolType,
                                        const QString& prefix = QString()) const;
    QStringList findGlobalSymbolCompletions(const QString& prefix = QString()) const;
    QStringList findGlobalSymbolsByType(sym_list::sym_type_e symbolType,
                                        const QString& prefix = QString()) const;
    QStringList findVariableCompletionsInScope(const QString& moduleName,
                                               sym_list::sym_type_e variableType,
                                               const QString& prefix = QString()) const;
    QStringList findTaskFunctionCompletions(const QString& prefix = QString()) const;
    QStringList findInstantiableModuleCompletions(const QString& prefix = QString()) const;
    QStringList findContextAwareCompletions(const ContextCompletionQuery& query) const;
    QStringList findStructMemberCompletions(const QString& prefix,
                                            const QString& structTypeName) const;
    QStringList findEnumValueCompletions(const QString& prefix,
                                         const QString& enumTypeName = QString()) const;
    QString findEnumTypeForVariable(const QString& variableName,
                                    const QString& moduleName = QString()) const;
    QStringList findModulePortCompletions(const QString& prefix,
                                          const QString& moduleTypeName) const;
    QList<sym_list::SymbolInfo> findModuleInternalSymbolInfosByType(
        const QString& moduleName,
        sym_list::sym_type_e symbolType,
        const QString& prefix = QString(),
        bool useRelationshipFallback = true) const;
    QList<sym_list::SymbolInfo> findModuleContextSymbolInfosByType(
        const QString& moduleName,
        const QString& fileName,
        sym_list::sym_type_e symbolType,
        const QString& prefix = QString()) const;
    QList<sym_list::SymbolInfo> findGlobalSymbolInfosByType(
        sym_list::sym_type_e symbolType,
        const QString& prefix = QString()) const;
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
    QString extractStructVariableFromContext(const QString& context) const;
    QString extractEnumVariableFromContext(const QString& context) const;
    QString extractModuleTypeFromContext(const QString& context) const;
    QStringList publicKeywordCompletions() const;
    QStringList svKeywordCompletions(const QString& prefix) const;
    int calculateContextMatchScore(const QString& text, const QString& abbreviation) const;
    int calculateSymbolTypeCompletionScore(const QString& text,
                                           const QString& abbreviation) const;
    bool isValidContextAbbreviationMatch(const QString& text,
                                         const QString& abbreviation) const;
    QList<int> findContextAbbreviationPositions(const QString& text,
                                                const QString& abbreviation) const;
    int calculateContextScore(const QString& symbol, const QString& context) const;
    int calculateRelationshipScore(const QString& symbol,
                                   const QString& currentContext) const;
    int calculateScopeScore(const QString& symbol, const QString& currentModule) const;
    bool isModuleRangeSymbolType(sym_list::sym_type_e type) const;
    bool isInternalCompletionType(sym_list::sym_type_e type) const;
    bool isGlobalCompletionType(sym_list::sym_type_e type) const;
    bool isGlobalSymbolType(sym_list::sym_type_e type) const;
    bool isGlobalSymbolInfoType(sym_list::sym_type_e type) const;
    bool isCommandGlobalCompletionType(sym_list::sym_type_e type) const;
    bool isAlwaysGlobalCommandType(sym_list::sym_type_e type) const;
    bool commandSymbolTypeMatches(sym_list::sym_type_e symbolType,
                                  const QString& dataType,
                                  sym_list::sym_type_e requestedType) const;
};

#endif // COMPLETIONSERVICE_H
