#include "suiteappintegration.h"

#include "mainwindow.h"
#include "tabmanager.h"

#include <suiteapp/protocol.h>
#include <suiteapp/provider.h>
#include <suiteapp/runtime.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTextStream>
#include <QUrl>
#include <QUrlQuery>

namespace {

constexpr auto kRevealAction = "zeroslack.source.reveal";
constexpr auto kPreviewSurface = "zeroslack.source.preview";

struct SourceTarget {
    QString uri;
    QString filePath;
    int line = 1;
    int column = 1;

    bool isValid() const
    {
        return !filePath.trimmed().isEmpty();
    }
};

SourceTarget sourceTarget(const QJsonObject& params)
{
    SourceTarget target;
    target.uri = params.value(QStringLiteral("resourceUri")).toString();
    if (target.uri.isEmpty())
        target.uri = params.value(QStringLiteral("uri")).toString();

    const QJsonObject arguments =
        params.value(QStringLiteral("arguments")).toObject();
    if (!target.uri.isEmpty()) {
        const QUrl url(target.uri, QUrl::StrictMode);
        if (url.isValid()
            && url.scheme().compare(QStringLiteral("zeroslack"),
                                    Qt::CaseInsensitive) == 0
            && url.host().compare(QStringLiteral("source"),
                                  Qt::CaseInsensitive) == 0) {
            const QUrlQuery query(url);
            target.filePath = query.queryItemValue(
                QStringLiteral("file"), QUrl::FullyDecoded);
            target.line = qMax(1, query.queryItemValue(
                QStringLiteral("line")).toInt());
            target.column = qMax(1, query.queryItemValue(
                QStringLiteral("column")).toInt());
        }
    }
    if (!arguments.value(QStringLiteral("filePath")).toString().isEmpty())
        target.filePath = arguments.value(QStringLiteral("filePath")).toString();
    if (arguments.value(QStringLiteral("line")).isDouble())
        target.line = qMax(1, arguments.value(QStringLiteral("line")).toInt());
    if (arguments.value(QStringLiteral("column")).isDouble())
        target.column = qMax(1, arguments.value(QStringLiteral("column")).toInt());
    if (!target.filePath.isEmpty())
        target.filePath = QFileInfo(target.filePath).absoluteFilePath();
    return target;
}

QString sourceSnippet(MainWindow* window, const SourceTarget& target)
{
    QString text;
    if (window && window->tabManager)
        text = window->tabManager->getPlainTextFromOpenFile(target.filePath);
    if (text.isEmpty()) {
        QFile file(target.filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            text = stream.readAll();
        }
    }
    if (text.isEmpty())
        return {};
    const QStringList lines = text.split(QLatin1Char('\n'));
    const int first = qMax(0, target.line - 4);
    const int last = qMin(lines.size(), target.line + 3);
    QStringList snippet;
    for (int index = first; index < last; ++index)
        snippet.append(lines.at(index));
    return snippet.join(QLatin1Char('\n'));
}

QJsonObject resolvedSource(MainWindow* window, const SourceTarget& target)
{
    return {
        {QStringLiteral("appId"), QStringLiteral("zeroslack")},
        {QStringLiteral("uri"), target.uri},
        {QStringLiteral("kind"), QStringLiteral("source")},
        {QStringLiteral("filePath"), target.filePath},
        {QStringLiteral("title"), QFileInfo(target.filePath).fileName()},
        {QStringLiteral("line"), target.line},
        {QStringLiteral("column"), target.column},
        {QStringLiteral("exists"), QFileInfo::exists(target.filePath)},
        {QStringLiteral("snippet"), sourceSnippet(window, target)},
    };
}

} // namespace

ZeroSlackSuiteIntegration::ZeroSlackSuiteIntegration(MainWindow* windowValue,
                                                     QObject* parent)
    : QObject(parent)
    , window(windowValue)
{
}

ZeroSlackSuiteIntegration::~ZeroSlackSuiteIntegration() = default;

bool ZeroSlackSuiteIntegration::start(QString* failureReason)
{
    if (provider && provider->isListening())
        return true;
    SuiteApp::RuntimeStartOptions runtimeOptions;
    const SuiteApp::RuntimeStatus runtime =
        SuiteApp::ensureRuntime(runtimeOptions);
    if (!runtime.available) {
        if (failureReason)
            *failureReason = runtime.errorMessage;
        return false;
    }
    provider = std::make_unique<SuiteApp::Provider>(
        appDescriptor(QCoreApplication::applicationVersion()),
        [this](const QJsonObject& request) {
            return processRequest(request);
        },
        this);
    return provider->start(runtimeOptions.endpoint, failureReason);
}

