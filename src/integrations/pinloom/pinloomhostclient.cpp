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

#include <cmath>
#include <limits>
#include <utility>

namespace {
const QString kProtocol = QStringLiteral("pinloom-host/v1");
const QString kServer = QStringLiteral("pinloom.Pinloom.Pinloom.host.v1");
constexpr int kConnectTimeoutMs = 160;
constexpr int kResponseTimeoutMs = 1800;
constexpr int kLaunchAttempts = 20;
constexpr qsizetype kMaximumResponseBytes = 4 * 1024 * 1024;
constexpr qsizetype kMaximumPreviewDescriptorBytes = 16 * 1024;
constexpr qint64 kMaximumPreviewFileBytes = 64LL * 1024LL * 1024LL;
constexpr int kMaximumPreviewDimension = 16384;
constexpr qint64 kMaximumPreviewPixels = 64LL * 1024LL * 1024LL;

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

void setPreviewValidationError(PinloomHostPreview* preview,
                               const QString& error)
{
    if (preview && preview->validationError.isEmpty())
        preview->validationError = error;
}

bool isUncPath(const QString& path)
{
    return QDir::fromNativeSeparators(path).startsWith(QStringLiteral("//"));
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

bool PinloomHostPreview::isImage() const
{
    return present
        && kind.compare(QStringLiteral("image"), Qt::CaseInsensitive) == 0;
}

bool PinloomHostPreview::isReady() const
{
    return isImage()
        && validationError.isEmpty()
        && state.compare(QStringLiteral("ready"), Qt::CaseInsensitive) == 0;
}

PinloomHostPreview PinloomHostPreview::fromJson(
    const QJsonObject& object)
{
    PinloomHostPreview result;
    result.present = true;
    if (QJsonDocument(object).toJson(QJsonDocument::Compact).size()
        > kMaximumPreviewDescriptorBytes) {
        result.kind = QStringLiteral("image");
        result.state = QStringLiteral("error");
        result.validationError = QStringLiteral(
            "Pinloom preview descriptor exceeds the 16 KiB size limit.");
        return result;
    }

    const auto stringValue = [&object, &result](const QString& key) {
        const QJsonValue value = object.value(key);
        if (value.isUndefined() || value.isNull())
            return QString();
        if (!value.isString()) {
            setPreviewValidationError(
                &result,
                QStringLiteral("Pinloom preview field '%1' has an invalid type.")
                    .arg(key));
            return QString();
        }
        return value.toString().trimmed();
    };
    const auto integerValue = [&object, &result](
                                  const QString& key,
                                  qint64* target,
                                  bool* present) {
        const QJsonValue value = object.value(key);
        *present = !value.isUndefined() && !value.isNull();
        if (!*present)
            return;
        if (!value.isDouble()) {
            setPreviewValidationError(
                &result,
                QStringLiteral("Pinloom preview field '%1' is not an integer.")
                    .arg(key));
            return;
        }
        const double number = value.toDouble();
        constexpr double kMaximumExactJsonInteger = 9007199254740991.0;
        if (!std::isfinite(number)
            || std::floor(number) != number
            || number < -kMaximumExactJsonInteger
            || number > kMaximumExactJsonInteger) {
            setPreviewValidationError(
                &result,
                QStringLiteral("Pinloom preview field '%1' is out of range.")
                    .arg(key));
            return;
        }
        *target = qint64(number);
    };

    result.kind = stringValue(QStringLiteral("kind")).toLower();
    if (result.kind.isEmpty())
        result.kind = QStringLiteral("image");
    result.state = stringValue(QStringLiteral("state")).toLower();
    result.mimeType = stringValue(QStringLiteral("mimeType")).toLower();
    result.filePath = stringValue(QStringLiteral("filePath"));
    result.altText = stringValue(QStringLiteral("altText"));
    result.error = stringValue(QStringLiteral("error"));

    const QString uriText = stringValue(QStringLiteral("uri"));
    if (!uriText.isEmpty())
        result.uri = QUrl(uriText, QUrl::StrictMode);

    bool byteSizePresent = false;
    integerValue(QStringLiteral("byteSize"),
                 &result.byteSize, &byteSizePresent);
    qint64 pixelWidth = 0;
    bool pixelWidthPresent = false;
    integerValue(QStringLiteral("pixelWidth"),
                 &pixelWidth, &pixelWidthPresent);
    qint64 pixelHeight = 0;
    bool pixelHeightPresent = false;
    integerValue(QStringLiteral("pixelHeight"),
                 &pixelHeight, &pixelHeightPresent);
    qint64 page = -1;
    bool pagePresent = false;
    integerValue(QStringLiteral("page"), &page, &pagePresent);
    if (pixelWidthPresent
        && pixelWidth > 0
        && pixelWidth <= std::numeric_limits<int>::max()) {
        result.pixelWidth = int(pixelWidth);
    }
    if (pixelHeightPresent
        && pixelHeight > 0
        && pixelHeight <= std::numeric_limits<int>::max()) {
        result.pixelHeight = int(pixelHeight);
    }
    if (pagePresent
        && page >= std::numeric_limits<int>::min()
        && page <= std::numeric_limits<int>::max()) {
        result.page = int(page);
    }

    const QJsonValue croppedValue = object.value(QStringLiteral("cropped"));
    if (!croppedValue.isUndefined() && !croppedValue.isNull()) {
        if (croppedValue.isBool()) {
            result.cropped = croppedValue.toBool();
        } else {
            setPreviewValidationError(
                &result,
                QStringLiteral("Pinloom preview field 'cropped' has an invalid type."));
        }
    }

    if (object.contains(QStringLiteral("data"))
        || object.contains(QStringLiteral("base64"))) {
        setPreviewValidationError(
            &result,
            QStringLiteral("Embedded Pinloom preview data is not supported."));
    }
    if (result.kind != QStringLiteral("image")) {
        setPreviewValidationError(
            &result,
            QStringLiteral("Unsupported Pinloom preview kind: %1.")
                .arg(result.kind.isEmpty()
                         ? QStringLiteral("(missing)")
                         : result.kind));
    }
    if (result.state.isEmpty()) {
        if (!result.filePath.isEmpty() || !result.uri.isEmpty())
            result.state = QStringLiteral("ready");
        else if (!result.error.isEmpty())
            result.state = QStringLiteral("unavailable");
        else
            setPreviewValidationError(
                &result,
                QStringLiteral("Pinloom preview state is missing."));
    } else if (result.state != QStringLiteral("ready")
               && result.state != QStringLiteral("unavailable")
               && result.state != QStringLiteral("error")) {
        setPreviewValidationError(
            &result,
            QStringLiteral("Unsupported Pinloom preview state: %1.")
                .arg(result.state));
    }
    if (!result.mimeType.isEmpty()
        && result.mimeType != QStringLiteral("image/png")) {
        setPreviewValidationError(
            &result,
            QStringLiteral("Unsupported Pinloom preview MIME type: %1.")
                .arg(result.mimeType));
    }
    if (!uriText.isEmpty()
        && (!result.uri.isValid()
            || !result.uri.isLocalFile()
            || !result.uri.host().isEmpty())) {
        setPreviewValidationError(
            &result,
            QStringLiteral("Pinloom preview URI is not a local file URI."));
    }
    if (!result.filePath.isEmpty()
        && (!QFileInfo(result.filePath).isAbsolute()
            || isUncPath(result.filePath))) {
        setPreviewValidationError(
            &result,
            QStringLiteral("Pinloom preview file path is not an absolute local path."));
    }
    if (byteSizePresent
        && (result.byteSize < 0
            || result.byteSize > kMaximumPreviewFileBytes)) {
        setPreviewValidationError(
            &result,
            QStringLiteral("Pinloom preview file size exceeds the safety limit."));
    }
    if (pixelWidthPresent
        && (pixelWidth <= 0 || pixelWidth > kMaximumPreviewDimension)) {
        setPreviewValidationError(
            &result,
            QStringLiteral("Pinloom preview width exceeds the safety limit."));
    }
    if (pixelHeightPresent
        && (pixelHeight <= 0 || pixelHeight > kMaximumPreviewDimension)) {
        setPreviewValidationError(
            &result,
            QStringLiteral("Pinloom preview height exceeds the safety limit."));
    }
    if (pixelWidth > 0 && pixelHeight > 0
        && (pixelWidth > kMaximumPreviewPixels / pixelHeight)) {
        setPreviewValidationError(
            &result,
            QStringLiteral("Pinloom preview pixel count exceeds the safety limit."));
    }
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
    const QJsonValue previewValue = object.value(QStringLiteral("preview"));
    if (!previewValue.isUndefined() && !previewValue.isNull()) {
        if (previewValue.isObject()) {
            result.preview = PinloomHostPreview::fromJson(
                previewValue.toObject());
        } else {
            result.preview.present = true;
            result.preview.kind = QStringLiteral("image");
            result.preview.state = QStringLiteral("error");
            result.preview.validationError = QStringLiteral(
                "Pinloom preview descriptor is not a JSON object.");
        }
    }
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
