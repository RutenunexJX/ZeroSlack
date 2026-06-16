#include "rtlinsightspanelcoordinator.h"

#include "clockresetdomainservice.h"
#include "fsmgraphservice.h"
#include "modulebriefservice.h"
#include "semanticdiffservice.h"
#include "semanticpanelutils.h"
#include "signaljourneyservice.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QVBoxLayout>

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

void appendSymbolGroup(QTreeWidget* tree,
                       const QString& title,
                       const QList<ModuleBriefSymbolRow>& rows)
{
    QTreeWidgetItem* group = createGroupItem(tree, title, rows.size());
    for (const ModuleBriefSymbolRow& row : rows) {
        const sym_list::SymbolInfo& symbol = row.symbol;
        createChildItem(group,
                        row.sectionDisplayName,
                        symbol.symbolName,
                        row.detailDisplayName,
                        symbol.fileName,
                        symbol.startLine,
                        symbol.startColumn);
    }
}

void appendDiagnostics(QTreeWidget* tree,
                       const QList<ModuleBriefDiagnosticRow>& diagnostics)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Diagnostics"),
                                            diagnostics.size());
    for (const ModuleBriefDiagnosticRow& row : diagnostics) {
        const SemanticDiagnostic& diagnostic = row.diagnostic;
        createChildItem(group,
                        row.severityDisplayName,
                        diagnostic.message,
                        row.detailDisplayName,
                        diagnostic.fileName,
                        diagnostic.line,
                        diagnostic.column);
    }
}

void appendContextRows(QTreeWidget* tree,
                       const QList<ModuleBriefContextRow>& rows)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Context"),
                                            rows.size());
    for (const ModuleBriefContextRow& row : rows) {
        createChildItem(group,
                        row.sectionDisplayName,
                        row.symbolDisplayName,
                        row.detailDisplayName,
                        row.symbol.fileName,
                        row.symbol.startLine,
                        row.symbol.startColumn);
    }
}

void appendRelationshipSummary(QTreeWidget* tree,
                               const ModuleBriefRelationshipSummary& summary)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Relationships"),
                                            summary.totalCount);
    for (const ModuleBriefRelationshipRow& row : summary.rows) {
        createChildItem(group,
                        row.directionDisplayName,
                        row.typeDisplayName,
                        row.detailDisplayName,
                        QString(),
                        0,
                        0);
    }
}

void appendClockResetDomains(QTreeWidget* tree,
                             const ClockResetDomainReport& report)
{
    QTreeWidgetItem* clocks = createGroupItem(tree,
                                             report.clockGroupDisplayName.isEmpty()
                                                 ? QStringLiteral("Clock Domains")
                                                 : report.clockGroupDisplayName,
                                             report.clockRelationshipCount);
    for (const ClockResetDomainEntry& domain : report.clockDomains) {
        QTreeWidgetItem* signal = createChildItem(clocks,
                                                  domain.sectionDisplayName.isEmpty()
                                                      ? QStringLiteral("Clock")
                                                      : domain.sectionDisplayName,
                                                  domain.domainSignal.symbolName,
                                                  domain.detailDisplayName.isEmpty()
                                                      ? QStringLiteral("drives %1 modules")
                                                            .arg(domain.modules.size())
                                                      : domain.detailDisplayName,
                                                  domain.domainSignal.fileName,
                                                  domain.domainSignal.startLine,
                                                  domain.domainSignal.startColumn);
        for (const ClockResetDomainMember& member : domain.modules) {
            createChildItem(signal,
                            member.sectionDisplayName.isEmpty()
                                ? QStringLiteral("Module")
                                : member.sectionDisplayName,
                            member.moduleSymbol.symbolName,
                            member.detailDisplayName.isEmpty()
                                ? QStringLiteral("clocked")
                                : member.detailDisplayName,
                            member.moduleSymbol.fileName,
                            member.moduleSymbol.startLine,
                            member.moduleSymbol.startColumn);
        }
    }

    QTreeWidgetItem* resets = createGroupItem(tree,
                                             report.resetGroupDisplayName.isEmpty()
                                                 ? QStringLiteral("Reset Domains")
                                                 : report.resetGroupDisplayName,
                                             report.resetRelationshipCount);
    for (const ClockResetDomainEntry& domain : report.resetDomains) {
        QTreeWidgetItem* signal = createChildItem(resets,
                                                  domain.sectionDisplayName.isEmpty()
                                                      ? QStringLiteral("Reset")
                                                      : domain.sectionDisplayName,
                                                  domain.domainSignal.symbolName,
                                                  domain.detailDisplayName.isEmpty()
                                                      ? QStringLiteral("resets %1 modules")
                                                            .arg(domain.modules.size())
                                                      : domain.detailDisplayName,
                                                  domain.domainSignal.fileName,
                                                  domain.domainSignal.startLine,
                                                  domain.domainSignal.startColumn);
        for (const ClockResetDomainMember& member : domain.modules) {
            createChildItem(signal,
                            member.sectionDisplayName.isEmpty()
                                ? QStringLiteral("Module")
                                : member.sectionDisplayName,
                            member.moduleSymbol.symbolName,
                            member.detailDisplayName.isEmpty()
                                ? QStringLiteral("reset")
                                : member.detailDisplayName,
                            member.moduleSymbol.fileName,
                            member.moduleSymbol.startLine,
                            member.moduleSymbol.startColumn);
        }
    }
}

