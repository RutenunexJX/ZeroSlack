#include "liveinsighttoolpage.h"

#include "rtlinsightworkbench.h"

#include <QDockWidget>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QPushButton>
#include <QSizePolicy>
#include <QShowEvent>
#include <QVBoxLayout>

#include <utility>

LiveInsightToolPage::LiveInsightToolPage(
    LiveInsightKind kind,
    QWidget* parent)
    : QWidget(parent)
    , insightKind(kind)
{
    setObjectName(
        QStringLiteral("liveInsightToolPage.%1")
            .arg(liveInsightKindId(kind)));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    createSurface();
}

LiveInsightToolPage::~LiveInsightToolPage() = default;

LiveInsightKind LiveInsightToolPage::kind() const
{
    return insightKind;
}

void LiveInsightToolPage::setNavigationHandler(
    NavigationHandler handler)
{
    navigationHandler = std::move(handler);
    if (detachedPage)
        detachedPage->setNavigationHandler(navigationHandler);
    if (workbench)
        workbench->setNavigationHandler(navigationHandler);
}

void LiveInsightToolPage::setStatusHandler(StatusHandler handler)
{
    statusHandler = std::move(handler);
    if (workbench) workbench->setStatusHandler(statusHandler);
    if (detachedPage)
        detachedPage->setStatusHandler(statusHandler);
}

void LiveInsightToolPage::setVisibilityHandler(
    VisibilityHandler handler)
{
    visibilityHandler = std::move(handler);
    if (detachedPage) {
        detachedPage->setVisibilityHandler(
            [owner = QPointer<LiveInsightToolPage>(this)](bool) {
                if (owner)
                    owner->notifyVisibility();
            });
    }
    notifyVisibility();
}

void LiveInsightToolPage::setCompactChrome(bool compact)
{
    if (workbench)
        workbench->setCompactChrome(compact);
}

void LiveInsightToolPage::fitGraph()
{
    if (workbench) workbench->fitGraph();
}

void LiveInsightToolPage::setContext(
    const LiveInsightToolContext& context)
{
    if (context.workspaceId == currentContext.workspaceId
        && context.documentId == currentContext.documentId
        && context.fileName == currentContext.fileName
        && (context.documentRevision < currentContext.documentRevision
            || (context.documentRevision == currentContext.documentRevision
                && context.semanticRevision < currentContext.semanticRevision))) return;
    currentContext = context;
    renderContext();
    if (detachedPage)
        detachedPage->setContext(context);
}

bool LiveInsightToolPage::hasVisibleSurface() const
{
    return isVisible()
        || (detachedWindow && detachedWindow->isVisible());
}

RtlInsightWorkbench* LiveInsightToolPage::workbenchForTest() const
{
    return workbench;
}

void LiveInsightToolPage::refreshTargetContext(const LiveInsightToolContext& editorContext)
{
    if (currentContext.fileName.isEmpty()) {
        setContext(editorContext);
        return;
    }
    if (currentContext.workspaceId != editorContext.workspaceId) return;
    auto refreshed = currentContext;
    refreshed.semanticRevision = qMax(refreshed.semanticRevision, editorContext.semanticRevision);
    if (refreshed.fileName == editorContext.fileName) {
        refreshed.documentId = editorContext.documentId;
        refreshed.documentRevision = editorContext.documentRevision;
        refreshed.documentText = editorContext.documentText;
        refreshed.dirty = editorContext.dirty;
    }
    setContext(refreshed);
}

QMainWindow* LiveInsightToolPage::detachToWindow()
{
    if (detachedWindow) {
        detachedWindow->show();
        detachedWindow->raise();
        detachedWindow->activateWindow();
        return detachedWindow;
    }
    auto* window = new QMainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    window->setObjectName(
        QStringLiteral("rtlInsightDetachedWindow.%1")
            .arg(liveInsightKindId(insightKind)));
    window->setWindowTitle(
        QStringLiteral("RTL Insight Workbench - %1")
            .arg(liveInsightKindDisplayName(insightKind)));
    auto* page = new LiveInsightToolPage(insightKind, window);
    detachedPage = page;
    page->setNavigationHandler(navigationHandler);
    page->setStatusHandler(statusHandler);
    page->setVisibilityHandler(
        [owner = QPointer<LiveInsightToolPage>(this)](bool) {
            if (owner)
                owner->notifyVisibility();
        });
    page->setContext(currentContext);
    window->setCentralWidget(page);
    window->resize(1120, 760);
    detachedWindow = window;
    QObject::connect(
        window,
        &QObject::destroyed,
        this,
        [owner = QPointer<LiveInsightToolPage>(this)]() {
            if (owner)
                owner->notifyVisibility();
        });
    window->show();
    return window;
}

QMainWindow* LiveInsightToolPage::detachedWindowForTest() const
{
    return detachedWindow;
}

void LiveInsightToolPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    notifyVisibility();
}

void LiveInsightToolPage::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    notifyVisibility();
}

void LiveInsightToolPage::createSurface()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    workbench = new RtlInsightWorkbench(this, true);
    const InsightWorkbenchViewKind viewKind =
        insightKind == LiveInsightKind::Kernel
        ? InsightWorkbenchViewKind::Kernel
        : insightKind == LiveInsightKind::Module
            ? InsightWorkbenchViewKind::Block
            : insightKind == LiveInsightKind::Hotspot
                ? InsightWorkbenchViewKind::Hotspot
                : InsightWorkbenchViewKind::StateTransition;
    workbench->setViewKind(viewKind);
    layout->addWidget(workbench, 1);
    if (QPushButton* button = workbench->detachButtonForTest()) {
        QObject::connect(
            button,
            &QPushButton::clicked,
            this,
            [this]() { detachToWindow(); });
    }
}

void LiveInsightToolPage::renderContext()
{

    if (workbench) {
        InsightViewContext context;
        context.workspaceId = currentContext.workspaceId;
        context.documentId = currentContext.documentId;
        context.documentRevision = currentContext.documentRevision;
        context.semanticRevision = currentContext.semanticRevision;
        context.fileName = currentContext.fileName;
        context.moduleName = currentContext.moduleName;
        context.signalName = currentContext.signalName;
        context.signalAccessPath = currentContext.signalAccessPath;
        workbench->setContext(context);
    }
}

void LiveInsightToolPage::notifyVisibility()
{
    if (visibilityHandler)
        visibilityHandler(hasVisibleSurface());
}
