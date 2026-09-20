#pragma once

#include "insightvisualstyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

// One state definition for application controls; widget helpers only assign roles.
namespace InsightControlStyle {
inline QString buttonRules(const InsightTheme& t, const QString& selector,
                           bool quiet = false, bool primary = false)
{
    const auto& b = t.button;
    const QString background = primary ? b.backgroundChecked.name()
        : quiet ? QStringLiteral("transparent") : b.background.name();
    const QString foreground = primary ? b.textChecked.name() : b.text.name();
    const QString border = primary ? b.borderChecked.name()
        : quiet ? QStringLiteral("transparent")
                : InsightVisualStyle::subtleBorder(b.background, t.textPrimary).name();
    const QColor selected = quiet ? t.itemView.selectedBackground
        : primary ? b.backgroundChecked.darker(110) : b.backgroundChecked;
    const QColor selectedText = quiet ? t.textPrimary : b.textChecked;
    return QStringLiteral(
        "%1 { background: %2; color: %3; border: 1px solid %4;"
        " border-radius: 8px; padding: 5px 9px; }"
        "%1:hover:enabled { background: %5; color: %6; border-color: %7; }"
        "%1:pressed:enabled { background: %8; }"
        "%1:checked:enabled { background: %9; color: %10; border-color: %9; }"
        "%1:checked:hover:enabled { border-color: %11; }"
        "%1:checked:pressed:enabled { background: %12; }"
        "%1:focus:enabled { border-color: %11; }"
        "%1:checked:focus:enabled { border-color: %13; }"
        "%1:disabled { background: %14; color: %15; border-color: %16; }")
        .arg(selector, background, foreground, border,
             primary ? b.backgroundChecked.lighter(108).name() : b.backgroundHover.name(),
             primary ? b.textChecked.name() : b.textHover.name(), b.borderHover.name(),
             primary ? b.backgroundChecked.darker(110).name() : b.backgroundPressed.name(),
             selected.name(), selectedText.name(),
             primary ? t.focus.ringOnAccent.name() : t.focus.ring.name(),
             selected.darker(110).name(),
             quiet ? t.focus.ring.name() : t.focus.ringOnAccent.name(),
             t.panelSubtle.name(), b.textDisabled.name(),
             InsightVisualStyle::subtleBorder(t.panelSubtle, t.textPrimary).name());
}

inline QString styleSheet(const InsightTheme& t)
{
    return buttonRules(t, QStringLiteral("QPushButton"))
        + buttonRules(t, QStringLiteral("QToolButton"), true)
        + buttonRules(t, QStringLiteral("QPushButton[_zeroslackInsightThemeHelper=\"primaryButton\"]"), false, true)
        + QStringLiteral(
            "QPushButton[_zeroslackInsightThemeHelper=\"toolbarButton\"] {"
            " padding: 4px 9px; min-height: 22px; }"
            "QCheckBox { color: %1; spacing: 6px; padding: 2px;"
            " border: 1px solid transparent; border-radius: 6px; }"
            "QCheckBox:hover:enabled { background: %2; }"
            "QCheckBox:pressed:enabled { background: %3; }"
            "QCheckBox:focus:enabled { border-color: %4; }"
            "QCheckBox:disabled { color: %5; }"
            "QCheckBox::indicator { width: 16px; height: 16px; }"
            "QCheckBox[_zeroslackInsightThemeHelper=\"segmentedCheckBox\"] {"
            " spacing: 4px; padding: 2px 6px; min-height: 22px; }")
            .arg(t.textPrimary.name(), t.itemView.hoverBackground.name(),
                 t.button.backgroundPressed.name(), t.focus.ring.name(),
                 t.button.textDisabled.name());
}

inline void drawCheckBoxIndicator(const QStyleOption& option, QPainter& painter)
{
    const auto& t = InsightVisualStyle::theme();
    const bool enabled = option.state.testFlag(QStyle::State_Enabled);
    const bool mixed = option.state.testFlag(QStyle::State_NoChange);
    const bool checked = mixed || option.state.testFlag(QStyle::State_On);
    const bool hovered = enabled && option.state.testFlag(QStyle::State_MouseOver);
    QColor fill = checked ? t.accent : t.panelBackground;
    QColor border = checked || hovered ? t.accent : t.borderStrong;
    QColor mark = t.button.textChecked;
    if (!enabled) {
        fill = t.panelSubtle;
        border = t.textMuted;
        mark = t.button.textDisabled;
    } else if (option.state.testFlag(QStyle::State_Sunken)) {
        fill = checked ? fill.darker(110) : t.button.backgroundPressed;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal side = qMin(option.rect.width(), option.rect.height());
    painter.translate(QRectF(option.rect).center() - QPointF(side / 2, side / 2));
    painter.scale(side / 16, side / 16);
    painter.setPen(QPen(border, 1));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(.5, .5, 15, 15), 4, 4);
    if (checked) {
        painter.setPen(QPen(mark, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath path;
        if (mixed) {
            path.moveTo(4, 8); path.lineTo(12, 8);
        } else {
            path.moveTo(3.8, 8); path.lineTo(6.7, 10.8); path.lineTo(12.2, 5.3);
        }
        painter.drawPath(path);
    }
    painter.restore();
}
}
