#pragma once
#include "stimulus.h"

namespace simdock {
// JSON is UI data: port bindings, numbers, enums and check switches. No HDL expressions.
QJsonObject defaultScoreboard();
bool scoreboardEnabled(const QJsonObject &);
QString scoreboardCode(const QList<StimulusSignal> &, const TbOptions &, const QJsonObject &,
                       QString *error);
QString scoreboardLabel(const QJsonObject &);
}
