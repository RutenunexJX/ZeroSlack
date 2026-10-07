#include "suiteappintegration.h"

#include "mainwindow.h"
#include "documentmodel.h"
#include "editorfileidentity.h"
#include <zeroslack/documents/documentfileread.h>
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "semanticstableidentity.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <suiteapp/protocol.h>
#include <suiteapp/provider.h>
#include <suiteapp/runtime.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>
#include <limits>

namespace {

constexpr auto kRevealAction = "zeroslack.source.reveal";
constexpr auto kRevealSymbolAction = "zeroslack.symbol.reveal";
constexpr auto kPreviewSurface = "zeroslack.source.preview";

struct SourceTarget {
    QString uri;
    QString kind = QStringLiteral("source");
    QString filePath;
    QString workspaceRoot;
    QString semanticId;
    int line = 1;
    int column = 1;
    bool lineSpecified = false;
    bool columnSpecified = false;
    QString indexedText;
    QString failureCode;
    QString failureReason;

    bool isValid() const
    {
        return failureCode.isEmpty() && !filePath.trimmed().isEmpty();
    }
};

bool mergeString(const QJsonObject& values, const QString& key,
                 QString* destination, bool path = false)
{
    if (!values.contains(key))
        return true;
    const auto value = values.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty())
        return false;
    const QString text = value.toString();
    if (path && !QFileInfo(text).isAbsolute())
        return false;
    if (!destination->isEmpty()
        && (path ? !EditorFileIdentity::same(*destination, text)
                 : *destination != text))
        return false;
    if (destination->isEmpty())
        *destination = text;
    return true;
}

bool mergeCoordinate(const QJsonObject& values, const QString& key,
                     int* destination, bool* specified)
{
    if (!values.contains(key))
        return true;
    const auto value = values.value(key);
    const double number = value.toDouble();
    if (!value.isDouble() || !std::isfinite(number) || number < 1
        || number > std::numeric_limits<int>::max() || std::floor(number) != number)
        return false;
    if (*specified && *destination != static_cast<int>(number))
        return false;
    *destination = static_cast<int>(number);
    *specified = true;
    return true;
}

QString activeWorkspaceRoot(MainWindow* window)
{
    return window && window->workspaceManager
        ? window->workspaceManager->getWorkspacePath()
        : QString();
}

void resolveSymbolTarget(SourceTarget* target)
{
    if (!target || target->semanticId.trimmed().isEmpty())
        return;
    const std::shared_ptr<const SemanticIndexSnapshot> snapshot =
        SemanticIndex::getInstance()->snapshot();
    if (!snapshot) {
        target->failureCode = QStringLiteral("symbol_index_unavailable");
        target->failureReason = QStringLiteral(
            "ZeroSlack has no current semantic snapshot for this workspace.");
        return;
    }
    QList<SemanticSymbolRecord> matches;
    for (const SemanticSymbolRecord& record : snapshot->getSymbolRecords()) {
        const SemanticStableIdentity identity =
            semanticStableIdentity(record, target->workspaceRoot);
        if (identity.stableId == target->semanticId
            || identity.exactId == target->semanticId) {
            matches.append(record);
        }
    }
    if (matches.isEmpty()) {
        target->failureCode = QStringLiteral("symbol_not_found");
        target->failureReason = QStringLiteral(
            "The ZeroSlack symbol ID is not present in the current snapshot.");
        return;
    }
    if (matches.size() > 1) {
        target->failureCode = QStringLiteral("symbol_ambiguous");
        target->failureReason = QStringLiteral(
            "The ZeroSlack symbol ID resolves to more than one declaration.");
        return;
    }
    const SemanticSymbolRecord& record = matches.constFirst();
    if ((!target->filePath.isEmpty()
         && !EditorFileIdentity::same(target->filePath, record.location.fileName))
        || (target->lineSpecified && target->line != record.location.startLine)
        || (target->columnSpecified && target->column != record.location.startColumn)) {
        target->failureCode = QStringLiteral("invalid_resource");
        target->failureReason = QStringLiteral("Symbol and source locators disagree.");
        return;
    }
    const auto source = snapshot->cachedFileSource(record.location.fileName);
    if (!source.exists) {
        target->failureCode = QStringLiteral("symbol_source_unavailable");
        target->failureReason = QStringLiteral("The symbol snapshot has no source revision.");
        return;
    }
    target->kind = QStringLiteral("symbol");
    target->filePath = QFileInfo(record.location.fileName).absoluteFilePath();
    target->line = record.location.startLine;
    target->column = record.location.startColumn;
    target->indexedText = source.text;
}

