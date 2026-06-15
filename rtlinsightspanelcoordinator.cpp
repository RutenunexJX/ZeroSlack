#include "rtlinsightspanelcoordinator.h"

#include "clockresetdomainservice.h"
#include "fsmgraphservice.h"
#include "modulebriefservice.h"
#include "semanticdiffservice.h"
#include "semanticpanelutils.h"
#include "signaljourneyservice.h"
#include "symboltaxonomy.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QVBoxLayout>

#include <utility>

namespace {

QString symbolTypeText(sym_list::sym_type_e type)
{
    return SymbolTaxonomy::symbolTypeLabel(type);
}

QString diagnosticSeverityText(SemanticDiagnostic::Severity severity)
{
    switch (severity) {
    case SemanticDiagnostic::Error:
        return QStringLiteral("Error");
    case SemanticDiagnostic::Warning:
        return QStringLiteral("Warning");
    case SemanticDiagnostic::Info:
    default:
        return QStringLiteral("Info");
    }
}

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
                                 int column)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, section);
    item->setText(1, name);
    item->setText(2, detail);
    item->setText(3, QFileInfo(fileName).fileName());
    item->setText(4, line > 0 ? QString::number(line) : QString());
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
                       const QString& section,
                       const QList<sym_list::SymbolInfo>& symbols)
{
    QTreeWidgetItem* group = createGroupItem(tree, title, symbols.size());
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const QString detail = symbol.dataType.isEmpty()
            ? symbolTypeText(symbol.symbolType)
            : QStringLiteral("%1 %2")
                  .arg(symbolTypeText(symbol.symbolType), symbol.dataType);
        createChildItem(group,
                        section,
                        symbol.symbolName,
                        detail,
                        symbol.fileName,
                        symbol.startLine,
                        symbol.startColumn);
    }
}

void appendDiagnostics(QTreeWidget* tree,
                       const QList<SemanticDiagnostic>& diagnostics)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Diagnostics"),
                                            diagnostics.size());
    for (const SemanticDiagnostic& diagnostic : diagnostics) {
        createChildItem(group,
                        diagnosticSeverityText(diagnostic.severity),
                        diagnostic.message,
                        QStringLiteral("diagnostic"),
                        diagnostic.fileName,
                        diagnostic.line,
                        diagnostic.column);
    }
}

void appendRelationshipSummary(QTreeWidget* tree,
                               const ModuleBriefRelationshipSummary& summary)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Relationships"),
                                            summary.totalCount);
    auto appendCounts = [group](const QString& direction,
                                const QMap<SymbolRelationshipEngine::RelationType, int>& counts) {
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
            createChildItem(group,
                            direction,
                            SemanticPanelUtils::relationshipTypeText(it.key()),
                            QStringLiteral("%1 relationships").arg(it.value()),
                            QString(),
                            0,
                            0);
        }
    };
    appendCounts(QStringLiteral("Outgoing"), summary.outgoingTypeCounts);
    appendCounts(QStringLiteral("Incoming"), summary.incomingTypeCounts);
}

void appendClockResetDomains(QTreeWidget* tree,
                             const ClockResetDomainReport& report)
{
    QTreeWidgetItem* clocks = createGroupItem(tree,
                                             QStringLiteral("Clock Domains"),
                                             report.clockRelationshipCount);
    for (const ClockResetDomainEntry& domain : report.clockDomains) {
        QTreeWidgetItem* signal = createChildItem(clocks,
                                                  QStringLiteral("Clock"),
                                                  domain.domainSignal.symbolName,
                                                  QStringLiteral("drives %1 modules")
                                                      .arg(domain.modules.size()),
                                                  domain.domainSignal.fileName,
                                                  domain.domainSignal.startLine,
                                                  domain.domainSignal.startColumn);
        for (const ClockResetDomainMember& member : domain.modules) {
            createChildItem(signal,
                            QStringLiteral("Module"),
                            member.moduleSymbol.symbolName,
                            QStringLiteral("clocked"),
                            member.moduleSymbol.fileName,
                            member.moduleSymbol.startLine,
                            member.moduleSymbol.startColumn);
        }
    }

    QTreeWidgetItem* resets = createGroupItem(tree,
                                             QStringLiteral("Reset Domains"),
                                             report.resetRelationshipCount);
    for (const ClockResetDomainEntry& domain : report.resetDomains) {
        QTreeWidgetItem* signal = createChildItem(resets,
                                                  QStringLiteral("Reset"),
                                                  domain.domainSignal.symbolName,
                                                  QStringLiteral("resets %1 modules")
                                                      .arg(domain.modules.size()),
                                                  domain.domainSignal.fileName,
                                                  domain.domainSignal.startLine,
                                                  domain.domainSignal.startColumn);
        for (const ClockResetDomainMember& member : domain.modules) {
            createChildItem(signal,
                            QStringLiteral("Module"),
                            member.moduleSymbol.symbolName,
                            QStringLiteral("reset"),
                            member.moduleSymbol.fileName,
                            member.moduleSymbol.startLine,
                            member.moduleSymbol.startColumn);
        }
    }
}

