#include "rtlinsightspresenter.h"

#include "activitylogservice.h"
#include "fsmgraphservice.h"
#include "moduleblockdiagramservice.h"
#include "rtlinsightsgraphcontroller.h"
#include "rtlinsightspanelviewstate.h"
#include "semanticdiffservice.h"
#include "semanticpanelutils.h"
#include "signalusagehotspotpanel.h"
#include "statetransitiongraphservice.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>

#include <exception>
#include <utility>

namespace {
QTreeWidgetItem* createGroupItem(QTreeWidget* tree,
                                 const QString& title,
                                 int count)
{
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, SemanticPanelUtils::countLabel(title, count));
    return item;
}

QTreeWidgetItem* createChildItem(QTreeWidgetItem* parent,
                                 const QString& section,
                                 const QString& name,
                                 const QString& detail,
                                 const QString& fileName,
                                 int line,
                                 int column,
                                 const QString& fileDisplayName = QString(),
                                 const QString& lineDisplayName = QString())
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, section);
    item->setText(1, name);
    item->setText(2, detail);
    item->setText(3, fileDisplayName.isEmpty()
                         ? QFileInfo(fileName).fileName()
                         : fileDisplayName);
    item->setText(4, lineDisplayName.isEmpty()
                         ? (line > 0 ? QString::number(line) : QString())
                         : lineDisplayName);
    item->setToolTip(1, name);
    item->setToolTip(2, detail);
    item->setToolTip(3, fileName);
    item->setData(0, Qt::UserRole, fileName);
    item->setData(0, Qt::UserRole + 1, line);
    item->setData(0, Qt::UserRole + 2, column);
    return item;
}

void appendSemanticDiff(QTreeWidget* tree, const SemanticDiffReport& report)
{
    QTreeWidgetItem* symbols = createGroupItem(tree,
                                              report.symbolGroupDisplayName.isEmpty()
                                                  ? QStringLiteral("Semantic Diff Symbols")
                                                  : report.symbolGroupDisplayName,
                                              report.symbolChangeCount);
    for (const SemanticDiffSymbolChange& change : report.symbolChanges) {
        QTreeWidgetItem* symbolChange =
            createChildItem(symbols,
                            QStringLiteral("%1 %2")
                                .arg(change.kindDisplayName,
                                     change.categoryGroupDisplayName),
                            change.symbolDisplayName,
                            change.detailDisplayName,
                            change.codeLink.fileName,
                            change.codeLink.line,
                            change.codeLink.column,
                            change.codeLink.fileDisplayName,
                            change.codeLink.lineDisplayName);
        if (!change.beforeSymbolTypeDisplayName.isEmpty()) {
            createChildItem(symbolChange,
                            QStringLiteral("Before"),
                            change.beforeSymbolTypeDisplayName,
                            change.beforeDataTypeDisplayName.isEmpty()
                                ? change.beforeScopeDisplayName
                                : QStringLiteral("%1, %2")
                                      .arg(change.beforeDataTypeDisplayName,
                                           change.beforeScopeDisplayName),
                            change.beforeCodeLink.fileName,
                            change.beforeCodeLink.line,
                            change.beforeCodeLink.column,
                            change.beforeCodeLink.fileDisplayName,
                            change.beforeCodeLink.lineDisplayName);
        }
        if (!change.afterSymbolTypeDisplayName.isEmpty()) {
            createChildItem(symbolChange,
                            QStringLiteral("After"),
                            change.afterSymbolTypeDisplayName,
                            change.afterDataTypeDisplayName.isEmpty()
                                ? change.afterScopeDisplayName
                                : QStringLiteral("%1, %2")
                                      .arg(change.afterDataTypeDisplayName,
                                           change.afterScopeDisplayName),
                            change.afterCodeLink.fileName,
                            change.afterCodeLink.line,
                            change.afterCodeLink.column,
                            change.afterCodeLink.fileDisplayName,
                            change.afterCodeLink.lineDisplayName);
        }
        createChildItem(symbolChange,
                        QStringLiteral("Source Role"),
                        change.sourceRoleDisplayName,
                        change.categoryDisplayName,
                        change.codeLink.fileName,
                        change.codeLink.line,
                        change.codeLink.column,
                        change.codeLink.fileDisplayName,
                        change.codeLink.lineDisplayName);
    }

    QTreeWidgetItem* relationships =
        createGroupItem(tree,
                        report.relationshipGroupDisplayName.isEmpty()
                            ? QStringLiteral("Semantic Diff Relationships")
                            : report.relationshipGroupDisplayName,
                        report.relationshipChangeCount);
    for (const SemanticDiffRelationshipChange& change : report.relationshipChanges) {
        QTreeWidgetItem* relationship =
            createChildItem(relationships,
                            change.kindDisplayName,
                            change.relationshipTypeDisplayName,
                            change.detailDisplayName,
                            change.codeLink.fileName,
                            change.codeLink.line,
                            change.codeLink.column,
                            change.codeLink.fileDisplayName,
                            change.codeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("From"),
                        change.fromSymbolDisplayName,
                        change.relationshipTypeDisplayName,
                        change.fromCodeLink.fileName,
                        change.fromCodeLink.line,
                        change.fromCodeLink.column,
                        change.fromCodeLink.fileDisplayName,
                        change.fromCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("To"),
                        change.toSymbolDisplayName,
                        change.relationshipTypeDisplayName,
                        change.toCodeLink.fileName,
                        change.toCodeLink.line,
                        change.toCodeLink.column,
                        change.toCodeLink.fileDisplayName,
                        change.toCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("Source Role"),
                        change.sourceRoleDisplayName,
                        change.relationshipTypeDisplayName,
                        change.codeLink.fileName,
                        change.codeLink.line,
                        change.codeLink.column,
                        change.codeLink.fileDisplayName,
                        change.codeLink.lineDisplayName);
    }

    QTreeWidgetItem* diagnostics =
        createGroupItem(tree,
                        report.diagnosticGroupDisplayName.isEmpty()
                            ? QStringLiteral("Semantic Diff Diagnostics")
                            : report.diagnosticGroupDisplayName,
                        report.diagnosticChangeCount);
    for (const SemanticDiffDiagnosticChange& change : report.diagnosticChanges) {
        const SemanticDiagnostic& diagnostic = change.displayDiagnostic;
        QTreeWidgetItem* diagnosticItem =
            createChildItem(diagnostics,
                            change.kindDisplayName,
                            diagnostic.message,
                            change.detailDisplayName.isEmpty()
                                ? change.severityDisplayName
                                : change.detailDisplayName,
                            change.codeLink.fileName,
                            change.codeLink.line,
                            change.codeLink.column,
                            change.codeLink.fileDisplayName,
                            change.codeLink.lineDisplayName);
        createChildItem(diagnosticItem,
                        QStringLiteral("Source Role"),
                        change.sourceRoleDisplayName,
                        change.severityDisplayName,
                        change.codeLink.fileName,
                        change.codeLink.line,
                        change.codeLink.column,
                        change.codeLink.fileDisplayName,
                        change.codeLink.lineDisplayName);
    }
}

} // namespace


