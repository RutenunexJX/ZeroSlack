#include "problemspanelcoordinator.h"

#include "activitylogservice.h"
#include "diagnosticservice.h"
#include "semanticpanelutils.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <utility>

namespace {

QTreeWidgetItem* createDiagnosticItem(QTreeWidgetItem* parent,
                                      const DiagnosticResult& result)
{
    const SemanticDiagnostic& diagnostic = result.diagnostic;
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, result.severityDisplayName);
    item->setText(1, result.fileDisplayName);
    item->setText(2, result.lineDisplayName);
    item->setText(3, result.columnDisplayName);
    item->setText(4, result.messageDisplayName);
    item->setText(5, result.ownerDisplayName);
    item->setText(6, result.analysisBandDisplayName);
    item->setToolTip(1, diagnostic.fileName);
    item->setToolTip(4, result.messageDisplayName);
    item->setToolTip(5, result.ownerDisplayName);
    item->setToolTip(6, result.analysisBandDisplayName);
    item->setData(0, Qt::UserRole, diagnostic.fileName);
    item->setData(0, Qt::UserRole + 1, diagnostic.line);
    item->setData(0, Qt::UserRole + 2, diagnostic.column);
    return item;
}

QString diagnosticSummaryText(const DiagnosticReport& report)
{
    const int errors = report.severityCounts.value(SemanticDiagnostic::Error);
    const int warnings = report.severityCounts.value(SemanticDiagnostic::Warning);
    const int infos = report.severityCounts.value(SemanticDiagnostic::Info);
    return QStringLiteral("Current file: %1 errors, %2 warnings, %3 info")
        .arg(errors)
        .arg(warnings)
        .arg(infos);
}

