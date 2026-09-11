#include "crashrecoveryservice.h"

#include "editorfileidentity.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {
constexpr const char* kSchema =
    "ZeroSlack.CrashRecoverySnapshot";
constexpr const char* kWorkspacesDirectory =
    "workspaces";
constexpr const char* kRecordsDirectory =
    "records";
constexpr const char* kQuarantineDirectory =
    "quarantine";

struct StoredRecoveryRecord {
    CrashRecoveryCandidate candidate;
    QString text;
};

struct SourceSnapshot {
    CrashRecoverySourceState state =
        CrashRecoverySourceState::BaselineUnavailable;
    QByteArray bytes;
    QByteArray sha256;
    QDateTime modifiedUtc;
    bool exists = false;
    bool readable = false;
    bool changedSinceBaseline = false;
    bool matchesRecovery = false;
};

QString pathIdentityKey(const QString& path)
{
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}

QString expectedRecoveryId(
    const CrashRecoveryCandidate& candidate)
{
    const QString identityText =
        QStringLiteral("workspace:")
        + candidate.workspaceIdentity
        + QLatin1Char('\n')
        + (candidate.untitledDocumentId.isEmpty()
               ? QStringLiteral("file:")
                     + pathIdentityKey(
                           candidate.canonicalFileIdentity)
               : QStringLiteral("untitled:")
                     + candidate.untitledDocumentId);
    return QString::fromLatin1(
        CrashRecoveryService::sha256(
            identityText.toUtf8())
            .toHex());
}

bool currentCanonicalIdentityMatches(
    const CrashRecoveryCandidate& candidate)
{
    if (!candidate.untitledDocumentId.isEmpty())
        return true;
    const QString currentCanonical =
        EditorFileIdentity::lookupKey(
            candidate.originalFilePath);
    if (currentCanonical.isEmpty())
        return true;
    const QString normalizedCurrent =
        QDir::cleanPath(
            QDir::fromNativeSeparators(
                currentCanonical));
    return pathIdentityKey(normalizedCurrent)
        == pathIdentityKey(
               candidate.canonicalFileIdentity);
}

QString statusReason(CrashRecoveryStatus status)
{
    switch (status) {
    case CrashRecoveryStatus::Success:
        return QStringLiteral("Operation completed.");
    case CrashRecoveryStatus::AlreadyClean:
        return QStringLiteral("No recovery snapshot exists.");
    case CrashRecoveryStatus::InvalidArgument:
        return QStringLiteral("The recovery request is invalid.");
    case CrashRecoveryStatus::StorageUnavailable:
        return QStringLiteral("Recovery storage is unavailable.");
    case CrashRecoveryStatus::NotFound:
        return QStringLiteral("The recovery snapshot was not found.");
    case CrashRecoveryStatus::IoError:
        return QStringLiteral("Recovery storage could not be accessed.");
    case CrashRecoveryStatus::CorruptRecord:
        return QStringLiteral("The recovery snapshot is corrupt.");
    case CrashRecoveryStatus::StaleRecord:
        return QStringLiteral("The recovery snapshot is stale.");
    case CrashRecoveryStatus::WorkspaceMismatch:
        return QStringLiteral("The snapshot belongs to another workspace.");
    case CrashRecoveryStatus::IdentityMismatch:
        return QStringLiteral("The snapshot identity is inconsistent.");
    }
    return QStringLiteral("Unknown recovery status.");
}

QDateTime parseUtcDate(const QJsonValue& value)
{
    QDateTime result =
        QDateTime::fromString(
            value.toString(),
            Qt::ISODateWithMs);
    if (!result.isValid()) {
        result =
            QDateTime::fromString(
                value.toString(),
                Qt::ISODate);
    }
    return result.isValid()
        ? result.toUTC()
        : QDateTime();
}

QString dateText(const QDateTime& value)
{
    return value.isValid()
        ? value.toUTC().toString(Qt::ISODateWithMs)
        : QString();
}

bool readBytes(const QString& path,
               QByteArray* bytes,
               QString* failureReason)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (failureReason) {
            *failureReason =
                QStringLiteral("Cannot read %1: %2")
                    .arg(path, file.errorString());
        }
        return false;
    }
    const QByteArray contents = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        if (failureReason) {
            *failureReason =
                QStringLiteral("Cannot read %1: %2")
                    .arg(path, file.errorString());
        }
        return false;
    }
    if (bytes)
        *bytes = contents;
    return true;
}

