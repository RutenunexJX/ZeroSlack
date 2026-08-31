#ifndef WORKSPACEHUBTYPES_H
#define WORKSPACEHUBTYPES_H

#include "contextresource.h"
#include "suitecontextcatalog.h"
#include "zeroslackexport.h"

#include <QList>
#include <QString>
#include <QUrl>
#include <QVariantMap>

enum class WorkspaceHubItemState {
    Available,
    Stale,
    Warning,
    Missing,
    Unavailable
};

enum class WorkspaceHubPhase {
    Idle,
    Updating,
    Ready,
    Error
};

struct ZEROSLACK_API WorkspaceHubRequest {
    QString workspaceRoot;
    QString workspaceId;
    QString documentId;
    quint64 documentRevision = 0;
    quint64 semanticRevision = 0;
    QString filePath;
    QString documentText;
    QString moduleName;
    QString symbolName;
    int cursorPosition = -1;
    int line = 1;
    int column = 1;
    QList<SemanticSymbolRecord> semanticSymbols;
    bool semanticSymbolsSupplied = false;

    bool isValid() const;
    QString stableKey() const;
};

struct ZEROSLACK_API WorkspaceHubItem {
    QString stableKey;
    QString providerId;
    QString title;
    QString summary;
    QString iconKey;
    WorkspaceHubItemState state = WorkspaceHubItemState::Available;
    ContextResource contextResource;
    QUrl suiteUri;
    QVariantMap metadata;

    bool isValid() const;
};

struct ZEROSLACK_API WorkspaceHubSection {
    QString id;
    QString title;
    QString statusText;
    WorkspaceHubItemState state = WorkspaceHubItemState::Available;
    QList<WorkspaceHubItem> items;
};

struct ZEROSLACK_API WorkspaceHubSnapshot {
    quint64 generation = 0;
    QString requestKey;
    WorkspaceHubPhase phase = WorkspaceHubPhase::Idle;
    QString failureReason;
    bool stale = false;
    QList<WorkspaceHubSection> sections;

    bool isCurrent(quint64 requestedGeneration,
                   const QString& requestedKey) const;
};

ZEROSLACK_API QString workspaceHubStateId(
    WorkspaceHubItemState state);

Q_DECLARE_METATYPE(WorkspaceHubItem)
Q_DECLARE_METATYPE(WorkspaceHubSnapshot)

#endif // WORKSPACEHUBTYPES_H
