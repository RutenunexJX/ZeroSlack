#ifndef ANALYSISCOORDINATOR_H
#define ANALYSISCOORDINATOR_H

#include "zeroslackexport.h"

#include <QObject>
#include <QString>

#include <functional>

class AnalysisProgressCoordinator;
class AnalysisScheduler;
class MyCodeEditor;
class NavigationManager;
class SemanticRuntimeCoordinator;
class TabManager;
class WorkspaceManager;
struct DocumentSnapshot;
struct SemanticAnalysisTelemetry;

class ZEROSLACK_API AnalysisCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisCoordinator(AnalysisScheduler* scheduler,
                                 AnalysisProgressCoordinator* progressCoordinator,
                                 SemanticRuntimeCoordinator* semanticRuntime,
                                 TabManager* tabManager,
                                 WorkspaceManager* workspaceManager,
                                 NavigationManager* navigationManager,
                                 QObject* parent = nullptr);

    void setFileChangeDebounceMs(int debounceMs);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);
    void setProblemsRefreshHandler(std::function<void(const QString&)> handler);

    void connectSignals();

private:
    struct AnalysisDependencies {
        AnalysisScheduler* scheduler = nullptr;
        AnalysisProgressCoordinator* progressCoordinator = nullptr;
        SemanticRuntimeCoordinator* semanticRuntime = nullptr;
        TabManager* tabManager = nullptr;
        WorkspaceManager* workspaceManager = nullptr;
        NavigationManager* navigationManager = nullptr;

        void set(AnalysisScheduler* scheduler,
                 AnalysisProgressCoordinator* progressCoordinator,
                 SemanticRuntimeCoordinator* semanticRuntime,
                 TabManager* tabManager,
                 WorkspaceManager* workspaceManager,
                 NavigationManager* navigationManager);
        bool hasScheduler() const;
        bool hasProgressCoordinator() const;
        bool hasWorkspaceFileWatcher() const;
        void configureScheduler() const;
        void connectProgressToScheduler() const;
        bool isWorkspaceOpen() const;
        bool isWorkspaceSymbolAnalysisCancelled() const;
        void refreshRelationshipDataView() const;
        void handleFileSymbolAnalysisFinished(const QString& fileName,
                                              int symbolCount) const;
        void handleWorkspaceSymbolProgress(const QString& fileName,
                                           int filesDone,
                                           int totalFiles) const;
        void handleWorkspaceSymbolAnalysisFinished(int filesAnalyzed,
                                                   int totalSymbols) const;
        void handleExternalFileChanged(const QString& filePath,
                                       int debounceMs) const;
        void refreshSemanticPresentations(
            const QString& fileName) const;
        DocumentSnapshot currentDocument() const;
        AnalysisScheduler* schedulerObject() const;
        AnalysisProgressCoordinator* progressCoordinatorObject() const;
        WorkspaceManager* workspaceManagerObject() const;
        NavigationManager* navigationManagerObject() const;
        void setSemanticAnalysisContext(
            const SemanticAnalysisTelemetry& telemetry) const;
    };

    AnalysisDependencies dependencies;
    int fileChangeDebounceMs = 350;
    bool signalsConnected = false;

    std::function<void(const QString&, int)> statusMessageHandler;
    std::function<void(const QString&)> problemsRefreshHandler;

    void configureScheduler();
    void connectSchedulerSignals();
    void connectProgressSignals();
    void connectWorkspaceSignals();
    void refreshActiveEditorForFile(const QString& fileName) const;
    void showRelationshipAnalysisCompleted(const QString& fileName, int relationshipsFound) const;
    void showRelationshipAnalysisError(const QString& error) const;
};

#endif // ANALYSISCOORDINATOR_H