QByteArray logicalTextDigest(QByteArray bytes)
{
    static const QByteArray utf8Bom =
        QByteArray::fromHex("efbbbf");
    if (bytes.startsWith(utf8Bom))
        bytes.remove(0, utf8Bom.size());
    QString text = QString::fromUtf8(bytes);
    text.replace(
        QStringLiteral("\r\n"),
        QStringLiteral("\n"));
    text.replace(
        QLatin1Char('\r'),
        QLatin1Char('\n'));
    return CrashRecoveryService::sha256(
        text.toUtf8());
}

bool isHexSha256(const QByteArray& value)
{
    if (value.size() != 64)
        return false;
    for (const char character : value) {
        const bool digit =
            character >= '0' && character <= '9';
        const bool lower =
            character >= 'a' && character <= 'f';
        const bool upper =
            character >= 'A' && character <= 'F';
        if (!digit && !lower && !upper)
            return false;
    }
    return true;
}

bool validRecoveryId(const QString& value)
{
    return isHexSha256(value.toLatin1());
}

QByteArray normalizedDigest(const QByteArray& value)
{
    if (value.isEmpty())
        return {};
    if (value.size() == 32)
        return value;
    if (isHexSha256(value))
        return QByteArray::fromHex(value);
    return {};
}

QJsonObject recordObject(
    const CrashRecoveryCandidate& candidate,
    const QString& text)
{
    QJsonObject object;
    object.insert(
        QStringLiteral("schema"),
        QString::fromLatin1(kSchema));
    object.insert(
        QStringLiteral("version"),
        CrashRecoveryService::kSchemaVersion);
    object.insert(
        QStringLiteral("workspacePath"),
        candidate.workspacePath);
    object.insert(
        QStringLiteral("workspaceIdentity"),
        candidate.workspaceIdentity);
    object.insert(
        QStringLiteral("recoveryId"),
        candidate.recoveryId);
    object.insert(
        QStringLiteral("kind"),
        candidate.untitledDocumentId.isEmpty()
            ? QStringLiteral("file")
            : QStringLiteral("untitled"));
    object.insert(
        QStringLiteral("originalFilePath"),
        candidate.originalFilePath);
    object.insert(
        QStringLiteral("canonicalFileIdentity"),
        candidate.canonicalFileIdentity);
    object.insert(
        QStringLiteral("untitledDocumentId"),
        candidate.untitledDocumentId);
    object.insert(
        QStringLiteral("documentRevision"),
        QString::number(candidate.documentRevision));
    object.insert(
        QStringLiteral("savedBaselineSha256"),
        QString::fromLatin1(
            candidate.savedBaselineSha256.toHex()));
    object.insert(
        QStringLiteral("savedBaselineModifiedUtc"),
        dateText(candidate.savedBaselineModifiedUtc));
    object.insert(
        QStringLiteral("snapshotCreatedUtc"),
        dateText(candidate.snapshotCreatedUtc));
    object.insert(
        QStringLiteral("recoveredTextSha256"),
        QString::fromLatin1(
            candidate.recoveredTextSha256.toHex()));
    object.insert(
        QStringLiteral("recoveredTextUtf8"),
        QString::fromLatin1(
            text.toUtf8().toBase64()));
    return object;
}

