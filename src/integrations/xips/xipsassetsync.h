#pragma once
#include "simulationcommandplanner.h"
#include "zeroslackexport.h"
#include <QVariantMap>
#include <atomic>
#include <memory>

struct XipsAssetSyncPlan
{
    QString error;
    QVariantMap operation;
};

class ZEROSLACK_API XipsAssetSync
{
  public:
    static QString executable();
    static QString stateDirectory();
    static XipsAssetSyncPlan prepare(const QString &workspace, const QStringList &files,
                                     const SimulationCommandRequest &context, const QVariantMap &binding,
                                     const QString &library, const QString &name,
                                     const std::shared_ptr<std::atomic_bool> &cancelled = {});
    static QVariantMap invoke(const QString &executable, const QString &stateDirectory,
                              const QVariantMap &request,
                              const std::shared_ptr<std::atomic_bool> &cancelled = {});
    static QVariantMap execute(const QVariantMap &operation, const QString &action,
                               const std::shared_ptr<std::atomic_bool> &cancelled = {});
    static QVariantList recoverable(const QString &workspace, QString *error = nullptr);
    static QString inputsCurrent(const QVariantMap &operation);
};
