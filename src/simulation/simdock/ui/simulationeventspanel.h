#pragma once
#include "../core/simulationevents.h"
#include <QAbstractTableModel>
#include <QHash>
#include <QWidget>
#include "zeroslackexport.h"
class QSortFilterProxyModel;
class QTableView;
class QLabel;

namespace simdock
{
class ZEROSLACK_API SimulationEventModel final : public QAbstractTableModel
{
    Q_OBJECT
  public:
    explicit SimulationEventModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex & = {}) const override;
    int columnCount(const QModelIndex & = {}) const override
    {
        return 9;
    }
    QVariant data(const QModelIndex &, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override;
    void startRun(const QString &runId);
    void append(const QList<SimulationEvent> &batch);
    void setMergeRepeats(bool enabled);
    quint64 overflow() const
    {
        return discarded;
    }
    void setCapacity(int capacity)
    {
        limit = qBound(1, capacity, 100000);
    }
    SimulationEvent eventAt(int row) const;

  private:
    struct Row
    {
        SimulationEvent event;
        quint64 count = 1;
        QString lastTime;
    };
    QList<SimulationEvent> raw;
    QList<Row> rows;
    QHash<QString, int> repeatRows;
    QString run;
    bool merge = false;
    int limit = 20000;
    quint64 discarded = 0;
    qsizetype retainedBytes = 0;
    void add(const SimulationEvent &event);
};

class ZEROSLACK_API SimulationEventsPanel final : public QWidget
{
    Q_OBJECT
  public:
    explicit SimulationEventsPanel(QWidget *parent = nullptr);
    SimulationEventModel *eventModel() const
    {
        return model;
    }
    void startRun(const QString &run);
    void append(const QList<SimulationEvent> &batch);
    void reject(quint64 count);
  signals:
    void sourceRequested(const QString &file, int line);

  private:
    void updateStatus();
    SimulationEventModel *model;
    QSortFilterProxyModel *filter;
    QTableView *table;
    QLabel *status;
    QString currentRun;
    quint64 rejected = 0;
};
} // namespace simdock
