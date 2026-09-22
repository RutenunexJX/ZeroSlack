#pragma once

#include "zeroslackexport.h"

class QWidget;
class QLabel;
class QToolButton;
class QMainWindow;

struct UiWindowTitleBar {
    QWidget* widget = nullptr;
    QLabel* label = nullptr;
    QToolButton* sidebar = nullptr;
    QToolButton* maximize = nullptr;
};

namespace UiWindowChrome {
// The host retains frame geometry, native hit testing and document close policy.
ZEROSLACK_API UiWindowTitleBar createTitleBar(QMainWindow* host);
}
