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
    static QStringList namesFromScored(
        const QVector<QPair<QString, int>>& scored,
        int maxResults);
    static QStringList namesFromRecords(
        const QList<SemanticSymbolRecord>& records);
    static bool nameMatches(const QString& name, const QString& prefix);
    static QList<SemanticSymbolRecord> structMemberRecords(
        SemanticIndex* semanticIndex,
        const QString& structTypeName,
        const QString& prefix);
    static QStringList scopeCompletions(
        SemanticIndex* semanticIndex,
        const QString& fileName,
        int cursorLine,
        const QString& prefix);
};

#endif // COMPLETIONSYMBOLQUERY_H
