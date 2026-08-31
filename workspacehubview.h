#ifndef WORKSPACEHUBVIEW_H
#define WORKSPACEHUBVIEW_H

#include "workspacehubtypes.h"
#include "zeroslackexport.h"

#include <QVariantMap>
#include <QWidget>

class QLabel;
class QModelIndex;
class QToolButton;
class QTreeView;
class WorkspaceHubModel;
class SemanticStateView;

class ZEROSLACK_API WorkspaceHubView final : public QWidget
{
    Q_OBJECT

public:
    explicit WorkspaceHubView(QWidget* parent = nullptr);

    WorkspaceHubModel* model() const;
    QTreeView* treeView() const;
    SemanticStateView* stateView() const;
    void setSnapshot(const WorkspaceHubSnapshot& snapshot);
    QVariantMap saveState() const;
    void restoreState(const QVariantMap& state);

signals:
    void itemActivated(const WorkspaceHubItem& item);
    void refreshRequested();
    void viewStateChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    WorkspaceHubModel* hubModel = nullptr;
    QTreeView* tree = nullptr;
    QLabel* statusChip = nullptr;
    QToolButton* previewButton = nullptr;
    QToolButton* refreshButton = nullptr;
    SemanticStateView* semanticState = nullptr;
    QWidget* preview = nullptr;
    QLabel* previewTitle = nullptr;
    QLabel* previewSummary = nullptr;
    QVariantMap pendingState;
    bool applyingState = false;
    bool previewEnabled = true;

    void activateIndex(const QModelIndex& index);
    void updatePreview();
    void applyPendingState();
    void refreshPresentation();
};

#endif // WORKSPACEHUBVIEW_H
