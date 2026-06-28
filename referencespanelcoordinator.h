#ifndef REFERENCESPANELCOORDINATOR_H
#define REFERENCESPANELCOORDINATOR_H

#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QStringList>
#include <QTreeWidget>

#include <functional>

class ReferencesPanelCoordinator
{
public:
    explicit ReferencesPanelCoordinator(QWidget* parent);

    void setWorkspaceFilesProvider(std::function<QStringList()> provider);
    void setNavigationHandler(std::function<bool(const QString&, int, int)> handler);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);

    void showReferencesForSymbol(const QString& symbolName,
                                 const QString& fileName,
                                 const QString& moduleName);
    void refresh();

    QDockWidget* dock() const { return referencesDock; }
    QTreeWidget* tree() const { return referencesTree; }
    QLabel* contextLabel() const { return referenceContextLabel; }
    QComboBox* scopeCombo() const { return referenceScopeCombo; }
    QComboBox* typeCombo() const { return referenceTypeCombo; }

private:
    QDockWidget* referencesDock = nullptr;
    QLabel* referenceContextLabel = nullptr;
    QTreeWidget* referencesTree = nullptr;
    QComboBox* referenceScopeCombo = nullptr;
    QComboBox* referenceTypeCombo = nullptr;
    QString currentReferenceSymbolName;
    QString currentReferenceFileName;
    QString currentReferenceModuleName;

    std::function<QStringList()> workspaceFilesProvider;
    std::function<bool(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;
};

#endif // REFERENCESPANELCOORDINATOR_H
