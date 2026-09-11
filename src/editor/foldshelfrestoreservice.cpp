#include "foldshelfrestoreservice.h"

#include "mycodeeditor.h"

FoldShelfRestoreReport FoldShelfRestoreService::restoreIntoEditor(
    FoldBlockShelfModel* model,
    MyCodeEditor* editor,
    const QString& itemId,
    int targetLine,
    FoldShelfRestoreCompletion completion)
{
    FoldShelfRestoreReport report;
    report.targetLine = targetLine;

    if (!model) {
        report.failureReason = QStringLiteral("Fold Shelf model unavailable");
        return report;
    }

    report.item = model->item(itemId);
    report.itemAvailable =
        !report.item.id.isEmpty() && !report.item.text.isEmpty();
    if (!report.itemAvailable) {
        report.failureReason = QStringLiteral("Fold Shelf item unavailable");
        return report;
    }

    if (!editor) {
        report.staleMarked = model->markItemStale(itemId);
        report.failureReason = QStringLiteral("No active editor");
        return report;
    }

    report.targetAvailable = true;
    report.targetFile = editor->documentFileName();
    report.inserted =
        editor->insertFoldShelfItemAtLineForTest(report.item, targetLine);
    if (!report.inserted) {
        report.staleMarked = model->markItemStale(itemId);
        report.failureReason = QStringLiteral("Restore insertion failed");
        return report;
    }

    if (completion == FoldShelfRestoreCompletion::RemoveItem)
        report.itemRemoved = model->removeItem(itemId);
    else
        report.itemConsumed = model->consumeItem(itemId);

    report.success = report.itemRemoved || report.itemConsumed;
    if (!report.success)
        report.failureReason = QStringLiteral("Fold Shelf item update failed");
    return report;
}
