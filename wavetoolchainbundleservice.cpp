#include "wavetoolchainbundleservice.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

namespace {
constexpr int kProcessStartTimeoutMs = 10000;
constexpr int kExtractionTimeoutMs = 30 * 60 * 1000;

QString cleanAbsolutePath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(
        QFileInfo(path).absoluteFilePath()));
}

bool isCancelled(const std::function<bool()>& callback)
{
    return callback && callback();
}

QString executableName(const QString& baseName)
{
#ifdef Q_OS_WIN
    return baseName + QStringLiteral(".exe");
#else
    return baseName;
#endif
}

QStringList requiredToolchainFiles()
{
    return {
        QStringLiteral("toolchain-manifest.json"),
        QStringLiteral("verilator/bin/")
            + executableName(QStringLiteral("verilator")),
        QStringLiteral("verilator/bin/")
            + executableName(QStringLiteral("verilator_bin")),
        QStringLiteral("verilator/include/verilated.mk"),
        QStringLiteral("mingw/bin/")
            + executableName(QStringLiteral("g++")),
        QStringLiteral("mingw/bin/")
            + executableName(QStringLiteral("mingw32-make")),
    };
}

bool isToolchainRootValid(const QString& root)
{
    if (!QFileInfo(root).isDir())
        return false;
    const QDir directory(root);
    for (const QString& relative : requiredToolchainFiles()) {
        if (!QFileInfo(directory.absoluteFilePath(relative)).isFile())
            return false;
    }
    QFile manifest(directory.absoluteFilePath(
        QStringLiteral("toolchain-manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(
        manifest.readAll());
    return document.isObject()
        && document.object().value(QStringLiteral("schema")).toString()
               == QStringLiteral("zeroslack.wave-toolchain/v1");
}

QString readyMarkerPath(const QString& root)
{
    return QDir(root).absoluteFilePath(
        QStringLiteral(".zeroslack-toolchain-ready.json"));
}

bool isReadyCache(const QString& root, const QString& sha256)
{
    if (!isToolchainRootValid(root))
        return false;
    QFile marker(readyMarkerPath(root));
    if (!marker.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(marker.readAll());
    return document.isObject()
        && document.object().value(QStringLiteral("archiveSha256"))
                   .toString().compare(sha256, Qt::CaseInsensitive) == 0;
}

bool writeReadyMarker(const QString& root,
                      const WaveToolchainBundleDescriptor& descriptor)
{
    QJsonObject object;
    object.insert(QStringLiteral("schema"),
                  QStringLiteral("zeroslack.wave-toolchain-cache/v1"));
    object.insert(QStringLiteral("bundleId"), descriptor.bundleId);
    object.insert(QStringLiteral("archiveSha256"),
                  descriptor.archiveSha256.toLower());
    QFile marker(readyMarkerPath(root));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return marker.write(QJsonDocument(object).toJson(
                            QJsonDocument::Compact)) >= 0;
}

bool removeOwnedCachePath(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return true;
    if (info.isFile() || info.isSymLink())
        return QFile::remove(path);
    return QDir(path).removeRecursively();
}

bool safeRelativeArchivePath(const QString& rawPath)
{
    QString path = QDir::fromNativeSeparators(rawPath.trimmed());
    while (path.startsWith(QStringLiteral("./")))
        path.remove(0, 2);
    if (path.isEmpty() || path == QStringLiteral("."))
        return true;
    const bool hasDrivePrefix = path.size() >= 2
        && path.at(1) == QLatin1Char(':')
        && ((path.at(0) >= QLatin1Char('A')
             && path.at(0) <= QLatin1Char('Z'))
            || (path.at(0) >= QLatin1Char('a')
                && path.at(0) <= QLatin1Char('z')));
    if (QDir::isAbsolutePath(path) || hasDrivePrefix) {
        return false;
    }
    const QString clean = QDir::cleanPath(path);
    return clean != QStringLiteral("..")
        && !clean.startsWith(QStringLiteral("../"));
}

bool isHexSha256(const QString& text)
{
    if (text.size() != 64)
        return false;
    for (const QChar character : text) {
        const bool decimal = character >= QLatin1Char('0')
            && character <= QLatin1Char('9');
        const QChar lower = character.toLower();
        if (!decimal
            && !(lower >= QLatin1Char('a')
                 && lower <= QLatin1Char('f'))) {
            return false;
        }
    }
    return true;
}

bool isSafeBundleId(const QString& text)
{
    if (text.isEmpty())
        return false;
    for (const QChar character : text) {
        if (!character.isLetterOrNumber()
            && character != QLatin1Char('.')
            && character != QLatin1Char('_')
            && character != QLatin1Char('-')) {
            return false;
        }
    }
    return true;
}

struct ProcessResult {
    bool started = false;
    bool cancelled = false;
    bool timedOut = false;
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
};

ProcessResult runProcess(const QString& program,
                         const QStringList& arguments,
                         const std::function<bool()>& cancellation)
{
    ProcessResult result;
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();
    result.started = process.waitForStarted(kProcessStartTimeoutMs);
    if (!result.started) {
        result.standardError = process.errorString().toUtf8();
        return result;
    }

    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(100)) {
        if (isCancelled(cancellation)) {
            result.cancelled = true;
            process.kill();
            process.waitForFinished();
            break;
        }
        if (timer.elapsed() >= kExtractionTimeoutMs) {
            result.timedOut = true;
            process.kill();
            process.waitForFinished();
            break;
        }
    }
    result.exitCode = process.exitCode();
    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    return result;
}

QString systemTarProgram()
{
    QString program = QStandardPaths::findExecutable(
        executableName(QStringLiteral("tar")));
#ifdef Q_OS_WIN
    if (program.isEmpty()) {
        const QString systemRoot = qEnvironmentVariable("SystemRoot");
        const QString candidate = QDir(systemRoot).absoluteFilePath(
            QStringLiteral("System32/tar.exe"));
        if (QFileInfo(candidate).isFile())
            program = candidate;
    }
#endif
    return program;
}

QString powershellProgram()
{
#ifdef Q_OS_WIN
    QString program = QStandardPaths::findExecutable(
        QStringLiteral("powershell.exe"));
    if (program.isEmpty()) {
        const QString systemRoot = qEnvironmentVariable("SystemRoot");
        const QString candidate = QDir(systemRoot).absoluteFilePath(
            QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
        if (QFileInfo(candidate).isFile())
            program = candidate;
    }
    return program;
#else
    return {};
#endif
}

WaveToolchainBundleResult failure(WaveToolchainBundleStatus status,
                                  const QString& message)
{
    WaveToolchainBundleResult result;
    result.status = status;
    result.message = message;
    return result;
}

WaveToolchainBundleResult extractWithTar(
    const QString& tar,
    const WaveToolchainBundleDescriptor& descriptor,
    const QString& staging,
    const std::function<bool()>& cancellation)
{
    const ProcessResult listing = runProcess(
        tar,
        {QStringLiteral("-tf"), descriptor.archivePath},
        cancellation);
    if (listing.cancelled)
        return failure(WaveToolchainBundleStatus::Cancelled,
                       QStringLiteral("Wave toolchain preparation was cancelled."));
    if (!listing.started || listing.timedOut || listing.exitCode != 0) {
        return failure(
            WaveToolchainBundleStatus::ExtractionFailed,
            QStringLiteral("Cannot inspect the Wave toolchain archive: %1")
                .arg(QString::fromUtf8(listing.standardError).trimmed()));
    }
    const QList<QByteArray> entries = listing.standardOutput.split('\n');
    for (const QByteArray& entry : entries) {
        if (!entry.trimmed().isEmpty()
            && !safeRelativeArchivePath(QString::fromUtf8(entry))) {
            return failure(
                WaveToolchainBundleStatus::UnsafeArchive,
                QStringLiteral("The Wave toolchain archive contains an unsafe path."));
        }
    }

    const ProcessResult extraction = runProcess(
        tar,
        {QStringLiteral("-xf"), descriptor.archivePath,
         QStringLiteral("-C"), staging},
        cancellation);
    if (extraction.cancelled)
        return failure(WaveToolchainBundleStatus::Cancelled,
                       QStringLiteral("Wave toolchain preparation was cancelled."));
    if (!extraction.started || extraction.timedOut
        || extraction.exitCode != 0) {
        return failure(
            WaveToolchainBundleStatus::ExtractionFailed,
            QStringLiteral("Cannot extract the Wave toolchain archive: %1")
                .arg(QString::fromUtf8(extraction.standardError).trimmed()));
    }
    WaveToolchainBundleResult result;
    result.status = WaveToolchainBundleStatus::Ready;
    return result;
}

WaveToolchainBundleResult extractWithPowerShell(
    const QString& powershell,
    const WaveToolchainBundleDescriptor& descriptor,
    const QString& staging,
    const std::function<bool()>& cancellation)
{
    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop';"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$archive=[IO.Path]::GetFullPath($args[0]);"
        "$dest=[IO.Path]::GetFullPath($args[1]);"
        "$prefix=$dest.TrimEnd([IO.Path]::DirectorySeparatorChar,[IO.Path]::AltDirectorySeparatorChar)+[IO.Path]::DirectorySeparatorChar;"
        "$zip=[IO.Compression.ZipFile]::OpenRead($archive);"
        "try{foreach($entry in $zip.Entries){"
        "$name=$entry.FullName.Replace('\\','/');"
        "if([IO.Path]::IsPathRooted($name)){throw 'Unsafe archive path.'};"
        "$target=[IO.Path]::GetFullPath([IO.Path]::Combine($dest,$name));"
        "if(($target -ne $dest)-and(-not $target.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase))){throw 'Unsafe archive path.'}"
        "}}finally{$zip.Dispose()};"
        "[IO.Compression.ZipFile]::ExtractToDirectory($archive,$dest)");
    const ProcessResult extraction = runProcess(
        powershell,
        {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
         QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
         script, descriptor.archivePath, staging},
        cancellation);
    if (extraction.cancelled)
        return failure(WaveToolchainBundleStatus::Cancelled,
                       QStringLiteral("Wave toolchain preparation was cancelled."));
    if (!extraction.started || extraction.timedOut
        || extraction.exitCode != 0) {
        const QString detail = QString::fromUtf8(
            extraction.standardError).trimmed();
        const WaveToolchainBundleStatus status =
            detail.contains(QStringLiteral("Unsafe archive path"),
                            Qt::CaseInsensitive)
            ? WaveToolchainBundleStatus::UnsafeArchive
            : WaveToolchainBundleStatus::ExtractionFailed;
        return failure(
            status,
            QStringLiteral("Cannot extract the Wave toolchain archive: %1")
                .arg(detail));
    }
    WaveToolchainBundleResult result;
    result.status = WaveToolchainBundleStatus::Ready;
    return result;
}

QString archiveDigest(const QString& archivePath,
                      const std::function<bool()>& cancellation,
                      bool* cancelled)
{
    if (cancelled)
        *cancelled = false;
    QFile archive(archivePath);
    if (!archive.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!archive.atEnd()) {
        if (isCancelled(cancellation)) {
            if (cancelled)
                *cancelled = true;
            return {};
        }
        const QByteArray chunk = archive.read(4 * 1024 * 1024);
        if (chunk.isEmpty() && archive.error() != QFile::NoError)
            return {};
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}
}

bool WaveToolchainBundleDescriptor::isValid() const
{
    return QFileInfo(manifestPath).isFile()
        && !archivePath.isEmpty()
        && isHexSha256(archiveSha256)
        && !bundleId.trimmed().isEmpty()
        && archiveBytes > 0;
}

bool WaveToolchainBundleResult::succeeded() const
{
    return status == WaveToolchainBundleStatus::Ready
        && isToolchainRootValid(toolchainRoot);
}

WaveToolchainBundleDescriptor WaveToolchainBundleService::fromManifest(
    const QString& manifestPath,
    QString* failureReason)
{
    const auto reject = [failureReason](const QString& message) {
        if (failureReason)
            *failureReason = message;
        return WaveToolchainBundleDescriptor{};
    };
    QFile file(cleanAbsolutePath(manifestPath));
    if (!file.open(QIODevice::ReadOnly))
        return reject(QStringLiteral("Cannot read the bundle manifest."));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        file.readAll(), &parseError);
    if (!document.isObject())
        return reject(QStringLiteral("The bundle manifest is not valid JSON."));
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toString()
        != QStringLiteral("zeroslack.wave-toolchain-bundle/v1")) {
        return reject(QStringLiteral("Unsupported Wave toolchain bundle schema."));
    }
    if (object.value(QStringLiteral("schemaVersion")).toInt() != 1)
        return reject(QStringLiteral("Unsupported Wave toolchain bundle version."));
    const QString archiveName = object.value(
        QStringLiteral("archive")).toString().trimmed();
    if (archiveName.isEmpty() || !safeRelativeArchivePath(archiveName))
        return reject(QStringLiteral("The bundle archive path is unsafe."));
    const QString hash = object.value(
        QStringLiteral("archiveSha256")).toString().trimmed().toLower();
    if (!isHexSha256(hash))
        return reject(QStringLiteral("The bundle SHA-256 is invalid."));

    WaveToolchainBundleDescriptor descriptor;
    descriptor.manifestPath = QFileInfo(file).absoluteFilePath();
    descriptor.archivePath = cleanAbsolutePath(
        QFileInfo(descriptor.manifestPath).absoluteDir()
            .absoluteFilePath(archiveName));
    descriptor.archiveSha256 = hash;
    descriptor.bundleId = object.value(
        QStringLiteral("bundleId")).toString().trimmed();
    if (descriptor.bundleId.isEmpty())
        descriptor.bundleId = QStringLiteral("sha256-") + hash;
    if (!isSafeBundleId(descriptor.bundleId))
        return reject(QStringLiteral("The bundle id is invalid."));
    const QJsonValue bytesValue = object.value(
        QStringLiteral("archiveBytes"));
    if (bytesValue.isDouble())
        descriptor.archiveBytes = static_cast<qint64>(bytesValue.toDouble());
    if (!descriptor.isValid())
        return reject(QStringLiteral("The bundle manifest is incomplete."));
    return descriptor;
}

