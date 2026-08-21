#ifndef WAVETOOLCHAINBUNDLESERVICE_H
#define WAVETOOLCHAINBUNDLESERVICE_H

#include <QString>
#include <QStringList>

#include <functional>

struct WaveToolchainBundleDescriptor {
    QString manifestPath;
    QString archivePath;
    QString archiveSha256;
    QString bundleId;
    qint64 archiveBytes = -1;

    bool isValid() const;
};

enum class WaveToolchainBundleStatus {
    Ready,
    Cancelled,
    InvalidManifest,
    CacheUnavailable,
    ArchiveMissing,
    ArchiveSizeMismatch,
    ArchiveHashMismatch,
    UnsafeArchive,
    ExtractorUnavailable,
    ExtractionFailed,
    InvalidToolchain
};

struct WaveToolchainBundleResult {
    WaveToolchainBundleStatus status =
        WaveToolchainBundleStatus::InvalidManifest;
    QString toolchainRoot;
    QString message;
    bool reused = false;

    bool succeeded() const;
};

class WaveToolchainBundleService
{
public:
    static constexpr const char* kManifestFileName =
        "wave-toolchain-bundle.json";

    static WaveToolchainBundleDescriptor fromManifest(
        const QString& manifestPath,
        QString* failureReason = nullptr);
    static WaveToolchainBundleDescriptor discover(
        const QStringList& manifestCandidates);
    static QString defaultCacheRoot();
    static QString cachedToolchainRoot(
        const WaveToolchainBundleDescriptor& descriptor,
        const QString& cacheRootOverride = QString());
    static WaveToolchainBundleResult prepare(
        const WaveToolchainBundleDescriptor& descriptor,
        const QString& cacheRootOverride = QString(),
        const std::function<bool()>& isCancelled = {});
};

#endif // WAVETOOLCHAINBUNDLESERVICE_H
