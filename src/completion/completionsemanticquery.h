#ifndef COMPLETIONSEMANTICQUERY_H
#define COMPLETIONSEMANTICQUERY_H

#include "completiontypes.h"

#include <QList>
class CompletionSemanticQuery
{
public:
    static QList<SemanticSymbolRecord> commandSymbolRecords(
        SemanticIndex* semanticIndex,
        const CommandCompletionQuery& query);
    static QList<QList<SemanticSymbolRecord>> commandSymbolRecordGroups(
        SemanticIndex* semanticIndex,
        const CommandCompletionQuery& query,
        const QList<CompletionCommandKind>& commandKinds);
};

#endif // COMPLETIONSEMANTICQUERY_H