WaveToolchainBundleDescriptor WaveToolchainBundleService::discover(
    const QStringList& manifestCandidates)
{
    QSet<QString> visited;
    for (const QString& candidate : manifestCandidates) {
        const QString normalized = cleanAbsolutePath(candidate);
        if (normalized.isEmpty() || visited.contains(normalized))
            continue;
        visited.insert(normalized);
        const WaveToolchainBundleDescriptor descriptor =
            fromManifest(normalized);
        if (descriptor.isValid())
            return descriptor;
    }
    return {};
}

QString WaveToolchainBundleService::defaultCacheRoot()
{
    QString base = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        return {};
    return QDir::cleanPath(QDir(base).absoluteFilePath(
        QStringLiteral("toolchains")));
}

QString WaveToolchainBundleService::cachedToolchainRoot(
    const WaveToolchainBundleDescriptor& descriptor,
    const QString& cacheRootOverride)
{
    if (!descriptor.isValid())
        return {};
    const QString cacheRoot = cacheRootOverride.trimmed().isEmpty()
        ? defaultCacheRoot()
        : cleanAbsolutePath(cacheRootOverride);
    if (cacheRoot.isEmpty())
        return {};
    const QString root = QDir(cacheRoot).absoluteFilePath(
        descriptor.archiveSha256.toLower());
    return isReadyCache(root, descriptor.archiveSha256)
        ? QDir::cleanPath(root)
        : QString();
}

