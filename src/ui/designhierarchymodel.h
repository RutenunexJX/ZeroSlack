#pragma once
#include "hierarchyservice.h"
#include "designtreeprojection.h"
#include "zeroslackexport.h"
#include <QAbstractItemModel>
#include <QTimer>
#include <memory>
#include <vector>

class ZEROSLACK_API DesignHierarchyModel final : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit DesignHierarchyModel(QObject* parent = nullptr);
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return 1; }
    bool hasChildren(const QModelIndex& parent = {}) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    QVariant data(const QModelIndex& index, int role) const override;
    void setReport(const DesignHierarchyReport& report, std::shared_ptr<const DesignTreeProjection> projection = {});
    void setSearchResults(std::shared_ptr<const DesignHierarchyReport> report, const QList<QString>& ids, std::shared_ptr<const DesignTreeProjection> projection = {});
    void setSimulationTarget(const QPair<QString, QString>& target);
    DesignHierarchyNode node(const QModelIndex& index) const;
    QModelIndex indexForId(const QString& id, bool reveal = true);
    std::shared_ptr<const DesignHierarchyReport> report() const { return m_report; }
    std::shared_ptr<const DesignTreeProjection> projection() const { return m_projection; }
    int materializedCount() const { return int(m_rows.size()); }
    bool applying() const { return m_apply.isActive(); }
signals:
    void reportApplied();
private:
    struct Row {
        QString id;
        Row* parent = nullptr;
        int row = 0;
        std::vector<std::unique_ptr<Row>> children;
    };
    Row* rowFor(const QModelIndex& index) const;
    QModelIndex indexFor(Row* row) const;
    const QList<int>& children(const Row* row) const;
    void forget(Row* row);
    void applyChunk();
    bool syncBranch(Row* row);
    std::shared_ptr<const DesignHierarchyReport> m_report;
    std::shared_ptr<const DesignTreeProjection> m_projection;
    Row m_root;
    QHash<QString, Row*> m_rows;
    QTimer m_apply;
    QStringList m_pending;
    QHash<QString, int> m_exposed;
    QPair<QString, QString> m_simulation;
    bool m_search = false;
    QList<int> m_matches;
};
