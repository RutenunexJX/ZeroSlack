#include "workspacehubsuitebridge.h"

#ifdef ZEROSLACK_CORE_HAS_SUITEAPP
#include <suiteapp/client.h>
#endif

namespace {

WorkspaceHubSuiteResult fromTransport(
#ifdef ZEROSLACK_CORE_HAS_SUITEAPP
    const SuiteApp::TransportResult& transport
#else
    const QJsonObject&
#endif
)
{
    WorkspaceHubSuiteResult result;
#ifdef ZEROSLACK_CORE_HAS_SUITEAPP
    if (!transport.hasResponse()) {
        result.errorCode = transport.errorCode.isEmpty()
            ? QStringLiteral("runtime_unavailable")
            : transport.errorCode;
        result.errorMessage = transport.errorMessage.isEmpty()
            ? QStringLiteral("Suite Runtime is unavailable.")
            : transport.errorMessage;
        return result;
    }
    const QJsonObject response = transport.response;
    result.ok = response.value(QStringLiteral("ok")).toBool();
    if (result.ok) {
        result.model = response.value(QStringLiteral("result")).toObject();
    } else {
        const QJsonObject error = response.value(
            QStringLiteral("error")).toObject();
        result.errorCode = error.value(QStringLiteral("code")).toString();
        result.errorMessage = error.value(
            QStringLiteral("message")).toString();
    }
#else
    result.errorCode = QStringLiteral("suiteapp_not_built");
    result.errorMessage = QStringLiteral(
        "This ZeroSlack build has no SuiteApp client support.");
#endif
    return result;
}

QString actionForProvider(const QString& provider)
{
    if (provider == QStringLiteral("wave"))
        return QStringLiteral("wave.project.open");
    if (provider == QStringLiteral("regmap"))
        return QStringLiteral("regmap.project.open");
    if (provider == QStringLiteral("pinloom"))
        return QStringLiteral("pinloom.entry.open");
    return {};
}

QString surfaceForProvider(const QString& provider)
{
    if (provider == QStringLiteral("wave"))
        return QStringLiteral("wave.waveform");
    if (provider == QStringLiteral("regmap"))
        return QStringLiteral("regmap.workbench");
    return {};
}

} // namespace

WorkspaceHubSuiteRegistry WorkspaceHubSuiteBridge::registry(
    int timeoutMs)
{
    WorkspaceHubSuiteRegistry result;
#ifdef ZEROSLACK_CORE_HAS_SUITEAPP
    SuiteApp::Client client(SuiteApp::defaultRuntimeEndpoint(),
                            qBound(100, timeoutMs, 3000));
    const SuiteApp::TransportResult transport = client.listProviders();
    if (!transport.hasResponse()) {
        result.errorCode = transport.errorCode.isEmpty()
            ? QStringLiteral("runtime_unavailable")
            : transport.errorCode;
        result.errorMessage = transport.errorMessage.isEmpty()
            ? QStringLiteral("Suite Runtime is unavailable.")
            : transport.errorMessage;
        return result;
    }
    const QJsonObject response = transport.response;
    if (!response.value(QStringLiteral("ok")).toBool()) {
        const QJsonObject error = response.value(
            QStringLiteral("error")).toObject();
        result.errorCode = error.value(QStringLiteral("code")).toString();
        result.errorMessage = error.value(
            QStringLiteral("message")).toString();
        return result;
    }
    for (const QJsonValue& value : response.value(
             QStringLiteral("result")).toObject().value(
             QStringLiteral("providers")).toArray()) {
        const QString appId = value.toObject().value(
            QStringLiteral("appId")).toString();
        if (!appId.isEmpty())
            result.providers.insert(appId);
    }
    result.ok = true;
#else
    Q_UNUSED(timeoutMs);
    result.errorCode = QStringLiteral("suiteapp_not_built");
    result.errorMessage = QStringLiteral(
        "This ZeroSlack build has no SuiteApp client support.");
#endif
    return result;
}

WorkspaceHubSuiteResult WorkspaceHubSuiteBridge::resolve(
    const WorkspaceHubItem& item,
    int timeoutMs)
{
    if (item.suiteUri.isEmpty() || !item.suiteUri.isValid()) {
        return {false, {}, QStringLiteral("invalid_resource"),
                QStringLiteral("A stable Suite resource URI is required.")};
    }
#ifdef ZEROSLACK_CORE_HAS_SUITEAPP
    SuiteApp::Client client(SuiteApp::defaultRuntimeEndpoint(),
                            qBound(100, timeoutMs, 10000));
    return fromTransport(client.resolveResource(
        item.suiteUri.toString(QUrl::FullyEncoded), item.providerId));
#else
    Q_UNUSED(timeoutMs);
    return fromTransport(QJsonObject{});
#endif
}

WorkspaceHubSuiteResult WorkspaceHubSuiteBridge::open(
    const WorkspaceHubItem& item,
    int timeoutMs)
{
    const QString action = actionForProvider(item.providerId);
    if (item.suiteUri.isEmpty() || !item.suiteUri.isValid()
        || action.isEmpty()) {
        return {false, {}, QStringLiteral("invalid_resource"),
                QStringLiteral("The selected item has no supported Suite action.")};
    }
#ifdef ZEROSLACK_CORE_HAS_SUITEAPP
    SuiteApp::Client client(SuiteApp::defaultRuntimeEndpoint(),
                            qBound(100, timeoutMs, 10000));
    return fromTransport(client.invokeAction(
        action, {}, item.suiteUri.toString(QUrl::FullyEncoded),
        item.providerId));
#else
    Q_UNUSED(timeoutMs);
    return fromTransport(QJsonObject{});
#endif
}

WorkspaceHubSuiteResult WorkspaceHubSuiteBridge::describeSurface(
    const WorkspaceHubItem& item,
    int timeoutMs)
{
    const QString surface = surfaceForProvider(item.providerId);
    if (item.suiteUri.isEmpty() || !item.suiteUri.isValid()
        || surface.isEmpty()) {
        return {false, {}, QStringLiteral("invalid_resource"),
                QStringLiteral("The selected item has no supported Suite surface.")};
    }
#ifdef ZEROSLACK_CORE_HAS_SUITEAPP
    SuiteApp::Client client(SuiteApp::defaultRuntimeEndpoint(),
                            qBound(100, timeoutMs, 10000));
    return fromTransport(client.describeSurface(
        surface, item.suiteUri.toString(QUrl::FullyEncoded),
        item.providerId));
#else
    Q_UNUSED(timeoutMs);
    return fromTransport(QJsonObject{});
#endif
}
