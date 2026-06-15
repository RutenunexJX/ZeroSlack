#include "referencespanelcoordinator.h"

#include "referenceservice.h"
#include "semanticpanelutils.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>

#include <utility>

namespace {

QTreeWidgetItem* createReferenceItem(QTreeWidgetItem* parent,
                                     const ReferenceResult& reference)
{
    const sym_list::SymbolInfo& source = reference.referencingSymbol;
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, source.symbolName);
    item->setText(1, QFileInfo(source.fileName).fileName());
    item->setText(2, QString::number(source.startLine));
    item->setText(3, SemanticPanelUtils::relationshipTypeText(
                         reference.relationship.relationship.type));
    item->setToolTip(1, source.fileName);
    item->setData(0, Qt::UserRole, source.fileName);
    item->setData(0, Qt::UserRole + 1, source.startLine);
    item->setData(0, Qt::UserRole + 2, source.startColumn);
    return item;
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
    referenceTypeCombo->addItem(QStringLiteral("All Types"), -1);
    referenceTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                    SymbolRelationshipEngine::REFERENCES),
                                static_cast<int>(SymbolRelationshipEngine::REFERENCES));
    referenceTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                    SymbolRelationshipEngine::INSTANTIATES),
                                static_cast<int>(SymbolRelationshipEngine::INSTANTIATES));
    referenceTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                    SymbolRelationshipEngine::CALLS),
                                static_cast<int>(SymbolRelationshipEngine::CALLS));
    referenceTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                    SymbolRelationshipEngine::ASSIGNS_TO),
                                static_cast<int>(SymbolRelationshipEngine::ASSIGNS_TO));
    referenceTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                    SymbolRelationshipEngine::READS_FROM),
                                static_cast<int>(SymbolRelationshipEngine::READS_FROM));
    referenceTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                    SymbolRelationshipEngine::CLOCKS),
                                static_cast<int>(SymbolRelationshipEngine::CLOCKS));
    referenceTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                    SymbolRelationshipEngine::RESETS),
                                static_cast<int>(SymbolRelationshipEngine::RESETS));
    referenceTypeCombo->setToolTip(QStringLiteral("Reference type"));
    filtersLayout->addWidget(referenceTypeCombo);
    filtersLayout->addStretch(1);
    layout->addLayout(filtersLayout);

    referencesTree = new QTreeWidget(panel);
    referencesTree->setObjectName(QStringLiteral("referencesTree"));
    referencesTree->setColumnCount(4);
    referencesTree->setHeaderLabels({"Symbol", "File", "Line", "Relationship"});
    referencesTree->setRootIsDecorated(true);
    referencesTree->setAlternatingRowColors(true);
    referencesTree->setSelectionMode(QAbstractItemView::SingleSelection);
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

    QObject::connect(referencesTree, &QTreeWidget::itemDoubleClicked,
                     referencesDock, [this](QTreeWidgetItem* item, int) {
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

void ReferencesPanelCoordinator::setWorkspaceFilesProvider(
    std::function<QStringList()> provider)
{
    workspaceFilesProvider = std::move(provider);
}

void ReferencesPanelCoordinator::setNavigationHandler(
    std::function<void(const QString&, int, int)> handler)
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
    if (!referencesTree || symbolName.isEmpty())
        return;

    currentReferenceSymbolName = symbolName;
    currentReferenceFileName = fileName;
    currentReferenceModuleName = moduleName;
    refresh();
}

void ReferencesPanelCoordinator::refresh()
{
    if (!referencesTree || currentReferenceSymbolName.isEmpty())
        return;

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
    const QString subjectName = report.subjectSymbol.symbolName.isEmpty()
        ? currentReferenceSymbolName
        : report.subjectSymbol.symbolName;

    const bool hadExpandableItems = SemanticPanelUtils::treeHasExpandableItems(referencesTree);
    const QSet<QString> expandedKeys = SemanticPanelUtils::collectExpandedKeys(referencesTree);
    referencesTree->clear();
    for (const ReferenceFileGroup& fileGroupReport : report.fileGroups) {
        auto* fileGroup = new QTreeWidgetItem(referencesTree);
        fileGroup->setText(0, SemanticPanelUtils::countLabel(fileGroupReport.displayName,
                                                             fileGroupReport.count));
        fileGroup->setText(1, fileGroupReport.fileName);
        fileGroup->setToolTip(0, fileGroupReport.fileName);
        fileGroup->setToolTip(1, fileGroupReport.fileName);

        for (const ReferenceTypeGroup& typeGroupReport : fileGroupReport.typeGroups) {
            auto* typeGroup = new QTreeWidgetItem(fileGroup);
            const QString typeText = typeGroupReport.displayName.isEmpty()
                ? SemanticPanelUtils::relationshipTypeText(typeGroupReport.type)
                : typeGroupReport.displayName;
            typeGroup->setText(3, SemanticPanelUtils::countLabel(typeText,
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
        statusMessageHandler(
            QStringLiteral("Found %1 references for %2")
                .arg(report.totalCount)
                .arg(subjectName),
            3000);
    }
}