RtlInsightsPresenter::RtlInsightsPresenter(
    RtlInsightsPanelViewState& viewState,
    RtlInsightsGraphController& graph)
    : state(viewState),
      graphController(graph)
{
}
void RtlInsightsPresenter::refresh()
{
    if (state.insightsStack && state.signalUsageHotspotPanel
        && state.insightsStack->currentWidget() == state.signalUsageHotspotPanel) {
        state.signalUsageHotspotPanel->refreshReport();
        return;
    }
    if (state.currentGraphMode
            == QStringLiteral("state-transition")) {
        showStateTransitionGraphForSignal(
            state.currentFileName,
            state.currentModuleName,
            state.currentSignalName);
        return;
    }
    if (state.currentGraphMode
            == QStringLiteral("fsm")) {
        showFsmGraph();
        return;
    }
    if (state.currentGraphMode
            == QStringLiteral("module-block")) {
        showModuleBlockDiagram();
        return;
    }
    // Ready/empty content follows context changes; semantic diffs retain their snapshots.
    updateActionState();
}

void RtlInsightsPresenter::renderNoContext()
{
    if (!state.insightsTree)
        return;
    graphController.showTreeSurface();

    state.insightsTree->clear();
    createGroupItem(state.insightsTree, QStringLiteral("No module context"), 0);
    if (state.insightsDock)
        state.insightsDock->setWindowTitle(QStringLiteral("RTL Insights"));
    updateActionState();
}

