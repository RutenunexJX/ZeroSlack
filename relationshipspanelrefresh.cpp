#include "relationshipspanelcoordinator.h"

#include "relationshipservice.h"
#include "semanticindex.h"
#include "semanticpanelutils.h"
#include "symboltaxonomy.h"

#include <QFileInfo>

namespace {

constexpr int kNavigationFileRole = Qt::UserRole;
constexpr int kNavigationLineRole = Qt::UserRole + 1;
constexpr int kNavigationColumnRole = Qt::UserRole + 2;
constexpr int kGraphSymbolRole = Qt::UserRole + 3;
constexpr int kGraphModuleRole = Qt::UserRole + 4;
constexpr int kRelationshipTypeRole = Qt::UserRole + 5;
constexpr int kModuleBlockFileRole = Qt::UserRole + 6;
constexpr int kModuleBlockNameRole = Qt::UserRole + 7;

bool isModuleBlockDefinition(const SemanticSymbolRecord& record)
{
    if (!record.isValid())
        return false;
    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    return metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Module
        || metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Interface;
}

SemanticSymbolRecord moduleDefinitionForRelationshipPeer(
    const SemanticSymbolRecord& peerRecord)
{
    if (isModuleBlockDefinition(peerRecord))
        return peerRecord;

    if (peerRecord.type.stableKey.isValid()) {
        const SemanticSymbolRecord stableRecord =
            SemanticIndex::getInstance()->getSymbolRecordByStableKey(
                peerRecord.type.stableKey);
        if (isModuleBlockDefinition(stableRecord))
            return stableRecord;
    }

    QString moduleTypeName = peerRecord.type.resolvedTypeName;
    if (moduleTypeName.isEmpty()
        && peerRecord.type.stableKey.isValid()) {
        moduleTypeName = peerRecord.type.stableKey.symbolName;
    }
    if (moduleTypeName.isEmpty())
        moduleTypeName =
            SymbolTaxonomy::interfaceTypeName(peerRecord.type.rawTypeText);
    if (moduleTypeName.isEmpty())
        return {};

    const QList<SemanticSymbolRecord> candidates =
        SemanticIndex::getInstance()->findDefinitionRecords(moduleTypeName);
    for (const SemanticSymbolRecord& candidate : candidates) {
        if (isModuleBlockDefinition(candidate))
            return candidate;
    }
    return {};
}

SemanticSymbolRecord navigationRecordForRelationship(
    const DirectedRelationshipResult& relationship)
{
    if (relationship.relationshipType != SymbolRelationshipEngine::INSTANTIATES)
        return relationship.peerSymbolRecord;

    const SemanticSymbolRecord moduleRecord =
        moduleDefinitionForRelationshipPeer(relationship.peerSymbolRecord);
    return moduleRecord.isValid() ? moduleRecord : relationship.peerSymbolRecord;
}

QTreeWidgetItem* createRelationshipItem(QTreeWidgetItem* parent,
                                        const DirectedRelationshipResult& relationship,
                                        const QString& direction)
{
    const SemanticSymbolRecord navigationRecord =
        navigationRecordForRelationship(relationship);
    const SemanticSymbolLocation peerLocation = navigationRecord.location;
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, direction);
    item->setText(1, relationship.peerSymbolDisplayName);
    item->setText(2, relationship.peerFileDisplayName);
    item->setText(3, relationship.peerLineDisplayName);
    item->setText(4, relationship.typeDisplayName);
    const QString& explanation = relationship.explanation;
    item->setText(5, explanation);
    item->setToolTip(0, explanation);
    item->setToolTip(1, explanation);
    item->setToolTip(2, peerLocation.fileName);
    item->setToolTip(4, explanation);
    item->setToolTip(5, explanation);
    item->setData(0, kNavigationFileRole, peerLocation.fileName);
    item->setData(0, kNavigationLineRole, peerLocation.startLine);
    item->setData(0, kNavigationColumnRole, peerLocation.startColumn);
    item->setData(0, kGraphSymbolRole, relationship.peerSymbolRecord.name);
    item->setData(0, kGraphModuleRole, relationship.peerSymbolRecord.owner.name);
    item->setData(0,
                  kRelationshipTypeRole,
                  static_cast<int>(relationship.relationshipType));
    if (isModuleBlockDefinition(navigationRecord)) {
        item->setData(0, kModuleBlockFileRole, navigationRecord.location.fileName);
        item->setData(0, kModuleBlockNameRole, navigationRecord.name);
    }
    return item;
}

