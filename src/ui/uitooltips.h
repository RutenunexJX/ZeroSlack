#pragma once

#include "zeroslackexport.h"
#include <QPoint>
#include <QRect>
#include <QString>

class QWidget;

namespace UiToolTips {
ZEROSLACK_API void install();
ZEROSLACK_API void showText(const QPoint& position, const QString& text, QWidget* owner = nullptr,
                           const QRect& anchor = {}, int duration = -1);
ZEROSLACK_API void hideText();
}
