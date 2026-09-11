#include "workspacehubtypes.h"

#include <QCryptographicHash>
#include <QDir>

bool WorkspaceHubRequest::isValid() const
{
    return !workspaceRoot.trimmed().isEmpty();
}

QString WorkspaceHubRequest::stableKey() const
{
    const QString identity = QStringLiteral("%1\n%2\n%3\n%4\n%5\n%6\n%7\n%8")
        .arg(QDir::cleanPath(workspaceRoot), documentId, filePath,
             QString::number(documentRevision),
             QString::number(semanticRevision),
             QString::number(cursorPosition), symbolName, moduleName);
    return QString::fromLatin1(QCryptographicHash::hash(
        identity.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool WorkspaceHubItem::isValid() const
{
    return !stableKey.trimmed().isEmpty()
        && !title.trimmed().isEmpty()
        && (contextResource.isValid()
            || (!suiteUri.isEmpty() && suiteUri.isValid()));
}

bool WorkspaceHubSnapshot::isCurrent(
    quint64 requestedGeneration,
    const QString& requestedKey) const
{
    return generation == requestedGeneration
        && requestKey == requestedKey;
}

QString workspaceHubStateId(WorkspaceHubItemState state)
{
    switch (state) {
    case WorkspaceHubItemState::Available:
        return QStringLiteral("available");
    case WorkspaceHubItemState::Stale:
        return QStringLiteral("stale");
    case WorkspaceHubItemState::Warning:
        return QStringLiteral("warning");
    case WorkspaceHubItemState::Missing:
        return QStringLiteral("missing");
    case WorkspaceHubItemState::Unavailable:
        return QStringLiteral("unavailable");
    }
    return {};
}