SourceTarget sourceTarget(const QJsonObject& params, MainWindow* window)
{
    SourceTarget target;
    const auto invalid = [&] {
        target.failureCode = QStringLiteral("invalid_resource");
        target.failureReason = QStringLiteral("Source locators must be valid, absolute and consistent.");
        return target;
    };
    for (const QString& key : {QStringLiteral("resourceUri"), QStringLiteral("uri")}) {
        if (params.contains(key) && !params.value(key).isString())
            return invalid();
    }
    target.uri = params.value(QStringLiteral("resourceUri")).toString();
    if (target.uri.isEmpty())
        target.uri = params.value(QStringLiteral("uri")).toString();
    else if (!params.value(QStringLiteral("uri")).toString().isEmpty()
             && QUrl(target.uri) != QUrl(params.value(QStringLiteral("uri")).toString()))
        return invalid();

    if (params.contains(QStringLiteral("arguments"))
        && !params.value(QStringLiteral("arguments")).isObject())
        return invalid();
    const QJsonObject arguments =
        params.value(QStringLiteral("arguments")).toObject();
    if (!target.uri.isEmpty()) {
        const QUrl url(target.uri, QUrl::StrictMode);
        if (!url.isValid() || url.scheme() != QStringLiteral("zeroslack")
            || !url.userInfo().isEmpty() || url.port() != -1 || url.hasFragment())
            return invalid();
        const QUrlQuery query(url);
        QJsonObject locator;
        for (const auto& item : query.queryItems(QUrl::FullyDecoded)) {
            QString key;
            if (item.first == QStringLiteral("file")) key = QStringLiteral("filePath");
            if (item.first == QStringLiteral("workspace")) key = QStringLiteral("workspaceRoot");
            if (item.first == QStringLiteral("line") || item.first == QStringLiteral("column"))
                key = item.first;
            if (key.isEmpty())
                continue;
            if (locator.contains(key))
                return invalid();
            if (key == QStringLiteral("line") || key == QStringLiteral("column")) {
                bool ok = false;
                const auto coordinate = item.second.toLongLong(&ok);
                if (!ok || coordinate < 1 || coordinate > std::numeric_limits<int>::max())
                    return invalid();
                locator.insert(key, static_cast<int>(coordinate));
            } else {
                locator.insert(key, item.second);
            }
        }
        if (!mergeString(locator, QStringLiteral("filePath"), &target.filePath, true)
            || !mergeString(locator, QStringLiteral("workspaceRoot"), &target.workspaceRoot, true)
            || !mergeCoordinate(locator, QStringLiteral("line"), &target.line, &target.lineSpecified)
            || !mergeCoordinate(locator, QStringLiteral("column"), &target.column, &target.columnSpecified))
            return invalid();
        if (url.host() == QStringLiteral("source")) {
            if (target.filePath.isEmpty() || (!url.path().isEmpty() && url.path() != QStringLiteral("/"))
                || arguments.contains(QStringLiteral("symbolId")))
                return invalid();
        } else if (url.host() == QStringLiteral("symbol")) {
            target.kind = QStringLiteral("symbol");
            target.semanticId = url.path(QUrl::FullyDecoded).mid(1);
            if (target.semanticId.isEmpty() || target.semanticId.contains(QLatin1Char('/')))
                return invalid();
        } else
            return invalid();
    }
    if (!mergeString(arguments, QStringLiteral("filePath"), &target.filePath, true)
        || !mergeString(arguments, QStringLiteral("workspaceRoot"), &target.workspaceRoot, true)
        || !mergeString(arguments, QStringLiteral("symbolId"), &target.semanticId)
        || !mergeCoordinate(arguments, QStringLiteral("line"), &target.line, &target.lineSpecified)
        || !mergeCoordinate(arguments, QStringLiteral("column"), &target.column, &target.columnSpecified))
        return invalid();
    const QString currentWorkspace = activeWorkspaceRoot(window);
    if (target.workspaceRoot.isEmpty())
        target.workspaceRoot = currentWorkspace;
    if (!target.filePath.isEmpty())
        target.filePath = QFileInfo(target.filePath).absoluteFilePath();
    if (!target.workspaceRoot.isEmpty()) {
        target.workspaceRoot = QFileInfo(
            target.workspaceRoot).absoluteFilePath();
    }
    if (!target.semanticId.isEmpty()) {
        if (!currentWorkspace.isEmpty()
            && !EditorFileIdentity::same(target.workspaceRoot, currentWorkspace)) {
            target.failureCode = QStringLiteral("workspace_mismatch");
            target.failureReason = QStringLiteral("The symbol does not name the active workspace.");
            return target;
        }
        // Use the active workspace spelling when an equivalent alias was supplied.
        if (!currentWorkspace.isEmpty()) target.workspaceRoot = currentWorkspace;
        resolveSymbolTarget(&target);
    }
    return target;
}

