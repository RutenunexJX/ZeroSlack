#ifndef WAVESIMULATIONRESULTNAVIGATIONCOORDINATOR_H
#define WAVESIMULATIONRESULTNAVIGATIONCOORDINATOR_H

#include "zeroslackexport.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class NavigationCommandCoordinator;
class TabManager;
class WorkspaceManager;
class QWidget;
struct ActionInvocation;

class ZEROSLACK_API WaveSimulationResultNavigationCoordinator final
    : public QObject
{
    Q_OBJECT

public:
    explicit WaveSimulationResultNavigationCoordinator(
        TabManager* tabManager,
        WorkspaceManager* workspaceManager,
        NavigationCommandCoordinator* navigationCoordinator,
        QObject* parent = nullptr);
    ~WaveSimulationResultNavigationCoordinator() override;

    void registerWorkspace(QWidget* workspace,
                           const QString& stableId,
                           const QString& workspaceRoot);
    bool revealSignalInResult(const ActionInvocation& invocation,
                              QString* failureReason = nullptr);

signals:
    void statusMessageRequested(const QString& message,
                                int timeoutMs);

private slots:
    void handleSourceNavigationRequest(
        const QString& sourceFile,
        int sourceLine,
        int sourceColumn,
        const QString& semanticId,
        const QString& kind);

private:
    struct EmbeddedWorkspace {
        QPointer<QWidget> widget;
        QString stableId;
        QString workspaceRoot;
    };

    TabManager* tabManager = nullptr;
    WorkspaceManager* workspaceManager = nullptr;
    NavigationCommandCoordinator* navigationCoordinator = nullptr;
    QList<EmbeddedWorkspace> workspaces;

    void removeWorkspace(QObject* object);
    void purgeClosedWorkspaces();
};

#endif // WAVESIMULATIONRESULTNAVIGATIONCOORDINATOR_H
