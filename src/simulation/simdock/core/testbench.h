#pragma once
#include "model.h"
namespace simdock {
bool isTimingInput(const Port& port);
QString generateTestbench(const Module& module, const TbOptions& options, qint64 durationNs, QString* error, const QString& stimulus = {}, const QString& scoreboard = {});
TbOptions suggestedTbOptions(const Module& module);
}
