#pragma once
#include "model.h"
#include <QHash>

namespace simdock {
struct DependencySelection {
    QStringList sources;
    QStringList messages;
};
// Owns an immutable scan snapshot; never keeps pointers into Workbench's scan.
struct DependencyIndex {
    explicit DependencyIndex(const Scan& scan = {});
    Scan scan;
    QHash<QString, qsizetype> files;
    QHash<QString, QString> paths;
    QHash<QString, QStringList> units, packages;
};
DependencySelection selectDependencies(const DependencyIndex& index, const QString& source, const QStringList& selected);
DependencySelection selectDependencies(const Scan& scan, const QString& source, const QStringList& selected);
}
