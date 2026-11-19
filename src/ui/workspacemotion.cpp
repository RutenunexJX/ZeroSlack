#include "workspacemotion.h"
#include "applicationthememanager.h"

#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QRegion>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

bool WorkspaceMotion::enabled()
{
    if (!ApplicationThemeManager::instance().animationsEnabled()
        || !QApplication::isEffectEnabled(Qt::UI_General))
        return false;
#ifdef Q_OS_WIN
    BOOL animate = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0))
        return animate;
#endif
    return true;
}

WorkspaceMotion::WelcomeFade::WelcomeFade(QWidget* welcome, QWidget* recentContent, QWidget* window)
    : QWidget(window), background(recentContent->palette().color(recentContent->backgroundRole())), opacity(this)
{
    setObjectName(QStringLiteral("welcomeWorkspaceFade"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setGeometry(QRect(welcome->mapTo(window, QPoint()), welcome->size()));
    const QPoint origin = recentContent->mapTo(welcome, QPoint());
    const QRect visible = QRect(origin, recentContent->size()).intersected(welcome->rect());
    if (!visible.isEmpty()) {
        recent = recentContent->grab(visible.translated(-origin));
        recentOrigin = visible.topLeft();
    }
    opacity.setStartValue(1.0);
    opacity.setEndValue(0.0);
    opacity.setDuration(160);
    opacity.setEasingCurve(QEasingCurve::OutCubic);
    connect(&opacity, &QVariantAnimation::valueChanged, this, [this] { update(); });
    connect(&opacity, &QVariantAnimation::finished, this, &WelcomeFade::dismiss);
    connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::animationsEnabledChanged,
            this, [this](bool on) { if (!on) dismiss(); });
}

void WorkspaceMotion::WelcomeFade::start()
{
    parentWidget()->installEventFilter(this);
    show();
    raise();
    opacity.start();
}

void WorkspaceMotion::WelcomeFade::dismiss()
{
    opacity.stop();
    hide();
    deleteLater();
}

bool WorkspaceMotion::WelcomeFade::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && (event->type() == QEvent::Resize || event->type() == QEvent::Hide
                                      || event->type() == QEvent::DevicePixelRatioChange))
        dismiss();
    return false;
}

void WorkspaceMotion::WelcomeFade::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setOpacity(opacity.currentValue().toReal());
    const QRect recentRect(recentOrigin, recent.deviceIndependentSize().toSize());
    painter.setClipRegion(QRegion(rect()).subtracted(QRegion(recentRect)));
    painter.fillRect(rect(), background);
    painter.setClipping(false);
    painter.drawPixmap(recentOrigin, recent);
}
