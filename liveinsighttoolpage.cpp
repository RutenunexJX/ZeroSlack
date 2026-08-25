#include "liveinsighttoolpage.h"

#include "rtlinsightspanelcoordinator.h"
#include "wavepreviewpanelcoordinator.h"

#include <QDockWidget>
#include <QHideEvent>
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
    if (waveCoordinator)
        waveCoordinator->setWaveformLibraryPath(path);
}

void LiveInsightToolPage::setContext(
    const LiveInsightToolContext& context)
{
    currentContext = context;
    renderContext();
}

RtlInsightsPanelCoordinator*
LiveInsightToolPage::rtlCoordinatorForTest() const
{
    return rtlCoordinator.get();
}

WavePreviewPanelCoordinator*
LiveInsightToolPage::waveCoordinatorForTest() const
{
    return waveCoordinator.get();
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

    rtlCoordinator =
        std::make_unique<RtlInsightsPanelCoordinator>(this);
    embedDock(rtlCoordinator->dock(), layout);
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
            currentContext.signalName);
        break;
    case LiveInsightKind::Wave:
        break;
    }
}
