#ifndef COMPLETIONMANAGER_H
#define COMPLETIONMANAGER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QPair>
#include <QList>
#include <memory>
#include "syminfo.h"

class CompletionManager
{
public:
    static CompletionManager* getInstance();
    ~CompletionManager();

    bool matchesAbbreviation(const QString &text, const QString &abbreviation);
    QStringList getAbbreviationMatches(const QStringList &candidates, const QString &abbreviation);
    int calculateMatchScore(const QString &text, const QString &abbreviation);

    QList<int> findAbbreviationPositions(const QString &text, const QString &abbreviation);

    QVector<QPair<QString, int>> getSmartCompletions(const QString& prefix,
                                                    const QString& fileName = "",
                                                    int cursorPosition = -1);
    QStringList getContextAwareCompletions(const QString& prefix,
                                          const QString& currentModule = "",
                                          const QString& context = "");

    QVector<QPair<QString, int>> getScoredKeywordMatches(const QString& prefix);

    QVector<QPair<QString, int>> getScoredAllSymbolMatches(const QString& prefix);
    QStringList getAllSymbolCompletions(const QString& prefix);

    QStringList getKeywordCompletions(const QString& prefix);
    QStringList getSymbolCompletions(sym_list::sym_type_e symbolType, const QString& prefix);

    QStringList getModuleChildrenCompletions(const QString& moduleName, const QString& prefix = "");
    QStringList getRelatedSymbolCompletions(const QString& symbolName, const QString& prefix = "");
    QStringList getSymbolReferencesCompletions(const QString& symbolName, const QString& prefix = "");
    QStringList getClockDomainCompletions(const QString& prefix = "");
    QStringList getResetSignalCompletions(const QString& prefix = "");

    QStringList getVariableCompletionsInScope(const QString& moduleName,
                                             sym_list::sym_type_e variableType,
                                             const QString& prefix = "");
    QStringList getTaskFunctionCompletions(const QString& prefix = "");
    QStringList getInstantiableModules(const QString& prefix = "");

    QString getCurrentModule(const QString& fileName, int cursorPosition);

    QStringList getCompletions(const QString& prefix, const QString& cursorFile, int cursorLine);

    QStringList getModuleInternalVariables(const QString& moduleName, const QString& prefix);
    QStringList getGlobalSymbolCompletions(const QString& prefix);


    QStringList getModuleInternalVariablesByType(const QString& moduleName,
                                                sym_list::sym_type_e symbolType,
                                                const QString& prefix = "");

    QStringList getGlobalSymbolsByType(sym_list::sym_type_e symbolType,
                                      const QString& prefix = "");


    QString getStructTypeForVariable(const QString &varName, const QString &currentModule);
    QStringList getStructMemberCompletions(const QString &prefix, const QString &structTypeName);
    bool tryParseStructMemberContext(const QString &line, QString &outVarName, QString &outMemberPrefix);

private:
    CompletionManager();

    static std::unique_ptr<CompletionManager> instance;

};

#endif // COMPLETIONMANAGER_H
