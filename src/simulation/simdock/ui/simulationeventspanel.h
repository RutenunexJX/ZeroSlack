#pragma once
#include "../core/simulationeventmodel.h"
#include <QAbstractTableModel>
#include <QHash>
#include <QWidget>
#include "zeroslackexport.h"
class QSortFilterProxyModel;
class QTableView;
class QLabel;

namespace simdock
{
class ZEROSLACK_API SimulationEventsPanel final : public QWidget
{
    Q_OBJECT
  public:
    explicit SimulationEventsPanel(QWidget *parent = nullptr, SimulationEventModel* sharedModel = nullptr);
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
};
} // namespace simdock