void RtlInsightsPresenter::renderActionList()
{
    if (!state.insightsTree)
        return;
    graphController.showTreeSurface();

    state.insightsTree->clear();
    if (state.currentFileName.isEmpty() || state.currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    createGroupItem(
        state.insightsTree,
        QStringLiteral("Ready: %1").arg(state.currentModuleName),
        3);
    createGroupItem(
        state.insightsTree,
        state.currentSignalName.isEmpty()
            ? QStringLiteral("Select a signal for Usage Hotspot or State Transition Graph, or open Module Block Diagram")
            : QStringLiteral("Current signal: %1").arg(state.currentSignalName),
        0);
    if (state.insightsDock)
        state.insightsDock->setWindowTitle(QStringLiteral("RTL Insights: %1")
                                         .arg(state.currentModuleName));
    updateActionState();
}

void RtlInsightsPresenter::showSignalUsageHotspot()
{
    showSignalUsageHotspotForSignal(state.currentFileName,
                                    state.currentModuleName,
                                    state.currentSignalName);
}

void RtlInsightsPresenter::showFsmGraph()
{
    if (!state.insightsGraphScene)
        return;

    if (state.currentFileName.isEmpty() || state.currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("FSM Graph"));
    FsmGraphReport report;
    try {
        ++graphBuildRequestCount;
        FsmGraphQuery query;
        query.fileName = state.currentFileName;
        query.moduleName = state.currentModuleName;
        report = FsmGraphService::getInstance()->buildFsmGraph(query);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("FSM Graph"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("FSM Graph"),
                       QStringLiteral("unknown error"));
        return;
    }
    graphController.renderFsmGraphScene(
        report,
        QStringLiteral("FSM Graph %1").arg(state.currentModuleName));
    if (state.insightsDock)
        state.insightsDock->setWindowTitle(QStringLiteral("RTL Insights: FSM Graph %1")
                                         .arg(state.currentModuleName));
    if (state.statusMessageHandler)
        state.statusMessageHandler(QStringLiteral("Rendered FSM graph"), 1500);
    logReportDone(QStringLiteral("FSM Graph"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPresenter::showModuleBlockDiagram()
{
    if (!state.insightsGraphScene)
        return;

    if (state.currentFileName.isEmpty() || state.currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("Module Block Diagram"));
    ModuleBlockDiagramReport report;
    try {
        ++graphBuildRequestCount;
        ModuleBlockDiagramQuery query;
        query.fileName = state.currentFileName;
        query.moduleName = state.currentModuleName;
        if (state.moduleBlockDepthSpin)
            query.maxDepth = state.moduleBlockDepthSpin->value();
        report = ModuleBlockDiagramService::getInstance()
            ->buildModuleBlockDiagram(query);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("Module Block Diagram"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("Module Block Diagram"),
                       QStringLiteral("unknown error"));
        return;
    }

    graphController.mapModuleBlockDiagram(report);
    if (state.insightsDock) {
        state.insightsDock->setWindowTitle(
            QStringLiteral("RTL Insights: Module Block Diagram %1")
                .arg(state.currentModuleName));
        state.insightsDock->show();
        state.insightsDock->raise();
    }
    if (state.statusMessageHandler) {
        state.statusMessageHandler(
            report.found
                ? QStringLiteral("Rendered module block diagram for %1")
                      .arg(report.root.moduleDisplayName)
                : report.notFoundReasonDisplayName,
            1500);
    }
    logReportDone(QStringLiteral("Module Block Diagram"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPresenter::updateActionState()
{
    const bool hasModule = !state.currentFileName.isEmpty() && !state.currentModuleName.isEmpty();
    if (state.signalUsageHotspotButton)
        state.signalUsageHotspotButton->setEnabled(hasModule);
    if (state.fsmGraphButton)
        state.fsmGraphButton->setEnabled(hasModule);
    if (state.moduleBlockDiagramButton)
        state.moduleBlockDiagramButton->setEnabled(hasModule);
}

void RtlInsightsPresenter::logReportStart(const QString& reportName) const
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("RTL Insights"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 start for %2")
            .arg(reportName,
                 state.currentModuleName.isEmpty()
                     ? QStringLiteral("<no module>")
                     : state.currentModuleName));
}

void RtlInsightsPresenter::logReportDone(
    const QString& reportName,
    int durationMs) const
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("RTL Insights"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 done for %2")
            .arg(reportName,
                 state.currentModuleName.isEmpty()
                     ? QStringLiteral("<no module>")
                     : state.currentModuleName),
        durationMs);
}

void RtlInsightsPresenter::logReportError(
    const QString& reportName,
    const QString& message) const
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("RTL Insights"),
        ActivityLogLevel::Error,
        QStringLiteral("%1 failed: %2").arg(reportName, message));
}
void RtlInsightsPresenter::updateModuleContext(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    RtlInsightSourceLocation location;
    location.fileName = fileName;
    location.moduleName = moduleName;
    location.symbolName = signalName;
    syncSourceLocation(location);
}

bool RtlInsightsPresenter::syncSourceLocation(
    const RtlInsightSourceLocation& sourceLocation)
{
    RtlInsightSourceLocation location = sourceLocation;
    const bool sameContext =
        QString::compare(
            QDir::cleanPath(QFileInfo(location.fileName)
                                .absoluteFilePath()),
            QDir::cleanPath(QFileInfo(state.currentFileName)
                                .absoluteFilePath()),
            Qt::CaseInsensitive)
            == 0
        && location.moduleName == state.currentModuleName
        && (location.instancePath.isEmpty()
            || state.currentSourceLocation
                   .instancePath.isEmpty()
            || location.instancePath
                   == state.currentSourceLocation
                          .instancePath);
    if (sameContext) {
        if (location.workspacePath.isEmpty()) {
            location.workspacePath =
                state.currentSourceLocation.workspacePath;
        }
        if (location.activeTopModule.isEmpty()) {
            location.activeTopModule =
                state.currentSourceLocation.activeTopModule;
        }
        if (location.instancePath.isEmpty()) {
            location.instancePath =
                state.currentSourceLocation.instancePath;
        }
    }

    if (state.pinned) {
        state.pendingSourceLocation = location;
        state.hasPendingSourceLocation = true;
        const bool selected =
            graphController.selectSourceLocation(
                location);
        if (sameContext
            && location.documentRevision != 0) {
            state.currentSourceLocation.documentRevision =
                location.documentRevision;
        }
        return selected;
    }

    state.hasPendingSourceLocation = false;
    if (!state.currentGraphMode.isEmpty()
        && graphController.selectSourceLocation(
            location)) {
        if (sameContext
            && location.documentRevision != 0) {
            state.currentSourceLocation.documentRevision =
                location.documentRevision;
        }
        return true;
    }
    state.currentSourceLocation = location;
    state.currentFileName = location.fileName;
    state.currentModuleName = location.moduleName;
    state.currentSignalName = location.symbolName;
    if (sameContext
        && !state.currentGraphMode.isEmpty()) {
        updateActionState();
        return false;
    }
    renderActionList();
    return false;
}

void RtlInsightsPresenter::setPinned(bool pinned)
{
    if (state.pinned == pinned)
        return;
    state.pinned = pinned;
    if (state.pinButton
        && state.pinButton->isChecked() != pinned) {
        const QSignalBlocker blocker(state.pinButton);
        state.pinButton->setChecked(pinned);
    }
    if (state.pinButton) {
        state.pinButton->setText(
            pinned ? QStringLiteral("Pinned")
                   : QStringLiteral("Pin"));
    }
    if (pinned || !state.hasPendingSourceLocation)
        return;
    const RtlInsightSourceLocation pending =
        state.pendingSourceLocation;
    state.hasPendingSourceLocation = false;
    syncSourceLocation(pending);
}

bool RtlInsightsPresenter::isPinned() const
{
    return state.pinned;
}

void RtlInsightsPresenter::setContextDirect(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    const bool sameContext =
        QString::compare(
            QDir::cleanPath(QFileInfo(fileName)
                                .absoluteFilePath()),
            QDir::cleanPath(QFileInfo(state.currentFileName)
                                .absoluteFilePath()),
            Qt::CaseInsensitive)
            == 0
        && moduleName == state.currentModuleName;
    if (!sameContext)
        state.currentSourceLocation = {};
    state.currentFileName = fileName;
    state.currentModuleName = moduleName;
    state.currentSignalName = signalName;
    state.currentSourceLocation.fileName = fileName;
    state.currentSourceLocation.moduleName = moduleName;
    state.currentSourceLocation.symbolName = signalName;
    state.hasPendingSourceLocation = false;
}

void RtlInsightsPresenter::showModuleInsights(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    setContextDirect(fileName, moduleName, signalName);
    renderActionList();
    if (state.insightsDock) {
        state.insightsDock->show();
        state.insightsDock->raise();
    }
}

void RtlInsightsPresenter::showStateTransitionGraphForSignal(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    setContextDirect(fileName, moduleName, signalName);
    if (!state.insightsGraphScene)
        return;

    if (state.currentFileName.isEmpty() || state.currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("State Transition Graph"));
    StateTransitionGraphReport report;
    try {
        ++graphBuildRequestCount;
        StateTransitionGraphQuery query;
        query.fileName = state.currentFileName;
        query.moduleName = state.currentModuleName;
        query.symbolName = state.currentSignalName;
        report = StateTransitionGraphService::getInstance()
            ->buildStateTransitionGraph(query);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("State Transition Graph"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("State Transition Graph"),
                       QStringLiteral("unknown error"));
        return;
    }

    graphController.renderStateTransitionGraphScene(report);
    if (state.insightsDock) {
        state.insightsDock->setWindowTitle(
            QStringLiteral("RTL Insights: State Transition Graph %1")
                .arg(signalName.isEmpty() ? moduleName : signalName));
        state.insightsDock->show();
        state.insightsDock->raise();
    }
    if (state.statusMessageHandler) {
        state.statusMessageHandler(
            report.found
                ? QStringLiteral("Rendered state transition graph for %1")
                      .arg(report.selectedSignalDisplayName)
                : report.notFoundReasonDisplayName,
            1500);
    }
    logReportDone(QStringLiteral("State Transition Graph"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPresenter::showSignalUsageHotspotForSignal(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName,
    const QString& signalAccessPath)
{
    setContextDirect(fileName, moduleName, signalName);
    graphController.showHotspotSurface();
    if (!state.signalUsageHotspotPanel)
        return;

    if (state.insightsDock) {
        state.insightsDock->setWindowTitle(
            QStringLiteral("RTL Insights: Signal Usage Hotspot %1")
                .arg(signalAccessPath.isEmpty() ? signalName
                                                : signalAccessPath));
        state.insightsDock->show();
        state.insightsDock->raise();
    }
    state.signalUsageHotspotPanel->showHotspotForSymbol(signalName,
                                                  fileName,
                                                  moduleName,
                                                  signalAccessPath);
}

void RtlInsightsPresenter::showModuleBlockDiagramForModule(
    const QString& fileName,
    const QString& moduleName)
{
    setContextDirect(fileName, moduleName, {});
    showModuleBlockDiagram();
}

void RtlInsightsPresenter::showSemanticDiff(
    std::shared_ptr<const SemanticIndexSnapshot> beforeSnapshot,
    std::shared_ptr<const SemanticIndexSnapshot> afterSnapshot,
    const QString& moduleName,
    const QString& beforeFileName,
    const QString& afterFileName)
{
    if (!state.insightsTree)
        return;
    graphController.showTreeSurface();

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("Semantic Diff"));

    const bool hadExpandableItems =
        SemanticPanelUtils::treeHasExpandableItems(state.insightsTree);
    const QSet<QString> expandedKeys =
        SemanticPanelUtils::collectExpandedKeys(state.insightsTree);
    state.insightsTree->clear();

    SemanticDiffQuery query;
    query.beforeSnapshot = std::move(beforeSnapshot);
    query.afterSnapshot = std::move(afterSnapshot);
    query.moduleName = moduleName;
    query.beforeFileName = beforeFileName;
    query.afterFileName = afterFileName;
    SemanticDiffReport report;
    try {
        report = SemanticDiffService::getInstance()->buildSemanticDiff(query);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("Semantic Diff"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("Semantic Diff"),
                       QStringLiteral("unknown error"));
        return;
    }

    appendSemanticDiff(state.insightsTree, report);
    SemanticPanelUtils::restoreTreeExpansion(state.insightsTree,
                                             hadExpandableItems,
                                             expandedKeys);

    const int totalChanges = report.symbolChanges.size()
        + report.relationshipChanges.size()
        + report.diagnosticChanges.size();
    if (state.insightsDock) {
        const QString title = moduleName.isEmpty()
            ? QStringLiteral("RTL Insights: Semantic Diff")
            : QStringLiteral("RTL Insights: Semantic Diff %1").arg(moduleName);
        state.insightsDock->setWindowTitle(title);
        state.insightsDock->show();
        state.insightsDock->raise();
    }
    if (state.statusMessageHandler) {
        state.statusMessageHandler(QStringLiteral("Rendered semantic diff (%1 changes)")
                                 .arg(totalChanges),
                             1500);
    }
    logReportDone(QStringLiteral("Semantic Diff"),
                  static_cast<int>(timer.elapsed()));
}

quint64 RtlInsightsPresenter::graphBuildRequestCountForTest() const
{
    return graphBuildRequestCount;
}