void appendClockResetEvidenceRows(
    QTreeWidget* tree,
    const QString& groupDisplayName,
    const QList<ClockResetDomainEvidenceRow>& rows)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            groupDisplayName,
                                            rows.size());
    for (const ClockResetDomainEvidenceRow& row : rows) {
        createChildItem(group,
                        row.sectionDisplayName,
                        row.signalDisplayName,
                        row.detailDisplayName,
                        row.domainSignal.fileName,
                        row.domainSignal.startLine,
                        row.domainSignal.startColumn);
    }
}

void appendFsmGraphs(QTreeWidget* tree, const FsmGraphReport& report)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            report.groupDisplayName.isEmpty()
                                                ? QStringLiteral("FSM Graphs")
                                                : report.groupDisplayName,
                                            report.graphs.size());
    for (const FsmGraph& graph : report.graphs) {
        QTreeWidgetItem* stateRegister =
            createChildItem(group,
                            graph.stateRegisterSectionDisplayName.isEmpty()
                                ? QStringLiteral("State Register")
                                : graph.stateRegisterSectionDisplayName,
                            graph.stateRegister.symbolName,
                            graph.stateRegisterDetailDisplayName.isEmpty()
                                ? QStringLiteral("state register")
                                : graph.stateRegisterDetailDisplayName,
                            graph.stateRegister.fileName,
                            graph.stateRegister.startLine,
                            graph.stateRegister.startColumn);

        QTreeWidgetItem* states = new QTreeWidgetItem(stateRegister);
        const QString statesGroup = graph.statesGroupDisplayName.isEmpty()
            ? QStringLiteral("States")
            : graph.statesGroupDisplayName;
        states->setText(0, SemanticPanelUtils::countLabel(statesGroup,
                                                          graph.states.size()));
        if (!graph.stateRows.isEmpty()) {
            for (const FsmStateRow& row : graph.stateRows) {
                createChildItem(states,
                                row.sectionDisplayName.isEmpty()
                                    ? QStringLiteral("State")
                                    : row.sectionDisplayName,
                                row.state.symbolName,
                                row.detailDisplayName,
                                row.state.fileName,
                                row.state.startLine,
                                row.state.startColumn);
            }
        } else {
            for (const sym_list::SymbolInfo& state : graph.states) {
                createChildItem(states,
                                QStringLiteral("State"),
                                state.symbolName,
                                state.dataType,
                                state.fileName,
                                state.startLine,
                                state.startColumn);
            }
        }

        QTreeWidgetItem* transitions = new QTreeWidgetItem(stateRegister);
        const QString transitionsGroup =
            graph.transitionsGroupDisplayName.isEmpty()
                ? QStringLiteral("Transitions")
                : graph.transitionsGroupDisplayName;
        transitions->setText(0, SemanticPanelUtils::countLabel(
                                    transitionsGroup,
                                    graph.transitions.size()));
        if (!graph.transitionRows.isEmpty()) {
            for (const FsmTransitionRow& row : graph.transitionRows) {
                createChildItem(transitions,
                                row.sectionDisplayName.isEmpty()
                                    ? row.fromStateDisplayName
                                    : row.sectionDisplayName,
                                row.toStateDisplayName,
                                row.detailDisplayName,
                                graph.moduleSymbol.fileName,
                                row.transition.line,
                                1);
            }
        } else {
            for (const FsmTransition& transition : graph.transitions) {
                createChildItem(transitions,
                                transition.sectionDisplayName.isEmpty()
                                    ? transition.fromState
                                    : transition.sectionDisplayName,
                                transition.toState,
                                transition.detailDisplayName.isEmpty()
                                    ? transition.assignmentTarget
                                    : transition.detailDisplayName,
                                graph.moduleSymbol.fileName,
                                transition.line,
                                1);
            }
        }
    }
}

