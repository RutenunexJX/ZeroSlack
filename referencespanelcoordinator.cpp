#include "referencespanelcoordinator.h"

#include "referenceservice.h"
#include "semanticindex.h"
#include "semanticpanelutils.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QPoint>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr int kNavigationFileRole = Qt::UserRole;
constexpr int kNavigationLineRole = Qt::UserRole + 1;
constexpr int kNavigationColumnRole = Qt::UserRole + 2;

QTreeWidgetItem* createReferenceItem(QTreeWidgetItem* parent,
                                     const ReferenceResult& reference)
{
    const SemanticSymbolLocation sourceLocation =
        reference.referencingSymbolRecord.location;
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, reference.symbolDisplayName);
    item->setText(1, reference.fileDisplayName);
    item->setText(2, reference.lineDisplayName);
    item->setText(3, reference.relationshipTypeDisplayName);
    item->setToolTip(1, sourceLocation.fileName);
    item->setData(0, kNavigationFileRole, sourceLocation.fileName);
    item->setData(0, kNavigationLineRole, sourceLocation.startLine);
    item->setData(0, kNavigationColumnRole, sourceLocation.startColumn);
    return item;
}

QString referenceSourceDisplayName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QStringLiteral("<none>");
    const QString displayName = QFileInfo(fileName).fileName();
    return displayName.isEmpty() ? fileName : displayName;
}

QString referenceQueryContextText(const QString& symbolName,
                                  const QString& fileName,
                                  QComboBox* scopeCombo,
                                  QComboBox* typeCombo)
{
    const QString symbol = symbolName.isEmpty()
        ? QStringLiteral("<none>")
        : symbolName;
    return QStringLiteral("Symbol: %1 | Source: %2 | Scope: %3 | Type: %4")
        .arg(symbol,
             referenceSourceDisplayName(fileName),
             scopeCombo ? scopeCombo->currentText()
                        : QStringLiteral("All Files"),
             typeCombo ? typeCombo->currentText()
                       : QStringLiteral("All Types"));
}

QString referenceEmptyReason(const ReferenceReport& report,
                             const QString& symbolName)
{
    if (symbolName.isEmpty())
        return QStringLiteral("no symbol under cursor");

    switch (report.notFoundReason) {
    case ReferenceReportNotFoundReason::None:
        break;
    case ReferenceReportNotFoundReason::NoSubjectSymbol:
        if (SemanticIndex::getInstance()->getSymbolRecords().isEmpty())
            return QStringLiteral("workspace analysis stale / not ready");
        return QStringLiteral("symbol not indexed");
    case ReferenceReportNotFoundReason::NoReferences:
        return QStringLiteral("no references found");
    }
    return report.notFoundReasonDisplayName.isEmpty()
        ? QStringLiteral("no references found")
        : report.notFoundReasonDisplayName;
}

bool itemHasNavigationTarget(QTreeWidgetItem* item)
{
    return item
        && !item->data(0, kNavigationFileRole).toString().isEmpty();
}

QString itemPath(QTreeWidgetItem* item)
{
    return item ? item->data(0, kNavigationFileRole).toString() : QString();
}

QString itemFileLine(QTreeWidgetItem* item)
{
    const QString fileName = itemPath(item);
    if (fileName.isEmpty())
        return QString();
    const int line = item ? item->data(0, kNavigationLineRole).toInt() : 0;
    if (line <= 0)
        return fileName;
    return QStringLiteral("%1:%2").arg(fileName).arg(line);
}

ReferencePanelScope referencePanelScopeFromValue(int value)
{
    switch (value) {
    case 1:
        return ReferencePanelScope::WorkspaceFiles;
    case 2:
        return ReferencePanelScope::CurrentFile;
    case 0:
    default:
        return ReferencePanelScope::AllFiles;
    }
}

} // namespace

