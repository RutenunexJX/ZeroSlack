#include "simulationeventspanel.h"
#include "compactlayout.h"
#include "uicontrols.h"
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace simdock
{
namespace
{
class EventFilter final : public QSortFilterProxyModel
{
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    QString level;
    void setLevel(const QString &value)
    {
        level = value;
        invalidateFilter();
    }
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        return (level.isEmpty() || sourceModel()->index(row, 1, parent).data().toString() == level) &&
               QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }
};
} // namespace
SimulationEventsPanel::SimulationEventsPanel(QWidget *parent, SimulationEventModel* sharedModel) : QWidget(parent)
{
    setObjectName(QStringLiteral("simulationEventsPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    auto *toolbar = new QHBoxLayout;
    auto *search = UiControls::lineEdit(this);
    search->setObjectName("simulationEventFilter");
    search->setPlaceholderText("Filter tag, instance, message or values");
    toolbar->addWidget(search, 1);
    auto *levels = UiControls::comboBox(this);
    levels->setObjectName("simulationEventLevel");
    levels->addItems({"All levels", "Info", "Warning", "Error"});
    toolbar->addWidget(levels);
    auto *group = UiControls::comboBox(this);
    group->setObjectName("simulationEventGroup");
    group->addItems({"Run order", "Group by level", "Group by tag", "Group by instance"});
    toolbar->addWidget(group);
    auto *merge = new QCheckBox("Merge repeats", this);
    merge->setObjectName(QStringLiteral("simulationEventMergeRepeats"));
    toolbar->addWidget(merge);
    auto *columns = new QToolButton(this);
    columns->setObjectName(QStringLiteral("simulationEventColumns"));
    columns->setText("Columns");
    columns->setPopupMode(QToolButton::InstantPopup);
    toolbar->addWidget(columns);
    auto *menu = new QMenu(columns);
    columns->setMenu(menu);
    model = sharedModel ? sharedModel : new SimulationEventModel(this);
    connect(model, &SimulationEventModel::summaryChanged, this, &SimulationEventsPanel::updateStatus);
    auto *proxy = new EventFilter(this);
    filter = proxy;
    filter->setSourceModel(model);
    filter->setFilterKeyColumn(-1);
    filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    table = new QTableView(this);
    table->setObjectName("simulationEventTable");
    table->setModel(filter);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setWordWrap(false);
    table->setShowGrid(false);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(qMax(25, fontMetrics().height() + 8));
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);
    status = new CompactTitleLabel(this);
    auto* actionBar = new CompactOverflowToolbar(toolbar, QStringLiteral("simulationEvents"), this);
    layout->insertWidget(0, actionBar);
    actionBar->addOverflowWidget(status);
    QSettings settings;
    merge->setChecked(settings.value("simulation/events/mergeRepeats", false).toBool());
    model->setMergeRepeats(merge->isChecked());
    for (int column = 0; column < model->columnCount(); ++column)
    {
        auto *action = menu->addAction(model->headerData(column, Qt::Horizontal).toString());
        action->setCheckable(true);
        action->setChecked(
            settings.value(QStringLiteral("simulation/events/column%1").arg(column), column < 6).toBool());
        table->setColumnHidden(column, !action->isChecked());
        connect(action, &QAction::toggled, this,
                [this, column](bool checked)
                {
                    table->setColumnHidden(column, !checked);
                    QSettings().setValue(QStringLiteral("simulation/events/column%1").arg(column), checked);
                });
    }
    search->setText(settings.value("simulation/events/filter").toString());
    filter->setFilterFixedString(search->text());
    levels->setCurrentIndex(settings.value("simulation/events/level", 0).toInt());
    proxy->setLevel(levels->currentIndex() ? levels->currentText() : QString());
    group->setCurrentIndex(settings.value("simulation/events/group", 0).toInt());
    filter->sort(group->currentIndex() ? group->currentIndex() : -1);
    connect(search, &QLineEdit::textChanged, this,
            [this](const QString &value)
            {
                filter->setFilterFixedString(value);
                QSettings().setValue("simulation/events/filter", value);
            });
    connect(levels, &QComboBox::currentIndexChanged, this,
            [proxy, levels](int index)
            {
                proxy->setLevel(index ? levels->currentText() : QString());
                QSettings().setValue("simulation/events/level", index);
            });
    connect(group, &QComboBox::currentIndexChanged, this,
            [this](int index)
            {
                filter->sort(index ? index : -1);
                QSettings().setValue("simulation/events/group", index);
            });
    connect(merge, &QCheckBox::toggled, this,
            [this](bool checked)
            {
                model->setMergeRepeats(checked);
                QSettings().setValue("simulation/events/mergeRepeats", checked);
                updateStatus();
            });
    connect(table, &QTableView::doubleClicked, this,
            [this](const QModelIndex &index)
            {
                const auto event = model->eventAt(filter->mapToSource(index).row());
                if (!event.sourceFile.isEmpty())
                    emit sourceRequested(event.sourceFile, event.sourceLine);
            });
    updateStatus();
}
void SimulationEventsPanel::startRun(const QString &run)
{
    model->startRun(run);
    updateStatus();
}
void SimulationEventsPanel::append(const QList<SimulationEvent> &batch)
{
    model->append(batch);
    updateStatus();
}
void SimulationEventsPanel::reject(quint64 count)
{
    model->reject(count);
    updateStatus();
}
void SimulationEventsPanel::updateStatus()
{
    status->setText(
        model->runId().isEmpty()
            ? QStringLiteral("No current simulation")
            : QStringLiteral("%1 rows · Event level does not determine run status%2%3")
                  .arg(model->rowCount())
                  .arg(model->overflow()
                           ? QStringLiteral(" · Capacity reached: %1 further events omitted; see Run log")
                                 .arg(model->overflow())
                           : QString())
                  .arg(model->rejectedCount()
                           ? QStringLiteral(" · %1 malformed or oversized records; see Run log").arg(model->rejectedCount())
                           : QString()));
}
} // namespace simdock
