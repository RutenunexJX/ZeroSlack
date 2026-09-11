#ifndef FOLDSHELFRESTORESERVICE_H
#define FOLDSHELFRESTORESERVICE_H

#include "foldblockshelfmodel.h"

#include <QString>

class MyCodeEditor;

enum class FoldShelfRestoreCompletion {
    ConsumeItem,
    RemoveItem
};

struct FoldShelfRestoreReport {
    bool success = false;
    bool itemAvailable = false;
    bool targetAvailable = false;
    bool inserted = false;
    bool itemConsumed = false;
    bool itemRemoved = false;
    bool staleMarked = false;
    QString failureReason;
    QString targetFile;
    int targetLine = -1;
    FoldShelfItem item;
};

class FoldShelfRestoreService
{
public:
    static FoldShelfRestoreReport restoreIntoEditor(
        FoldBlockShelfModel* model,
        MyCodeEditor* editor,
        const QString& itemId,
        int targetLine,
        FoldShelfRestoreCompletion completion);
};

#endif // FOLDSHELFRESTORESERVICE_H
