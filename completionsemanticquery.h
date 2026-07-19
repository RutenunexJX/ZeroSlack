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
};

#endif // COMPLETIONSEMANTICQUERY_H
