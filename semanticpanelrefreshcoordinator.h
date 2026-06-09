#ifndef SEMANTICPANELREFRESHCOORDINATOR_H
#define SEMANTICPANELREFRESHCOORDINATOR_H

#include <QString>
#include <QStringList>

#include <functional>

class MyCodeEditor;
class NavigationCommandCoordinator;
class NavigationManager;
class ProblemsPanelCoordinator;
class ReferencesPanelCoordinator;
class RelationshipsPanelCoordinator;
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
                                    RelationshipsPanelCoordinator* relationshipsPanel);

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
    void handleActiveEditorChanged(MyCodeEditor* editor);

private:
    QString currentFileName() const;
    QStringList workspaceFiles() const;
    void navigateToFileAndLine(const QString& fileName, int line, int column) const;
    void showStatusMessage(const QString& message, int timeoutMs) const;
    bool problemsPanelShowsCurrentFile() const;

    TabManager* tabManager = nullptr;
    WorkspaceManager* workspaceManager = nullptr;
    NavigationManager* navigationManager = nullptr;
    NavigationCommandCoordinator* navigationCommandCoordinator = nullptr;
    ProblemsPanelCoordinator* problemsPanel = nullptr;
    ReferencesPanelCoordinator* referencesPanel = nullptr;
    RelationshipsPanelCoordinator* relationshipsPanel = nullptr;
    bool panelsConfigured = false;

    std::function<void(const QString&, int)> statusMessageHandler;
};

#endif // SEMANTICPANELREFRESHCOORDINATOR_H