void appendSignalJourneyItems(QTreeWidgetItem* parent,
                              const QString& section,
                              const QList<SignalJourneyItem>& items)
{
    QTreeWidgetItem* group = new QTreeWidgetItem(parent);
    group->setText(0, SemanticPanelUtils::countLabel(section, items.size()));
    for (const SignalJourneyItem& item : items) {
        createChildItem(group,
                        section,
                        item.peerSymbolDisplayName,
                        item.detailDisplayName,
                        item.peerSymbol.fileName,
                        item.peerSymbol.startLine,
                        item.peerSymbol.startColumn,
                        item.peerFileDisplayName,
                        item.peerLineDisplayName);
    }
}

void appendSignalJourney(QTreeWidget* tree,
                         const QString& fileName,
                         const QString& moduleName,
                         const QString& signalName)
{
    if (signalName.isEmpty())
        return;

    SignalJourneyQuery query;
    query.fileName = fileName;
    query.moduleName = moduleName;
    query.signalName = signalName;
    const SignalJourneyReport report =
        SignalJourneyService::getInstance()->buildSignalJourney(query);
    if (!report.found)
        return;

    const int totalItems = 1
        + report.assignments.size()
        + report.reads.size()
        + report.portConnections.size()
        + report.interfaceConnections.size();
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Signal Journey: %1")
                                                .arg(report.declaration.symbolName),
                                            totalItems);
    createChildItem(group,
                    QStringLiteral("Declaration"),
                    report.declarationDisplayName,
                    report.declarationTypeDisplayName,
                    report.declaration.fileName,
                    report.declaration.startLine,
                    report.declaration.startColumn,
                    report.declarationFileDisplayName,
                    report.declarationLineDisplayName);
    appendSignalJourneyItems(group, QStringLiteral("Assignments"), report.assignments);
    appendSignalJourneyItems(group, QStringLiteral("Reads"), report.reads);
    appendSignalJourneyItems(group,
                             QStringLiteral("Port Connections"),
                             report.portConnections);
    appendSignalJourneyItems(group,
                             QStringLiteral("Interface Connections"),
                             report.interfaceConnections);
}

void appendSemanticDiff(QTreeWidget* tree, const SemanticDiffReport& report)
{
    QTreeWidgetItem* symbols = createGroupItem(tree,
                                              QStringLiteral("Semantic Diff Symbols"),
                                              report.symbolChanges.size());
    for (const SemanticDiffSymbolChange& change : report.symbolChanges) {
        const sym_list::SymbolInfo& symbol = change.displaySymbol;
        createChildItem(symbols,
                        change.kindDisplayName,
                        symbol.symbolName,
                        change.detailDisplayName,
                        symbol.fileName,
                        symbol.startLine,
                        symbol.startColumn);
    }

    QTreeWidgetItem* relationships =
        createGroupItem(tree,
                        QStringLiteral("Semantic Diff Relationships"),
                        report.relationshipChanges.size());
    for (const SemanticDiffRelationshipChange& change : report.relationshipChanges) {
        const sym_list::SymbolInfo& fromSymbol = change.displayFromSymbol;
        createChildItem(relationships,
                        change.kindDisplayName,
                        change.relationshipTypeDisplayName,
                        change.detailDisplayName,
                        fromSymbol.fileName,
                        fromSymbol.startLine,
                        fromSymbol.startColumn);
    }

    QTreeWidgetItem* diagnostics =
        createGroupItem(tree,
                        QStringLiteral("Semantic Diff Diagnostics"),
                        report.diagnosticChanges.size());
    for (const SemanticDiffDiagnosticChange& change : report.diagnosticChanges) {
        const SemanticDiagnostic& diagnostic = change.displayDiagnostic;
        createChildItem(diagnostics,
                        change.kindDisplayName,
                        diagnostic.message,
                        change.severityDisplayName,
                        diagnostic.fileName,
                        diagnostic.line,
                        diagnostic.column);
    }
}

} // namespace

