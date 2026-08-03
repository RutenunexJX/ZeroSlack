#ifndef UNSAVEDDOCUMENTMANAGER_H
#define UNSAVEDDOCUMENTMANAGER_H

#include <QList>
#include <QPointer>
#include <QString>

#include <functional>

class SharedDocument;
class QWidget;

enum class PendingDocumentChangeKind {
    Unsaved,
    ExternallyModified,
    Conflict
};

struct PendingDocumentChange {
    QPointer<SharedDocument> document;
    QString documentId;
    QString fileName;
    PendingDocumentChangeKind kind =
        PendingDocumentChangeKind::Unsaved;
    bool readOnly = false;
};

enum class UnsavedDocumentBatchDecision {
    SaveAll,
    DiscardAll,
    Cancel
};

class UnsavedDocumentManager
{
public:
    using DecisionProvider = std::function<
        UnsavedDocumentBatchDecision(
            const QList<PendingDocumentChange>& changes,
            QWidget* dialogParent)>;
    using SaveDocument =
        std::function<bool(SharedDocument* document)>;

    void setDecisionProvider(DecisionProvider provider);

    QList<PendingDocumentChange> collect(
        const QList<SharedDocument*>& documents) const;
    bool resolve(
        const QList<SharedDocument*>& documents,
        QWidget* dialogParent,
        const SaveDocument& saveDocument) const;

private:
    DecisionProvider decisionProvider;

    static UnsavedDocumentBatchDecision requestDecision(
        const QList<PendingDocumentChange>& changes,
        QWidget* dialogParent);
};

#endif // UNSAVEDDOCUMENTMANAGER_H
