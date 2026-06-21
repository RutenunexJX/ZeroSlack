#include "globalcontrolcoordinator.h"

#include "globalcontrolpanel.h"

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QKeyEvent>
#include <QWidget>
#include <utility>

namespace {
constexpr qint64 kDoubleShiftIntervalMs = 350;
}

GlobalControlCoordinator::GlobalControlCoordinator(QWidget* anchorWidget,
                                                   QObject* parent)
    : QObject(parent)
    , anchor(anchorWidget)
{
    panel = std::make_unique<GlobalControlPanel>(anchorWidget);
    panel->setQueryChangedHandler(
        [this](const QString& text) { refresh(text); });
    panel->setItemActivatedHandler(
        [this](const GlobalControlItem& item) { dispatch(item); });
}

GlobalControlCoordinator::~GlobalControlCoordinator()
{
    if (installed && qApp)
        qApp->removeEventFilter(this);
}

void GlobalControlCoordinator::setProjectModel(ProjectModel* nextProjectModel)
{
    projectModel = nextProjectModel;
}

void GlobalControlCoordinator::setSemanticIndex(SemanticIndex* nextSemanticIndex)
{
    semanticIndex = nextSemanticIndex;
}

void GlobalControlCoordinator::setActionHandler(
    std::function<void(const GlobalControlItem&)> handler)
{
    actionHandler = std::move(handler);
}

void GlobalControlCoordinator::install()
{
    if (installed || !qApp)
        return;
    qApp->installEventFilter(this);
    installOnWidgetTree(anchor);
    installed = true;
}

bool GlobalControlCoordinator::eventFilter(QObject*, QEvent* event)
{
    if (event->type() == QEvent::ChildAdded)
        installOnWidgetTree(anchor);
    return handleKeyEvent(event);
}

void GlobalControlCoordinator::installOnWidgetTree(QWidget* widget)
{
    if (!widget)
        return;
    widget->installEventFilter(this);
    for (QWidget* child : widget->findChildren<QWidget*>())
        child->installEventFilter(this);
}

bool GlobalControlCoordinator::handleKeyEvent(QEvent* event)
{
    if (event == lastProcessedEvent)
        return false;
    lastProcessedEvent = event;

    if (event->type() != QEvent::KeyPress
        && event->type() != QEvent::KeyRelease) {
        return false;
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->isAutoRepeat())
        return false;

    if (keyEvent->type() == QEvent::KeyPress) {
        if (keyEvent->key() == Qt::Key_Shift) {
            shiftPressed = true;
            standaloneShift = true;
            return false;
        }
        if (shiftPressed)
            standaloneShift = false;
        return false;
    }

    if (keyEvent->key() != Qt::Key_Shift)
        return false;

    const bool validStandalone = shiftPressed && standaloneShift;
    shiftPressed = false;
    standaloneShift = false;
    if (!validStandalone) {
        lastShiftReleaseMs = -1;
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastShiftReleaseMs >= 0
        && now - lastShiftReleaseMs <= kDoubleShiftIntervalMs) {
        lastShiftReleaseMs = -1;
        open();
        keyEvent->accept();
        return true;
    }

    lastShiftReleaseMs = now;
    return false;
}

void GlobalControlCoordinator::open()
{
    if (!panel)
        return;
    if (panel->isVisible()) {
        panel->focusSearch();
        return;
    }
    refresh();
    panel->showCentered(anchor);
}

void GlobalControlCoordinator::refresh(const QString& queryText)
{
    if (!panel)
        return;
    panel->setItems(service.query(queryText, projectModel, semanticIndex));
}

void GlobalControlCoordinator::dispatch(const GlobalControlItem& item)
{
    if (actionHandler)
        actionHandler(item);
}
