#ifndef SEMANTICPANELUTILS_H
#define SEMANTICPANELUTILS_H

#include "symbolrelationshipengine.h"

#include <QSet>
#include <QString>

class QTreeWidget;

namespace SemanticPanelUtils {

QString relationshipTypeText(SymbolRelationshipEngine::RelationType type);
QString countLabel(const QString& text, int count);

bool treeHasExpandableItems(QTreeWidget* tree);
QSet<QString> collectExpandedKeys(QTreeWidget* tree);
void restoreTreeExpansion(QTreeWidget* tree,
                          bool hadExpandableItems,
                          const QSet<QString>& expandedKeys);

} // namespace SemanticPanelUtils

#endif // SEMANTICPANELUTILS_H
