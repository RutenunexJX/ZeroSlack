#ifndef WAVEEMBEDDEDWORKSPACELOADER_H
#define WAVEEMBEDDEDWORKSPACELOADER_H

#include "zeroslackexport.h"

#include <QString>

#include <memory>

class QLibrary;
class QWidget;

class ZEROSLACK_API WaveEmbeddedWorkspaceLoader
{
public:
    static constexpr int kSupportedAbiVersion = 1;
    static constexpr const char* kWorkspaceContract =
        "wave-workbench.simulation-workspace/v1";

    WaveEmbeddedWorkspaceLoader();
    ~WaveEmbeddedWorkspaceLoader();

    QWidget* createWorkspace(const QString& libraryPath,
                             const QString& resultProjectPath,
                             QWidget* parent,
                             QString* failureReason = nullptr);
    QString loadedLibraryPath() const;

private:
    std::unique_ptr<QLibrary> library;
    QString currentLibraryPath;
};

#endif // WAVEEMBEDDEDWORKSPACELOADER_H
