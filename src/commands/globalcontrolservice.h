#ifndef GLOBALCONTROLSERVICE_H
#define GLOBALCONTROLSERVICE_H

#include "completiontypes.h"

#include <QString>
#include <QStringList>
#include <QList>
#include <QVariantMap>

enum class GlobalControlCategory {
    Symbols,
    Templates,
    Commands
};

enum class GlobalControlItemKind {
    Domain,
    Command,
    Symbol,
    Template
};

enum class GlobalControlItemOperation {
    InsertText,
    InsertPackageImport,
    InsertHeaderInclude,
    CreateHeader
};

struct GlobalControlQueryContext {
    bool editorAvailable = false;
    QString fileName;
    QString moduleName;
    QString packageName;
    QString documentText;
    QStringList includeFiles;
    QString initialQuery;
    QStringList memberPath;
    QString expectedTypeIdentifier;
    int cursorLine = -1;
    int cursorPosition = -1;
    int replacementStart = -1;
    int replacementLength = 0;
    int documentRevision = -1;
    bool memberAccess = false;
};

struct GlobalControlItem {
    GlobalControlItemKind kind = GlobalControlItemKind::Command;
    QString id;
    QString title;
    QString subtitle;
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
    GlobalControlItemOperation operation =
        GlobalControlItemOperation::InsertText;
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
