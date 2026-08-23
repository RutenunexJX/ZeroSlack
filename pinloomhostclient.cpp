#include "pinloomhostclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent>

#include <utility>

namespace {
const QString kProtocol = QStringLiteral("pinloom-host/v1");
const QString kServer = QStringLiteral("pinloom.Pinloom.Pinloom.host.v1");
constexpr int kConnectTimeoutMs = 160;
constexpr int kResponseTimeoutMs = 1800;
constexpr int kLaunchAttempts = 20;
constexpr qsizetype kMaximumResponseBytes = 4 * 1024 * 1024;

struct RawReply {
    QJsonObject result;
    QString error;
};

QStringList jsonStringList(const QJsonValue& value)
{
    QStringList result;
    for (const QJsonValue& item : value.toArray())
        result.append(item.toString());
    return result;
}

QString firstExistingExecutable(const QStringList& candidates)
{
    for (const QString& candidate : candidates) {
        const QFileInfo info(QDir::cleanPath(candidate));
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return {};
}

RawReply exchangeRequest(const QJsonObject& request,
                         const QString& executablePath)
{
    const auto connect = []() {
        auto socket = std::make_unique<QLocalSocket>();
        socket->connectToServer(kServer, QIODevice::ReadWrite);
        if (!socket->waitForConnected(kConnectTimeoutMs))
            return std::unique_ptr<QLocalSocket>();
        return socket;
    };

    std::unique_ptr<QLocalSocket> socket = connect();
    if (!socket) {
        if (executablePath.isEmpty()) {
            return {{}, QStringLiteral(
                "Pinloom is not running and its executable was not found. "
                "Set the Pinloom executable in Settings or PINLOOM_APP_PATH.")};
        }
        if (!QProcess::startDetached(
                executablePath,
                {QStringLiteral("--hidden")},
                QFileInfo(executablePath).absolutePath())) {
            return {{}, QStringLiteral("Unable to start Pinloom: %1")
                            .arg(QDir::toNativeSeparators(executablePath))};
        }
        for (int attempt = 0; attempt < kLaunchAttempts && !socket; ++attempt) {
            QThread::msleep(150);
            socket = connect();
        }
        if (!socket) {
            return {{}, QStringLiteral(
                "Pinloom started but its host bridge did not become available.")};
        }
    }

    QByteArray payload =
        QJsonDocument(request).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (socket->write(payload) != payload.size()
        || !socket->waitForBytesWritten(kResponseTimeoutMs)) {
        return {{}, QStringLiteral("Unable to send the Pinloom request.")};
    }

    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kResponseTimeoutMs) {
        response.append(socket->readAll());
        if (response.size() > kMaximumResponseBytes) {
            return {{}, QStringLiteral("Pinloom response exceeds the size limit.")};
        }
        const qsizetype newline = response.indexOf('\n');
        if (newline >= 0) {
            QJsonParseError parseError;
            const QJsonDocument document =
                QJsonDocument::fromJson(response.left(newline), &parseError);
            if (parseError.error != QJsonParseError::NoError
                || !document.isObject()) {
                return {{}, QStringLiteral("Pinloom returned invalid JSON.")};
            }
            const QJsonObject envelope = document.object();
            if (envelope.value(QStringLiteral("protocol")).toString()
                    != kProtocol
                || envelope.value(QStringLiteral("requestId")).toString()
                    != request.value(QStringLiteral("requestId")).toString()) {
                return {{}, QStringLiteral("Pinloom returned a mismatched response.")};
            }
            if (!envelope.value(QStringLiteral("ok")).toBool()) {
                const QString message =
                    envelope.value(QStringLiteral("error")).toObject()
                        .value(QStringLiteral("message")).toString().trimmed();
                return {{}, message.isEmpty()
                                ? QStringLiteral("Pinloom request failed.")
                                : message};
            }
            return {envelope.value(QStringLiteral("result")).toObject(), {}};
        }
        const int remaining = kResponseTimeoutMs - int(timer.elapsed());
        if (remaining <= 0 || !socket->waitForReadyRead(qMin(100, remaining)))
            continue;
    }
    return {{}, QStringLiteral("Pinloom did not answer in time.")};
}
}