CrashRecoveryStatus parseRecord(
    const QByteArray& bytes,
    StoredRecoveryRecord* record,
    QString* reason)
{
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        if (reason) {
            *reason =
                QStringLiteral("Invalid recovery JSON: %1")
                    .arg(parseError.errorString());
        }
        return CrashRecoveryStatus::CorruptRecord;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toString()
            != QString::fromLatin1(kSchema)
        || object.value(QStringLiteral("version")).toInt()
               != CrashRecoveryService::kSchemaVersion) {
        if (reason) {
            *reason =
                QStringLiteral(
                    "Unsupported recovery schema or version.");
        }
        return CrashRecoveryStatus::CorruptRecord;
    }

    StoredRecoveryRecord parsed;
    parsed.candidate.workspacePath =
        object.value(QStringLiteral("workspacePath")).toString();
    parsed.candidate.workspaceIdentity =
        object.value(QStringLiteral("workspaceIdentity")).toString();
    parsed.candidate.recoveryId =
        object.value(QStringLiteral("recoveryId")).toString();
    parsed.candidate.originalFilePath =
        object.value(QStringLiteral("originalFilePath")).toString();
    parsed.candidate.canonicalFileIdentity =
        object.value(
                  QStringLiteral("canonicalFileIdentity"))
            .toString();
    parsed.candidate.untitledDocumentId =
        object.value(
                  QStringLiteral("untitledDocumentId"))
            .toString();

    bool revisionOk = false;
    parsed.candidate.documentRevision =
        object.value(QStringLiteral("documentRevision"))
            .toString()
            .toULongLong(&revisionOk);
    parsed.candidate.savedBaselineSha256 =
        QByteArray::fromHex(
            object.value(
                      QStringLiteral("savedBaselineSha256"))
                .toString()
                .toLatin1());
    parsed.candidate.savedBaselineModifiedUtc =
        parseUtcDate(
            object.value(
                QStringLiteral("savedBaselineModifiedUtc")));
    parsed.candidate.snapshotCreatedUtc =
        parseUtcDate(
            object.value(
                QStringLiteral("snapshotCreatedUtc")));
    parsed.candidate.recoveredTextSha256 =
        QByteArray::fromHex(
            object.value(
                      QStringLiteral("recoveredTextSha256"))
                .toString()
                .toLatin1());
    const QByteArray textBytes =
        QByteArray::fromBase64(
            object.value(
                      QStringLiteral("recoveredTextUtf8"))
                .toString()
                .toLatin1());
    parsed.text = QString::fromUtf8(textBytes);
    parsed.candidate.recoveredTextLength =
        parsed.text.size();

    const QString kind =
        object.value(QStringLiteral("kind")).toString();
    const bool fileKind =
        kind == QStringLiteral("file")
        && !parsed.candidate.originalFilePath.isEmpty()
        && !parsed.candidate.canonicalFileIdentity.isEmpty()
        && parsed.candidate.untitledDocumentId.isEmpty();
    const bool untitledKind =
        kind == QStringLiteral("untitled")
        && parsed.candidate.originalFilePath.isEmpty()
        && parsed.candidate.canonicalFileIdentity.isEmpty()
        && !parsed.candidate.untitledDocumentId.isEmpty();
    const bool baselineDigestValid =
        parsed.candidate.savedBaselineSha256.isEmpty()
        || parsed.candidate.savedBaselineSha256.size() == 32;
    const bool requiredFieldsValid =
        !parsed.candidate.workspacePath.isEmpty()
        && !parsed.candidate.workspaceIdentity.isEmpty()
        && !parsed.candidate.recoveryId.isEmpty()
        && revisionOk
        && parsed.candidate.snapshotCreatedUtc.isValid()
        && parsed.candidate.recoveredTextSha256.size() == 32
        && baselineDigestValid
        && (fileKind || untitledKind);
    if (!requiredFieldsValid
        || CrashRecoveryService::sha256(textBytes)
               != parsed.candidate.recoveredTextSha256) {
        if (reason) {
            *reason =
                QStringLiteral(
                    "Recovery metadata or content integrity is invalid.");
        }
        return CrashRecoveryStatus::CorruptRecord;
    }

    if (record)
        *record = parsed;
    return CrashRecoveryStatus::Success;
}

SourceSnapshot inspectSource(
    const StoredRecoveryRecord& record)
{
    SourceSnapshot source;
    if (!record.candidate.untitledDocumentId.isEmpty()) {
        source.state = CrashRecoverySourceState::Untitled;
        return source;
    }

    const QFileInfo info(
        record.candidate.originalFilePath);
    source.exists = info.isFile();
    if (!source.exists) {
        source.state = CrashRecoverySourceState::Missing;
        return source;
    }
    source.modifiedUtc = info.lastModified().toUTC();

    QString readFailure;
    source.readable =
        readBytes(
            record.candidate.originalFilePath,
            &source.bytes,
            &readFailure);
    if (!source.readable) {
        source.state = CrashRecoverySourceState::Unreadable;
        return source;
    }

    source.sha256 =
        CrashRecoveryService::sha256(source.bytes);
    source.matchesRecovery =
        logicalTextDigest(source.bytes)
        == record.candidate.recoveredTextSha256;
    if (record.candidate.savedBaselineSha256.isEmpty()) {
        source.state =
            CrashRecoverySourceState::BaselineUnavailable;
        return source;
    }

    source.changedSinceBaseline =
        source.sha256
        != record.candidate.savedBaselineSha256;
    source.state =
        source.changedSinceBaseline
        ? CrashRecoverySourceState::ExternallyModified
        : CrashRecoverySourceState::UnchangedSinceBaseline;
    return source;
}

void applySourceSnapshot(
    const SourceSnapshot& source,
    CrashRecoveryCandidate* candidate)
{
    if (!candidate)
        return;
    candidate->sourceState = source.state;
    candidate->currentSourceSha256 = source.sha256;
    candidate->currentSourceModifiedUtc =
        source.modifiedUtc;
    candidate->sourceExists = source.exists;
    candidate->sourceReadable = source.readable;
    candidate->sourceChangedSinceBaseline =
        source.changedSinceBaseline;
}

