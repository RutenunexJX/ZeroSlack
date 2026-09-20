#pragma once
#include <oclero/qlementine/style/Theme.hpp>
#include <QColor>
#include <QString>

namespace Pilot {
struct States { QColor normal, hover, pressed, disabled; };
struct Controls {
    States surface, text, accent, onAccent, border;
    QColor focus;
};
inline void applyControls(oclero::qlementine::Theme& q, const Controls& p) {
    q.neutralColor = p.surface.normal; q.neutralColorHovered = p.surface.hover;
    q.neutralColorPressed = p.surface.pressed; q.neutralColorDisabled = p.surface.disabled;
    q.secondaryColor = p.text.normal; q.secondaryColorHovered = p.text.hover;
    q.secondaryColorPressed = p.text.pressed; q.secondaryColorDisabled = p.text.disabled;
    q.primaryColor = p.accent.normal; q.primaryColorHovered = p.accent.hover;
    q.primaryColorPressed = p.accent.pressed; q.primaryColorDisabled = p.accent.disabled;
    q.primaryColorForeground = p.onAccent.normal; q.primaryColorForegroundHovered = p.onAccent.hover;
    q.primaryColorForegroundPressed = p.onAccent.pressed; q.primaryColorForegroundDisabled = p.onAccent.disabled;
    q.borderColor = p.border.normal; q.borderColorHovered = p.border.hover;
    q.borderColorPressed = p.border.pressed; q.borderColorDisabled = p.border.disabled;
    q.focusColor = p.focus;
}
struct Badge {
    QColor text, background, border;
    int radius = 8, verticalPadding = 2, horizontalPadding = 8, weight = 0;
};
inline QString badgeRule(const QString& selector, const Badge& p) {
    return QStringLiteral("%1 { color: %2; background: %3; border: 1px solid %4; "
                          "border-radius: %5px; padding: %6px %7px; %8 }")
        .arg(selector, p.text.name(), p.background.name(QColor::HexArgb), p.border.name(QColor::HexArgb))
        .arg(p.radius).arg(p.verticalPadding).arg(p.horizontalPadding)
        .arg(p.weight ? QStringLiteral("font-weight: %1;").arg(p.weight) : QString());
}
}
