#include "problemspanelcoordinator.h"

#include "diagnosticservice.h"
#include "semanticpanelutils.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>

#include <utility>

namespace {

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

QTreeWidgetItem* createDiagnosticItem(QTreeWidgetItem* parent,
                                      const SemanticDiagnostic& diagnostic)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, diagnosticSeverityText(diagnostic.severity));
    item->setText(1, QFileInfo(diagnostic.fileName).fileName());
    item->setText(2, QString::number(diagnostic.line));
    item->setText(3, QString::number(diagnostic.column));
    item->setText(4, diagnostic.message);
    item->setToolTip(1, diagnostic.fileName);
    item->setToolTip(4, diagnostic.message);
    item->setData(0, Qt::UserRole, diagnostic.fileName);
    item->setData(0, Qt::UserRole + 1, diagnostic.line);
    item->setData(0, Qt::UserRole + 2, diagnostic.column);
    return item;
}

DiagnosticPanelScope diagnosticPanelScopeFromValue(int value)
{
    switch (value) {
    case 1:
        return DiagnosticPanelScope::WorkspaceFiles;
    case 2:
        return DiagnosticPanelScope::AllFiles;
    case 0:
    default:
        return DiagnosticPanelScope::CurrentFile;
    }
}

DiagnosticSeverityFilter diagnosticSeverityFilterFromValue(int value)
{
    switch (value) {
    case 1:
        return DiagnosticSeverityFilter::Errors;
    case 2:
        return DiagnosticSeverityFilter::Warnings;
    case 3:
        return DiagnosticSeverityFilter::Info;
    case 0:
    default:
        return DiagnosticSeverityFilter::All;
    }
}

} // namespace

ProblemsPanelCoordinator::ProblemsPanelCoordinator(QWidget* parent)
{
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* filtersLayout = new QHBoxLayout();
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(6);

    problemsScopeCombo = new QComboBox(panel);
    problemsScopeCombo->setObjectName(QStringLiteral("problemsScopeCombo"));
    problemsScopeCombo->addItem(QStringLiteral("Current File"), 0);
    problemsScopeCombo->addItem(QStringLiteral("Workspace Files"), 1);
    problemsScopeCombo->addItem(QStringLiteral("All Files"), 2);
    problemsScopeCombo->setToolTip(QStringLiteral("Problem scope"));
    filtersLayout->addWidget(problemsScopeCombo);

    problemsSeverityCombo = new QComboBox(panel);
    problemsSeverityCombo->setObjectName(QStringLiteral("problemsSeverityCombo"));
    problemsSeverityCombo->addItem(QStringLiteral("All Severities"), 0);
    problemsSeverityCombo->addItem(QStringLiteral("Errors"), 1);
    problemsSeverityCombo->addItem(QStringLiteral("Warnings"), 2);
    problemsSeverityCombo->addItem(QStringLiteral("Info"), 3);
    problemsSeverityCombo->setToolTip(QStringLiteral("Severity filter"));
    filtersLayout->addWidget(problemsSeverityCombo);
    filtersLayout->addStretch(1);
    layout->addLayout(filtersLayout);

    problemsTree = new QTreeWidget(panel);
    problemsTree->setObjectName(QStringLiteral("problemsTree"));
    problemsTree->setColumnCount(5);
    problemsTree->setHeaderLabels({"Severity", "File", "Line", "Column", "Message"});
    problemsTree->setRootIsDecorated(true);
    problemsTree->setAlternatingRowColors(true);
    problemsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    problemsTree->header()->setStretchLastSection(true);
    problemsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(problemsTree);

    problemsDock = new QDockWidget("Problems", parent);
    problemsDock->setObjectName(QStringLiteral("problemsDock"));
    problemsDock->setWidget(panel);
    problemsDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetFloatable |
                              QDockWidget::DockWidgetClosable);

    QObject::connect(problemsScopeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                     problemsDock, [this](int) { update(); });
    QObject::connect(problemsSeverityCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                     problemsDock, [this](int) { update(); });

    QObject::connect(problemsTree, &QTreeWidget::itemDoubleClicked,
                     problemsDock, [this](QTreeWidgetItem* item, int) {
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

void ProblemsPanelCoordinator::setCurrentFileProvider(std::function<QString()> provider)
{
    currentFileProvider = std::move(provider);
}

void ProblemsPanelCoordinator::setWorkspaceFilesProvider(std::function<QStringList()> provider)
{
    workspaceFilesProvider = std::move(provider);
}

void ProblemsPanelCoordinator::setNavigationHandler(
    std::function<void(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
}

void ProblemsPanelCoordinator::update(const QString& fileName)
{
    if (!problemsTree)
        return;

    DiagnosticPanelQueryOptions queryOptions;
    queryOptions.scope = diagnosticPanelScopeFromValue(
        problemsScopeCombo ? problemsScopeCombo->currentData().toInt() : 0);
    queryOptions.severity = diagnosticSeverityFilterFromValue(
        problemsSeverityCombo ? problemsSeverityCombo->currentData().toInt() : 0);
    queryOptions.requestedFileName = fileName;
    if (currentFileProvider)
        queryOptions.currentFileName = currentFileProvider();
    if (workspaceFilesProvider)
        queryOptions.workspaceFiles = workspaceFilesProvider();

    DiagnosticService* diagnosticService = DiagnosticService::getInstance();
    const DiagnosticQuery query = diagnosticService->queryForPanel(queryOptions);
    const DiagnosticReport report = diagnosticService->findDiagnosticReport(query);
    const QList<DiagnosticResult>& diagnostics = report.diagnostics;

    const bool currentFileOnly =
        queryOptions.scope == DiagnosticPanelScope::CurrentFile;
    const bool hadExpandableItems = SemanticPanelUtils::treeHasExpandableItems(problemsTree);
    const QSet<QString> expandedKeys = SemanticPanelUtils::collectExpandedKeys(problemsTree);
    problemsTree->clear();
    if (currentFileOnly) {
        for (const DiagnosticResult& result : diagnostics) {
            const SemanticDiagnostic& diagnostic = result.diagnostic;
            createDiagnosticItem(problemsTree->invisibleRootItem(), diagnostic);
        }
    } else {
        for (const DiagnosticFileGroup& group : report.fileGroups) {
            auto* fileGroup = new QTreeWidgetItem(problemsTree);
            fileGroup->setText(0, SemanticPanelUtils::countLabel(group.displayName,
                                                                 group.count));
            fileGroup->setText(1, group.fileName);
            fileGroup->setToolTip(0, group.fileName);
            fileGroup->setToolTip(1, group.fileName);
            for (const DiagnosticResult& result : group.diagnostics)
                createDiagnosticItem(fileGroup, result.diagnostic);
        }
        SemanticPanelUtils::restoreTreeExpansion(problemsTree,
                                                 hadExpandableItems,
                                                 expandedKeys);
    }
    if (diagnostics.isEmpty()) {
        auto* emptyItem = new QTreeWidgetItem(problemsTree);
        emptyItem->setText(4, QStringLiteral("No problems"));
    }

    if (problemsDock) {
        problemsDock->setWindowTitle(QStringLiteral("Problems (%1)").arg(report.totalCount));
        if (!diagnostics.isEmpty() || problemsDock->isVisible())
            problemsDock->show();
    }
}