bool recordIsStale(
    const StoredRecoveryRecord& record,
    const SourceSnapshot& source,
    QString* reason)
{
    if (!record.candidate.savedBaselineSha256.isEmpty()
        && record.candidate.savedBaselineSha256
               == record.candidate.recoveredTextSha256) {
        if (reason) {
            *reason =
                QStringLiteral(
                    "Recovered text is identical to its saved baseline.");
        }
        return true;
    }
    if (source.matchesRecovery) {
        if (reason) {
            *reason =
                QStringLiteral(
                    "The source file already contains the recovered text.");
        }
        return true;
    }
    return false;
}

CrashRecoveryIsolatedRecord isolateRecord(
    const QString& root,
    const QString& workspaceIdentity,
    const QString& path,
    CrashRecoveryStatus status,
    const QString& reason)
{
    CrashRecoveryIsolatedRecord isolated;
    isolated.originalStoragePath = path;
    isolated.status = status;
    isolated.reason = reason;

    const QString quarantineDirectory =
        QDir(root).absoluteFilePath(
            QStringLiteral("%1/%2")
                .arg(QString::fromLatin1(kQuarantineDirectory),
                     workspaceIdentity));
    if (!QDir().mkpath(quarantineDirectory)) {
        isolated.reason +=
            QStringLiteral(
                " Quarantine directory could not be created.");
        return isolated;
    }

    const QString suffix =
        status == CrashRecoveryStatus::StaleRecord
        ? QStringLiteral("stale")
        : QStringLiteral("corrupt");
    const QString destination =
        QDir(quarantineDirectory).absoluteFilePath(
            QStringLiteral("%1-%2-%3.json")
                .arg(
                    QFileInfo(path).completeBaseName(),
                    suffix,
                    QUuid::createUuid()
                        .toString(QUuid::WithoutBraces)));
    if (QFile::rename(path, destination)) {
        isolated.quarantinePath = destination;
    } else {
        isolated.reason +=
            QStringLiteral(
                " The record could not be moved to quarantine.");
    }
    return isolated;
}

QString defaultRecoveryRoot()
{
    const QString base =
        QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir(base).absoluteFilePath(
            QStringLiteral("recovery-v1")));
}
}

bool CrashRecoveryOperationResult::succeeded() const
{
    return status == CrashRecoveryStatus::Success
        || status == CrashRecoveryStatus::AlreadyClean;
}

bool CrashRecoveryService::ResolvedDocumentKey::valid() const
{
    return failureReason.isEmpty()
        && !workspacePath.isEmpty()
        && !workspaceIdentity.isEmpty()
        && !recoveryId.isEmpty();
}

bool CrashRecoveryService::ResolvedDocumentKey::untitled() const
{
    return !untitledDocumentId.isEmpty();
}

CrashRecoveryService::CrashRecoveryService(
    const QString& recoveryRoot)
    : configuredRecoveryRoot(
          recoveryRoot.trimmed().isEmpty()
              ? defaultRecoveryRoot()
              : normalizePath(
                    recoveryRoot,
                    false))
{
}

QString CrashRecoveryService::recoveryRootPath() const
{
    return configuredRecoveryRoot;
}

