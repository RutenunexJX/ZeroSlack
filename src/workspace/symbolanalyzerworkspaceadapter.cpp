#include "symbolanalyzer.h"
#include "workspacemanager.h"

void SymbolAnalyzer::analyzeWorkspace(WorkspaceManager* manager, std::function<bool()> cancelled)
{
    if (manager && manager->isWorkspaceOpen())
        analyzeProject(manager->projectSnapshot(), std::move(cancelled));
}