bool PinloomHostIdentity::isValid() const
{
    return !entryId.trimmed().isEmpty()
        || !resourceId.trimmed().isEmpty()
        || !clipId.trimmed().isEmpty();
}

QVariantMap PinloomHostIdentity::toVariantMap() const
{
    return {{QStringLiteral("entryId"), entryId},
            {QStringLiteral("resourceId"), resourceId},
            {QStringLiteral("anchorId"), anchorId},
            {QStringLiteral("clipId"), clipId}};
}

QJsonObject PinloomHostIdentity::toJson() const
{
    return QJsonObject::fromVariantMap(toVariantMap());
}

QUrl PinloomHostIdentity::toUri() const
{
    if (!isValid())
        return {};
    QUrl uri;
    uri.setScheme(QStringLiteral("pinloom"));
    uri.setHost(QStringLiteral("entry"));
    uri.setPath(QStringLiteral("/") + entryId);
    QUrlQuery query;
    if (!resourceId.isEmpty())
        query.addQueryItem(QStringLiteral("resource"), resourceId);
    if (!anchorId.isEmpty())
        query.addQueryItem(QStringLiteral("anchor"), anchorId);
    if (!clipId.isEmpty())
        query.addQueryItem(QStringLiteral("clip"), clipId);
    uri.setQuery(query);
    return uri;
}

PinloomHostIdentity PinloomHostIdentity::fromVariantMap(
    const QVariantMap& map)
{
    PinloomHostIdentity result;
    result.entryId = map.value(QStringLiteral("entryId")).toString().trimmed();
    result.resourceId = map.value(QStringLiteral("resourceId")).toString().trimmed();
    result.anchorId = map.value(QStringLiteral("anchorId")).toString().trimmed();
    result.clipId = map.value(QStringLiteral("clipId")).toString().trimmed();
    return result;
}

PinloomHostIdentity PinloomHostIdentity::fromJson(const QJsonObject& object)
{
    return fromVariantMap(object.toVariantMap());
}

PinloomHostIdentity PinloomHostIdentity::fromUri(const QUrl& uri)
{
    PinloomHostIdentity result;
    if (!uri.isValid()
        || uri.scheme().compare(QStringLiteral("pinloom"),
                                Qt::CaseInsensitive) != 0
        || uri.host().compare(QStringLiteral("entry"),
                              Qt::CaseInsensitive) != 0) {
        return result;
    }
    result.entryId = uri.path().mid(1).trimmed();
    const QUrlQuery query(uri);
    result.resourceId =
        query.queryItemValue(QStringLiteral("resource")).trimmed();
    result.anchorId =
        query.queryItemValue(QStringLiteral("anchor")).trimmed();
    result.clipId =
        query.queryItemValue(QStringLiteral("clip")).trimmed();
    return result.isValid() ? result : PinloomHostIdentity{};
}

bool PinloomHostEntry::isValid() const
{
    return identity.isValid() && !title.trimmed().isEmpty();
}

QVariantMap PinloomHostEntry::toVariantMap() const
{
    return {{QStringLiteral("identity"), identity.toVariantMap()},
            {QStringLiteral("uri"), uri.toString(QUrl::FullyEncoded)},
            {QStringLiteral("type"), type},
            {QStringLiteral("title"), title},
            {QStringLiteral("aliases"), aliases},
            {QStringLiteral("tags"), tags},
            {QStringLiteral("summary"), summary},
            {QStringLiteral("location"), location},
            {QStringLiteral("matchedField"), matchedField},
            {QStringLiteral("matchSummary"), matchSummary},
            {QStringLiteral("pinned"), pinned},
            {QStringLiteral("deleted"), deleted},
            {QStringLiteral("metadata"), metadata}};
}

