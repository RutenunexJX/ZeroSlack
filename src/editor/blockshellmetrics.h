#pragma once
#include <QMargins>

namespace BlockShellMetrics {
inline constexpr int viewportInset = 4;
inline constexpr int border = 1;
inline constexpr QMargins contents{12, 34, 12, 10};
inline constexpr int horizontalOverhead = 2 * (viewportInset + border) + contents.left() + contents.right();
inline constexpr int verticalOverhead = 2 * border + contents.top() + contents.bottom();
}
