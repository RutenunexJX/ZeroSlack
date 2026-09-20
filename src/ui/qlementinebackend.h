#pragma once

class QStyle;
enum class ThemeMode;

// Internal optional backend; no third-party types in the product API.
namespace QlementineBackend {
QStyle* create();
void applyTheme(QStyle* style, ThemeMode mode, bool animationsEnabled);
}