RtlInsightsPanelCoordinator::RtlInsightsPanelCoordinator(QWidget* parent)
{
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    insightsTree = new QTreeWidget(panel);
    insightsTree->setObjectName(QStringLiteral("rtlInsightsTree"));
    insightsTree->setColumnCount(5);
    insightsTree->setHeaderLabels({"Section", "Symbol", "Detail", "File", "Line"});
    insightsTree->setRootIsDecorated(true);
    insightsTree->setAlternatingRowColors(true);
    insightsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    insightsTree->header()->setStretchLastSection(true);
    insightsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    insightsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    insightsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    insightsTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    layout->addWidget(insightsTree);

    insightsDock = new QDockWidget(QStringLiteral("RTL Insights"), parent);
    insightsDock->setObjectName(QStringLiteral("rtlInsightsDock"));
    insightsDock->setWidget(panel);
    insightsDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetFloatable |
                              QDockWidget::DockWidgetClosable);

    QObject::connect(insightsTree, &QTreeWidget::itemDoubleClicked,
                     insightsDock, [this](QTreeWidgetItem* item, int) {
                         if (!item || !navigationHandler)
                             return;
                         const QString fileName = item->data(0, Qt::UserRole).toString();
                         if (fileName.isEmpty())
                             return;
                         const int line = item->data(0, Qt::UserRole + 1).toInt();
                         const int column = item->data(0, Qt::UserRole + 2).toInt();
                         navigationHandler(fileName, line, column);
                     });
}