QString inferredDiagnosticState(const DiagnosticReport& report)
{
    if (report.analysisBandGroups.isEmpty())
        return QStringLiteral("current");

    bool hasCurrent = false;
    bool hasBackground = false;
    bool hasStale = false;
    for (const DiagnosticAnalysisBandGroup& group : report.analysisBandGroups) {
        const QString label = group.label;
        if (label == QStringLiteral("background"))
            hasBackground = true;
        else if (label == QStringLiteral("unbanded"))
            hasStale = true;
        else
            hasCurrent = true;
    }
    QStringList parts;
    if (hasCurrent)
        parts.append(QStringLiteral("current"));
    if (hasStale)
        parts.append(QStringLiteral("stale"));
    if (hasBackground)
        parts.append(QStringLiteral("background"));
    return parts.isEmpty() ? QStringLiteral("current")
                           : parts.join(QStringLiteral(" + "));
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

QString diagnosticVisibilityCountText(int count)
{
    return QStringLiteral("%1 %2")
        .arg(count)
        .arg(count == 1
                 ? QStringLiteral("diagnostic")
                 : QStringLiteral("diagnostics"));
}

QString diagnosticBandComboText(const QString& baseText, int count)
{
    return QStringLiteral("%1 (%2)").arg(baseText).arg(count);
}

QString missingDiagnosticBandTooltipText(const QString& baseText)
{
    return QStringLiteral("%1 0 diagnostics").arg(baseText);
}

QString diagnosticBandComboToolTipText(const QString& baseText,
                                       const QString& bandLabel,
                                       const DiagnosticReport& report)
{
    if (bandLabel.isEmpty())
        return report.analysisBandSummaryText();

    for (const DiagnosticAnalysisBandGroup& group :
         report.analysisBandGroups) {
        if (group.label == bandLabel)
            return group.summaryText();
    }

    return missingDiagnosticBandTooltipText(baseText);
}

void updateDiagnosticBandComboPresentation(QComboBox* combo,
                                           const DiagnosticReport& report)
{
    if (!combo)
        return;

    const QSignalBlocker blocker(combo);
    for (int i = 0; i < combo->count(); ++i) {
        const QString bandLabel = combo->itemData(i).toString();
        const QString baseText =
            combo->itemData(i, Qt::UserRole + 1).toString();
        if (baseText.isEmpty())
            continue;
        const int count = bandLabel.isEmpty()
            ? report.totalCount
            : report.analysisBandCounts.value(bandLabel);
        combo->setItemText(i, diagnosticBandComboText(baseText, count));
        const QString toolTip =
            diagnosticBandComboToolTipText(baseText, bandLabel, report);
        combo->setItemData(i, toolTip, Qt::ToolTipRole);
        combo->setItemData(i, toolTip, Qt::StatusTipRole);
        combo->setItemData(i, toolTip, Qt::AccessibleDescriptionRole);
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

    problemsBandCombo = new QComboBox(panel);
    problemsBandCombo->setObjectName(QStringLiteral("problemsBandCombo"));
    problemsBandCombo->addItem(QStringLiteral("All Bands"), QString());
    problemsBandCombo->addItem(QStringLiteral("Current"), QStringLiteral("current"));
    problemsBandCombo->addItem(QStringLiteral("Dirty Open"), QStringLiteral("dirty-open"));
    problemsBandCombo->addItem(QStringLiteral("Open"), QStringLiteral("open"));
    problemsBandCombo->addItem(QStringLiteral("Background"), QStringLiteral("background"));
    problemsBandCombo->addItem(QStringLiteral("Unbanded"), QStringLiteral("unbanded"));
    for (int i = 0; i < problemsBandCombo->count(); ++i) {
        problemsBandCombo->setItemData(i,
                                       problemsBandCombo->itemText(i),
                                       Qt::UserRole + 1);
    }
    problemsBandCombo->setToolTip(QStringLiteral("Diagnostic band"));
    filtersLayout->addWidget(problemsBandCombo);
    filtersLayout->addStretch(1);
    layout->addLayout(filtersLayout);

    auto* statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(8);
    diagnosticSummaryLabel = new QLabel(panel);
    diagnosticSummaryLabel->setObjectName(QStringLiteral("diagnosticSummaryLabel"));
    diagnosticSummaryLabel->setText(QStringLiteral("Current file: 0 errors, 0 warnings, 0 info"));
    diagnosticStateLabel = new QLabel(panel);
    diagnosticStateLabel->setObjectName(QStringLiteral("diagnosticStateLabel"));
    diagnosticStateLabel->setText(QStringLiteral("Diagnostics: current"));
    statusLayout->addWidget(diagnosticSummaryLabel);
    statusLayout->addStretch(1);
    statusLayout->addWidget(diagnosticStateLabel);
    layout->addLayout(statusLayout);

    problemsTree = new QTreeWidget(panel);
    problemsTree->setObjectName(QStringLiteral("problemsTree"));
    problemsTree->setColumnCount(7);
    problemsTree->setHeaderLabels({"Severity", "File", "Line", "Column", "Message", "Owner", "Band"});
    problemsTree->setRootIsDecorated(true);
    problemsTree->setAlternatingRowColors(true);
    problemsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    problemsTree->header()->setStretchLastSection(true);
    problemsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
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
    QObject::connect(problemsBandCombo, qOverload<int>(&QComboBox::currentIndexChanged),
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
                         if (!QFileInfo::exists(fileName)) {
                             if (statusMessageHandler) {
                                 statusMessageHandler(
                                     QStringLiteral("Diagnostic file does not exist: %1")
                                         .arg(fileName),
                                     4000);
                             }
                             return;
                         }
                         if (line <= 0) {
                             if (statusMessageHandler)
                                 statusMessageHandler(
                                     QStringLiteral("Diagnostic location is invalid"),
                                     3000);
                             return;
                         }
                         if (!navigationHandler(fileName, line, column)
                             && statusMessageHandler) {
                             statusMessageHandler(
                                 QStringLiteral("Failed to open diagnostic location"),
                                 4000);
                         }
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
    std::function<bool(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
}

void ProblemsPanelCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void ProblemsPanelCoordinator::setAnalysisState(const QString& state)
{
    externalAnalysisState = state.trimmed();
    if (diagnosticStateLabel && !externalAnalysisState.isEmpty()) {
        diagnosticStateLabel->setText(
            QStringLiteral("Diagnostics: %1").arg(externalAnalysisState));
    }
}

void ProblemsPanelCoordinator::update()
{
    if (!problemsTree)
        return;

    DiagnosticPanelQueryOptions queryOptions;
    queryOptions.scope = diagnosticPanelScopeFromValue(
        problemsScopeCombo ? problemsScopeCombo->currentData().toInt() : 0);
    queryOptions.severity = diagnosticSeverityFilterFromValue(
        problemsSeverityCombo ? problemsSeverityCombo->currentData().toInt() : 0);
    if (problemsBandCombo)
        queryOptions.analysisBandLabel = problemsBandCombo->currentData().toString();
    if (currentFileProvider)
        queryOptions.currentFileName = currentFileProvider();
    if (workspaceFilesProvider)
        queryOptions.workspaceFiles = workspaceFilesProvider();

    DiagnosticService* diagnosticService = DiagnosticService::getInstance();
    const DiagnosticQuery query = diagnosticService->queryForPanel(queryOptions);
    DiagnosticPanelQueryOptions countQueryOptions = queryOptions;
    countQueryOptions.analysisBandLabel.clear();
    const DiagnosticQuery countQuery =
        diagnosticService->queryForPanel(countQueryOptions);
    const DiagnosticReport countReport =
        diagnosticService->findDiagnosticReport(countQuery);
    updateDiagnosticBandComboPresentation(problemsBandCombo, countReport);
    const DiagnosticReport report = diagnosticService->findDiagnosticReport(query);
    DiagnosticPanelQueryOptions currentFileSummaryOptions;
    currentFileSummaryOptions.scope = DiagnosticPanelScope::CurrentFile;
    currentFileSummaryOptions.severity = DiagnosticSeverityFilter::All;
    currentFileSummaryOptions.currentFileName = queryOptions.currentFileName;
    const DiagnosticReport currentFileReport =
        diagnosticService->findDiagnosticReport(
            diagnosticService->queryForPanel(currentFileSummaryOptions));
    if (diagnosticSummaryLabel)
        diagnosticSummaryLabel->setText(diagnosticSummaryText(currentFileReport));
    if (diagnosticStateLabel) {
        const QString state = externalAnalysisState.isEmpty()
            ? inferredDiagnosticState(report)
            : externalAnalysisState;
        diagnosticStateLabel->setText(
            QStringLiteral("Diagnostics: %1").arg(state));
    }
    const QList<DiagnosticResult>& diagnostics = report.diagnostics;
    if (report.totalCount > 0) {
        const QString activityMessage =
            QStringLiteral("Diagnostics visible: %1; %2")
                .arg(diagnosticVisibilityCountText(report.totalCount),
                     report.analysisBandSummaryText());
        if (activityMessage != lastDiagnosticActivityMessage) {
            ActivityLogService::getInstance()->append(
                QStringLiteral("Analyzer"),
                ActivityLogLevel::Info,
                activityMessage);
            lastDiagnosticActivityMessage = activityMessage;
        }
    } else {
        lastDiagnosticActivityMessage.clear();
    }

    const bool currentFileOnly =
        queryOptions.scope == DiagnosticPanelScope::CurrentFile;
    const bool hadExpandableItems = SemanticPanelUtils::treeHasExpandableItems(problemsTree);
    const QSet<QString> expandedKeys = SemanticPanelUtils::collectExpandedKeys(problemsTree);
    problemsTree->clear();
    if (currentFileOnly) {
        for (const DiagnosticResult& result : diagnostics) {
            createDiagnosticItem(problemsTree->invisibleRootItem(), result);
        }
    } else {
        for (const DiagnosticFileGroup& group : report.fileGroups) {
            auto* fileGroup = new QTreeWidgetItem(problemsTree);
            fileGroup->setText(0, SemanticPanelUtils::countLabel(group.displayName,
                                                                 group.count));
            fileGroup->setText(1, group.fileName);
            if (!group.diagnostics.isEmpty())
                fileGroup->setText(6, group.diagnostics.first().analysisBandDisplayName);
            fileGroup->setToolTip(0, group.fileName);
            fileGroup->setToolTip(1, group.fileName);
            fileGroup->setToolTip(6, fileGroup->text(6));
            for (const DiagnosticResult& result : group.diagnostics)
                createDiagnosticItem(fileGroup, result);
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

bool ProblemsPanelCoordinator::showsCurrentFileScope() const
{
    return !problemsScopeCombo || problemsScopeCombo->currentData().toInt() == 0;
}