struct SourceContent {
    QString text;
    QString failureReason;
    bool fromDocument = false;
    bool dirty = false;
    int textVersion = 0;
};

SourceContent readSource(MainWindow* window, const SourceTarget& target)
{
    SourceContent source;
    const auto* documents = window && window->tabManager
        ? window->tabManager->getDocumentModel() : nullptr;
    if (documents && documents->editorForFile(target.filePath)) {
        const auto snapshot = documents->documentForFile(target.filePath);
        source.text = snapshot.text;
        source.fromDocument = true;
        source.dirty = snapshot.dirty;
        source.textVersion = snapshot.textVersion;
    } else {
        const auto read = readDocumentFile(target.filePath);
        source.text = read.text;
        if (!read.available)
            source.failureReason = read.failureReason;
    }
    return source;
}

bool positionAvailable(const SourceTarget& target, const QStringList& lines)
{
    return target.line > 0 && target.line <= lines.size()
        && target.column > 0 && target.column <= lines.at(target.line - 1).size() + 1;
}

QString sourceSnippet(const SourceTarget& target, const QStringList& lines)
{
    if (target.line < 1)
        return {};
    const qsizetype first = qMax(qsizetype(0), qsizetype(target.line) - 4);
    const qsizetype last = qMin(lines.size(), qsizetype(target.line) + 3);
    QStringList snippet;
    for (qsizetype index = first; index < last; ++index)
        snippet.append(lines.at(index));
    return snippet.join(QLatin1Char('\n'));
}

QJsonObject resolvedSource(const SourceTarget& target, const SourceContent& source,
                           const QStringList& lines)
{
    QJsonObject result{
        {QStringLiteral("appId"), QStringLiteral("zeroslack")},
        {QStringLiteral("uri"), target.uri},
        {QStringLiteral("kind"), target.kind},
        {QStringLiteral("symbolId"), target.semanticId},
        {QStringLiteral("workspaceRoot"), target.workspaceRoot},
        {QStringLiteral("filePath"), target.filePath},
        {QStringLiteral("title"), QFileInfo(target.filePath).fileName()},
        {QStringLiteral("line"), target.line},
        {QStringLiteral("column"), target.column},
        {QStringLiteral("columnEncoding"), QStringLiteral("utf-16")},
        {QStringLiteral("exists"), QFileInfo::exists(target.filePath)},
        {QStringLiteral("snippet"), sourceSnippet(target, lines)},
        {QStringLiteral("positionAvailable"), positionAvailable(target, lines)},
        {QStringLiteral("source"), source.fromDocument ? QStringLiteral("document") : QStringLiteral("disk")},
        {QStringLiteral("dirty"), source.dirty},
        {QStringLiteral("contentSha256"), QString::fromLatin1(
             QCryptographicHash::hash(source.text.toUtf8(), QCryptographicHash::Sha256).toHex())},
    };
    if (source.fromDocument)
        result.insert(QStringLiteral("textVersion"), source.textVersion);
    return result;
}

} // namespace

ZeroSlackSuiteIntegration::ZeroSlackSuiteIntegration(QMainWindow* windowValue,
                                                     QObject* parent)
    : QObject(parent)
    , window(qobject_cast<MainWindow*>(windowValue))
{
}

ZeroSlackSuiteIntegration::~ZeroSlackSuiteIntegration() = default;

bool ZeroSlackSuiteIntegration::start(QString* failureReason)
{
    return start(SuiteApp::RuntimeStartOptions{}, failureReason);
}