CrashRecoveryWriteResult
CrashRecoveryService::writeSnapshot(
    const CrashRecoverySnapshotRequest& request) const
{
    CrashRecoveryWriteResult result;
    const ResolvedDocumentKey key =
        resolveDocumentKey(request.document);
    if (!key.valid()) {
        result.status =
            CrashRecoveryStatus::InvalidArgument;
        result.reason = key.failureReason;
        return result;
    }
    if (configuredRecoveryRoot.isEmpty()) {
        result.status =
            CrashRecoveryStatus::StorageUnavailable;
        result.reason = statusReason(result.status);
        return result;
    }

    QByteArray baselineDigest =
        normalizedDigest(
            request.savedBaselineSha256);
    if (!request.savedBaselineSha256.isEmpty()
        && baselineDigest.isEmpty()) {
        result.status =
            CrashRecoveryStatus::InvalidArgument;
        result.reason =
            QStringLiteral(
                "Saved baseline SHA-256 must contain 32 raw bytes "
                "or 64 hexadecimal characters.");
        return result;
    }

    QDateTime baselineModifiedUtc =
        request.savedBaselineModifiedUtc.isValid()
        ? request.savedBaselineModifiedUtc.toUTC()
        : QDateTime();
    if (!key.untitled()
        && !baselineModifiedUtc.isValid()) {
        const QFileInfo sourceInfo(
            key.originalFilePath);
        if (sourceInfo.isFile()) {
            baselineModifiedUtc =
                sourceInfo.lastModified().toUTC();
        }
    }
    if (!key.untitled()
        && baselineDigest.isEmpty()) {
        QByteArray sourceBytes;
        QString readFailure;
        if (readBytes(
                key.originalFilePath,
                &sourceBytes,
                &readFailure)) {
            baselineDigest = sha256(sourceBytes);
        }
    }

    const QByteArray textBytes =
        request.text.toUtf8();
    const QByteArray textDigest =
        sha256(textBytes);
    if (!baselineDigest.isEmpty()
        && baselineDigest == textDigest) {
        result.status =
            CrashRecoveryStatus::StaleRecord;
        result.reason =
            QStringLiteral(
                "A clean document does not require a recovery snapshot.");
        return result;
    }

    CrashRecoveryCandidate candidate;
    candidate.recoveryId = key.recoveryId;
    candidate.workspacePath = key.workspacePath;
    candidate.workspaceIdentity =
        key.workspaceIdentity;
    candidate.originalFilePath =
        key.originalFilePath;
    candidate.canonicalFileIdentity =
        key.canonicalFileIdentity;
    candidate.untitledDocumentId =
        key.untitledDocumentId;
    candidate.documentRevision =
        request.documentRevision;
    candidate.recoveredTextLength =
        request.text.size();
    candidate.recoveredTextSha256 =
        textDigest;
    candidate.savedBaselineSha256 =
        baselineDigest;
    candidate.savedBaselineModifiedUtc =
        baselineModifiedUtc;
    candidate.snapshotCreatedUtc =
        QDateTime::currentDateTimeUtc();

    const QString directory =
        workspaceDirectory(
            key.workspaceIdentity);
    if (!QDir().mkpath(directory)) {
        result.status =
            CrashRecoveryStatus::StorageUnavailable;
        result.reason =
            QStringLiteral(
                "The workspace recovery directory could not be created.");
        return result;
    }

    result.recoveryId = key.recoveryId;
    result.storagePath =
        recordPath(
            key.workspaceIdentity,
            key.recoveryId);
    candidate.storagePath = result.storagePath;

    QSaveFile file(result.storagePath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        result.status = CrashRecoveryStatus::IoError;
        result.reason =
            QStringLiteral("Cannot open recovery snapshot: %1")
                .arg(file.errorString());
        return result;
    }
    const QByteArray payload =
        QJsonDocument(
            recordObject(
                candidate,
                request.text))
            .toJson(QJsonDocument::Compact);
    if (file.write(payload) != payload.size()) {
        result.status = CrashRecoveryStatus::IoError;
        result.reason =
            QStringLiteral("Cannot write recovery snapshot: %1")
                .arg(file.errorString());
        file.cancelWriting();
        return result;
    }
    if (!file.commit()) {
        result.status = CrashRecoveryStatus::IoError;
        result.reason =
            QStringLiteral("Cannot commit recovery snapshot: %1")
                .arg(file.errorString());
        return result;
    }

    result.status = CrashRecoveryStatus::Success;
    result.reason =
        QStringLiteral(
            "Dirty document recovery snapshot was stored atomically.");
    return result;
}