void appendFsmGraphs(QTreeWidget* tree, const FsmGraphReport& report)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("FSM Graphs"),
                                            report.graphs.size());
    for (const FsmGraph& graph : report.graphs) {
        QTreeWidgetItem* stateRegister =
            createChildItem(group,
                            QStringLiteral("State Register"),
                            graph.stateRegister.symbolName,
                            graph.nextStateSignal.symbolId >= 0
                                ? QStringLiteral("next %1")
                                      .arg(graph.nextStateSignal.symbolName)
                                : QStringLiteral("state register"),
                            graph.stateRegister.fileName,
                            graph.stateRegister.startLine,
                            graph.stateRegister.startColumn);

        QTreeWidgetItem* states = new QTreeWidgetItem(stateRegister);
        states->setText(0, SemanticPanelUtils::countLabel(QStringLiteral("States"),
                                                          graph.states.size()));
        for (const sym_list::SymbolInfo& state : graph.states) {
            createChildItem(states,
                            QStringLiteral("State"),
                            state.symbolName,
                            state.dataType,
                            state.fileName,
                            state.startLine,
                            state.startColumn);
        }

        QTreeWidgetItem* transitions = new QTreeWidgetItem(stateRegister);
        transitions->setText(0, SemanticPanelUtils::countLabel(
                                    QStringLiteral("Transitions"),
                                    graph.transitions.size()));
        for (const FsmTransition& transition : graph.transitions) {
            const QString detail = transition.condition.isEmpty()
                ? transition.assignmentTarget
                : QStringLiteral("%1 when %2")
                      .arg(transition.assignmentTarget, transition.condition);
            createChildItem(transitions,
                            transition.fromState,
                            transition.toState,
                            detail,
                            graph.moduleSymbol.fileName,
                            transition.line,
                            1);
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
                        item.peerSymbol.symbolName,
                        item.detailDisplayName.isEmpty()
                            ? QStringLiteral("%1 %2")
                                  .arg(item.outgoing
                                           ? QStringLiteral("outgoing")
                                           : QStringLiteral("incoming"),
                                       SemanticPanelUtils::relationshipTypeText(
                                           item.relationship.relationship.type))
                            : item.detailDisplayName,
                        item.peerSymbol.fileName,
                        item.peerSymbol.startLine,
                        item.peerSymbol.startColumn);
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
        + report.portConnections.size();
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Signal Journey: %1")
                                                .arg(report.declaration.symbolName),
                                            totalItems);
    createChildItem(group,
                    QStringLiteral("Declaration"),
                    report.declaration.symbolName,
                    report.declarationTypeDisplayName.isEmpty()
                        ? symbolTypeText(report.declaration.symbolType)
                        : report.declarationTypeDisplayName,
                    report.declaration.fileName,
                    report.declaration.startLine,
                    report.declaration.startColumn);
    appendSignalJourneyItems(group, QStringLiteral("Assignments"), report.assignments);
    appendSignalJourneyItems(group, QStringLiteral("Reads"), report.reads);
    appendSignalJourneyItems(group,
                             QStringLiteral("Port Connections"),
                             report.portConnections);
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
                      QStringLiteral("Port"),
                      moduleReport.ports);
    appendSymbolGroup(insightsTree,
                      QStringLiteral("Parameters"),
                      QStringLiteral("Parameter"),
                      moduleReport.parameters);
    appendSymbolGroup(insightsTree,
                      QStringLiteral("Instances"),
                      QStringLiteral("Instance"),
                      moduleReport.instances);
    appendSymbolGroup(insightsTree,
                      QStringLiteral("Imports"),
                      QStringLiteral("Import"),
                      moduleReport.imports);
    appendDiagnostics(insightsTree, moduleReport.diagnostics);
    appendRelationshipSummary(insightsTree, moduleReport.relationshipSummary);
    appendSignalJourney(insightsTree,
                        currentFileName,
                        currentModuleName,
                        currentSignalName);

    ClockResetDomainQuery domainQuery;
    domainQuery.fileName = currentFileName;
    domainQuery.moduleName = currentModuleName;
    appendClockResetDomains(
        insightsTree,
        ClockResetDomainService::getInstance()->buildClockResetDomainMap(domainQuery));

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
