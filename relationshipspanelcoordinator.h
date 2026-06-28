#ifndef RELATIONSHIPSPANELCOORDINATOR_H
#define RELATIONSHIPSPANELCOORDINATOR_H

#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QTreeWidget>

#include <functional>

class RelationshipsPanelCoordinator
{
public:
    explicit RelationshipsPanelCoordinator(QWidget* parent);

    void setNavigationHandler(std::function<bool(const QString&, int, int)> handler);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);
    void setSignalKernelGraphHandler(
        std::function<void(const QString&, const QString&, const QString&)> handler);
    void setStateTransitionGraphHandler(
        std::function<void(const QString&, const QString&, const QString&)> handler);
    void setModuleBlockDiagramHandler(
        std::function<void(const QString&, const QString&)> handler);

    void showRelationshipsForSymbol(const QString& symbolName,
                                    const QString& fileName,
                                    const QString& moduleName);
    void refresh();

    QDockWidget* dock() const { return relationshipsDock; }
    QTreeWidget* tree() const { return relationshipsTree; }
    QLabel* contextLabel() const { return relationshipContextLabel; }
    QComboBox* viewCombo() const { return relationshipViewCombo; }
    QComboBox* directionCombo() const { return relationshipDirectionCombo; }
    QComboBox* typeCombo() const { return relationshipTypeCombo; }
    QComboBox* depthCombo() const { return relationshipDepthCombo; }

private:
    QDockWidget* relationshipsDock = nullptr;
    QLabel* relationshipContextLabel = nullptr;
    QTreeWidget* relationshipsTree = nullptr;
    QComboBox* relationshipViewCombo = nullptr;
    QComboBox* relationshipDirectionCombo = nullptr;
    QComboBox* relationshipTypeCombo = nullptr;
    QComboBox* relationshipDepthCombo = nullptr;
    QString currentRelationshipSymbolName;
    QString currentRelationshipFileName;
    QString currentRelationshipModuleName;

    std::function<bool(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;
    std::function<void(const QString&, const QString&, const QString&)>
        signalKernelGraphHandler;
    std::function<void(const QString&, const QString&, const QString&)>
        stateTransitionGraphHandler;
    std::function<void(const QString&, const QString&)>
        moduleBlockDiagramHandler;

    void refreshHierarchyTree(int typeFilter);
};

#endif // RELATIONSHIPSPANELCOORDINATOR_H
