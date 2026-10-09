#pragma once
#include "stimulus.h"
#include "workspace.h"
#include <memory>

namespace simdock {
struct PreparedStimulus {
    Scan scan;
    Module module;
    StimulusSemantics semantics;
};
// Runs in the workbench's serial worker pool. Reuse requires identical ordered
// source bytes and unchanged positive/negative compiler include observations.
std::shared_ptr<const PreparedStimulus> prepareStimulus(
    const QString& root, const Project&, const std::shared_ptr<const PreparedStimulus>& previous,
    SourceCache*, const std::atomic_bool* cancelled, QString* error);
}
