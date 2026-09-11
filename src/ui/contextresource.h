#ifndef CONTEXTRESOURCE_H
#define CONTEXTRESOURCE_H

#include "zeroslackexport.h"

#include <QFlags>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QVariantMap>

enum class ContextPresentation : quint8 {
    Peek = 0x1,
    Pinned = 0x2,
    FullView = 0x4
};
Q_DECLARE_FLAGS(ContextPresentations, ContextPresentation)
Q_DECLARE_OPERATORS_FOR_FLAGS(ContextPresentations)

struct ZEROSLACK_API ContextViewCapabilities {
    ContextPresentations presentations =
        ContextPresentation::Peek
        | ContextPresentation::Pinned;
    int minimumWidth = 320;
    int preferredWidth = 520;
    int maximumWidth = 720;
    int minimumHeight = 220;
    int preferredHeight = 440;
    int maximumHeight = 920;

    QSize preferredSize() const
    {
        return QSize(preferredWidth, preferredHeight);
    }

    bool supports(ContextPresentation presentation) const
    {
        return presentations.testFlag(presentation);
    }
};

struct ZEROSLACK_API ContextResource {
    QString providerId;
    QString resourceId;
    QUrl uri;
    QString title;
    QString iconKey;
    QString workspaceId;
    QVariantMap state;

    bool isValid() const;
    QString stableKey() const;
    QVariantMap toVariantMap() const;

    static ContextResource fromVariantMap(
        const QVariantMap& map,
        QString* failureReason = nullptr);
};

ZEROSLACK_API bool operator==(
    const ContextResource& lhs,
    const ContextResource& rhs);
ZEROSLACK_API bool operator!=(
    const ContextResource& lhs,
    const ContextResource& rhs);

Q_DECLARE_METATYPE(ContextResource)

#endif // CONTEXTRESOURCE_H
