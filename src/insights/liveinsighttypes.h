#ifndef LIVEINSIGHTTYPES_H
#define LIVEINSIGHTTYPES_H

#include <QMetaType>
#include <QString>
#include <QVariantMap>

enum class LiveInsightKind : quint8 {
    Module = 0,
    State = 1,
    Hotspot = 2,
    Kernel = 4
};

enum class LiveInsightPhase : quint8 {
    Empty,
    Debouncing,
    Building,
    Ready,
    HiddenDirty,
    Error
};

inline QString liveInsightKindId(LiveInsightKind kind)
{
    switch (kind) {
    case LiveInsightKind::Kernel:
        return QStringLiteral("kernel");
    case LiveInsightKind::Module:
        return QStringLiteral("block");
    case LiveInsightKind::State:
        return QStringLiteral("state");
    case LiveInsightKind::Hotspot:
        return QStringLiteral("hotspot");
    }
    return {};
}

inline QString liveInsightKindDisplayName(LiveInsightKind kind)
{
    switch (kind) {
    case LiveInsightKind::Kernel:
        return QStringLiteral("Kernel");
    case LiveInsightKind::Module:
        return QStringLiteral("Block");
    case LiveInsightKind::State:
        return QStringLiteral("State");
    case LiveInsightKind::Hotspot:
        return QStringLiteral("Hotspot");
    }
    return {};
}

inline bool liveInsightKindFromId(const QString& id,
                                  LiveInsightKind* kind)
{
    const QString normalized = id.trimmed().toLower();
    LiveInsightKind parsed = LiveInsightKind::Module;
    if (normalized == QStringLiteral("kernel"))
        parsed = LiveInsightKind::Kernel;
    else if (normalized == QStringLiteral("block")
             || normalized == QStringLiteral("module"))
        parsed = LiveInsightKind::Module;
    else if (normalized == QStringLiteral("state"))
        parsed = LiveInsightKind::State;
    else if (normalized == QStringLiteral("hotspot"))
        parsed = LiveInsightKind::Hotspot;
    else
        return false;
    if (kind)
        *kind = parsed;
    return true;
}

inline QString liveInsightRequestSchema()
{
    return QStringLiteral("zeroslack-live-insight-request/v1");
}

inline QString liveInsightResultSchema()
{
    return QStringLiteral("zeroslack-live-insight-result/v1");
}

struct LiveInsightRequestKey {
    LiveInsightKind kind = LiveInsightKind::Module;
    QString workspaceId;
    QString documentId;
    quint64 documentRevision = 0;
    quint64 semanticRevision = 0;
    QString contextKey;

    bool isValid() const
    {
        return !workspaceId.trimmed().isEmpty()
            && !documentId.trimmed().isEmpty()
            && !contextKey.trimmed().isEmpty();
    }

    QVariantMap toVariantMap() const
    {
        return {
            {QStringLiteral("kind"), liveInsightKindId(kind)},
            {QStringLiteral("workspaceId"), workspaceId},
            {QStringLiteral("documentId"), documentId},
            {QStringLiteral("documentRevision"),
             QVariant::fromValue<qulonglong>(documentRevision)},
            {QStringLiteral("semanticRevision"),
             QVariant::fromValue<qulonglong>(semanticRevision)},
            {QStringLiteral("contextKey"), contextKey}
        };
    }

    static LiveInsightRequestKey fromVariantMap(
        const QVariantMap& map,
        bool* valid = nullptr)
    {
        LiveInsightRequestKey result;
        LiveInsightKind parsedKind = LiveInsightKind::Module;
        const bool kindValid = liveInsightKindFromId(
            map.value(QStringLiteral("kind")).toString(),
            &parsedKind);
        result.kind = parsedKind;
        result.workspaceId =
            map.value(QStringLiteral("workspaceId")).toString();
        result.documentId =
            map.value(QStringLiteral("documentId")).toString();
        result.documentRevision =
            map.value(QStringLiteral("documentRevision")).toULongLong();
        result.semanticRevision =
            map.value(QStringLiteral("semanticRevision")).toULongLong();
        result.contextKey =
            map.value(QStringLiteral("contextKey")).toString();
        if (valid)
            *valid = kindValid && result.isValid();
        return result;
    }
};

