#pragma once
#include "wavepalette.h"
#include <QList>
#include <QStringList>
#include <QHash>

namespace slang { class SourceManager; namespace ast { class Compilation; } }
namespace simdock {
struct WaveGroupObject { QString group, path, file; int line = 0; };
struct WaveGroupPlan { QList<WaveGroupObject> objects; QStringList diagnostics; };
bool validWaveGroupName(const QString& name);
QStringList waveGroupNames(const QHash<QString, QString>& sources);
WaveGroupPlan collectWaveGroups(slang::ast::Compilation& compilation, const slang::SourceManager& manager);
QString waveGroupsScript(const WaveGroupPlan& groups, WaveTheme theme = WaveTheme::QuestaDefault);
}
