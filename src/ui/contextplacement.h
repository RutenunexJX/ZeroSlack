#ifndef CONTEXTPLACEMENT_H
#define CONTEXTPLACEMENT_H

#include <QMetaType>
#include <QString>

enum class ContextSurface { Docked, Floating };
enum class ContextPersistence { Transient, Kept };
enum class ContextBinding { Global, DocumentBound };

struct ContextPlacement {
    ContextSurface surface = ContextSurface::Docked;
    ContextPersistence persistence = ContextPersistence::Transient;
    ContextBinding binding = ContextBinding::Global;

    bool operator==(const ContextPlacement& other) const
    {
        return surface == other.surface && persistence == other.persistence
            && binding == other.binding;
    }
    bool operator!=(const ContextPlacement& other) const
    {
        return !(*this == other);
    }
};

inline QString contextPlacementName(const ContextPlacement& placement)
{
    return QStringLiteral("%1/%2/%3")
        .arg(placement.surface == ContextSurface::Floating
                 ? QStringLiteral("Floating") : QStringLiteral("Docked"),
             placement.persistence == ContextPersistence::Kept
                 ? QStringLiteral("Kept") : QStringLiteral("Transient"),
             placement.binding == ContextBinding::DocumentBound
                 ? QStringLiteral("DocumentBound") : QStringLiteral("Global"));
}

Q_DECLARE_METATYPE(ContextPlacement)

#endif // CONTEXTPLACEMENT_H
