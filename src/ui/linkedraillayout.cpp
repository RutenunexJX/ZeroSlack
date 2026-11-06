#include "linkedraillayout.h"

#include <algorithm>
#include <limits>

namespace LinkedRailLayout {

bool Placement::operator==(const Placement& other) const
{
    return id == other.id && contentY == other.contentY && height == other.height
        && viewportY == other.viewportY && outlines == other.outlines && visible == other.visible;
}

bool Result::operator==(const Result& other) const
{
    return groups == other.groups && totalHeight == other.totalHeight && valid == other.valid;
}

Result arrange(const QList<Input>& groups, qint64 viewportHeight,
               qint64 scrollOffset, qint64 gap, qint64 contentTop)
{
    constexpr qint64 limit = std::numeric_limits<qint64>::max() / 4;
    viewportHeight = std::max<qint64>(0, viewportHeight);
    scrollOffset = std::max<qint64>(0, scrollOffset);
    contentTop = std::max<qint64>(0, contentTop);
    gap = std::max<qint64>(0, gap);
    if (viewportHeight > limit || scrollOffset > limit || contentTop > limit || gap > limit)
        return {{}, 0, false};

    Result result;
    result.groups.reserve(groups.size());
    qint64 bottom = contentTop;
    for (const auto& input : groups) {
        const qint64 height = std::max<qint64>(0, input.preferredHeight);
        const qint64 precedingGap = result.groups.isEmpty() ? 0 : gap;
        if (bottom > limit - precedingGap || height > limit || input.idealY > limit)
            return {{}, 0, false};
        const qint64 y = std::max({contentTop, input.idealY, bottom + precedingGap});
        if (y > limit - height)
            return {{}, 0, false};
        bottom = y + height;
        const qint64 projected = y - scrollOffset;
        const int outlines = input.expanded ? 0 : std::clamp(input.cardCount, 1, 3) - 1;
        const bool visible = viewportHeight > 0 && height > 0
            && projected < viewportHeight && bottom > scrollOffset;
        result.groups.append({input.id, y, height, projected, outlines, visible});
    }
    result.totalHeight = groups.isEmpty() ? 0 : bottom;
    return result;
}

} // namespace LinkedRailLayout
