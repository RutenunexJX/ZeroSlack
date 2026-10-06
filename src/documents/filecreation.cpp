#include "filecreation.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <utility>

namespace {
thread_local FileCreation::Probe probe;
}

FileCreation::Probe FileCreation::exchangeProbeForTesting(Probe replacement)
{
    return std::exchange(probe, std::move(replacement));
}

FileCreationResult FileCreation::create(const QString& path, const QByteArray& bytes)
{
    const QString destination = QFileInfo(path).absoluteFilePath();
    QTemporaryFile staged(destination + QStringLiteral(".creating.XXXXXX"));
    if (path.isEmpty() || !staged.open())
        return {FileCreationStatus::WriteFailed,
                QStringLiteral("Cannot prepare new file: %1").arg(path)};
    qint64 written = 0;
    while (written < bytes.size()) {
        const auto count = staged.write(bytes.constData() + written, bytes.size() - written);
        if (count <= 0)
            return {FileCreationStatus::WriteFailed, staged.errorString()};
        written += count;
    }
    if (probe && !probe(Stage::BeforeFlush, destination))
        return {FileCreationStatus::WriteFailed, QStringLiteral("New file write failed.")};
    if (!staged.flush())
        return {FileCreationStatus::WriteFailed, staged.errorString()};
    staged.close();
    if (probe && !probe(Stage::BeforePublish, destination))
        return {FileCreationStatus::PublishFailed, QStringLiteral("New file creation was cancelled.")};

    // QTemporaryFile keeps its native handle after close(). Its rename closes
    // that handle and atomically acquires the new name without overwriting or
    // QFile's copy/delete fallback. Staging is on the destination filesystem.
    if (staged.rename(destination)) {
        staged.setAutoRemove(false);
        return {FileCreationStatus::Created, {}};
    }
    // This post-failure observation only improves the diagnosis. It never
    // decides whether publication may replace an existing destination.
    const QFileInfo target(destination);
    const bool exists = target.exists() || target.isSymbolicLink();
    return {exists ? FileCreationStatus::AlreadyExists : FileCreationStatus::PublishFailed,
            exists ? QStringLiteral("The file was created by another writer: %1").arg(path)
                   : QStringLiteral("Cannot create new file %1: %2").arg(path, staged.errorString())};
}
