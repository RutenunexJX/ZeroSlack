#ifndef COMPLETIONSYMBOLQUERY_H
#define COMPLETIONSYMBOLQUERY_H

#include "semanticindex.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

class CompletionSymbolQuery
{
public:
    static QVector<QPair<QString, int>> scoredNames(
        const QStringList& names,
        const QString& prefix,
        int maxResults);
    static QVector<QPair<sym_list::SymbolInfo, int>> scoredTypedSymbols(
        SemanticIndex* semanticIndex,
        sym_list::sym_type_e symbolType,
        const QString& prefix,
        int maxResults);
    static QStringList namesFromScored(
        const QVector<QPair<QString, int>>& scored,
        int maxResults);
    static QStringList symbolNamesFromScored(
        const QVector<QPair<sym_list::SymbolInfo, int>>& scored,
        int maxResults);
    static QStringList namesFromSymbols(
        const QList<sym_list::SymbolInfo>& symbols);
    static bool nameMatches(const QString& name, const QString& prefix);
    static QList<sym_list::SymbolInfo> structMemberSymbols(
        SemanticIndex* semanticIndex,
        const QString& structTypeName,
        const QString& prefix);
    static QStringList scopeCompletions(
        SemanticIndex* semanticIndex,
        const QString& fileName,
        int cursorLine,
        const QString& prefix);
    static QStringList moduleSymbolsByType(
        SemanticIndex* semanticIndex,
        const QString& moduleName,
        sym_list::sym_type_e symbolType,
        const QString& prefix);
    static QStringList globalSymbolsByType(
        SemanticIndex* semanticIndex,
        sym_list::sym_type_e symbolType,
        const QString& prefix);
    static QStringList taskFunctionCompletions(
        SemanticIndex* semanticIndex,
        const QString& prefix);
    static bool isModuleRangeSymbolType(sym_list::sym_type_e type);
    static bool isGlobalSymbolType(sym_list::sym_type_e type);
};

#endif // COMPLETIONSYMBOLQUERY_H
