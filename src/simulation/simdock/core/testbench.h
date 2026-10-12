#pragma once
#include "model.h"
class SemanticIndex;
namespace simdock {
bool completeTestbenchTypes(Module& module, SemanticIndex& index, const QString& absoluteFile,
                           const QString& documentText, QString* error);
bool isTimingInput(const Port& port);
QString generateTestbench(const Module& module, const TbOptions& options, qint64 durationNs, QString* error);
TbOptions suggestedTbOptions(const Module& module);
}
