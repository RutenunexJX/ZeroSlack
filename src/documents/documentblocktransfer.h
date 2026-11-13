#pragma once
#include "zeroslackexport.h"
#include <QPointer>
#include <QString>
#include <QTextCursor>
#include <QJsonObject>

class SharedDocument;

struct BlockTransferSource {
    QPointer<SharedDocument> document;
    QString identity, blockId;
    quint64 instance=0, textRevision=0, associationRevision=0;
};

// A transfer is an edit of two live documents, not two independent remove/add
// actions. Immutable assets are validated before either history is published.
class ZEROSLACK_API DocumentBlockTransfer {
public:
    enum class Mode { Move, Copy };
    static BlockTransferSource capture(SharedDocument* document, const QString& id);
    static bool current(const BlockTransferSource& source);
    static bool validateParticipant(SharedDocument* document, bool write, QString* error);
    static QString apply(const BlockTransferSource& source, SharedDocument* target,
        const QTextCursor& destination, Mode mode, const QString& beforeId, QString* error);
    static QJsonObject snapshot(SharedDocument* source, const QString& id, QString* error);
    static QString paste(const QJsonObject& snapshot, SharedDocument* target,
        const QTextCursor& destination, const QString& beforeId, QString* error);
};