PinloomHostEntry PinloomHostEntry::fromJson(const QJsonObject& object)
{
    PinloomHostEntry result;
    result.identity = PinloomHostIdentity::fromJson(
        object.value(QStringLiteral("identity")).toObject());
    result.uri = QUrl(object.value(QStringLiteral("uri")).toString(),
                      QUrl::StrictMode);
    result.type = object.value(QStringLiteral("type")).toString();
    result.title = object.value(QStringLiteral("title")).toString();
    result.aliases = jsonStringList(object.value(QStringLiteral("aliases")));
    result.tags = jsonStringList(object.value(QStringLiteral("tags")));
    result.summary = object.value(QStringLiteral("summary")).toString();
    result.location = object.value(QStringLiteral("location")).toString();
    result.matchedField = object.value(QStringLiteral("matchedField")).toString();
    result.matchSummary = object.value(QStringLiteral("matchSummary")).toString();
    result.pinned = object.value(QStringLiteral("pinned")).toBool();
    result.deleted = object.value(QStringLiteral("deleted")).toBool();
    result.metadata = object.value(QStringLiteral("metadata")).toObject().toVariantMap();
    return result;
}

PinloomHostEntry PinloomHostEntry::fromVariantMap(const QVariantMap& map)
{
    PinloomHostEntry result;
    result.identity = PinloomHostIdentity::fromVariantMap(
        map.value(QStringLiteral("identity")).toMap());
    result.uri = QUrl(map.value(QStringLiteral("uri")).toString(),
                      QUrl::StrictMode);
    result.type = map.value(QStringLiteral("type")).toString();
    result.title = map.value(QStringLiteral("title")).toString();
    result.aliases = map.value(QStringLiteral("aliases")).toStringList();
    result.tags = map.value(QStringLiteral("tags")).toStringList();
    result.summary = map.value(QStringLiteral("summary")).toString();
    result.location = map.value(QStringLiteral("location")).toString();
    result.matchedField = map.value(QStringLiteral("matchedField")).toString();
    result.matchSummary = map.value(QStringLiteral("matchSummary")).toString();
    result.pinned = map.value(QStringLiteral("pinned")).toBool();
    result.deleted = map.value(QStringLiteral("deleted")).toBool();
    result.metadata = map.value(QStringLiteral("metadata")).toMap();
    return result;
}

bool PinloomHostDocument::isValid() const
{
    return entry.isValid();
}

PinloomHostDocument PinloomHostDocument::fromJson(const QJsonObject& object)
{
    PinloomHostDocument result;
    result.entry = PinloomHostEntry::fromJson(
        object.value(QStringLiteral("entry")).toObject());
    result.content = object.value(QStringLiteral("content")).toString();
    result.contentType = object.value(QStringLiteral("contentType")).toString();
    result.details = object.value(QStringLiteral("details")).toObject().toVariantMap();
    return result;
}

PinloomHostClient::PinloomHostClient(QObject* parent)
    : QObject(parent)
{
}

PinloomHostClient::PinloomHostClient(RequestTransport transport,
                                     QObject* parent)
    : QObject(parent)
    , customTransport(std::move(transport))
{
}

QString PinloomHostClient::executablePath() const
{
    return configuredExecutablePath;
}

void PinloomHostClient::setExecutablePath(const QString& path)
{
    const QString trimmed = path.trimmed();
    configuredExecutablePath = trimmed.isEmpty()
        ? QString()
        : QDir::cleanPath(trimmed);
}

void PinloomHostClient::search(const QString& query,
                               int limit,
                               SearchHandler handler)
{
    request(QStringLiteral("search"),
            {{QStringLiteral("query"), query},
             {QStringLiteral("limit"), qBound(1, limit, 100)}},
            [handler = std::move(handler)](
                const QJsonObject& result,
                const QString& error) {
                QList<PinloomHostEntry> entries;
                if (error.isEmpty()) {
                    for (const QJsonValue& value :
                         result.value(QStringLiteral("entries")).toArray()) {
                        PinloomHostEntry entry =
                            PinloomHostEntry::fromJson(value.toObject());
                        if (entry.isValid())
                            entries.append(std::move(entry));
                    }
                }
                if (handler)
                    handler(entries, error);
            });
}

