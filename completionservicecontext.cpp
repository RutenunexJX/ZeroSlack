#include "completionservice.h"

#include "completioncontextquery.h"

bool CompletionService::relationshipCompletionsAvailable() const
{
    return CompletionContextQuery::relationshipCompletionsAvailable(
        semanticIndex());
}

QVector<QPair<QString, int>> CompletionService::findSmartCompletions(
    const QString& prefix,
    const QString& fileName,
    int cursorPosition,
    bool relationshipCompletionsEnabled) const
{
    return CompletionContextQuery::smartCompletions(
        semanticIndex(),
        prefix,
        fileName,
        cursorPosition,
        relationshipCompletionsEnabled);
}

QStringList CompletionService::findModuleChildCompletions(
    const QString& moduleName,
    const QString& prefix) const
{
    return CompletionContextQuery::moduleChildCompletions(
        semanticIndex(), moduleName, prefix);
}

QStringList CompletionService::findRelatedSymbolCompletions(
    const QString& symbolName,
    const QString& prefix) const
{
    return CompletionContextQuery::relatedSymbolCompletions(
        semanticIndex(), symbolName, prefix);
}

QStringList CompletionService::findSymbolReferenceCompletions(
    const QString& symbolName,
    const QString& prefix) const
{
    return CompletionContextQuery::symbolReferenceCompletions(
        semanticIndex(), symbolName, prefix);
}

QStringList CompletionService::findClockDomainCompletions(const QString& prefix) const
{
    return CompletionContextQuery::clockDomainCompletions(
        semanticIndex(), prefix);
}

QStringList CompletionService::findResetSignalCompletions(const QString& prefix) const
{
    return CompletionContextQuery::resetSignalCompletions(
        semanticIndex(), prefix);
}

QStringList CompletionService::findContextAwareCompletions(
    const ContextCompletionQuery& query) const
{
    return CompletionContextQuery::contextAwareCompletions(
        semanticIndex(),
        query.prefix,
        query.currentModule,
        query.context,
        query.relationshipCompletionsEnabled);
}
