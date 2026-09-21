#include "applicationthememanager.h"

#include "insightvisualstyle.h"
#include "roundedicons.h"
#include "uitypography.h"
#ifdef ZEROSLACK_ENABLE_ELA
#include "elabackend.h"
#endif
#ifdef ZEROSLACK_ENABLE_QLEMENTINE
#include "qlementinebackend.h"
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QChildEvent>
#include <QScopedValueRollback>
#include <QTimer>
#include <QWidget>

namespace {
constexpr auto classicRoot = "_zeroslackClassicRoot";
constexpr auto managedChild = "_zeroslackClassicChild";
constexpr auto originalQss = "_zeroslackOriginalSurfaceQss";
constexpr auto appliedQss = "_zeroslackAppliedSurfaceQss";
constexpr auto updatePending = "_zeroslackSurfaceUpdatePending";

QWidget* surfaceRoot(QWidget* widget) {
    for (auto* parent = widget; parent; parent = parent->parentWidget())
        if (parent->property(classicRoot).toBool()) return parent;
    return nullptr;
}
}

ApplicationThemeManager& ApplicationThemeManager::instance()
{
    static ApplicationThemeManager manager;
    return manager;
}

ApplicationThemeManager::ApplicationThemeManager() = default;

bool ApplicationThemeManager::qlementineAvailable()
{
#ifdef ZEROSLACK_ENABLE_QLEMENTINE
    return true;
#else
    return false;
#endif
}

bool ApplicationThemeManager::selectBackend(UiStyleBackend backend)
{
    if ((installed && backend != currentBackend)
        || (backend == UiStyleBackend::Qlementine && !qlementineAvailable())
        || (backend == UiStyleBackend::Ela && !elaAvailable()))
        return false;
    currentBackend = backend;
    return true;
}

bool ApplicationThemeManager::elaAvailable()
{
#ifdef ZEROSLACK_ENABLE_ELA
    return true;
#else
    return false;
#endif
}

void ApplicationThemeManager::setAnimationsEnabled(bool enabled)
{
    animateControls = enabled;
#ifdef ZEROSLACK_ENABLE_QLEMENTINE
    if (backendStyle && currentBackend == UiStyleBackend::Qlementine)
        QlementineBackend::applyTheme(backendStyle, currentMode, enabled);
#endif
}

void ApplicationThemeManager::preserveClassicSurface(QWidget* root)
{
    if (!root || currentBackend != UiStyleBackend::Qlementine) return;
    root->setProperty(classicRoot, true);
    if (!classicRoots.contains(root)) {
        classicRoots.insert(root);
        connect(root, &QObject::destroyed, this, [this, root] { classicRoots.remove(root); });
    }
    synchronizeSurface(root);
}

void ApplicationThemeManager::synchronizeSurface(QWidget* widget)
{
    if (!widget || !classicStyle || updatingSurface) return;
    QScopedValueRollback<bool> guard(updatingSurface, true);
    QList<QPointer<QWidget>> widgets{widget};
    for (auto* child : widget->findChildren<QWidget*>()) widgets.append(child);
    for (const auto& pointer : widgets) {
        auto* child = pointer.data();
        if (!child) continue;
        auto* root = surfaceRoot(child);
        const bool managed = child->property(managedChild).toBool();
        if (root) {
            if (!managed) {
                child->setProperty(managedChild, true);
                child->setStyle(classicStyle);
            }
            if (root == child) {
                const QString current = child->styleSheet();
                if (current != child->property(appliedQss).toString())
                    child->setProperty(originalQss, current);
                const QString sheet = InsightVisualStyle::applicationStyleSheet(currentMode)
                    + child->property(originalQss).toString();
                child->setProperty(appliedQss, sheet);
                if (current != sheet) child->setStyleSheet(sheet);
            }
        } else if (managed) {
            child->setProperty(managedChild, false);
            child->setStyle(nullptr);
        }
    }
}

void ApplicationThemeManager::queueSurfaceUpdate(QWidget* widget)
{
    if (!widget || updatingSurface || widget->property(updatePending).toBool()) return;
    widget->setProperty(updatePending, true);
    QTimer::singleShot(0, widget, [this, widget] {
        widget->setProperty(updatePending, false);
        synchronizeSurface(widget);
    });
}

bool ApplicationThemeManager::eventFilter(QObject* watched, QEvent* event)
{
    if (updatingSurface) return false;
    if (event->type() == QEvent::ChildPolished) {
        if (auto* child = qobject_cast<QWidget*>(static_cast<QChildEvent*>(event)->child()))
            if (surfaceRoot(child)) queueSurfaceUpdate(child);
    } else if (event->type() == QEvent::Polish || event->type() == QEvent::ParentChange
               || event->type() == QEvent::StyleChange) {
        if (auto* widget = qobject_cast<QWidget*>(watched))
            if (surfaceRoot(widget) || widget->property(managedChild).toBool())
                queueSurfaceUpdate(widget);
    }
    return false;
}

ThemeMode ApplicationThemeManager::mode() const
{
    return currentMode;
}

const InsightTheme& ApplicationThemeManager::theme() const
{
    return InsightVisualStyle::theme(currentMode);
}

void ApplicationThemeManager::setMode(ThemeMode mode)
{
    if (currentMode == mode) {
        applyToApplication();
        return;
    }

    emit themeAboutToChange(currentMode, mode);
    currentMode = mode;
    applyToApplication();
    emit themeChanged(currentMode);
}

void ApplicationThemeManager::applyToApplication()
{
    auto* application = qobject_cast<QApplication*>(
        QCoreApplication::instance());
    if (!application)
        return;

    if (!installed) {
#ifdef ZEROSLACK_ENABLE_ELA
        if (currentBackend == UiStyleBackend::Ela) ElaBackend::initialize();
#endif
#ifdef ZEROSLACK_ENABLE_QLEMENTINE
        if (currentBackend == UiStyleBackend::Qlementine) {
            backendStyle = QlementineBackend::create();
            classicStyle = new RoundedIcons::Style;
            classicStyle->setParent(application);
            application->installEventFilter(this);
        } else
#endif
        {
            backendStyle = new RoundedIcons::Style;
        }
        application->setStyle(backendStyle);
        application->setFont(UiTypography::font());
        installed = true;
    }
#ifdef ZEROSLACK_ENABLE_QLEMENTINE
    if (currentBackend == UiStyleBackend::Qlementine)
        QlementineBackend::applyTheme(backendStyle, currentMode, animateControls);
#endif
    application->setPalette(
        InsightVisualStyle::applicationPalette(currentMode));
#ifdef ZEROSLACK_ENABLE_ELA
    if (currentBackend == UiStyleBackend::Ela) {
        ElaBackend::applyTheme(currentMode);
        application->setStyleSheet(ElaBackend::styleSheet(currentMode));
        return;
    }
#endif
    application->setStyleSheet(
        currentBackend == UiStyleBackend::Qlementine
            ? InsightVisualStyle::chromeStyleSheet(currentMode)
            : InsightVisualStyle::applicationStyleSheet(currentMode));
    for (auto* root : classicRoots) synchronizeSurface(root);
}
