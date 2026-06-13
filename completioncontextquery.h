#ifndef COMPLETIONCONTEXTQUERY_H
#define COMPLETIONCONTEXTQUERY_H

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

class SemanticIndex;

class CompletionContextQuery
{
public:
    static bool relationshipCompletionsAvailable(SemanticIndex* semanticIndex);
    static QVector<QPair<QString, int>> smartCompletions(
        SemanticIndex* semanticIndex,
        const QString& prefix,
        const QString& fileName,
        int cursorPosition,
        bool relationshipCompletionsEnabled);
    static QStringList contextAwareCompletions(
        SemanticIndex* semanticIndex,
        const QString& prefix,
        const QString& currentModule,
        const QString& context,
        bool relationshipCompletionsEnabled);
    static QStringList moduleChildCompletions(
        SemanticIndex* semanticIndex,
        const QString& moduleName,
        const QString& prefix);
    static QStringList relatedSymbolCompletions(
        SemanticIndex* semanticIndex,
        const QString& symbolName,
        const QString& prefix);
    static QStringList symbolReferenceCompletions(
        SemanticIndex* semanticIndex,
        const QString& symbolName,
        const QString& prefix);
    static QStringList clockDomainCompletions(
        SemanticIndex* semanticIndex,
        const QString& prefix);
    static QStringList resetSignalCompletions(
        SemanticIndex* semanticIndex,
        const QString& prefix);
};

#endif // COMPLETIONCONTEXTQUERY_H
