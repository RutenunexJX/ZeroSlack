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
    static QList<sym_list::SymbolInfo> commandSymbols(
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
    static QList<sym_list::SymbolInfo> moduleInternalSymbolInfosByType(
        SemanticIndex* semanticIndex,
        const QString& moduleName,
        sym_list::sym_type_e symbolType,
        const QString& prefix,
        bool useRelationshipFallback);
    static QList<sym_list::SymbolInfo> moduleContextSymbolInfosByType(
        SemanticIndex* semanticIndex,
        const QString& moduleName,
        const QString& fileName,
        sym_list::sym_type_e symbolType,
        const QString& prefix);
    static QList<sym_list::SymbolInfo> globalSymbolInfosByType(
        SemanticIndex* semanticIndex,
        sym_list::sym_type_e symbolType,
        const QString& prefix);
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
