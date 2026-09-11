#ifndef SEMANTICPANELREFRESHCOORDINATOR_H
#define SEMANTICPANELREFRESHCOORDINATOR_H

#include "liveinsighttypes.h"
#include "rtlinsightlink.h"

#include <QMetaObject>
#include <QString>
#include <QStringList>

#include <functional>

class MyCodeEditor;
class DocumentModel;
class NavigationCommandCoordinator;
class NavigationManager;
class ProblemsPanelCoordinator;
class RtlInsightsPanelCoordinator;
class SignalKernelGraphPanelCoordinator;
class TabManager;
class WorkspaceManager;

class SemanticPanelRefreshCoordinator
{
public:
    using LiveInsightOpenHandler = std::function<bool(
        LiveInsightKind,
        const QString&,
        const QString&,
        const QString&,
        const QString&)>;

    SemanticPanelRefreshCoordinator(TabManager* tabManager,
                                    WorkspaceManager* workspaceManager,
                                    NavigationManager* navigationManager,
                                    NavigationCommandCoordinator* navigationCommandCoordinator,
                                    ProblemsPanelCoordinator* problemsPanel,
                                    RtlInsightsPanelCoordinator* rtlInsightsPanel,
                                    SignalKernelGraphPanelCoordinator* signalKernelGraphPanel);
    ~SemanticPanelRefreshCoordinator();

    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);
    void setLiveInsightOpenHandler(LiveInsightOpenHandler handler);
    void configurePanels();

    void updateProblemsPanel();
    void showSignalKernelGraphForSymbol(const QString& symbolName,
                                        const QString& fileName,
                                        const QString& moduleName,
                                        const QString& signalAccessPath = {});
    void showSignalUsageHotspotForSymbol(const QString& symbolName,
                                         const QString& fileName,
                                         const QString& moduleName,
                                         const QString& signalAccessPath = {});
    void showStateTransitionGraphForSymbol(const QString& symbolName,
                                           const QString& fileName,
                                           const QString& moduleName);
    void showModuleBlockDiagramForSymbol(const QString& symbolName,
                                         const QString& fileName,
                                         const QString& moduleName);
    void handleActiveEditorChanged(MyCodeEditor* editor);

private:
    using CurrentFileProvider = std::function<QString()>;
    using WorkspaceFilesProvider = std::function<QStringList()>;
    using NavigationHandler =
        std::function<bool(const QString&, int, int)>;
    using SourceNavigationHandler =
        std::function<bool(
            const RtlInsightSourceLocation&)>;
    using ProblemsNavigationHandler =
        std::function<bool(const QString&, int, int)>;
    using StatusMessageHandler =
        std::function<void(const QString&, int)>;

    struct ContextDependencies {
        TabManager* tabManager = nullptr;
        WorkspaceManager* workspaceManager = nullptr;
        NavigationManager* navigationManager = nullptr;
        NavigationCommandCoordinator* navigationCommandCoordinator = nullptr;

        void set(TabManager* tabManager,
                 WorkspaceManager* workspaceManager,
                 NavigationManager* navigationManager,
                 NavigationCommandCoordinator* navigationCommandCoordinator);
        QString currentFileName() const;
        QStringList workspaceFiles() const;
        bool navigateToFileAndLine(const QString& fileName,
                                   int line,
                                   int column) const;
        bool navigateToFileAndLineAndFlash(const QString& fileName,
                                           int line,
                                           int column) const;
        bool navigateToSourceLocation(
            const RtlInsightSourceLocation& location) const;
        void revealFileAndFlashLine(const QString& fileName,
                                    int line) const;
        void handleActiveEditorChanged(MyCodeEditor* editor) const;
    };

    struct PanelSet {
        ProblemsPanelCoordinator* problemsPanel = nullptr;
        RtlInsightsPanelCoordinator* rtlInsightsPanel = nullptr;
        SignalKernelGraphPanelCoordinator* signalKernelGraphPanel = nullptr;
        bool configured = false;

        void set(ProblemsPanelCoordinator* problemsPanel,
                 RtlInsightsPanelCoordinator* rtlInsightsPanel,
                 SignalKernelGraphPanelCoordinator* signalKernelGraphPanel);
        bool isConfigured() const;
        void markConfigured();
        void configureProblemsPanel(
            const CurrentFileProvider& currentFileProvider,
            const WorkspaceFilesProvider& workspaceFilesProvider,
            const ProblemsNavigationHandler& navigationHandler,
            const StatusMessageHandler& statusMessageHandler) const;
        void configureRtlInsightsPanel(
            const SourceNavigationHandler&
                sourceNavigationHandler,
            const StatusMessageHandler& statusMessageHandler) const;
        void configureSignalKernelGraphPanel(
            DocumentModel* documentModel,
            const NavigationHandler& navigationHandler,
            const NavigationHandler& revealHandler,
            const StatusMessageHandler& statusMessageHandler) const;
        void updateProblemsPanel() const;
        void showSignalKernelGraphForSymbol(const QString& symbolName,
                                            const QString& fileName,
                                            const QString& moduleName,
                                            const QString& signalAccessPath = {}) const;
        void showSignalUsageHotspotForSymbol(const QString& symbolName,
                                             const QString& fileName,
                                             const QString& moduleName,
                                             const QString& signalAccessPath = {}) const;
        void showStateTransitionGraphForSymbol(const QString& symbolName,
                                               const QString& fileName,
                                               const QString& moduleName) const;
        void showModuleBlockDiagramForSymbol(const QString& symbolName,
                                             const QString& fileName,
                                             const QString& moduleName) const;
        bool syncRtlInsightsSourceLocation(
            const RtlInsightSourceLocation& location) const;
        bool problemsPanelShowsCurrentFile() const;
    };

    QString currentFileName() const;
    QStringList workspaceFiles() const;
    QString currentEditorWord(MyCodeEditor* editor) const;
    bool navigateToFileAndLine(const QString& fileName, int line, int column) const;
    bool navigateToFileAndLineAndFlash(const QString& fileName,
                                       int line,
                                       int column) const;
    bool navigateToSourceLocation(
        const RtlInsightSourceLocation& location) const;
    void revealFileAndFlashLine(const QString& fileName, int line) const;
    void showStatusMessage(const QString& message, int timeoutMs) const;
    bool problemsPanelShowsCurrentFile() const;
    RtlInsightSourceLocation sourceLocationForEditor(
        MyCodeEditor* editor) const;
    void syncEditorSourceLocation(MyCodeEditor* editor);

    ContextDependencies dependencies;
    PanelSet panels;

    std::function<void(const QString&, int)> statusMessageHandler;
    LiveInsightOpenHandler liveInsightOpenHandler;
    MyCodeEditor* observedEditor = nullptr;
    QMetaObject::Connection editorCursorConnection;
    QMetaObject::Connection editorSelectionConnection;
    QMetaObject::Connection editorInstanceContextConnection;
};

#endif // SEMANTICPANELREFRESHCOORDINATOR_H