void PinloomHostClient::resolve(const PinloomHostIdentity& identity,
                                ResolveHandler handler)
{
    if (!identity.isValid()) {
        if (handler)
            handler({}, QStringLiteral("Pinloom identity is invalid."));
        return;
    }
    request(QStringLiteral("resolve"),
            {{QStringLiteral("identity"), identity.toJson()}},
            [handler = std::move(handler)](
                const QJsonObject& result,
                const QString& error) {
                const PinloomHostDocument document =
                    error.isEmpty()
                    ? PinloomHostDocument::fromJson(result)
                    : PinloomHostDocument{};
                if (handler)
                    handler(document, error);
            });
}

void PinloomHostClient::open(const PinloomHostIdentity& identity,
                             CompletionHandler handler)
{
    if (!identity.isValid()) {
        if (handler)
            handler(false, QStringLiteral("Pinloom identity is invalid."));
        return;
    }
    request(QStringLiteral("open"),
            {{QStringLiteral("identity"), identity.toJson()}},
            [handler = std::move(handler)](
                const QJsonObject& result,
                const QString& error) {
                if (!handler)
                    return;
                handler(error.isEmpty(),
                        error.isEmpty()
                            ? result.value(QStringLiteral("message")).toString()
                            : error);
            });
}

void PinloomHostClient::createSourceAnchor(
    const QVariantMap& source,
    const QString& title,
    CreateSourceAnchorHandler handler)
{
    QJsonObject params = QJsonObject::fromVariantMap(source);
    params.insert(
        QStringLiteral("content"),
        source.value(QStringLiteral("selectedText")).toString());
    params.insert(QStringLiteral("title"), title.trimmed());
    request(QStringLiteral("createSourceAnchor"),
            params,
            [handler = std::move(handler)](
                const QJsonObject& result,
                const QString& error) {
                const PinloomHostEntry entry = error.isEmpty()
                    ? PinloomHostEntry::fromJson(
                          result.value(QStringLiteral("entry")).toObject())
                    : PinloomHostEntry{};
                if (handler)
                    handler(entry, error);
            });
}

void PinloomHostClient::capabilities(RawReplyHandler handler)
{
    request(QStringLiteral("capabilities"), {}, std::move(handler));
}

QString PinloomHostClient::protocolName()
{
    return kProtocol;
}

QString PinloomHostClient::defaultServerName()
{
    return kServer;
}

QString PinloomHostClient::discoverExecutablePath(
    const QString& configuredPath)
{
    QStringList candidates;
    if (!configuredPath.trimmed().isEmpty())
        candidates.append(configuredPath.trimmed());
    const QString environmentPath =
        qEnvironmentVariable("PINLOOM_APP_PATH").trimmed();
    if (!environmentPath.isEmpty())
        candidates.append(environmentPath);

    const QDir appDir(QCoreApplication::applicationDirPath());
    candidates.append(appDir.filePath(QStringLiteral("pinloom_app.exe")));
    candidates.append(appDir.filePath(QStringLiteral("../Pinloom/pinloom_app.exe")));
    candidates.append(appDir.filePath(QStringLiteral("../../Pinloom/pinloom_app.exe")));
    const QString pathExecutable =
        QStandardPaths::findExecutable(QStringLiteral("pinloom_app"));
    if (!pathExecutable.isEmpty())
        candidates.append(pathExecutable);
    return firstExistingExecutable(candidates);
}

void PinloomHostClient::request(const QString& method,
                                const QJsonObject& params,
                                RawReplyHandler handler)
{
    QJsonObject envelope{
        {QStringLiteral("protocol"), kProtocol},
        {QStringLiteral("requestId"),
         QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    };
    if (customTransport) {
        customTransport(envelope, std::move(handler));
        return;
    }

    const QString executable =
        discoverExecutablePath(configuredExecutablePath);
    auto* watcher = new QFutureWatcher<RawReply>(this);
    connect(watcher,
            &QFutureWatcher<RawReply>::finished,
            this,
            [watcher, handler = std::move(handler)]() {
                const RawReply reply = watcher->result();
                watcher->deleteLater();
                if (handler)
                    handler(reply.result, reply.error);
            });
    watcher->setFuture(QtConcurrent::run(
        [envelope = std::move(envelope), executable]() {
            return exchangeRequest(envelope, executable);
        }));
}
