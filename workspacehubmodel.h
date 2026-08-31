#ifndef WORKSPACEHUBMODEL_H
#define WORKSPACEHUBMODEL_H

#include "workspacehubtypes.h"
#include "zeroslackexport.h"

#include <QAbstractItemModel>

class ZEROSLACK_API WorkspaceHubModel final : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Role {
        StableKeyRole = Qt::UserRole + 1,
        SectionIdRole,
        SummaryRole,
        StateRole,
        ProviderIdRole,
        IsSectionRole
    };

    explicit WorkspaceHubModel(QObject* parent = nullptr);

    QModelIndex index(int row, int column,
                      const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;

    WorkspaceHubSnapshot snapshot() const;
    void setSnapshot(const WorkspaceHubSnapshot& snapshot);
    WorkspaceHubItem itemForIndex(const QModelIndex& index) const;
    QString sectionIdForIndex(const QModelIndex& index) const;
    QModelIndex indexForStableKey(const QString& stableKey) const;

private:
    WorkspaceHubSnapshot snapshotValue;

    bool isSectionIndex(const QModelIndex& index) const;
};

#endif // WORKSPACEHUBMODEL_H
