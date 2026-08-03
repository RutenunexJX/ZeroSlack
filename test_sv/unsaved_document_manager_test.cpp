#include "shareddocument.h"
#include "unsaveddocumentmanager.h"

#include <QApplication>
#include <QList>
#include <QTextCursor>
#include <QTextDocument>

#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* name, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n",
                value ? "PASS" : "FAIL",
                name);
}

void makeDirty(SharedDocument* document)
{
    QTextCursor cursor(document->textDocument());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("changed"));
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);

    SharedDocument clean(
        QStringLiteral("clean"),
        QStringLiteral("c:/workspace/clean.sv"),
        QStringLiteral("module clean; endmodule\n"));
    SharedDocument dirty(
        QStringLiteral("dirty"),
        QStringLiteral("c:/workspace/b_dirty.sv"),
        QStringLiteral("module dirty; endmodule\n"));
    SharedDocument external(
        QStringLiteral("external"),
        QStringLiteral("c:/workspace/a_external.sv"),
        QStringLiteral("module external; endmodule\n"));
    SharedDocument conflict(
        QStringLiteral("conflict"),
        QStringLiteral("c:/workspace/c_conflict.sv"),
        QStringLiteral("module conflict; endmodule\n"));

    makeDirty(&dirty);
    external.setExternalState(
        SharedDocumentExternalState::ExternallyModified);
    makeDirty(&conflict);
    conflict.setExternalState(
        SharedDocumentExternalState::Conflict);
    conflict.setReadOnly(true);

    UnsavedDocumentManager manager;
    const QList<PendingDocumentChange> collected =
        manager.collect(
            {&clean, &dirty, &external, &conflict, &dirty});
    expect("collects each pending shared document once",
           collected.size() == 3);
    expect("orders the centralized review deterministically",
           collected.size() == 3
               && collected.at(0).document.data()
                      == &external
               && collected.at(1).document.data()
                      == &dirty
               && collected.at(2).document.data()
                      == &conflict);
    expect("classifies dirty, external, and conflict state",
           collected.size() == 3
               && collected.at(0).kind
                      == PendingDocumentChangeKind::
                          ExternallyModified
               && collected.at(1).kind
                      == PendingDocumentChangeKind::Unsaved
               && collected.at(2).kind
                      == PendingDocumentChangeKind::Conflict
               && collected.at(2).readOnly);

    int providerCalls = 0;
    int saveCalls = 0;
    manager.setDecisionProvider(
        [&providerCalls](
            const QList<PendingDocumentChange>& changes,
            QWidget*) {
            ++providerCalls;
            return changes.size() == 3
                ? UnsavedDocumentBatchDecision::Cancel
                : UnsavedDocumentBatchDecision::DiscardAll;
        });
    expect("cancel is atomic and performs no save",
           !manager.resolve(
               {&clean, &dirty, &external, &conflict},
               nullptr,
               [&saveCalls](SharedDocument*) {
                   ++saveCalls;
                   return true;
               })
               && providerCalls == 1
               && saveCalls == 0);

    manager.setDecisionProvider(
        [](const QList<PendingDocumentChange>&, QWidget*) {
            return UnsavedDocumentBatchDecision::DiscardAll;
        });
    expect("discard resolves the whole batch without writes",
           manager.resolve(
               {&dirty, &external, &conflict},
               nullptr,
               [&saveCalls](SharedDocument*) {
                   ++saveCalls;
                   return true;
               })
               && saveCalls == 0);

    QList<SharedDocument*> savedDocuments;
    manager.setDecisionProvider(
        [](const QList<PendingDocumentChange>&, QWidget*) {
            return UnsavedDocumentBatchDecision::SaveAll;
        });
    expect("save decision visits every pending document once",
           manager.resolve(
               {&conflict, &dirty, &external, &dirty},
               nullptr,
               [&savedDocuments](SharedDocument* document) {
                   savedDocuments.append(document);
                   return true;
               })
               && savedDocuments
                      == QList<SharedDocument*>{
                          &external, &dirty, &conflict});

    manager.setDecisionProvider(
        [](const QList<PendingDocumentChange>&, QWidget*) {
            return UnsavedDocumentBatchDecision::SaveAll;
        });
    int failedSaveCalls = 0;
    expect("save failure prevents close",
           !manager.resolve(
               {&dirty},
               nullptr,
               [&failedSaveCalls](SharedDocument*) {
                   ++failedSaveCalls;
                   return false;
               })
               && failedSaveCalls == 1);

    bool unexpectedPrompt = false;
    manager.setDecisionProvider(
        [&unexpectedPrompt](
            const QList<PendingDocumentChange>&,
            QWidget*) {
            unexpectedPrompt = true;
            return UnsavedDocumentBatchDecision::Cancel;
        });
    expect("clean documents close without prompting",
           manager.resolve(
               {&clean},
               nullptr,
               [](SharedDocument*) { return false; })
               && !unexpectedPrompt);

    std::printf("%d checks, %d failures\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