ReferencesPanelCoordinator::ReferencesPanelCoordinator(QWidget* parent)
{
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* filtersLayout = new QHBoxLayout();
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(6);

    referenceScopeCombo = new QComboBox(panel);
    referenceScopeCombo->setObjectName(QStringLiteral("referenceScopeCombo"));
    referenceScopeCombo->addItem(QStringLiteral("All Files"), 0);
    referenceScopeCombo->addItem(QStringLiteral("Workspace Files"), 1);
    referenceScopeCombo->addItem(QStringLiteral("Current File"), 2);
    referenceScopeCombo->setToolTip(QStringLiteral("Reference scope"));
    filtersLayout->addWidget(referenceScopeCombo);

    referenceTypeCombo = new QComboBox(panel);
    referenceTypeCombo->setObjectName(QStringLiteral("referenceTypeCombo"));
    for (const RelationshipTypeFilterOption& option :
         RelationshipService::referencePanelTypeFilterOptions()) {
        referenceTypeCombo->addItem(option.displayName, option.value);
    }
    referenceTypeCombo->setToolTip(QStringLiteral("Reference type"));
    filtersLayout->addWidget(referenceTypeCombo);
    filtersLayout->addStretch(1);
    layout->addLayout(filtersLayout);

    referenceContextLabel = new QLabel(panel);
    referenceContextLabel->setObjectName(QStringLiteral("referenceContextLabel"));
    referenceContextLabel->setText(referenceQueryContextText(
        currentReferenceSymbolName,
        currentReferenceFileName,
        referenceScopeCombo,
        referenceTypeCombo));
    layout->addWidget(referenceContextLabel);

    referencesTree = new QTreeWidget(panel);
    referencesTree->setObjectName(QStringLiteral("referencesTree"));
    referencesTree->setColumnCount(4);
    referencesTree->setHeaderLabels({"Symbol", "File", "Line", "Relationship"});
    referencesTree->setRootIsDecorated(true);
    referencesTree->setAlternatingRowColors(true);
    referencesTree->setSelectionMode(QAbstractItemView::SingleSelection);
    referencesTree->setContextMenuPolicy(Qt::CustomContextMenu);
    referencesTree->header()->setStretchLastSection(true);
    referencesTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    referencesTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    referencesTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(referencesTree);

    referencesDock = new QDockWidget("References", parent);
    referencesDock->setObjectName(QStringLiteral("referencesDock"));
    referencesDock->setWidget(panel);
    referencesDock->setFeatures(QDockWidget::DockWidgetMovable |
                                QDockWidget::DockWidgetFloatable |
                                QDockWidget::DockWidgetClosable);
    referencesDock->hide();

    QObject::connect(referenceScopeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                     referencesDock, [this](int) { refresh(); });
    QObject::connect(referenceTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                     referencesDock, [this](int) { refresh(); });

    auto navigateReferenceItem = [this](QTreeWidgetItem* item) {
        if (!item)
            return;
        if (!itemHasNavigationTarget(item)) {
            if (statusMessageHandler) {
                statusMessageHandler(QStringLiteral("Reference row has no source location"),
                                     3000);
            }
            return;
        }
        if (!navigationHandler) {
            if (statusMessageHandler)
                statusMessageHandler(QStringLiteral("Navigation unavailable"), 3000);
            return;
        }

        const QString fileName = item->data(0, kNavigationFileRole).toString();
        const int line = item->data(0, kNavigationLineRole).toInt();
        const int column = item->data(0, kNavigationColumnRole).toInt();
        if (!navigationHandler(fileName, line, column)
            && statusMessageHandler) {
            statusMessageHandler(QStringLiteral("Reference jump failed"), 4000);
        }
    };
    QObject::connect(referencesTree, &QTreeWidget::itemClicked,
                     referencesDock, [navigateReferenceItem](QTreeWidgetItem* item, int) {
                         navigateReferenceItem(item);
                     });
    QObject::connect(referencesTree, &QTreeWidget::itemActivated,
                     referencesDock, [navigateReferenceItem](QTreeWidgetItem* item, int) {
                         navigateReferenceItem(item);
                     });
    QObject::connect(referencesTree, &QTreeWidget::customContextMenuRequested,
                     referencesDock, [this](const QPoint& pos) {
                         QTreeWidgetItem* item = referencesTree
                             ? referencesTree->itemAt(pos)
                             : nullptr;
                         if (!itemHasNavigationTarget(item))
                             return;

                         QMenu menu(referencesTree);
                         QAction* copyPath =
                             menu.addAction(QStringLiteral("Copy Path"));
                         QAction* copyFileLine =
                             menu.addAction(QStringLiteral("Copy file:line"));
                         QAction* chosen =
                             menu.exec(referencesTree->viewport()->mapToGlobal(pos));
                         if (!chosen)
                             return;

                         const QString text = chosen == copyFileLine
                             ? itemFileLine(item)
                             : itemPath(item);
                         if (text.isEmpty())
                             return;
                         QApplication::clipboard()->setText(text);
                         if (statusMessageHandler) {
                             statusMessageHandler(QStringLiteral("Copied %1").arg(text),
                                                  1500);
                         }
                     });
}

void ReferencesPanelCoordinator::setWorkspaceFilesProvider(
    std::function<QStringList()> provider)
{
    workspaceFilesProvider = std::move(provider);
}

