#pragma once

#include "applicationthememanager.h"
#include <QString>

namespace ElaBackend {
void initialize();
void applyTheme(ThemeMode mode);
QString styleSheet(ThemeMode mode);
}
