#ifndef PROBLEMSPANELCOORDINATOR_H
#define PROBLEMSPANELCOORDINATOR_H

#include <QComboBox>
#include <QDockWidget>
#include <QStringList>
#include <QTreeWidget>

#include <functional>

class ProblemsPanelCoordinator
{
public:
    explicit ProblemsPanelCoordinator(QWidget* parent);

    void setCurrentFileProvider(std::function<QString()> provider);
    void setWorkspaceFilesProvider(std::function<QStringList()> provider);
    void setNavigationHandler(std::function<void(const QString&, int, int)> handler);

    void update(const QString& fileName = QString());

    QDockWidget* dock() const { return problemsDock; }
    QTreeWidget* tree() const { return problemsTree; }
    QComboBox* scopeCombo() const { return problemsScopeCombo; }
    QComboBox* severityCombo() const { return problemsSeverityCombo; }
    bool showsCurrentFileScope() const;

private:
    QDockWidget* problemsDock = nullptr;
    QTreeWidget* problemsTree = nullptr;
    QComboBox* problemsScopeCombo = nullptr;
    QComboBox* problemsSeverityCombo = nullptr;

    std::function<QString()> currentFileProvider;
    std::function<QStringList()> workspaceFilesProvider;
    std::function<void(const QString&, int, int)> navigationHandler;
};

#endif // PROBLEMSPANELCOORDINATOR_H