CrashRecoveryListResult
CrashRecoveryService::listCandidates(
    const QString& workspacePath) const
{
    CrashRecoveryListResult result;
    CrashRecoveryDocumentKey workspaceKey;
    workspaceKey.workspacePath = workspacePath;
    workspaceKey.untitledDocumentId =
        QStringLiteral("__workspace_probe__");
    const ResolvedDocumentKey resolved =
        resolveDocumentKey(workspaceKey);
    if (!resolved.valid()) {
        result.status =
            CrashRecoveryStatus::InvalidArgument;
        result.reason = resolved.failureReason;
        return result;
    }
    if (configuredRecoveryRoot.isEmpty()) {
        result.status =
            CrashRecoveryStatus::StorageUnavailable;
        result.reason = statusReason(result.status);
        return result;
    }

    const QString directory =
        workspaceDirectory(
            resolved.workspaceIdentity);
    const QDir records(directory);
    if (!records.exists()) {
        result.status = CrashRecoveryStatus::Success;
        result.reason =
            QStringLiteral(
                "No recovery candidates exist for this workspace.");
        return result;
    }

    const QFileInfoList files =
        records.entryInfoList(
            {QStringLiteral("*.json")},
            QDir::Files | QDir::NoSymLinks,
            QDir::Name);
    for (const QFileInfo& info : files) {
        QByteArray bytes;
        QString readFailure;
        if (!readBytes(
                info.absoluteFilePath(),
                &bytes,
                &readFailure)) {
            CrashRecoveryIsolatedRecord unreadable;
            unreadable.originalStoragePath =
                info.absoluteFilePath();
            unreadable.status =
                CrashRecoveryStatus::IoError;
            unreadable.reason = readFailure;
            result.isolatedRecords.append(
                unreadable);
            continue;
        }

        StoredRecoveryRecord record;
        QString parseFailure;
        const CrashRecoveryStatus parseStatus =
            parseRecord(
                bytes,
                &record,
                &parseFailure);
        if (parseStatus != CrashRecoveryStatus::Success) {
            result.isolatedRecords.append(
                isolateRecord(
                    configuredRecoveryRoot,
                    resolved.workspaceIdentity,
                    info.absoluteFilePath(),
                    parseStatus,
                    parseFailure));
            continue;
        }

        const QString expectedPath =
            recordPath(
                resolved.workspaceIdentity,
                record.candidate.recoveryId);
        if (record.candidate.workspaceIdentity
                != resolved.workspaceIdentity
            || comparisonKey(record.candidate.workspacePath)
                   != comparisonKey(resolved.workspacePath)) {
            result.isolatedRecords.append(
                isolateRecord(
                    configuredRecoveryRoot,
                    resolved.workspaceIdentity,
                    info.absoluteFilePath(),
                    CrashRecoveryStatus::WorkspaceMismatch,
                    statusReason(
                        CrashRecoveryStatus::WorkspaceMismatch)));
            continue;
        }
        if (comparisonKey(expectedPath)
                != comparisonKey(info.absoluteFilePath())
            || expectedRecoveryId(record.candidate)
                   != record.candidate.recoveryId
            || !currentCanonicalIdentityMatches(
                   record.candidate)) {
            result.isolatedRecords.append(
                isolateRecord(
                    configuredRecoveryRoot,
                    resolved.workspaceIdentity,
                    info.absoluteFilePath(),
                    CrashRecoveryStatus::IdentityMismatch,
                    statusReason(
                        CrashRecoveryStatus::IdentityMismatch)));
            continue;
        }

        const SourceSnapshot source =
            inspectSource(record);
        QString staleReason;
        if (recordIsStale(
                record,
                source,
                &staleReason)) {
            result.isolatedRecords.append(
                isolateRecord(
                    configuredRecoveryRoot,
                    resolved.workspaceIdentity,
                    info.absoluteFilePath(),
                    CrashRecoveryStatus::StaleRecord,
                    staleReason));
            continue;
        }

        record.candidate.storagePath =
            info.absoluteFilePath();
        applySourceSnapshot(
            source,
            &record.candidate);
        result.candidates.append(
            record.candidate);
    }

    result.status = CrashRecoveryStatus::Success;
    result.reason =
        result.candidates.isEmpty()
        ? QStringLiteral(
              "No active recovery candidates exist for this workspace.")
        : QStringLiteral(
              "Recovery candidates were loaded without modifying source files.");
    if (!result.isolatedRecords.isEmpty()) {
        result.reason +=
            QStringLiteral(
                " Invalid or stale records were quarantined; "
                "transient I/O failures were left in place.");
    }
    return result;
}

CrashRecoveryReadResult
CrashRecoveryService::readComparison(
    const QString& workspacePath,
    const QString& recoveryId) const
{
    CrashRecoveryReadResult result;
    CrashRecoveryDocumentKey workspaceKey;
    workspaceKey.workspacePath = workspacePath;
    workspaceKey.untitledDocumentId =
        QStringLiteral("__workspace_probe__");
    const ResolvedDocumentKey resolved =
        resolveDocumentKey(workspaceKey);
    if (!resolved.valid()
        || !validRecoveryId(recoveryId)) {
        result.status =
            CrashRecoveryStatus::InvalidArgument;
        result.reason =
            !resolved.valid()
            ? resolved.failureReason
            : QStringLiteral(
                  "Recovery ID must be a SHA-256 identity.");
        return result;
    }
    if (configuredRecoveryRoot.isEmpty()) {
        result.status =
            CrashRecoveryStatus::StorageUnavailable;
        result.reason = statusReason(result.status);
        return result;
    }

    const QString path =
        recordPath(
            resolved.workspaceIdentity,
            recoveryId);
    if (!QFileInfo(path).isFile()) {
        result.status = CrashRecoveryStatus::NotFound;
        result.reason = statusReason(result.status);
        return result;
    }

    QByteArray bytes;
    QString readFailure;
    if (!readBytes(path, &bytes, &readFailure)) {
        result.status = CrashRecoveryStatus::IoError;
        result.reason = readFailure;
        return result;
    }

    StoredRecoveryRecord record;
    QString parseFailure;
    result.status =
        parseRecord(
            bytes,
            &record,
            &parseFailure);
    if (result.status != CrashRecoveryStatus::Success) {
        const CrashRecoveryIsolatedRecord isolated =
            isolateRecord(
                configuredRecoveryRoot,
                resolved.workspaceIdentity,
                path,
                result.status,
                parseFailure);
        result.reason = isolated.reason;
        return result;
    }

    if (record.candidate.workspaceIdentity
            != resolved.workspaceIdentity
        || comparisonKey(record.candidate.workspacePath)
               != comparisonKey(resolved.workspacePath)) {
        result.status =
            CrashRecoveryStatus::WorkspaceMismatch;
        result.reason = statusReason(result.status);
        isolateRecord(
            configuredRecoveryRoot,
            resolved.workspaceIdentity,
            path,
            result.status,
            result.reason);
        return result;
    }
    if (record.candidate.recoveryId != recoveryId
        || expectedRecoveryId(record.candidate)
               != recoveryId
        || !currentCanonicalIdentityMatches(
               record.candidate)) {
        result.status =
            CrashRecoveryStatus::IdentityMismatch;
        result.reason = statusReason(result.status);
        isolateRecord(
            configuredRecoveryRoot,
            resolved.workspaceIdentity,
            path,
            result.status,
            result.reason);
        return result;
    }

    const SourceSnapshot source =
        inspectSource(record);
    QString staleReason;
    if (recordIsStale(
            record,
            source,
            &staleReason)) {
        result.status =
            CrashRecoveryStatus::StaleRecord;
        result.reason = staleReason;
        isolateRecord(
            configuredRecoveryRoot,
            resolved.workspaceIdentity,
            path,
            result.status,
            staleReason);
        return result;
    }

    record.candidate.storagePath = path;
    applySourceSnapshot(
        source,
        &record.candidate);
    result.candidate = record.candidate;
    result.recoveredText = record.text;
    result.currentSourceBytes = source.bytes;
    result.status = CrashRecoveryStatus::Success;
    result.reason =
        QStringLiteral(
            "Recovery and current source content were read for comparison; "
            "the source file was not modified.");
    return result;
}

