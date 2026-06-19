#ifndef COMPLETIONSEMANTICQUERY_H
#define COMPLETIONSEMANTICQUERY_H

#include "semanticindex.h"

#include <QList>
#include <QString>
#include <QStringList>

struct CommandCompletionQuery;

class CompletionSemanticQuery
{
public:
    static QList<SemanticSymbolRecord> commandSymbolRecords(
        SemanticIndex* semanticIndex,
        const CommandCompletionQuery& query);
    static QStringList enumValueCompletions(
        SemanticIndex* semanticIndex,
        const QString& prefix,
        const QString& enumTypeName);
    static QString enumTypeForVariable(
        SemanticIndex* semanticIndex,
        const QString& variableName,
        const QString& moduleName);
    static QStringList modulePortCompletions(
        SemanticIndex* semanticIndex,
        const QString& prefix,
        const QString& moduleTypeName);
    static QString currentModuleAt(
        SemanticIndex* semanticIndex,
        const QString& fileName,
        int cursorPosition);
    static QString structTypeForVariable(
        SemanticIndex* semanticIndex,
        const QString& variableName,
        const QString& moduleName);
};

#endif // COMPLETIONSEMANTICQUERY_H
