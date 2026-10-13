#include "simulationeventspanel.h"
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
QString valueText(const SimulationEvent &event)
{
    QStringList result;
    for (const auto &value : event.values)
        result << QStringLiteral("%1=%2%3").arg(
            value.value("name").toString(), value.value("value").toString(),
            value.value("unit").toString().isEmpty() ? QString()
                                                     : QLatin1Char(' ') + value.value("unit").toString());
    return result.join(QStringLiteral(" · "));
}
QString repeatKey(const SimulationEvent &event)
{
    QVariantList values;
    for (const auto &item : event.values)
        values << item;
    return QString::fromUtf8(
        QJsonDocument::fromVariant(QVariantList{event.level, event.tag, event.instance, event.sourceFile,
                                                event.sourceLine, event.message, values})
            .toJson(QJsonDocument::Compact));
}
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
SimulationEventModel::SimulationEventModel(QObject *parent) : QAbstractTableModel(parent) {}
int SimulationEventModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : rows.size();
}
SimulationEvent SimulationEventModel::eventAt(int row) const
{
    return row >= 0 && row < rows.size() ? rows[row].event : SimulationEvent{};
}
QVariant SimulationEventModel::headerData(int column, Qt::Orientation orientation, int role) const
{
    static const QStringList names{"Time (ps)", "Level", "Tag",    "Instance", "Message",
                                   "Values",    "Count", "Source", "Line"};
    return orientation == Qt::Horizontal && role == Qt::DisplayRole && column >= 0 && column < names.size()
               ? names[column]
               : QVariant();
}
QVariant SimulationEventModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rows.size())
        return {};
    const auto &row = rows[index.row()];
    const auto &event = row.event;
    if (role == Qt::UserRole)
        return event.runId;
    if (role == Qt::ToolTipRole)
        return QStringLiteral("Run %1\n%2:%3\nFirst %4 ps · Last %5 ps\n%6\n%7")
            .arg(event.runId, event.sourceFile)
            .arg(event.sourceLine)
            .arg(event.timePs, row.lastTime, event.message, valueText(event));
    if (role != Qt::DisplayRole)
        return {};
    switch (index.column())
    {
    case 0:
        return event.timePs;
    case 1:
        return event.level;
    case 2:
        return event.tag;
    case 3:
        return event.instance;
    case 4:
        return event.message;
    case 5:
        return valueText(event);
    case 6:
        return QVariant::fromValue(row.count);
    case 7:
        return event.sourceFile;
    case 8:
        return event.sourceLine;
    default:
        return {};
    }
}
void SimulationEventModel::startRun(const QString &id)
{
    beginResetModel();
    run = id;
    raw.clear();
    rows.clear();
    repeatRows.clear();
    discarded = 0;
    retainedBytes = 0;
    endResetModel();
}
void SimulationEventModel::add(const SimulationEvent &event)
{
    const auto key = merge ? repeatKey(event) : QString();
    if (merge && repeatRows.contains(key))
    {
        auto &row = rows[repeatRows.value(key)];
        ++row.count;
        row.lastTime = event.timePs;
    }
    else
    {
        if (merge)
            repeatRows.insert(key, rows.size());
        rows << Row{event, 1, event.timePs};
    }
}
void SimulationEventModel::append(const QList<SimulationEvent> &batch)
{
    if (batch.isEmpty())
        return;
    beginResetModel();
    for (const auto &event : batch)
    {
        if (event.runId != run)
            continue;
        // Bound retained text as well as row count; unusually wide fields must not exhaust memory.
        const qsizetype eventBytes = 512 + 2 * (event.runId.size() + event.timePs.size() + repeatKey(event).size());
        if (raw.size() >= limit || retainedBytes + eventBytes > 32 * 1024 * 1024)
        {
            ++discarded;
            continue;
        }
        retainedBytes += eventBytes;
        raw << event;
        add(event);
    }
    endResetModel();
}
void SimulationEventModel::setMergeRepeats(bool enabled)
{
    if (merge == enabled)
        return;
    beginResetModel();
    merge = enabled;
    rows.clear();
    repeatRows.clear();
    for (const auto &event : raw)
        add(event);
    endResetModel();
}
SimulationEventsPanel::SimulationEventsPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("simulationEventsPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    auto *toolbar = new QHBoxLayout;
    layout->addLayout(toolbar);
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
    toolbar->addWidget(merge);
    auto *columns = new QToolButton(this);
    columns->setText("Columns");
    columns->setPopupMode(QToolButton::InstantPopup);
    toolbar->addWidget(columns);
    auto *menu = new QMenu(columns);
    columns->setMenu(menu);
    model = new SimulationEventModel(this);
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
    status = new QLabel(this);
    layout->addWidget(status);
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
    currentRun = run;
    rejected = 0;
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
    rejected = count;
    updateStatus();
}
void SimulationEventsPanel::updateStatus()
{
    status->setText(
        currentRun.isEmpty()
            ? QStringLiteral("No current simulation")
            : QStringLiteral("%1 rows · Event level does not determine run status%2%3")
                  .arg(model->rowCount())
                  .arg(model->overflow()
                           ? QStringLiteral(" · Capacity reached: %1 further events omitted; see Run log")
                                 .arg(model->overflow())
                           : QString())
                  .arg(rejected
                           ? QStringLiteral(" · %1 malformed or oversized records; see Run log").arg(rejected)
                           : QString()));
}
} // namespace simdock