CrashRecoveryRecoverResult
CrashRecoveryService::recoverText(
    const QString& workspacePath,
    const QString& recoveryId) const
{
    CrashRecoveryRecoverResult result;
    const CrashRecoveryReadResult read =
        readComparison(
            workspacePath,
            recoveryId);
    result.status = read.status;
    result.reason = read.reason;
    result.candidate = read.candidate;
    if (!read.succeeded())
        return result;

    result.text = read.recoveredText;
    result.reason =
        QStringLiteral(
            "Recovery text was returned in memory; no source file write "
            "was attempted.");
    return result;
}

CrashRecoveryOperationResult
CrashRecoveryService::discard(
    const QString& workspacePath,
    const QString& recoveryId) const
{
    CrashRecoveryOperationResult result;
    CrashRecoveryDocumentKey workspaceKey;
    workspaceKey.workspacePath = workspacePath;
    workspaceKey.untitledDocumentId =
        QStringLiteral("__workspace_probe__");
    const ResolvedDocumentKey resolved =
        resolveDocumentKey(workspaceKey);
    if (!resolved.valid()
        || !validRecoveryId(recoveryId)) {
        result.status =
            CrashRecoveryStatus::InvalidArgument;
        result.reason =
            !resolved.valid()
            ? resolved.failureReason
            : QStringLiteral(
                  "Recovery ID must be a SHA-256 identity.");
        return result;
    }
    if (configuredRecoveryRoot.isEmpty()) {
        result.status =
            CrashRecoveryStatus::StorageUnavailable;
        result.reason = statusReason(result.status);
        return result;
    }

    const QString path =
        recordPath(
            resolved.workspaceIdentity,
            recoveryId);
    if (!QFileInfo::exists(path)) {
        result.status =
            CrashRecoveryStatus::AlreadyClean;
        result.reason =
            QStringLiteral(
                "The recovery snapshot was already absent.");
        return result;
    }
    if (!QFile::remove(path)) {
        result.status = CrashRecoveryStatus::IoError;
        result.reason =
            QStringLiteral(
                "The recovery snapshot could not be discarded.");
        return result;
    }
    result.status = CrashRecoveryStatus::Success;
    result.reason =
        QStringLiteral(
            "The recovery snapshot was explicitly discarded.");
    return result;
}

CrashRecoveryOperationResult
CrashRecoveryService::clearAfterNormalSave(
    const CrashRecoveryDocumentKey& document) const
{
    return clearDocument(
        document,
        QStringLiteral(
            "The recovery snapshot was removed after a normal save."));
}

CrashRecoveryOperationResult
CrashRecoveryService::clearAfterNormalClose(
    const CrashRecoveryDocumentKey& document) const
{
    return clearDocument(
        document,
        QStringLiteral(
            "The recovery snapshot was removed after a normal close."));
}

QByteArray CrashRecoveryService::sha256(
    const QByteArray& bytes)
{
    return QCryptographicHash::hash(
        bytes,
        QCryptographicHash::Sha256);
}

