#pragma once

#include <QByteArray>
#include <QString>
#include <functional>

enum class FileCreationStatus { Created, AlreadyExists, WriteFailed, PublishFailed };
struct FileCreationResult {
    FileCreationStatus status = FileCreationStatus::PublishFailed;
    QString message;
    bool created() const { return status == FileCreationStatus::Created; }
};

// Prepare complete bytes privately, then acquire the destination name without
// replacement. Only the private staging file is ever cleaned up on failure.
class FileCreation
{
public:
    static FileCreationResult create(const QString& path, const QByteArray& bytes);
    enum class Stage { BeforeFlush, BeforePublish };
    using Probe = std::function<bool(Stage, const QString&)>;
    // Scoped tests can interleave a real competing writer at the I/O boundary.
    // Thread-local; absent in ordinary application execution.
    static Probe exchangeProbeForTesting(Probe probe);
};
