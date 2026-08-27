#include "liveinsighttoolpage.h"

#include "rtlinsightspanelcoordinator.h"
#include "rtlinsightworkbench.h"
#include "wavepreviewpanelcoordinator.h"

#include <QDockWidget>
#include <QHideEvent>
#include <QMainWindow>
#include <QPushButton>
#include <QSizePolicy>
#include <QShowEvent>
#include <QVBoxLayout>

#include <utility>

namespace {
void embedDock(QDockWidget* dock, QVBoxLayout* layout)
{
    if (!dock || !layout)
        return;
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* emptyTitleBar = new QWidget(dock);
    emptyTitleBar->setObjectName(
        QStringLiteral("liveInsightEmbeddedDockTitleBar"));
    emptyTitleBar->setFixedHeight(0);
    dock->setTitleBarWidget(emptyTitleBar);
    dock->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(dock, 1);
    dock->show();
}
}

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
    if (rtlCoordinator) {
        rtlCoordinator->setNavigationHandler(navigationHandler);
    }
    if (waveCoordinator) {
        waveCoordinator->setNavigationHandler(
            [handler = navigationHandler](
                const QString& fileName, int line, int column) {
                if (handler)
                    handler(fileName, line, column);
            });
    }
}

void LiveInsightToolPage::setStatusHandler(StatusHandler handler)
{
    statusHandler = std::move(handler);
    if (detachedPage)
        detachedPage->setStatusHandler(statusHandler);
    if (rtlCoordinator)
        rtlCoordinator->setStatusMessageHandler(statusHandler);
    if (waveCoordinator)
        waveCoordinator->setStatusMessageHandler(statusHandler);
}

void LiveInsightToolPage::setVisibilityHandler(
    VisibilityHandler handler)
{
    visibilityHandler = std::move(handler);
}

void LiveInsightToolPage::setWaveformLibraryPath(
    const QString& path)
{
    waveformLibraryPath = path;
    if (detachedPage)
        detachedPage->setWaveformLibraryPath(path);
    if (waveCoordinator)
        waveCoordinator->setWaveformLibraryPath(path);
}

void LiveInsightToolPage::setContext(
    const LiveInsightToolContext& context)
{
    currentContext = context;
    renderContext();
    if (detachedPage)
        detachedPage->setContext(context);
}

RtlInsightsPanelCoordinator*
LiveInsightToolPage::rtlCoordinatorForTest() const
{
    return rtlCoordinator.get();
}

RtlInsightWorkbench* LiveInsightToolPage::workbenchForTest() const
{
    return workbench;
}

WavePreviewPanelCoordinator*
LiveInsightToolPage::waveCoordinatorForTest() const
{
    return waveCoordinator.get();
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
    page->setWaveformLibraryPath(waveformLibraryPath);
    page->setNavigationHandler(navigationHandler);
    page->setStatusHandler(statusHandler);
    page->setContext(currentContext);
    window->setCentralWidget(page);
    window->resize(1120, 760);
    detachedWindow = window;
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
    if (visibilityHandler)
        visibilityHandler(true);
}

void LiveInsightToolPage::hideEvent(QHideEvent* event)
{
    if (visibilityHandler)
        visibilityHandler(false);
    QWidget::hideEvent(event);
}

void LiveInsightToolPage::createSurface()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    if (insightKind == LiveInsightKind::Wave) {
        waveCoordinator =
            std::make_unique<WavePreviewPanelCoordinator>(this);
        embedDock(waveCoordinator->dock(), layout);
        return;
    }
    workbench = new RtlInsightWorkbench(this);
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
    if (waveCoordinator) {
        waveCoordinator->setWorkspaceRoot(currentContext.workspaceRoot);
        if (!waveformLibraryPath.trimmed().isEmpty()) {
            waveCoordinator->setWaveformLibraryPath(
                waveformLibraryPath);
        }
        if (currentContext.fileName.trimmed().isEmpty()) {
            waveCoordinator->renderUnavailable(
                QStringLiteral("No document selected."));
            return;
        }
        waveCoordinator->refreshFromDocument(
            currentContext.fileName,
            currentContext.documentText,
            currentContext.dirty,
            currentContext.scopeStartPosition,
            currentContext.scopeEndPosition,
            currentContext.scopeLabel,
            currentContext.scopeStartLineZeroBased);
        return;
    }

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
        return;
    }
    if (!rtlCoordinator)
        return;
    rtlCoordinator->updateModuleContext(
        currentContext.fileName,
        currentContext.moduleName,
        currentContext.signalName);
    switch (insightKind) {
    case LiveInsightKind::Module:
        rtlCoordinator->showModuleBlockDiagramForModule(
            currentContext.fileName,
            currentContext.moduleName);
        break;
    case LiveInsightKind::State:
        if (!currentContext.signalName.trimmed().isEmpty()) {
            rtlCoordinator->showStateTransitionGraphForSignal(
                currentContext.fileName,
                currentContext.moduleName,
                currentContext.signalName);
        } else {
            rtlCoordinator->showFsmGraph();
        }
        break;
    case LiveInsightKind::Hotspot:
        rtlCoordinator->showSignalUsageHotspotForSignal(
            currentContext.fileName,
            currentContext.moduleName,
            currentContext.signalName,
            currentContext.signalAccessPath);
        break;
    case LiveInsightKind::Wave:
        break;
    case LiveInsightKind::Kernel:
        break;
    }
}