bool ZeroSlackSuiteIntegration::start(
    const SuiteApp::RuntimeStartOptions& runtimeOptions,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (runtimeEndpoint == runtimeOptions.endpoint && isRegistered())
        return true;
    const SuiteApp::RuntimeStatus runtime =
        SuiteApp::ensureRuntime(runtimeOptions);
    if (!runtime.available) {
        if (failureReason)
            *failureReason = runtime.errorMessage;
        return false;
    }
    if (provider && runtimeEndpoint != runtimeOptions.endpoint)
        provider.reset();
    if (!provider) {
        provider = std::make_unique<SuiteApp::Provider>(
            appDescriptor(QCoreApplication::applicationVersion()),
            [this](const QJsonObject& request) { return processRequest(request); },
            this);
    }
    if (!provider->start(runtimeOptions.endpoint, failureReason))
        return false;
    runtimeEndpoint = runtimeOptions.endpoint;
    return true;
}

bool ZeroSlackSuiteIntegration::isRegistered() const
{
    if (!provider || !provider->isListening() || runtimeEndpoint.isEmpty())
        return false;
    const auto listed = SuiteApp::Client(runtimeEndpoint, 250).listProviders();
    if (!listed.hasResponse() || !listed.response.value(QStringLiteral("ok")).toBool())
        return false;
    const auto descriptor = provider->descriptor();
    for (const auto& value : listed.response.value(QStringLiteral("result")).toObject()
             .value(QStringLiteral("providers")).toArray()) {
        const auto registered = value.toObject();
        if (SuiteApp::descriptorAppId(registered) == SuiteApp::descriptorAppId(descriptor)
            && SuiteApp::descriptorEndpoint(registered) == SuiteApp::descriptorEndpoint(descriptor))
            return true;
    }
    return false;
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
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("id"), QString::fromLatin1(kRevealAction)},
                 {QStringLiteral("resourceSchemes"),
                  QJsonArray{QStringLiteral("zeroslack")}},
                 {QStringLiteral("sideEffect"), QStringLiteral("ui")}},
             QJsonObject{
                 {QStringLiteral("id"),
                  QString::fromLatin1(kRevealSymbolAction)},
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
    const SourceTarget target = sourceTarget(params, window);
    if (!target.isValid()) {
        return SuiteApp::errorResponse(
            request,
            target.failureCode.isEmpty()
                ? QStringLiteral("invalid_resource") : target.failureCode,
            target.failureReason.isEmpty()
                ? QStringLiteral(
                      "A zeroslack://source or zeroslack://symbol resource is required")
                : target.failureReason);
    }
    const SourceContent source = readSource(window, target);
    if (!source.failureReason.isEmpty()) {
        return SuiteApp::errorResponse(request, QStringLiteral("source_unavailable"),
                                        source.failureReason);
    }
    if (target.kind == QStringLiteral("symbol") && source.text != target.indexedText) {
        return SuiteApp::errorResponse(request, QStringLiteral("symbol_source_stale"),
            QStringLiteral("The source has changed since this symbol snapshot was built."));
    }
    const QStringList lines = source.text.split(QLatin1Char('\n'));
    if (method == QStringLiteral("resource.resolve"))
        return SuiteApp::successResponse(request, resolvedSource(target, source, lines));

    if (method == QStringLiteral("action.invoke")) {
        const QString actionId = params.value(
            QStringLiteral("actionId")).toString();
        if (actionId != QString::fromLatin1(kRevealAction)
            && actionId != QString::fromLatin1(kRevealSymbolAction)) {
            return SuiteApp::errorResponse(
                request, QStringLiteral("action_not_supported"),
                QStringLiteral("Unknown ZeroSlack action"));
        }
        if (!positionAvailable(target, lines)) {
            return SuiteApp::errorResponse(request, QStringLiteral("source_position_unavailable"),
                QStringLiteral("The requested position is outside the current source."));
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
                       resolvedSource(target, source, lines)}});
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
             {QStringLiteral("model"), resolvedSource(target, source, lines)}});
    }
    if (method == QStringLiteral("surface.open")) {
        if (!positionAvailable(target, lines)) {
            return SuiteApp::errorResponse(request, QStringLiteral("source_position_unavailable"),
                QStringLiteral("The requested position is outside the current source."));
        }
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
