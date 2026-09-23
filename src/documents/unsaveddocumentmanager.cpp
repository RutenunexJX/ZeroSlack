#include "uidialogs.h"
#include "unsaveddocumentmanager.h"

#include "shareddocument.h"

#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace {
QString changeKindText(PendingDocumentChangeKind kind)
{
    switch (kind) {
    case PendingDocumentChangeKind::Unsaved:
        return QStringLiteral("unsaved local changes");
    case PendingDocumentChangeKind::ExternallyModified:
        return QStringLiteral("modified or removed externally");
    case PendingDocumentChangeKind::Conflict:
        return QStringLiteral("local and external changes conflict");
    }
    return QStringLiteral("pending changes");
}

QString displayName(const PendingDocumentChange& change)
{
    if (!change.fileName.isEmpty())
        return change.fileName;
    if (!change.documentId.isEmpty())
        return change.documentId;
    return QStringLiteral("untitled");
}
}

void UnsavedDocumentManager::setDecisionProvider(
    DecisionProvider provider)
{
    decisionProvider = std::move(provider);
}

QList<PendingDocumentChange>
UnsavedDocumentManager::collect(
    const QList<SharedDocument*>& documents) const
{
    QList<PendingDocumentChange> changes;
    QSet<SharedDocument*> seen;
    for (SharedDocument* document : documents) {
        if (!document || seen.contains(document))
            continue;
        seen.insert(document);

        PendingDocumentChangeKind kind;
        if (document->externalState()
                == SharedDocumentExternalState::Conflict) {
            kind = PendingDocumentChangeKind::Conflict;
        } else if (document->externalState()
                   == SharedDocumentExternalState::
                       ExternallyModified) {
            kind =
                PendingDocumentChangeKind::ExternallyModified;
        } else if (document->dirty()) {
            kind = PendingDocumentChangeKind::Unsaved;
        } else {
            continue;
        }

        PendingDocumentChange change;
        change.document = document;
        change.documentId = document->documentId();
        change.fileName = document->fileName();
        change.kind = kind;
        change.readOnly = document->readOnly();
        changes.append(change);
    }
    std::sort(
        changes.begin(),
        changes.end(),
        [](const PendingDocumentChange& left,
           const PendingDocumentChange& right) {
            const QString leftKey =
                left.fileName.isEmpty()
                ? left.documentId
                : left.fileName;
            const QString rightKey =
                right.fileName.isEmpty()
                ? right.documentId
                : right.fileName;
            return leftKey.compare(
                       rightKey,
                       Qt::CaseInsensitive)
                < 0;
        });
    return changes;
}

bool UnsavedDocumentManager::resolve(
    const QList<SharedDocument*>& documents,
    QWidget* dialogParent,
    const SaveDocument& saveDocument) const
{
    const QList<PendingDocumentChange> changes =
        collect(documents);
    if (changes.isEmpty())
        return true;

    const UnsavedDocumentBatchDecision decision =
        decisionProvider
        ? decisionProvider(changes, dialogParent)
        : requestDecision(changes, dialogParent);
    if (decision == UnsavedDocumentBatchDecision::Cancel)
        return false;
    if (decision
        == UnsavedDocumentBatchDecision::DiscardAll) {
        return true;
    }
    if (!saveDocument)
        return false;

    for (const PendingDocumentChange& change : changes) {
        SharedDocument* document = change.document.data();
        if (!document || !saveDocument(document))
            return false;
    }
    return true;
}

UnsavedDocumentBatchDecision
UnsavedDocumentManager::requestDecision(
    const QList<PendingDocumentChange>& changes,
    QWidget* dialogParent)
{
    UiMessageDialog dialog(dialogParent);
    dialog.setIcon(QMessageBox::Warning);
    dialog.setWindowTitle(
        QStringLiteral("Pending Documents"));

    bool hasConflict = false;
    bool hasExternalChange = false;
    QStringList details;
    for (const PendingDocumentChange& change : changes) {
        hasConflict =
            hasConflict
            || change.kind
                   == PendingDocumentChangeKind::Conflict;
        hasExternalChange =
            hasExternalChange
            || change.kind
                   == PendingDocumentChangeKind::
                       ExternallyModified;
        details.append(
            QStringLiteral("%1 — %2%3")
                .arg(displayName(change),
                     changeKindText(change.kind),
                     change.readOnly
                         ? QStringLiteral(", read-only")
                         : QString()));
    }

    QString message = QStringLiteral(
        "%1 document(s) require a close decision.")
                          .arg(changes.size());
    if (hasConflict) {
        message += QStringLiteral(
            "\nResolve each conflict with Keep Local, Reload External, "
            "or Save Local As before saving.");
    } else if (hasExternalChange) {
        message += QStringLiteral(
            "\nSaving will overwrite externally changed "
            "disk content.");
    }
    dialog.setText(message);
    dialog.setDetailedText(
        details.join(QLatin1Char('\n')));

    QPushButton* saveButton = UiDialogs::addButton(&dialog,
        QStringLiteral("Save All"),
        QMessageBox::AcceptRole);
    if (hasConflict) {
        saveButton->setText(
            QStringLiteral("Resolve Conflicts First"));
        saveButton->setEnabled(false);
    }
    QPushButton* discardButton = UiDialogs::addButton(&dialog,
        QStringLiteral("Discard and Close All"),
        QMessageBox::DestructiveRole);
    QPushButton* cancelButton = UiDialogs::addButton(&dialog,
        QMessageBox::Cancel);
    dialog.setDefaultButton(cancelButton);
    dialog.setEscapeButton(cancelButton);
    dialog.exec();

    const QAbstractButton* clicked = dialog.clickedButton();
    if (clicked == saveButton)
        return UnsavedDocumentBatchDecision::SaveAll;
    if (clicked == discardButton)
        return UnsavedDocumentBatchDecision::DiscardAll;
    return UnsavedDocumentBatchDecision::Cancel;
}