bool ZeroSlackSuiteIntegration::isRegistered() const
{
    return provider && provider->isListening();
}

QJsonObject ZeroSlackSuiteIntegration::appDescriptor(
    const QString& version,
    const QString& endpoint)
{
    return {
        {QStringLiteral("appId"), QStringLiteral("zeroslack")},
        {QStringLiteral("displayName"), QStringLiteral("ZeroSlack")},
        {QStringLiteral("version"), version.isEmpty()
             ? QStringLiteral("0.0.0") : version},
        {QStringLiteral("processId"),
         static_cast<double>(QCoreApplication::applicationPid())},
        {QStringLiteral("endpoint"), endpoint.isEmpty()
             ? SuiteApp::endpointForApp(QStringLiteral("zeroslack"))
             : endpoint},
        {QStringLiteral("protocols"),
         QJsonArray{QString::fromLatin1(SuiteApp::kProtocol)}},
        {QStringLiteral("resourceSchemes"),
         QJsonArray{QStringLiteral("zeroslack")}},
        {QStringLiteral("actions"),
         QJsonArray{QJsonObject{
             {QStringLiteral("id"), QString::fromLatin1(kRevealAction)},
             {QStringLiteral("resourceSchemes"),
              QJsonArray{QStringLiteral("zeroslack")}},
             {QStringLiteral("sideEffect"), QStringLiteral("ui")}}}},
        {QStringLiteral("surfaces"),
         QJsonArray{QJsonObject{
             {QStringLiteral("id"), QString::fromLatin1(kPreviewSurface)},
             {QStringLiteral("mode"), QStringLiteral("model")},
             {QStringLiteral("fallback"), QStringLiteral("external")}}}},
    };
}

QJsonObject ZeroSlackSuiteIntegration::processRequestForTesting(
    const QJsonObject& request)
{
    return processRequest(request);
}

QJsonObject ZeroSlackSuiteIntegration::processRequest(
    const QJsonObject& request)
{
    const QString method = request.value(QStringLiteral("method")).toString();
    const QJsonObject params =
        request.value(QStringLiteral("params")).toObject();
    const SourceTarget target = sourceTarget(params);
    if (!target.isValid()) {
        return SuiteApp::errorResponse(
            request,
            QStringLiteral("invalid_resource"),
            QStringLiteral("A zeroslack://source URI or filePath is required"));
    }
    if (method == QStringLiteral("resource.resolve"))
        return SuiteApp::successResponse(request, resolvedSource(window, target));

    if (method == QStringLiteral("action.invoke")) {
        if (params.value(QStringLiteral("actionId")).toString()
            != QString::fromLatin1(kRevealAction)) {
            return SuiteApp::errorResponse(
                request, QStringLiteral("action_not_supported"),
                QStringLiteral("Unknown ZeroSlack action"));
        }
        const bool opened = window
            && window->revealSuiteSource(
                target.filePath, target.line, target.column);
        if (!opened) {
            return SuiteApp::errorResponse(
                request, QStringLiteral("source_open_failed"),
                QStringLiteral("ZeroSlack could not reveal the source resource"));
        }
        return SuiteApp::successResponse(
            request, {{QStringLiteral("opened"), true},
                      {QStringLiteral("resource"),
                       resolvedSource(window, target)}});
    }

    const QString surfaceId =
        params.value(QStringLiteral("surfaceId")).toString();
    if (surfaceId != QString::fromLatin1(kPreviewSurface)) {
        return SuiteApp::errorResponse(
            request, QStringLiteral("surface_not_supported"),
            QStringLiteral("Unknown ZeroSlack surface"));
    }
    if (method == QStringLiteral("surface.describe")) {
        return SuiteApp::successResponse(
            request,
            {{QStringLiteral("surfaceId"), surfaceId},
             {QStringLiteral("mode"), QStringLiteral("model")},
             {QStringLiteral("fallback"), QStringLiteral("external")},
             {QStringLiteral("model"), resolvedSource(window, target)}});
    }
    if (method == QStringLiteral("surface.open")) {
        const bool opened = window
            && window->revealSuiteSource(
                target.filePath, target.line, target.column);
        return opened
            ? SuiteApp::successResponse(
                  request, {{QStringLiteral("opened"), true}})
            : SuiteApp::errorResponse(
                  request, QStringLiteral("source_open_failed"),
                  QStringLiteral("ZeroSlack could not open the source surface"));
    }
    return SuiteApp::errorResponse(
        request, QStringLiteral("method_not_supported"),
        QStringLiteral("ZeroSlack does not implement this provider method"));
}