void RtlInsightsPanelCoordinator::setNavigationHandler(
    std::function<void(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
}

void RtlInsightsPanelCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void RtlInsightsPanelCoordinator::updateModuleContext(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    currentFileName = fileName;
    currentModuleName = moduleName;
    currentSignalName = signalName;
    refresh();
}

void RtlInsightsPanelCoordinator::showModuleInsights(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    updateModuleContext(fileName, moduleName, signalName);
    if (insightsDock) {
        insightsDock->show();
        insightsDock->raise();
    }
}

void RtlInsightsPanelCoordinator::showSemanticDiff(
    std::shared_ptr<const SemanticIndexSnapshot> beforeSnapshot,
    std::shared_ptr<const SemanticIndexSnapshot> afterSnapshot,
    const QString& moduleName,
    const QString& beforeFileName,
    const QString& afterFileName)
{
    if (!insightsTree)
        return;

    const bool hadExpandableItems =
        SemanticPanelUtils::treeHasExpandableItems(insightsTree);
    const QSet<QString> expandedKeys =
        SemanticPanelUtils::collectExpandedKeys(insightsTree);
    insightsTree->clear();

    SemanticDiffQuery query;
    query.beforeSnapshot = std::move(beforeSnapshot);
    query.afterSnapshot = std::move(afterSnapshot);
    query.moduleName = moduleName;
    query.beforeFileName = beforeFileName;
    query.afterFileName = afterFileName;
    const SemanticDiffReport report =
        SemanticDiffService::getInstance()->buildSemanticDiff(query);

    appendSemanticDiff(insightsTree, report);
    SemanticPanelUtils::restoreTreeExpansion(insightsTree,
                                             hadExpandableItems,
                                             expandedKeys);

    const int totalChanges = report.symbolChanges.size()
        + report.relationshipChanges.size()
        + report.diagnosticChanges.size();
    if (insightsDock) {
        const QString title = moduleName.isEmpty()
            ? QStringLiteral("RTL Insights: Semantic Diff")
            : QStringLiteral("RTL Insights: Semantic Diff %1").arg(moduleName);
        insightsDock->setWindowTitle(title);
        insightsDock->show();
        insightsDock->raise();
    }
    if (statusMessageHandler) {
        statusMessageHandler(QStringLiteral("Rendered semantic diff (%1 changes)")
                                 .arg(totalChanges),
                             1500);
    }
}

void RtlInsightsPanelCoordinator::refresh()
{
    if (!insightsTree)
        return;

    const bool hadExpandableItems =
        SemanticPanelUtils::treeHasExpandableItems(insightsTree);
    const QSet<QString> expandedKeys =
        SemanticPanelUtils::collectExpandedKeys(insightsTree);
    insightsTree->clear();

    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        createGroupItem(insightsTree, QStringLiteral("No module context"), 0);
        if (insightsDock)
            insightsDock->setWindowTitle(QStringLiteral("RTL Insights"));
        return;
    }

    ModuleBriefQuery moduleQuery;
    moduleQuery.fileName = currentFileName;
    moduleQuery.moduleName = currentModuleName;
    const ModuleBriefReport moduleReport =
        ModuleBriefService::getInstance()->buildModuleBrief(moduleQuery);

    if (!moduleReport.found) {
        createGroupItem(insightsTree,
                        QStringLiteral("Module not found: %1").arg(currentModuleName),
                        0);
        if (insightsDock)
            insightsDock->setWindowTitle(QStringLiteral("RTL Insights: %1")
                                             .arg(currentModuleName));
        return;
    }

    appendSymbolGroup(insightsTree,
                      QStringLiteral("Ports"),
                      moduleReport.portRows);
    appendSymbolGroup(insightsTree,
                      QStringLiteral("Parameters"),
                      moduleReport.parameterRows);
    appendSymbolGroup(insightsTree,
                      QStringLiteral("Instances"),
                      moduleReport.instanceRows);
    appendSymbolGroup(insightsTree,
                      QStringLiteral("Imports"),
                      moduleReport.importRows);
    appendContextRows(insightsTree, moduleReport.contextRows);
    appendDiagnostics(insightsTree, moduleReport.diagnosticRows);
    appendRelationshipSummary(insightsTree, moduleReport.relationshipSummary);
    appendSignalJourney(insightsTree,
                        currentFileName,
                        currentModuleName,
                        currentSignalName);

    ClockResetDomainQuery domainQuery;
    domainQuery.fileName = currentFileName;
    domainQuery.moduleName = currentModuleName;
    const ClockResetDomainReport clockResetReport =
        ClockResetDomainService::getInstance()->buildClockResetDomainMap(domainQuery);
    appendClockResetDomains(insightsTree, clockResetReport);
    appendClockResetEvidenceRows(
        insightsTree,
        clockResetReport.evidenceGroupDisplayName.isEmpty()
            ? QStringLiteral("Domain Evidence")
            : clockResetReport.evidenceGroupDisplayName,
        clockResetReport.evidenceRows);
    appendClockResetEvidenceRows(
        insightsTree,
        clockResetReport.ambiguityGroupDisplayName.isEmpty()
            ? QStringLiteral("Ambiguity")
            : clockResetReport.ambiguityGroupDisplayName,
        clockResetReport.ambiguityRows);

    FsmGraphQuery fsmQuery;
    fsmQuery.fileName = currentFileName;
    fsmQuery.moduleName = currentModuleName;
    appendFsmGraphs(insightsTree,
                    FsmGraphService::getInstance()->buildFsmGraph(fsmQuery));

    SemanticPanelUtils::restoreTreeExpansion(insightsTree,
                                             hadExpandableItems,
                                             expandedKeys);

    if (insightsDock) {
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights: %1")
                                         .arg(currentModuleName));
    }
    if (statusMessageHandler) {
        statusMessageHandler(QStringLiteral("Updated RTL insights for %1")
                                 .arg(currentModuleName),
                             1500);
    }
}
