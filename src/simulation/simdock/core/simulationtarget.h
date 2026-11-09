#pragma once
#include "zeroslackexport.h"
#include <QHash>
#include <QMap>
#include <QStringList>

namespace simdock {
struct SimulationTarget {
    QString file, top;
    bool operator==(const SimulationTarget&) const = default;
    bool isEmpty() const { return file.isEmpty() && top.isEmpty(); }
};
ZEROSLACK_API QStringList declaredSimulationTops(const QString& root, const QString& file,
    const QStringList& includes, const QHash<QString, QString>& defines,
    const QMap<QString, QByteArray>& overrides, QString* error);
ZEROSLACK_API QString validateSimulationTarget(const QString& root, const SimulationTarget& target,
    const QStringList& includes = {}, const QHash<QString, QString>& defines = {},
    const QMap<QString, QByteArray>& overrides = {});
}
