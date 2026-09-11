#include "liveinsighttoolpage.h"

#include "rtlinsightworkbench.h"
#include "wavepreviewpanelcoordinator.h"

#include <QDockWidget>
#include <QHideEvent>
#include <QHBoxLayout>
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
    if (workbench) workbench->setStatusHandler(statusHandler);
    if (detachedPage)
        detachedPage->setStatusHandler(statusHandler);
    if (waveCoordinator)
        waveCoordinator->setStatusMessageHandler(statusHandler);
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

void LiveInsightToolPage::setRefreshHandler(
    RefreshHandler handler)
{
    refreshHandler = std::move(handler);
    if (detachedPage)
        detachedPage->setRefreshHandler(refreshHandler);
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
    page->setRefreshHandler(refreshHandler);
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

    if (insightKind == LiveInsightKind::Wave) {
        auto* toolbar = new QHBoxLayout;
        toolbar->setContentsMargins(8, 6, 8, 0);
        toolbar->setSpacing(6);
        toolbar->addStretch(1);
        auto* refreshButton = new QPushButton(
            QStringLiteral("Refresh"), this);
        refreshButton->setObjectName(
            QStringLiteral("liveInsightWaveRefresh"));
        refreshButton->setToolTip(
            QStringLiteral("Refresh from the current editor context"));
        auto* detachButton = new QPushButton(
            QStringLiteral("Detach"), this);
        detachButton->setObjectName(
            QStringLiteral("liveInsightWaveDetach"));
        detachButton->setToolTip(
            QStringLiteral("Open this Wave view in a separate window"));
        toolbar->addWidget(refreshButton);
        toolbar->addWidget(detachButton);
        layout->addLayout(toolbar);
        waveCoordinator =
            std::make_unique<WavePreviewPanelCoordinator>(this);
        embedDock(waveCoordinator->dock(), layout);
        QObject::connect(
            refreshButton,
            &QPushButton::clicked,
            this,
            [this]() {
                if (refreshHandler)
                    refreshHandler();
                else
                    renderContext();
            });
        QObject::connect(
            detachButton,
            &QPushButton::clicked,
            this,
            [this]() { detachToWindow(); });
        return;
    }
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
    }
}

void LiveInsightToolPage::notifyVisibility()
{
    if (visibilityHandler)
        visibilityHandler(hasVisibleSurface());
}