QString CrashRecoveryService::normalizePath(
    const QString& path,
    bool canonicalizeExisting)
{
    if (path.trimmed().isEmpty())
        return QString();
    return canonicalizeExisting
        ? EditorFileIdentity::lookupKey(path)
        : EditorFileIdentity::normalized(path);
}

QString CrashRecoveryService::comparisonKey(
    const QString& path)
{
    const QString identity =
        EditorFileIdentity::lookupKey(path);
    return identity.isEmpty()
        ? pathIdentityKey(path)
        : identity;
}

QString CrashRecoveryService::identityDigest(
    const QString& value)
{
    return QString::fromLatin1(
        sha256(value.toUtf8()).toHex());
}

CrashRecoveryService::ResolvedDocumentKey
CrashRecoveryService::resolveDocumentKey(
    const CrashRecoveryDocumentKey& document)
{
    ResolvedDocumentKey result;
    result.workspacePath =
        normalizePath(
            document.workspacePath,
            true);
    if (result.workspacePath.isEmpty()) {
        result.failureReason =
            QStringLiteral(
                "Workspace path must not be empty.");
        return result;
    }
    result.workspaceIdentity =
        identityDigest(
            comparisonKey(
                result.workspacePath));

    const bool hasFile =
        !document.originalFilePath.trimmed().isEmpty();
    const bool hasUntitled =
        !document.untitledDocumentId.trimmed().isEmpty();
    if (hasFile == hasUntitled) {
        result.failureReason =
            QStringLiteral(
                "Exactly one file path or untitled document ID is required.");
        return result;
    }

    QString identityText;
    if (hasFile) {
        result.originalFilePath =
            normalizePath(
                document.originalFilePath,
                false);
        result.canonicalFileIdentity =
            normalizePath(
                document.originalFilePath,
                true);
        if (result.originalFilePath.isEmpty()
            || result.canonicalFileIdentity.isEmpty()) {
            result.failureReason =
                QStringLiteral(
                    "The source file identity is invalid.");
            return result;
        }
        identityText =
            QStringLiteral("file:")
            + comparisonKey(
                  result.canonicalFileIdentity);
    } else {
        result.untitledDocumentId =
            document.untitledDocumentId.trimmed();
        identityText =
            QStringLiteral("untitled:")
            + result.untitledDocumentId;
    }
    result.recoveryId =
        identityDigest(
            QStringLiteral("workspace:")
            + result.workspaceIdentity
            + QLatin1Char('\n')
            + identityText);
    return result;
}

QString CrashRecoveryService::workspaceDirectory(
    const QString& workspaceIdentity) const
{
    if (configuredRecoveryRoot.isEmpty()
        || workspaceIdentity.isEmpty()) {
        return QString();
    }
    return QDir(configuredRecoveryRoot)
        .absoluteFilePath(
            QStringLiteral("%1/%2/%3")
                .arg(
                    QString::fromLatin1(
                        kWorkspacesDirectory),
                    workspaceIdentity,
                    QString::fromLatin1(
                        kRecordsDirectory)));
}

QString CrashRecoveryService::recordPath(
    const QString& workspaceIdentity,
    const QString& recoveryId) const
{
    const QString directory =
        workspaceDirectory(
            workspaceIdentity);
    if (directory.isEmpty()
        || recoveryId.isEmpty()) {
        return QString();
    }
    return QDir(directory)
        .absoluteFilePath(
            recoveryId
            + QStringLiteral(".json"));
}

CrashRecoveryOperationResult
CrashRecoveryService::clearDocument(
    const CrashRecoveryDocumentKey& document,
    const QString& successReason) const
{
    CrashRecoveryOperationResult result;
    const ResolvedDocumentKey key =
        resolveDocumentKey(document);
    if (!key.valid()) {
        result.status =
            CrashRecoveryStatus::InvalidArgument;
        result.reason = key.failureReason;
        return result;
    }
    if (configuredRecoveryRoot.isEmpty()) {
        result.status =
            CrashRecoveryStatus::StorageUnavailable;
        result.reason = statusReason(result.status);
        return result;
    }

    const QString path =
        recordPath(
            key.workspaceIdentity,
            key.recoveryId);
    if (!QFileInfo::exists(path)) {
        result.status =
            CrashRecoveryStatus::AlreadyClean;
        result.reason =
            QStringLiteral(
                "No recovery snapshot remained to clean.");
        return result;
    }
    if (!QFile::remove(path)) {
        result.status = CrashRecoveryStatus::IoError;
        result.reason =
            QStringLiteral(
                "The recovery snapshot could not be removed.");
        return result;
    }

    result.status = CrashRecoveryStatus::Success;
    result.reason = successReason;
    return result;
}
