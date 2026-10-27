#pragma once
#include "slangmanager.h"
namespace slang::ast { class Compilation; }
QVector<SignalDependencyInfo> collectSlangSignalDependencies(
    slang::ast::Compilation& compilation, const std::function<bool()>& cancelled);
