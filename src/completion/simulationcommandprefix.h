#pragma once
#include "globalcontrolservice.h"
namespace SimulationCommandPrefix
{
bool capture(GlobalControlQueryContext &context);
bool refersToValue(const QString &expression, SemanticIndex &index, const SemanticQueryContext &context);
} // namespace SimulationCommandPrefix
