#ifndef SEMANTICSTABLEIDENTITY_H
#define SEMANTICSTABLEIDENTITY_H

#include <zeroslack/semantic/semanticapi.h>

#include <QString>

struct SemanticSymbolRecord;
struct SymbolStableKey;

// Exact declaration key, qualified by the declaring owner's source and scope.
// The owner position is excluded so relocation does not rename its members.
ZEROSLACK_SEMANTIC_API SymbolStableKey semanticDeclarationKey(const SemanticSymbolRecord& record);

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

// zsym-v2 includes the complete declaring-owner chain and workspace-relative
// source paths, excluding owner/source positions for named declarations.
// zexact-v1 remains the current SymbolStableKey hash; old zsym-v1 IDs are not
// aliases because they can refer to multiple declarations.
ZEROSLACK_SEMANTIC_API SemanticStableIdentity semanticStableIdentity(
    const SemanticSymbolRecord& record,
    const QString& workspaceRoot = QString());

ZEROSLACK_SEMANTIC_API QString semanticStableSymbolUri(
    const SemanticSymbolRecord& record,
    const QString& workspaceRoot = QString());

#endif // SEMANTICSTABLEIDENTITY_H
