#include "simdocksuitecompatibility.h"
#include "../../simulation/simdock/core/workspace.h"
#include <QCryptographicHash>
#include <suiteapp/provider.h>
#include <suiteapp/runtime.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>

SimDockSuiteCompatibility::SimDockSuiteCompatibility(OpenTarget open, QObject* parent)
    : QObject(parent), openTarget(std::move(open)) {}
SimDockSuiteCompatibility::~SimDockSuiteCompatibility() = default;

QJsonObject SimDockSuiteCompatibility::descriptor(const QString& endpoint)
{
    QJsonArray actions;
    for (const auto& id : {QStringLiteral("simdock.workspace.open"), QStringLiteral("simdock.project.open")})
        actions << QJsonObject{{QStringLiteral("id"), id},
            {QStringLiteral("resourceSchemes"), QJsonArray{QStringLiteral("simdock")}},
            {QStringLiteral("sideEffect"), QStringLiteral("ui")},
            {QStringLiteral("requiresUi"), true}};
    return {{QStringLiteral("appId"), QStringLiteral("simdock")},
        {QStringLiteral("displayName"), QStringLiteral("SimDock")},
        {QStringLiteral("version"), QStringLiteral("0.6.1")},
        {QStringLiteral("processId"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("endpoint"), endpoint.isEmpty() ? SuiteApp::endpointForApp(QStringLiteral("simdock")) : endpoint},
        {QStringLiteral("protocols"), QJsonArray{QString::fromLatin1(SuiteApp::kProtocol)}},
        {QStringLiteral("resourceSchemes"), QJsonArray{QStringLiteral("simdock")}},
        {QStringLiteral("actions"), actions},
        {QStringLiteral("surfaces"), QJsonArray{QJsonObject{
            {QStringLiteral("id"), QStringLiteral("simdock.workbench")},
            {QStringLiteral("mode"), QStringLiteral("external")},
            {QStringLiteral("fallback"), QStringLiteral("external")}}}}};
}

bool SimDockSuiteCompatibility::start(QString* error, const SuiteApp::RuntimeStartOptions& options)
{
    if (runtimeEndpoint == options.endpoint && isRegistered()) return true;
    const auto runtime = SuiteApp::ensureRuntime(options);
    if (!runtime.available) {
        if (error) *error = runtime.errorMessage;
        return false;
    }
    if (provider && runtimeEndpoint != options.endpoint) provider.reset();
    if (!provider) {
        const auto suffix = QString::fromLatin1(QCryptographicHash::hash(options.endpoint.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
        const auto endpoint = SuiteApp::endpointForApp(QStringLiteral("simdock")) + QLatin1Char('.') + suffix;
        provider = std::make_unique<SuiteApp::Provider>(descriptor(endpoint),
            [this](const QJsonObject& request) { return handle(request); }, this);
    }
    if (!provider->start(options.endpoint, error)) return false;
    runtimeEndpoint = options.endpoint;
    return true;
}

bool SimDockSuiteCompatibility::isRegistered() const
{
    if (!provider || !provider->isListening() || runtimeEndpoint.isEmpty()) return false;
    const auto listed = SuiteApp::Client(runtimeEndpoint, 250).listProviders();
    if (!listed.hasResponse() || !listed.response.value(QStringLiteral("ok")).toBool()) return false;
    const auto expected = provider->descriptor();
    for (const auto& value : listed.response.value(QStringLiteral("result")).toObject().value(QStringLiteral("providers")).toArray()) {
        const auto registered = value.toObject();
        if (SuiteApp::descriptorAppId(registered) == QStringLiteral("simdock")
            && SuiteApp::descriptorEndpoint(registered) == SuiteApp::descriptorEndpoint(expected)) return true;
    }
    return false;
}

QJsonObject SimDockSuiteCompatibility::handle(const QJsonObject& request)
{
    const auto method = request.value(QStringLiteral("method")).toString();
    const auto params = request.value(QStringLiteral("params")).toObject();
    const auto fail = [&](const QString& code, const QString& message) {
        return SuiteApp::errorResponse(request, code, message);
    };
    if (method != QStringLiteral("resource.resolve") && method != QStringLiteral("action.invoke")
        && method != QStringLiteral("surface.describe") && method != QStringLiteral("surface.open"))
        return fail(QStringLiteral("method_not_supported"), QStringLiteral("Unknown SimDock provider method."));
    QString uri = params.value(QStringLiteral("resourceUri")).toString();
    if (uri.isEmpty()) uri = params.value(QStringLiteral("uri")).toString();
    const QUrl url(uri, QUrl::StrictMode);
    const bool project = url.host() == QStringLiteral("project");
    if (!url.isValid() || url.scheme() != QStringLiteral("simdock")
        || (!project && url.host() != QStringLiteral("workspace"))
        || (!url.path().isEmpty() && url.path() != QStringLiteral("/"))
        || !url.userInfo().isEmpty() || url.port() != -1 || url.hasFragment())
        return fail(QStringLiteral("invalid_resource"), QStringLiteral("A simdock://workspace or simdock://project URI is required."));
    const QUrlQuery query(url);
    const auto path = query.queryItemValue(project ? QStringLiteral("workspace") : QStringLiteral("path"), QUrl::FullyDecoded);
    if (!QDir::isAbsolutePath(path) || !QFileInfo(path).isDir())
        return fail(QStringLiteral("workspace_not_found"), QStringLiteral("The workspace must be an existing absolute directory."));
    const auto root = QFileInfo(path).canonicalFilePath();
    QString projectId;
    QJsonObject resource{{QStringLiteral("appId"), QStringLiteral("simdock")},
        {QStringLiteral("uri"), uri}, {QStringLiteral("workspaceRoot"), root},
        {QStringLiteral("kind"), project ? QStringLiteral("project") : QStringLiteral("workspace")},
        {QStringLiteral("title"), QFileInfo(root).fileName()}};
    if (project) {
        projectId = query.queryItemValue(QStringLiteral("id"), QUrl::FullyDecoded);
        bool found = false;
        for (const auto& p : simdock::loadProjects(root)) {
            if (p.id != projectId) continue;
            found = true;
            resource.insert(QStringLiteral("title"), p.name);
            resource.insert(QStringLiteral("project"), simdock::projectJson(p));
            break;
        }
        if (!found) return fail(QStringLiteral("project_not_found"), QStringLiteral("The simulation project does not exist in this workspace."));
    }
    if (method == QStringLiteral("resource.resolve")) return SuiteApp::successResponse(request, resource);
    if (method == QStringLiteral("action.invoke")) {
        const QString expected = project ? QStringLiteral("simdock.project.open") : QStringLiteral("simdock.workspace.open");
        if (params.value(QStringLiteral("actionId")).toString() != expected)
            return fail(QStringLiteral("action_not_supported"), QStringLiteral("The action does not match this SimDock resource."));
    } else {
        if (params.value(QStringLiteral("surfaceId")).toString() != QStringLiteral("simdock.workbench"))
            return fail(QStringLiteral("surface_not_supported"), QStringLiteral("Unknown SimDock surface."));
        if (method == QStringLiteral("surface.describe"))
            return SuiteApp::successResponse(request, {{QStringLiteral("surfaceId"), QStringLiteral("simdock.workbench")},
                {QStringLiteral("mode"), QStringLiteral("external")}, {QStringLiteral("fallback"), QStringLiteral("external")},
                {QStringLiteral("resource"), resource}});
    }
    QString error;
    if (!openTarget || !openTarget(root, projectId, &error))
        return fail(QStringLiteral("open_failed"), error.isEmpty() ? QStringLiteral("The ZeroSlack simulation workbench is unavailable.") : error);
    return SuiteApp::successResponse(request, {{QStringLiteral("opened"), true}, {QStringLiteral("resource"), resource}});
}
