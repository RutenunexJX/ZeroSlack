#ifndef ANALYSISCOORDINATOR_H
#define ANALYSISCOORDINATOR_H

#include <QObject>
#include <QString>

#include <functional>

class AnalysisProgressCoordinator;
class AnalysisScheduler;
class MyCodeEditor;
class NavigationManager;
class SmartRelationshipBuilder;
class SymbolAnalyzer;
class SymbolRelationshipEngine;
class TabManager;
class WorkspaceManager;

class AnalysisCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisCoordinator(AnalysisScheduler* scheduler,
                                 AnalysisProgressCoordinator* progressCoordinator,
                                 SymbolAnalyzer* symbolAnalyzer,
                                 TabManager* tabManager,
                                 WorkspaceManager* workspaceManager,
                                 NavigationManager* navigationManager,
                                 SymbolRelationshipEngine* relationshipEngine,
                                 SmartRelationshipBuilder* relationshipBuilder,
                                 QObject* parent = nullptr);

    void setFileChangeDebounceMs(int debounceMs);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);
    void setProblemsRefreshHandler(std::function<void(const QString&)> handler);

    void connectSignals();

private:
    AnalysisScheduler* scheduler = nullptr;
    AnalysisProgressCoordinator* progressCoordinator = nullptr;
    SymbolAnalyzer* symbolAnalyzer = nullptr;
    TabManager* tabManager = nullptr;
    WorkspaceManager* workspaceManager = nullptr;
    NavigationManager* navigationManager = nullptr;
    SymbolRelationshipEngine* relationshipEngine = nullptr;
    SmartRelationshipBuilder* relationshipBuilder = nullptr;
    int fileChangeDebounceMs = 350;
    bool signalsConnected = false;

    std::function<void(const QString&, int)> statusMessageHandler;
    std::function<void(const QString&)> problemsRefreshHandler;

    void configureScheduler();
    void connectSchedulerSignals();
    void connectProgressSignals();
    void connectWorkspaceSignals();
    void connectSymbolSignals();
    void refreshActiveEditorForFile(const QString& fileName) const;
    void showRelationshipAnalysisCompleted(const QString& fileName, int relationshipsFound) const;
    void showRelationshipAnalysisError(const QString& error) const;
};

#endif // ANALYSISCOORDINATOR_H