QString relationshipEmptyReason(const RelationshipReport& report,
                                const QString& symbolName)
{
    if (symbolName.isEmpty())
        return QStringLiteral("no symbol under cursor");
    switch (report.notFoundReason) {
    case RelationshipReportNotFoundReason::None:
        break;
    case RelationshipReportNotFoundReason::NoSubjectSymbol:
        if (SemanticIndex::getInstance()->getSymbolRecords().isEmpty())
            return QStringLiteral("workspace analysis stale / not ready");
        return QStringLiteral("symbol not indexed");
    case RelationshipReportNotFoundReason::NoRelationships:
        return QStringLiteral("no relationships found");
    }
    return report.notFoundReasonDisplayName.isEmpty()
        ? QStringLiteral("no relationships found")
        : report.notFoundReasonDisplayName;
}

QString relationshipSourceDisplayName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QStringLiteral("<none>");
    const QString displayName = QFileInfo(fileName).fileName();
    return displayName.isEmpty() ? fileName : displayName;
}

QString relationshipQueryContextText(const QString& symbolName,
                                     const QString& fileName,
                                     QComboBox* viewCombo,
                                     QComboBox* directionCombo,
                                     QComboBox* typeCombo,
                                     QComboBox* depthCombo)
{
    const QString symbol = symbolName.isEmpty()
        ? QStringLiteral("<none>")
        : symbolName;
    QStringList parts;
    parts.append(QStringLiteral("Symbol: %1").arg(symbol));
    parts.append(QStringLiteral("Source: %1")
                     .arg(relationshipSourceDisplayName(fileName)));
    parts.append(QStringLiteral("View: %1")
                     .arg(viewCombo ? viewCombo->currentText()
                                    : QStringLiteral("Direct")));
    parts.append(QStringLiteral("Direction: %1")
                     .arg(directionCombo ? directionCombo->currentText()
                                         : QStringLiteral("All Directions")));
    parts.append(QStringLiteral("Type: %1")
                     .arg(typeCombo ? typeCombo->currentText()
                                    : QStringLiteral("All Types")));
    if (viewCombo && viewCombo->currentData().toInt() == 1) {
        parts.append(QStringLiteral("Depth: %1")
                         .arg(depthCombo ? depthCombo->currentText()
                                         : QStringLiteral("Depth 1")));
    }
    return parts.join(QStringLiteral(" | "));
}

RelationshipPanelDirection relationshipPanelDirectionFromValue(int value)
{
    switch (value) {
    case 1:
        return RelationshipPanelDirection::Outgoing;
    case 2:
        return RelationshipPanelDirection::Incoming;
    case 0:
    default:
        return RelationshipPanelDirection::All;
    }
}

} // namespace

