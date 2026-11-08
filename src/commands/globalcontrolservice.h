#ifndef GLOBALCONTROLSERVICE_H
#define GLOBALCONTROLSERVICE_H

#include "completiontypes.h"
#include "semanticindex.h"

#include <QString>
#include <QStringList>
#include <QList>
#include <QVariantMap>

enum class GlobalControlCategory {
    Symbols = 0,
    Commands = 2
};

enum class GlobalControlItemKind {
    Domain,
    Command,
    Symbol
};

struct GlobalControlQueryContext {
    bool editorAvailable = false;
    bool readOnly = false;
    QString templateParentId, templateParentSlot, templateContent;
    QString templateBoundaryError;
    QString fileName;
    QString moduleName;
    QString packageName;
    QString documentText;
    QString initialQuery;
    QStringList memberPath;
    QString expectedTypeIdentifier;
    int cursorLine = -1;
    int cursorPosition = -1;
    int replacementStart = -1;
    int replacementLength = 0;
    int documentRevision = -1;
    quint64 documentInstance = 0;
    quint64 documentTextRevision = 0;
    bool memberAccess = false;
};

struct GlobalControlItem {
    QString disabledReason;
    QString templateParentId, templateParentSlot, templateContent;
    bool requiresCurrentSemantic = false;
    SemanticSnapshotToken sourceSemanticSnapshot;
    GlobalControlItemKind kind = GlobalControlItemKind::Command;
    QString id;
    QString title;
    QString subtitle;
    QString typeBadge;
    QString actionId;
    QString executionRoute;
    QVariantMap parameters;
    QString insertionText;
    int selectionStart = -1;
    int selectionLength = 0;
    CodeTemplateSlotList templateSlots;
    int replacementStart = -1;
    int replacementLength = 0;
    int sourceDocumentRevision = -1;
    quint64 sourceDocumentInstance = 0;
    QString sourceFileName;
    int sourceCursorPosition = -1;
};

class GlobalControlService
{
public:
    QList<GlobalControlItem> query(
        GlobalControlCategory category,
        const QString& text,
        const GlobalControlQueryContext& context = {}) const;
    QList<GlobalControlItem> query(const QString& text) const;
};

#endif // GLOBALCONTROLSERVICE_H
