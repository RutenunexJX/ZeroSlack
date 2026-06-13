#ifndef RELATIONSHIPSPANELCOORDINATOR_H
#define RELATIONSHIPSPANELCOORDINATOR_H

#include <QComboBox>
#include <QDockWidget>
#include <QTreeWidget>

#include <functional>

class RelationshipsPanelCoordinator
{
public:
    explicit RelationshipsPanelCoordinator(QWidget* parent);

    void setNavigationHandler(std::function<void(const QString&, int, int)> handler);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);

    void showRelationshipsForSymbol(const QString& symbolName,
                                    const QString& fileName,
                                    const QString& moduleName);
    void refresh();

    QDockWidget* dock() const { return relationshipsDock; }
    QTreeWidget* tree() const { return relationshipsTree; }
    QComboBox* viewCombo() const { return relationshipViewCombo; }
    QComboBox* directionCombo() const { return relationshipDirectionCombo; }
    QComboBox* typeCombo() const { return relationshipTypeCombo; }
    QComboBox* depthCombo() const { return relationshipDepthCombo; }

private:
    QDockWidget* relationshipsDock = nullptr;
    QTreeWidget* relationshipsTree = nullptr;
    QComboBox* relationshipViewCombo = nullptr;
    QComboBox* relationshipDirectionCombo = nullptr;
    QComboBox* relationshipTypeCombo = nullptr;
    QComboBox* relationshipDepthCombo = nullptr;
    QString currentRelationshipSymbolName;
    QString currentRelationshipFileName;
    QString currentRelationshipModuleName;

    std::function<void(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;

    void refreshHierarchyTree(int typeFilter);
};

#endif // RELATIONSHIPSPANELCOORDINATOR_H
