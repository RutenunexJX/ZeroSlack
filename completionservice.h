#ifndef COMPLETIONSERVICE_H
#define COMPLETIONSERVICE_H

#include "semanticindex.h"

#include <QString>
#include <QStringList>
#include <memory>

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
    QStringList findCommandCompletions(const CommandCompletionQuery& query) const;
    QList<sym_list::SymbolInfo> findCommandCompletionSymbols(const CommandCompletionQuery& query) const;
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
    bool completionNameMatches(const QString& name, const QString& prefix) const;
};

#endif // COMPLETIONSERVICE_H
