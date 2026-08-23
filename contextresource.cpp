#include "contextresource.h"

namespace {
const QString kSchema = QStringLiteral("zeroslack-context-resource/v1");
}

bool ContextResource::isValid() const
{
    return !providerId.trimmed().isEmpty()
        && (!resourceId.trimmed().isEmpty()
            || (!uri.isEmpty() && uri.isValid()));
}

QString ContextResource::stableKey() const
{
    const QString normalizedProvider = providerId.trimmed();
    const QString normalizedResource = resourceId.trimmed();
    if (!normalizedProvider.isEmpty()
        && !normalizedResource.isEmpty()) {
        return normalizedProvider + QLatin1Char(':')
            + normalizedResource;
    }
    if (!normalizedProvider.isEmpty() && uri.isValid()) {
        return normalizedProvider + QLatin1Char(':')
            + uri.toString(QUrl::FullyEncoded);
    }
    return {};
}

QVariantMap ContextResource::toVariantMap() const
{
    QVariantMap result;
    result.insert(QStringLiteral("schema"), kSchema);
    result.insert(QStringLiteral("providerId"), providerId);
    result.insert(QStringLiteral("resourceId"), resourceId);
    result.insert(QStringLiteral("uri"),
                  uri.toString(QUrl::FullyEncoded));
    result.insert(QStringLiteral("title"), title);
    result.insert(QStringLiteral("iconKey"), iconKey);
    result.insert(QStringLiteral("workspaceId"), workspaceId);
    result.insert(QStringLiteral("state"), state);
    return result;
}

ContextResource ContextResource::fromVariantMap(
    const QVariantMap& map,
    QString* failureReason)
{
    ContextResource result;
    if (failureReason)
        failureReason->clear();

    const QString schema =
        map.value(QStringLiteral("schema")).toString();
    if (!schema.isEmpty() && schema != kSchema) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Unsupported context-resource schema: %1")
                                 .arg(schema);
        }
        return result;
    }

    result.providerId =
        map.value(QStringLiteral("providerId")).toString().trimmed();
    result.resourceId =
        map.value(QStringLiteral("resourceId")).toString().trimmed();
    result.uri = QUrl(
        map.value(QStringLiteral("uri")).toString(),
        QUrl::StrictMode);
    result.title = map.value(QStringLiteral("title")).toString();
    result.iconKey = map.value(QStringLiteral("iconKey")).toString();
    result.workspaceId =
        map.value(QStringLiteral("workspaceId")).toString();
    result.state = map.value(QStringLiteral("state")).toMap();

    if (!result.isValid()) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Context resource requires a provider and resource identity.");
        }
        return {};
    }
    return result;
}

bool operator==(const ContextResource& lhs,
                const ContextResource& rhs)
{
    return lhs.providerId == rhs.providerId
        && lhs.resourceId == rhs.resourceId
        && lhs.uri == rhs.uri
        && lhs.title == rhs.title
        && lhs.iconKey == rhs.iconKey
        && lhs.workspaceId == rhs.workspaceId
        && lhs.state == rhs.state;
}

bool operator!=(const ContextResource& lhs,
                const ContextResource& rhs)
{
    return !(lhs == rhs);
}
