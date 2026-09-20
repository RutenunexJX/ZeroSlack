#pragma once
#include <QApplication>
#include <QStyle>
#include "workbench_theme.hpp"
namespace RegMapPilot {
bool enabled();
QStyle* install(QApplication& application);
void update(QStyle* style, WorkbenchTheme::Mode mode, const QFont& font, const QPalette& palette);
}
