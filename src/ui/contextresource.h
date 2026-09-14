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
    // Live source editors retain the overlay until multi-window editing is supported.
    bool detachable = true;
    ContextPresentations presentations =
        ContextPresentation::Peek
        | ContextPresentation::Pinned;
    int minimumWidth = 320;
    int preferredWidth = 520;
    int maximumWidth = 720;
    int minimumHeight = 220;
    int preferredHeight = 440;
    int maximumHeight = 920;
    // Initial height of this resource's sidebar section. 0 keeps the dock's
    // own behaviour of splitting the viewport between expanded sections; a
    // positive value is a suggested starting height, never a floor: the user
    // can still drag the section smaller and that height is what persists.
    // Deliberately separate from preferredHeight, which sizes the preview
    // overlay and floating windows through preferredSize().
    int preferredSectionHeight = 0;

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
