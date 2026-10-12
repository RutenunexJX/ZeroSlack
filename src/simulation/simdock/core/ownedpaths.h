#pragma once
#include <QString>

namespace simdock {
bool plainPathWithin(const QString& parent, const QString& path);
bool removeOwnedTree(const QString& parent, const QString& path, QString* error);
}
