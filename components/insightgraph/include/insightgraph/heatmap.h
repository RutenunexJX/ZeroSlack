#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace insightgraph {
enum class HeatRole { Write, Read, Port, Control, Clock, Unknown };
enum HeatColumn { File, Module, Write, Read, Port, Control, Clock, Unknown, Total, Distribution, Columns };
struct HeatEvidence {
    QString id, file, module, text, roleLabel;
    HeatRole role = HeatRole::Unknown;
    QByteArray sourceHash;
    int fileLineCount = 0, line = 0;
};
struct HeatRow {
    QString file, module;
    QList<int> items;
    int counts[6] = {};
    bool available = true;
};
struct HeatProjection {
    QList<HeatRow> rows;
    int maximum = 0;
};
HeatProjection projectHeatmap(const QList<HeatEvidence>& items,
    const QString& filter, int roleMask, int sortColumn, Qt::SortOrder order);
QString heatHeading(int column, bool complete);
QByteArray heatCsv(const HeatProjection& data, bool complete);
QList<qreal> distribution(const QList<HeatEvidence>& items);
}