WaveToolchainBundleResult WaveToolchainBundleService::prepare(
    const WaveToolchainBundleDescriptor& descriptor,
    const QString& cacheRootOverride,
    const std::function<bool()>& cancellation)
{
    if (!descriptor.isValid()) {
        return failure(WaveToolchainBundleStatus::InvalidManifest,
                       QStringLiteral("The Wave toolchain bundle manifest is invalid."));
    }
    const QString cacheRoot = cacheRootOverride.trimmed().isEmpty()
        ? defaultCacheRoot()
        : cleanAbsolutePath(cacheRootOverride);
    if (cacheRoot.isEmpty() || !QDir().mkpath(cacheRoot)) {
        return failure(WaveToolchainBundleStatus::CacheUnavailable,
                       QStringLiteral("Cannot create the local Wave toolchain cache."));
    }
    const QString finalRoot = QDir(cacheRoot).absoluteFilePath(
        descriptor.archiveSha256.toLower());
    if (isReadyCache(finalRoot, descriptor.archiveSha256)) {
        WaveToolchainBundleResult result;
        result.status = WaveToolchainBundleStatus::Ready;
        result.toolchainRoot = QDir::cleanPath(finalRoot);
        result.message = QStringLiteral("Using the cached Wave toolchain.");
        result.reused = true;
        return result;
    }
    QLockFile cacheLock(finalRoot + QStringLiteral(".lock"));
    cacheLock.setStaleLockTime(kExtractionTimeoutMs * 2);
    QElapsedTimer lockTimer;
    lockTimer.start();
    while (!cacheLock.tryLock(100)) {
        if (isReadyCache(finalRoot, descriptor.archiveSha256)) {
            WaveToolchainBundleResult result;
            result.status = WaveToolchainBundleStatus::Ready;
            result.toolchainRoot = QDir::cleanPath(finalRoot);
            result.message = QStringLiteral("Using the cached Wave toolchain.");
            result.reused = true;
            return result;
        }
        if (isCancelled(cancellation)) {
            return failure(WaveToolchainBundleStatus::Cancelled,
                           QStringLiteral("Wave toolchain preparation was cancelled."));
        }
        if (lockTimer.elapsed() >= kExtractionTimeoutMs) {
            return failure(WaveToolchainBundleStatus::CacheUnavailable,
                           QStringLiteral("Timed out waiting for another process to prepare the Wave toolchain cache."));
        }
    }
    if (isReadyCache(finalRoot, descriptor.archiveSha256)) {
        WaveToolchainBundleResult result;
        result.status = WaveToolchainBundleStatus::Ready;
        result.toolchainRoot = QDir::cleanPath(finalRoot);
        result.message = QStringLiteral("Using the cached Wave toolchain.");
        result.reused = true;
        return result;
    }
    if (isCancelled(cancellation)) {
        return failure(WaveToolchainBundleStatus::Cancelled,
                       QStringLiteral("Wave toolchain preparation was cancelled."));
    }
    const QFileInfo archiveInfo(descriptor.archivePath);
    if (!archiveInfo.isFile()) {
        return failure(WaveToolchainBundleStatus::ArchiveMissing,
                       QStringLiteral("The bundled Wave toolchain archive is missing: %1")
                           .arg(QDir::toNativeSeparators(descriptor.archivePath)));
    }
    if (descriptor.archiveBytes >= 0
        && archiveInfo.size() != descriptor.archiveBytes) {
        return failure(WaveToolchainBundleStatus::ArchiveSizeMismatch,
                       QStringLiteral("The bundled Wave toolchain archive size does not match its manifest."));
    }
    bool digestCancelled = false;
    const QString digest = archiveDigest(
        descriptor.archivePath, cancellation, &digestCancelled);
    if (digestCancelled) {
        return failure(WaveToolchainBundleStatus::Cancelled,
                       QStringLiteral("Wave toolchain preparation was cancelled."));
    }
    if (digest.compare(descriptor.archiveSha256,
                       Qt::CaseInsensitive) != 0) {
        return failure(WaveToolchainBundleStatus::ArchiveHashMismatch,
                       QStringLiteral("The bundled Wave toolchain archive failed SHA-256 verification."));
    }

    if (!removeOwnedCachePath(finalRoot)) {
        return failure(WaveToolchainBundleStatus::CacheUnavailable,
                       QStringLiteral("Cannot replace an incomplete Wave toolchain cache."));
    }
    const QString staging = finalRoot
        + QStringLiteral(".staging-")
        + QString::number(QCoreApplication::applicationPid())
        + QLatin1Char('-')
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(staging)) {
        return failure(WaveToolchainBundleStatus::CacheUnavailable,
                       QStringLiteral("Cannot create the Wave toolchain staging directory."));
    }
    const auto cleanup = [&staging]() {
        if (QFileInfo(staging).exists())
            QDir(staging).removeRecursively();
    };

    WaveToolchainBundleResult extraction;
    const QString tar = systemTarProgram();
    if (!tar.isEmpty()) {
        extraction = extractWithTar(tar, descriptor, staging, cancellation);
    } else {
        const QString powershell = powershellProgram();
        if (powershell.isEmpty()) {
            cleanup();
            return failure(
                WaveToolchainBundleStatus::ExtractorUnavailable,
                QStringLiteral("Windows tar and PowerShell are unavailable; the Wave toolchain archive cannot be extracted."));
        }
        extraction = extractWithPowerShell(
            powershell, descriptor, staging, cancellation);
    }
    if (extraction.status != WaveToolchainBundleStatus::Ready) {
        cleanup();
        return extraction;
    }
    if (!isToolchainRootValid(staging)) {
        cleanup();
        return failure(WaveToolchainBundleStatus::InvalidToolchain,
                       QStringLiteral("The extracted Wave toolchain is incomplete."));
    }
    if (!writeReadyMarker(staging, descriptor)) {
        cleanup();
        return failure(WaveToolchainBundleStatus::CacheUnavailable,
                       QStringLiteral("Cannot finalize the local Wave toolchain cache."));
    }

    if (isReadyCache(finalRoot, descriptor.archiveSha256)) {
        cleanup();
    } else {
        if (!removeOwnedCachePath(finalRoot)) {
            cleanup();
            return failure(WaveToolchainBundleStatus::CacheUnavailable,
                           QStringLiteral("Cannot replace an incomplete Wave toolchain cache."));
        }
        if (!QDir().rename(staging, finalRoot)) {
            if (!isReadyCache(finalRoot, descriptor.archiveSha256)) {
                cleanup();
                return failure(WaveToolchainBundleStatus::CacheUnavailable,
                               QStringLiteral("Cannot publish the local Wave toolchain cache."));
            }
            cleanup();
        }
    }
    WaveToolchainBundleResult result;
    result.status = WaveToolchainBundleStatus::Ready;
    result.toolchainRoot = QDir::cleanPath(finalRoot);
    result.message = QStringLiteral("The Wave toolchain is ready in the local cache.");
    return result;
}
