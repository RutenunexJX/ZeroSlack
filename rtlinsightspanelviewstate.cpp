#include "rtlinsightspanelviewstate.h"

#include <type_traits>

static_assert(
    std::is_aggregate_v<RtlInsightsPanelViewState>,
    "Insight view state must remain a passive, single-owner aggregate");
