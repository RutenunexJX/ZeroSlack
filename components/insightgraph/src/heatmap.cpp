#include <insightgraph/heatmap.h>
#include <QHash>
#include <algorithm>

namespace insightgraph {
HeatProjection projectHeatmap(const QList<HeatEvidence>& items,
    const QString& filter, int mask, int sortColumn, Qt::SortOrder order)
{
    HeatProjection result;
    auto& rows = result.rows;
    QHash<QString, int> indexes;
    for (int i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        if (!(mask & (1 << int(item.role)))) continue;
        if (!filter.isEmpty() && !(item.file + item.module + item.text + item.roleLabel)
                .contains(filter, Qt::CaseInsensitive)) continue;
        const QString key = item.file + QLatin1Char('\n') + item.module;
        if (!indexes.contains(key)) {
            indexes[key] = rows.size();
            rows.append({item.file, item.module, {}, {}, true});
        }
        auto& row = rows[indexes[key]];
        row.items.append(i);
        ++row.counts[int(item.role)];
        row.available &= !item.sourceHash.isEmpty() && item.fileLineCount > 0;
    }
    for (const auto& row : rows)
        for (int count : row.counts) result.maximum = qMax(result.maximum, count);
    std::stable_sort(rows.begin(), rows.end(), [&](const HeatRow& a, const HeatRow& b) {
        int compared = 0;
        if (sortColumn == File || sortColumn == Distribution)
            compared = QString::compare(a.file, b.file, Qt::CaseInsensitive);
        else if (sortColumn == Module)
            compared = QString::compare(a.module, b.module, Qt::CaseInsensitive);
        else compared = (sortColumn == Total ? a.items.size() : a.counts[sortColumn - Write])
            - (sortColumn == Total ? b.items.size() : b.counts[sortColumn - Write]);
        if (!compared) compared = QString::compare(a.file + a.module, b.file + b.module);
        return order == Qt::AscendingOrder ? compared < 0 : compared > 0;
    });
    return result;
}
QString heatHeading(int column, bool complete)
{
    if (column == Total && !complete) return QStringLiteral("Loaded total");
    static const QStringList names{"File", "Module", "Write", "Read", "Port", "Control",
        "Clock / reset", "Unknown", "Total", "File distribution"};
    return names.value(column);
}
QByteArray heatCsv(const HeatProjection& data, bool complete)
{
    const auto escape = [](QString value) {
        value.replace('"', QStringLiteral("\"\""));
        return QLatin1Char('"') + value + QLatin1Char('"');
    };
    QStringList lines, headings;
    for (int c = 0; c < Distribution; ++c) headings << escape(heatHeading(c, complete));
    headings << escape(QStringLiteral("Coverage"));
    lines << headings.join(',');
    for (const auto& row : data.rows) {
        QStringList values{escape(row.file), escape(row.module)};
        for (int c = Write; c <= Total; ++c)
            values << escape(!row.available ? QStringLiteral("—") : QString::number(
                c == Total ? row.items.size() : row.counts[c - Write]));
        values << escape(complete ? QStringLiteral("Complete")
            : QStringLiteral("Partial: loaded reference pages only"));
        lines << values.join(',');
    }
    return (lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n")).toUtf8();
}
QList<qreal> distribution(const QList<HeatEvidence>& items)
{
    QList<qreal> result;
    for (const auto& item : items) {
        if (item.fileLineCount < 1) continue;
        result.append(qBound(0.0, double(item.line - 1) / qMax(1, item.fileLineCount - 1), 1.0));
    }
    return result;
}
}
