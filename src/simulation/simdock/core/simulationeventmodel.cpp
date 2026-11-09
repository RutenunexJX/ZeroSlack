#include "simulationeventmodel.h"
#include <QJsonDocument>
namespace simdock {
namespace {
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
}
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
    rejected = 0;
    raw.clear();
    rows.clear();
    repeatRows.clear();
    discarded = 0;
    retainedBytes = 0;
    endResetModel();
    emit summaryChanged();
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
    emit summaryChanged();
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
    emit summaryChanged();
}
void SimulationEventModel::reject(quint64 count)
{ rejected = count; emit summaryChanged(); }
}
