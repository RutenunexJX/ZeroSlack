#ifndef SEMANTICSTABLEIDENTITY_H
#define SEMANTICSTABLEIDENTITY_H

#include <zeroslack/semantic/semanticapi.h>

#include <QString>

struct SemanticSymbolRecord;

struct ZEROSLACK_SEMANTIC_API SemanticStableIdentity {
    QString stableId;
    QString exactId;
    QString stability;
    QString canonicalIdentity;

    bool isValid() const
    {
        return !stableId.isEmpty() && !exactId.isEmpty();
    }
};

ZEROSLACK_SEMANTIC_API SemanticStableIdentity semanticStableIdentity(
    const SemanticSymbolRecord& record,
    const QString& workspaceRoot = QString());

ZEROSLACK_SEMANTIC_API QString semanticStableSymbolUri(
    const SemanticSymbolRecord& record,
    const QString& workspaceRoot = QString());

#endif // SEMANTICSTABLEIDENTITY_H
