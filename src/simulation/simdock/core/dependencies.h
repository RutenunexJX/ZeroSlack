#pragma once
#include "model.h"
#include <QHash>
#include <zeroslack/semantic/compilationorderservice.h>
#include <atomic>

namespace simdock {
struct BuildInputPlan {
    CompilationOrderResult order;
    QStringList includeDirectories;
    QHash<QString, QString> defines;
    QString error() const { return order.diagnostics.join(QLatin1Char('\n')); }
    void apply(Project& project, const QString& root) const;
};
BuildInputPlan planBuildInputs(const QString& root, const Project& project,
                              const std::atomic_bool* cancelled = nullptr,
                              const QMap<QString, QByteArray>& overrides = {});
}
