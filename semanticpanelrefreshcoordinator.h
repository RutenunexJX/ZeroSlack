#ifndef SEMANTICPANELREFRESHCOORDINATOR_H
#define SEMANTICPANELREFRESHCOORDINATOR_H

#include <QString>
#include <QStringList>

#include <functional>

class MyCodeEditor;
class DocumentModel;
class NavigationCommandCoordinator;
class NavigationManager;
class ProblemsPanelCoordinator;
class ReferencesPanelCoordinator;
class RelationshipsPanelCoordinator;
class RtlInsightsPanelCoordinator;
class SignalKernelGraphPanelCoordinator;
class TabManager;
class WorkspaceManager;

class SemanticPanelRefreshCoordinator
{
public:
    SemanticPanelRefreshCoordinator(TabManager* tabManager,
                                    WorkspaceManager* workspaceManager,
                                    NavigationManager* navigationManager,
                                    NavigationCommandCoordinator* navigationCommandCoordinator,
                                    ProblemsPanelCoordinator* problemsPanel,
                                    ReferencesPanelCoordinator* referencesPanel,
                                    RelationshipsPanelCoordinator* relationshipsPanel,
                                    RtlInsightsPanelCoordinator* rtlInsightsPanel,
                                    SignalKernelGraphPanelCoordinator* signalKernelGraphPanel);

    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);
    void configurePanels();

    void updateProblemsPanel(const QString& fileName = QString());
    void showReferencesForSymbol(const QString& symbolName,
                                 const QString& fileName,
                                 const QString& moduleName);
    void refreshReferencesPanel();
    void showRelationshipsForSymbol(const QString& symbolName,
                                    const QString& fileName,
                                    const QString& moduleName);
    void refreshRelationshipsPanel();
    void showSignalKernelGraphForSymbol(const QString& symbolName,
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
        std::function<void(const QString&, int, int)>;
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
        void navigateToFileAndLine(const QString& fileName,
                                   int line,
                                   int column) const;
        void revealFileAndFlashLine(const QString& fileName,
                                    int line) const;
        void handleActiveEditorChanged(MyCodeEditor* editor) const;
    };

    struct PanelSet {
        ProblemsPanelCoordinator* problemsPanel = nullptr;
        ReferencesPanelCoordinator* referencesPanel = nullptr;
        RelationshipsPanelCoordinator* relationshipsPanel = nullptr;
        RtlInsightsPanelCoordinator* rtlInsightsPanel = nullptr;
        SignalKernelGraphPanelCoordinator* signalKernelGraphPanel = nullptr;
        bool configured = false;

        void set(ProblemsPanelCoordinator* problemsPanel,
                 ReferencesPanelCoordinator* referencesPanel,
                 RelationshipsPanelCoordinator* relationshipsPanel,
                 RtlInsightsPanelCoordinator* rtlInsightsPanel,
                 SignalKernelGraphPanelCoordinator* signalKernelGraphPanel);
        bool isConfigured() const;
        void markConfigured();
        void configureProblemsPanel(
            const CurrentFileProvider& currentFileProvider,
            const WorkspaceFilesProvider& workspaceFilesProvider,
            const NavigationHandler& navigationHandler) const;
        void configureReferencesPanel(
            const WorkspaceFilesProvider& workspaceFilesProvider,
            const NavigationHandler& navigationHandler,
            const StatusMessageHandler& statusMessageHandler) const;
        void configureRelationshipsPanel(
            const NavigationHandler& navigationHandler,
            const StatusMessageHandler& statusMessageHandler) const;
        void configureRtlInsightsPanel(
            const NavigationHandler& navigationHandler,
            const StatusMessageHandler& statusMessageHandler) const;
        void configureSignalKernelGraphPanel(
            DocumentModel* documentModel,
            const NavigationHandler& navigationHandler,
            const NavigationHandler& revealHandler,
            const StatusMessageHandler& statusMessageHandler) const;
        void updateProblemsPanel(const QString& fileName) const;
        void showReferencesForSymbol(const QString& symbolName,
                                     const QString& fileName,
                                     const QString& moduleName) const;
        void refreshReferencesPanel() const;
        void showRelationshipsForSymbol(const QString& symbolName,
                                        const QString& fileName,
                                        const QString& moduleName) const;
        void refreshRelationshipsPanel() const;
        void showSignalKernelGraphForSymbol(const QString& symbolName,
                                            const QString& fileName,
                                            const QString& moduleName,
                                            const QString& signalAccessPath = {}) const;
        void showStateTransitionGraphForSymbol(const QString& symbolName,
                                               const QString& fileName,
                                               const QString& moduleName) const;
        void showModuleBlockDiagramForSymbol(const QString& symbolName,
                                             const QString& fileName,
                                             const QString& moduleName) const;
        void updateRtlInsightsPanel(const QString& fileName,
                                    const QString& moduleName,
                                    const QString& signalName) const;
        bool problemsPanelShowsCurrentFile() const;
    };

    QString currentFileName() const;
    QStringList workspaceFiles() const;
    QString currentEditorWord(MyCodeEditor* editor) const;
    void navigateToFileAndLine(const QString& fileName, int line, int column) const;
    void revealFileAndFlashLine(const QString& fileName, int line) const;
    void showStatusMessage(const QString& message, int timeoutMs) const;
    bool problemsPanelShowsCurrentFile() const;

    ContextDependencies dependencies;
    PanelSet panels;

    std::function<void(const QString&, int)> statusMessageHandler;
};

#endif // SEMANTICPANELREFRESHCOORDINATOR_H