void ReferencesPanelCoordinator::setNavigationHandler(
    std::function<bool(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
}

void ReferencesPanelCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void ReferencesPanelCoordinator::showReferencesForSymbol(const QString& symbolName,
                                                        const QString& fileName,
                                                        const QString& moduleName)
{
    if (!referencesTree)
        return;

    currentReferenceSymbolName = symbolName;
    currentReferenceFileName = fileName;
    currentReferenceModuleName = moduleName;
    refresh();
}

void ReferencesPanelCoordinator::refresh()
{
    if (!referencesTree)
        return;

    if (referenceContextLabel) {
        referenceContextLabel->setText(referenceQueryContextText(
            currentReferenceSymbolName,
            currentReferenceFileName,
            referenceScopeCombo,
            referenceTypeCombo));
        referenceContextLabel->setToolTip(currentReferenceFileName);
    }

    if (currentReferenceSymbolName.isEmpty()) {
        referencesTree->clear();
        auto* emptyItem = new QTreeWidgetItem(referencesTree);
        emptyItem->setText(0, QStringLiteral("no symbol under cursor"));
        if (referencesDock) {
            referencesDock->setWindowTitle(QStringLiteral("References"));
            referencesDock->show();
            referencesDock->raise();
        }
        if (statusMessageHandler) {
            statusMessageHandler(QStringLiteral("References: no symbol under cursor"),
                                 3000);
        }
        return;
    }

    ReferencePanelQueryOptions queryOptions;
    queryOptions.symbolName = currentReferenceSymbolName;
    queryOptions.fileName = currentReferenceFileName;
    queryOptions.moduleName = currentReferenceModuleName;
    queryOptions.scope = referencePanelScopeFromValue(
        referenceScopeCombo ? referenceScopeCombo->currentData().toInt() : 0);
    queryOptions.typeFilter = referenceTypeCombo
        ? referenceTypeCombo->currentData().toInt()
        : -1;
    if (workspaceFilesProvider)
        queryOptions.workspaceFiles = workspaceFilesProvider();

    ReferenceService* referenceService = ReferenceService::getInstance();
    const ReferenceQuery query = referenceService->queryForPanel(queryOptions);
    const ReferenceReport report = referenceService->findReferenceReport(query);
    QString subjectName = report.subjectDisplayName;
    if (subjectName.isEmpty())
        subjectName = currentReferenceSymbolName;

    const bool hadExpandableItems = SemanticPanelUtils::treeHasExpandableItems(referencesTree);
    const QSet<QString> expandedKeys = SemanticPanelUtils::collectExpandedKeys(referencesTree);
    referencesTree->clear();
    if (report.totalCount == 0) {
        const QString reason = referenceEmptyReason(report, currentReferenceSymbolName);
        auto* emptyItem = new QTreeWidgetItem(referencesTree);
        emptyItem->setText(0, reason);
        emptyItem->setToolTip(0, reason);
    }
    for (const ReferenceFileGroup& fileGroupReport : report.fileGroups) {
        auto* fileGroup = new QTreeWidgetItem(referencesTree);
        fileGroup->setText(0, SemanticPanelUtils::countLabel(fileGroupReport.displayName,
                                                             fileGroupReport.count));
        fileGroup->setText(1, fileGroupReport.fileName);
        fileGroup->setToolTip(0, fileGroupReport.fileName);
        fileGroup->setToolTip(1, fileGroupReport.fileName);

        for (const ReferenceTypeGroup& typeGroupReport : fileGroupReport.typeGroups) {
            auto* typeGroup = new QTreeWidgetItem(fileGroup);
            typeGroup->setText(3, SemanticPanelUtils::countLabel(typeGroupReport.displayName,
                                                                 typeGroupReport.count));
            for (const ReferenceResult& reference : typeGroupReport.references)
                createReferenceItem(typeGroup, reference);
        }
    }
    SemanticPanelUtils::restoreTreeExpansion(referencesTree,
                                             hadExpandableItems,
                                             expandedKeys);

    if (referencesDock) {
        referencesDock->setWindowTitle(
            QStringLiteral("References: %1 (%2)")
                .arg(subjectName)
                .arg(report.totalCount));
        referencesDock->show();
        referencesDock->raise();
    }

    if (statusMessageHandler) {
        const QString message = report.totalCount == 0
            ? QStringLiteral("References: %1")
                  .arg(referenceEmptyReason(report,
                                            currentReferenceSymbolName))
            : QStringLiteral("Found %1 references for %2")
                  .arg(report.totalCount)
                  .arg(subjectName);
        statusMessageHandler(message, 3000);
    }
}
