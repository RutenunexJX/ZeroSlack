#ifndef COMPLETIONSERVICE_H
#define COMPLETIONSERVICE_H

#include "completiontypes.h"

#include <memory>

class SemanticIndex;
class UserTemplateService;

class CompletionService
{
public:
    static CompletionService* getInstance();

    explicit CompletionService(SemanticIndex* semanticIndex = nullptr);
    ~CompletionService();

    void setSemanticIndex(SemanticIndex* semanticIndex);
    void setUserTemplateService(UserTemplateService* service);

    bool matchesCompletionAbbreviation(const QString& text,
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
    CompletionActivationState completionActivationState(
        const CompletionActivationQuery& query) const;
    CompletionPopupKeyState completionPopupKeyState(
        const CompletionPopupKeyQuery& query) const;
    QList<SemanticSymbolRecord> findCommandCompletionSymbolRecords(
        const CommandCompletionQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    UserTemplateService* templates = nullptr;
    static std::unique_ptr<CompletionService> instance;

    SemanticIndex* semanticIndex() const;
    UserTemplateService* userTemplateService() const;
};

#endif // COMPLETIONSERVICE_H
