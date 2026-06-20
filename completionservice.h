#ifndef COMPLETIONSERVICE_H
#define COMPLETIONSERVICE_H

#include "completiontypes.h"

#include <QPair>
#include <QVector>
#include <memory>

class SemanticIndex;

class CompletionService
{
public:
    static CompletionService* getInstance();

    explicit CompletionService(SemanticIndex* semanticIndex = nullptr);
    ~CompletionService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QStringList findCompletions(const CompletionQuery& query) const;
    CompletionResult findCompletionResult(const CompletionQuery& query) const;
    QVector<QPair<QString, int>> findScoredAllSymbolCompletions(
        const QString& prefix,
        int maxResults = 20) const;
    QStringList findAllSymbolCompletions(const QString& prefix,
                                         int maxResults = 15) const;
    QStringList findSymbolCompletionsByKind(
        CompletionCommandKind commandKind,
        const QString& prefix,
        int maxResults = 15) const;
    bool matchesCompletionAbbreviation(const QString& text,
                                       const QString& abbreviation) const;
    int calculateCompletionMatchScore(const QString& text,
                                      const QString& abbreviation) const;
    int completionItemScore(const QString& text, const QString& prefix) const;
    CommandSymbolPresentation commandSymbolPresentation(
        CompletionCommandKind kind) const;
    CommandSymbolCompletionItem commandSymbolCompletionItem(
        const SemanticSymbolRecord& record,
        CompletionCommandKind requestedKind,
        const QString& prefix = QString()) const;
    QList<CommandModeCommand> commandModeCommands() const;
    CommandModeMatch matchCommandMode(const QString& lineUpToCursor) const;
    CommandModeInputState commandModeInputState(
        const QString& lineUpToCursor) const;
    CommandModeCompletionState commandModeCompletionState(
        const CommandModeCompletionQuery& query) const;
    EditorCompletionState editorCompletionState(
        const EditorCompletionQuery& query) const;
    CompletionTriggerState completionTriggerState(
        const CompletionTriggerQuery& query) const;
    CompletionActivationState completionActivationState(
        const CompletionActivationQuery& query) const;
    CompletionPopupKeyState completionPopupKeyState(
        const CompletionPopupKeyQuery& query) const;
    bool shouldContinueCompletion(const CompletionTriggerQuery& query) const;
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
    QList<SemanticSymbolRecord> findCommandCompletionSymbolRecords(
        const CommandCompletionQuery& query) const;
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
    QStringList findModuleSymbolsByKind(const QString& moduleName,
                                        CompletionCommandKind commandKind,
                                        const QString& prefix = QString()) const;
    QStringList findGlobalSymbolCompletions(const QString& prefix = QString()) const;
    QStringList findGlobalSymbolsByKind(CompletionCommandKind commandKind,
                                        const QString& prefix = QString()) const;
    QStringList findVariableCompletionsInScope(const QString& moduleName,
                                               CompletionCommandKind commandKind,
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
    QString currentModuleAt(const QString& fileName, int cursorPosition) const;
    QString getStructTypeForVariable(const QString& variableName, const QString& moduleName) const;
    bool tryParseStructMemberContext(const QString& line,
                                     QString& outVariableName,
                                     QString& outMemberPrefix) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<CompletionService> instance;

    SemanticIndex* semanticIndex() const;
};

#endif // COMPLETIONSERVICE_H