void RelationshipsPanelCoordinator::refresh()
{
    if (!relationshipsTree)
        return;

    if (relationshipContextLabel) {
        relationshipContextLabel->setText(relationshipQueryContextText(
            currentRelationshipSymbolName,
            currentRelationshipFileName,
            relationshipViewCombo,
            relationshipDirectionCombo,
            relationshipTypeCombo,
            relationshipDepthCombo));
        relationshipContextLabel->setToolTip(currentRelationshipFileName);
    }

    if (currentRelationshipSymbolName.isEmpty()) {
        relationshipsTree->clear();
        auto* emptyItem = new QTreeWidgetItem(relationshipsTree);
        emptyItem->setText(5, QStringLiteral("no symbol under cursor"));
        if (relationshipsDock) {
            relationshipsDock->setWindowTitle(QStringLiteral("Relationships"));
            relationshipsDock->show();
            relationshipsDock->raise();
        }
        if (statusMessageHandler) {
            statusMessageHandler(QStringLiteral("Relationships: no symbol under cursor"),
                                 3000);
        }
        return;
    }

    const int typeFilter = relationshipTypeCombo
        ? relationshipTypeCombo->currentData().toInt()
        : -1;

    const bool treeMode = relationshipViewCombo
        && relationshipViewCombo->currentData().toInt() == 1;
    if (treeMode) {
        refreshHierarchyTree(typeFilter);
        return;
    }

    RelationshipPanelQueryOptions browseOptions;
    browseOptions.symbolName = currentRelationshipSymbolName;
    browseOptions.fileName = currentRelationshipFileName;
    browseOptions.moduleName = currentRelationshipModuleName;
    browseOptions.typeFilter = typeFilter;
    browseOptions.direction = relationshipPanelDirectionFromValue(
        relationshipDirectionCombo ? relationshipDirectionCombo->currentData().toInt() : 0);

    RelationshipService* relationshipService = RelationshipService::getInstance();
    const RelationshipBrowseQuery browseQuery =
        relationshipService->queryForPanel(browseOptions);
    const RelationshipReport report =
        relationshipService->findRelationshipReport(browseQuery);
    QString subjectName = report.subjectDisplayName;
    if (subjectName.isEmpty())
        subjectName = currentRelationshipSymbolName;

    const bool hadExpandableItems =
        SemanticPanelUtils::treeHasExpandableItems(relationshipsTree);
    const QSet<QString> expandedKeys =
        SemanticPanelUtils::collectExpandedKeys(relationshipsTree);
    relationshipsTree->clear();
    if (report.totalCount == 0) {
        const QString reason =
            relationshipEmptyReason(report, currentRelationshipSymbolName);
        auto* emptyItem = new QTreeWidgetItem(relationshipsTree);
        emptyItem->setText(5, reason);
        emptyItem->setToolTip(5, reason);
    }
    for (const RelationshipDirectionGroup& directionGroupReport : report.directionGroups) {
        const QString direction = directionGroupReport.displayName.isEmpty()
            ? (directionGroupReport.direction == DirectedRelationshipResult::Outgoing
                   ? QStringLiteral("Outgoing")
                   : QStringLiteral("Incoming"))
            : directionGroupReport.displayName;
        auto* directionGroup = new QTreeWidgetItem(relationshipsTree);
        directionGroup->setText(0, SemanticPanelUtils::countLabel(
                                       direction,
                                       directionGroupReport.count));

        for (const RelationshipTypeGroup& typeGroupReport : directionGroupReport.typeGroups) {
            auto* typeGroup = new QTreeWidgetItem(directionGroup);
            typeGroup->setText(4, SemanticPanelUtils::countLabel(typeGroupReport.displayName,
                                                                 typeGroupReport.count));

            for (const DirectedRelationshipResult& directed : typeGroupReport.relationships) {
                createRelationshipItem(typeGroup, directed, direction);
            }
        }
    }
    SemanticPanelUtils::restoreTreeExpansion(relationshipsTree,
                                             hadExpandableItems,
                                             expandedKeys);

    if (relationshipsDock) {
        relationshipsDock->setWindowTitle(
            QStringLiteral("Relationships: %1 (%2)")
                .arg(subjectName)
                .arg(report.totalCount));
        relationshipsDock->show();
        relationshipsDock->raise();
    }

    if (statusMessageHandler) {
        const QString message = report.totalCount == 0
            ? QStringLiteral("Relationships: %1")
                  .arg(relationshipEmptyReason(report,
                                               currentRelationshipSymbolName))
            : QStringLiteral("Found %1 relationships for %2")
                  .arg(report.totalCount)
                  .arg(subjectName);
        statusMessageHandler(message, 3000);
    }
}