inline bool operator==(const LiveInsightRequestKey& lhs,
                       const LiveInsightRequestKey& rhs)
{
    return lhs.kind == rhs.kind
        && lhs.workspaceId == rhs.workspaceId
        && lhs.documentId == rhs.documentId
        && lhs.documentRevision == rhs.documentRevision
        && lhs.semanticRevision == rhs.semanticRevision
        && lhs.contextKey == rhs.contextKey;
}

inline bool operator!=(const LiveInsightRequestKey& lhs,
                       const LiveInsightRequestKey& rhs)
{
    return !(lhs == rhs);
}

struct LiveInsightBuildRequest {
    QString schema = liveInsightRequestSchema();
    LiveInsightRequestKey key;
    quint64 generation = 0;
    QVariantMap input;

    bool isWellFormed() const
    {
        return schema == liveInsightRequestSchema()
            && key.isValid()
            && generation > 0;
    }
};

struct LiveInsightBuildResult {
    QString schema;
    LiveInsightRequestKey key;
    quint64 generation = 0;
    QVariantMap payload;
    QString errorText;
    bool succeeded = false;
    bool cancelled = false;

    static LiveInsightBuildResult success(
        const LiveInsightBuildRequest& request,
        const QVariantMap& payload)
    {
        LiveInsightBuildResult result;
        result.schema = liveInsightResultSchema();
        result.key = request.key;
        result.generation = request.generation;
        result.payload = payload;
        result.succeeded = true;
        return result;
    }

    static LiveInsightBuildResult failure(
        const LiveInsightBuildRequest& request,
        const QString& errorText)
    {
        LiveInsightBuildResult result;
        result.schema = liveInsightResultSchema();
        result.key = request.key;
        result.generation = request.generation;
        result.errorText = errorText.trimmed().isEmpty()
            ? QStringLiteral("Live Insight build failed.")
            : errorText;
        return result;
    }

    static LiveInsightBuildResult cancellation(
        const LiveInsightBuildRequest& request)
    {
        LiveInsightBuildResult result;
        result.schema = liveInsightResultSchema();
        result.key = request.key;
        result.generation = request.generation;
        result.cancelled = true;
        return result;
    }

    bool isWellFormedFor(
        const LiveInsightBuildRequest& request) const
    {
        if (schema != liveInsightResultSchema()
            || !request.isWellFormed()
            || key != request.key
            || generation != request.generation) {
            return false;
        }
        if (cancelled)
            return !succeeded && errorText.isEmpty();
        if (succeeded)
            return errorText.isEmpty();
        return !errorText.trimmed().isEmpty();
    }
};

struct LiveInsightSnapshot {
    LiveInsightKind kind = LiveInsightKind::Module;
    LiveInsightPhase phase = LiveInsightPhase::Empty;
    LiveInsightRequestKey requestedKey;
    LiveInsightRequestKey publishedKey;
    quint64 requestedGeneration = 0;
    quint64 publishedGeneration = 0;
    QVariantMap payload;
    QString errorText;
    bool hasLastValid = false;
    bool stale = false;
    bool dirty = false;
    bool visible = false;

    bool isUpdating() const
    {
        return phase == LiveInsightPhase::Debouncing
            || phase == LiveInsightPhase::Building;
    }
};

Q_DECLARE_METATYPE(LiveInsightKind)
Q_DECLARE_METATYPE(LiveInsightPhase)
Q_DECLARE_METATYPE(LiveInsightRequestKey)
Q_DECLARE_METATYPE(LiveInsightBuildRequest)
Q_DECLARE_METATYPE(LiveInsightBuildResult)
Q_DECLARE_METATYPE(LiveInsightSnapshot)

#endif // LIVEINSIGHTTYPES_H
