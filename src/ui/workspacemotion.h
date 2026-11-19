#pragma once

#include "zeroslackexport.h"
#include <QPixmap>
#include <QVariantAnimation>
#include <QWidget>

namespace WorkspaceMotion {
ZEROSLACK_API bool enabled();

class WelcomeFade final : public QWidget {
public:
    WelcomeFade(QWidget* welcome, QWidget* recentContent, QWidget* window);
    void start();
    void dismiss();

protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPixmap recent;
    QPoint recentOrigin;
    QColor background;
    QVariantAnimation opacity;
};
}
