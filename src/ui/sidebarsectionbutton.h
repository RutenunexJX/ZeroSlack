#pragma once
#include "roundedicons.h"
#include <QPushButton>
#include <QStyleOptionButton>

class SidebarSectionButton final : public QPushButton {
public:
    SidebarSectionButton(const QString& text, QWidget* parent) : QPushButton(text, parent) {
        setFlat(true); setCheckable(true); setChecked(true);
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(28);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QStyleOptionButton option; initStyleOption(&option);
        if (hasFocus()) style()->drawPrimitive(QStyle::PE_FrameFocusRect, &option, &painter, this);
        auto color = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::Text);
        color.setAlphaF(.68); painter.setPen(color);
        painter.drawText(rect().adjusted(20, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, text());
        if (underMouse() || hasFocus()) RoundedIcons::icon(isChecked() ? RoundedIcons::Down : RoundedIcons::Right)
            .paint(&painter, QRect(1, (height()-14)/2, 14, 14));
    }
    void enterEvent(QEnterEvent* event) override { QPushButton::enterEvent(event); update(); }
    void leaveEvent(QEvent* event) override { QPushButton::leaveEvent(event); update(); }
};
