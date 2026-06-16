#ifndef SEMANTICPANELUTILS_H
#define SEMANTICPANELUTILS_H

#include <QSet>
#include <QString>

class QTreeWidget;

namespace SemanticPanelUtils {

QString countLabel(const QString& text, int count);

bool treeHasExpandableItems(QTreeWidget* tree);
QSet<QString> collectExpandedKeys(QTreeWidget* tree);
void restoreTreeExpansion(QTreeWidget* tree,
                          bool hadExpandableItems,
                          const QSet<QString>& expandedKeys);

} // namespace SemanticPanelUtils

#endif // SEMANTICPANELUTILS_H
