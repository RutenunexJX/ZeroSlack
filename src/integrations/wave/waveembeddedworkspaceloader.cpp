#include "waveembeddedworkspaceloader.h"

#include <QByteArray>
#include <QFileInfo>
#include <QLibrary>
#include <QVariant>
#include <QWidget>

#include <array>
#include <cstddef>

namespace {
using AbiVersionFunction = int (*)();
using CreateWorkspaceFunction = int (*)(
    const char*, QWidget*, QWidget**, char*, std::size_t);

QString absolutePath(const QString& path)
{
    return QFileInfo(path).absoluteFilePath();
}

void setFailure(QString* destination, const QString& message)
{
    if (destination)
        *destination = message;
}
}

WaveEmbeddedWorkspaceLoader::WaveEmbeddedWorkspaceLoader() = default;

WaveEmbeddedWorkspaceLoader::~WaveEmbeddedWorkspaceLoader() = default;

QWidget* WaveEmbeddedWorkspaceLoader::createWorkspace(
    const QString& libraryPath,
    const QString& resultProjectPath,
    QWidget* parent,
    QString* failureReason)
{
    setFailure(failureReason, QString());
    const QString requestedLibrary = absolutePath(libraryPath);
    if (!QFileInfo::exists(requestedLibrary)) {
        setFailure(failureReason,
                   QStringLiteral("Wave widget library is missing: %1")
                       .arg(requestedLibrary));
        return nullptr;
    }
    if (!QFileInfo::exists(resultProjectPath)) {
        setFailure(failureReason,
                   QStringLiteral("Wave result project is missing: %1")
                       .arg(absolutePath(resultProjectPath)));
        return nullptr;
    }

    if (!library || currentLibraryPath != requestedLibrary) {
        auto candidate = std::make_unique<QLibrary>(requestedLibrary);
        candidate->setLoadHints(
            QLibrary::ResolveAllSymbolsHint
            | QLibrary::PreventUnloadHint);
        if (!candidate->load()) {
            setFailure(failureReason,
                       QStringLiteral("Cannot load Wave widgets: %1")
                           .arg(candidate->errorString()));
            return nullptr;
        }
        library = std::move(candidate);
        currentLibraryPath = requestedLibrary;
    }

    const auto abiVersion = reinterpret_cast<AbiVersionFunction>(
        library->resolve("wavewidgets_abi_version"));
    const auto createWorkspace = reinterpret_cast<CreateWorkspaceFunction>(
        library->resolve("wavewidgets_create_simulation_workspace_v1"));
    if (!abiVersion || !createWorkspace) {
        setFailure(failureReason,
                   QStringLiteral("Wave widgets do not expose the S11 ABI."));
        return nullptr;
    }
    if (abiVersion() != kSupportedAbiVersion) {
        setFailure(failureReason,
                   QStringLiteral("Unsupported Wave widget ABI %1; expected %2.")
                       .arg(abiVersion())
                       .arg(kSupportedAbiVersion));
        return nullptr;
    }

    QWidget* workspace = nullptr;
    std::array<char, 4096> error{};
    const QByteArray projectUtf8 = absolutePath(resultProjectPath).toUtf8();
    const int result = createWorkspace(
        projectUtf8.constData(),
        parent,
        &workspace,
        error.data(),
        error.size());
    if (result != 0 || !workspace) {
        const QString detail = QString::fromUtf8(error.data()).trimmed();
        setFailure(failureReason,
                   detail.isEmpty()
                       ? QStringLiteral("Wave widgets rejected the result project (%1).")
                             .arg(result)
                       : detail);
        return nullptr;
    }

    if (workspace->property("wavewidgets.abiVersion").toInt()
            != kSupportedAbiVersion
        || workspace->property("wavewidgets.contract").toString()
            != QString::fromLatin1(kWorkspaceContract)) {
        workspace->close();
        workspace->deleteLater();
        setFailure(failureReason,
                   QStringLiteral("Wave workspace contract validation failed."));
        return nullptr;
    }
    return workspace;
}

QString WaveEmbeddedWorkspaceLoader::loadedLibraryPath() const
{
    return currentLibraryPath;
}
