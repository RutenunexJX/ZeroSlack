#pragma once
#include "semanticindex.h"
namespace SimulationCommandPrefix
{
bool refersToValue(const QString &expression, SemanticIndex &index, const SemanticQueryContext &context);
} // namespace SimulationCommandPrefix
