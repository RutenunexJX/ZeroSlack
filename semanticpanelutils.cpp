#include "semanticpanelutils.h"

#include <QDir>
#include <QFileInfo>
#include <QTreeWidget>

namespace {

QString normalizedUiFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QString stripCountSuffix(const QString& text)
{
    if (!text.endsWith(QLatin1Char(')')))
        return text;
    const int open = text.lastIndexOf(QStringLiteral(" ("));
    if (open < 0)
        return text;
    for (int i = open + 2; i < text.size() - 1; ++i) {
        if (!text.at(i).isDigit())
            return text;
    }
    return text.left(open);
}

QString expansionKeyForItem(QTreeWidgetItem* item)
{
    if (!item)
        return QString();

    QStringList pathParts;
    for (QTreeWidgetItem* current = item; current; current = current->parent()) {
        QStringList columns;
        for (int column = 0; column < current->columnCount(); ++column) {
            const QString text = stripCountSuffix(current->text(column));
            if (!text.isEmpty())
                columns.append(QStringLiteral("%1=%2").arg(column).arg(text));
        }

        const QString fileName = current->data(0, Qt::UserRole).toString();
        if (!fileName.isEmpty()) {
            const QString normalized = normalizedUiFileName(fileName);
            columns.append(QStringLiteral("file=%1")
                               .arg(normalized.isEmpty() ? fileName : normalized));
            columns.append(QStringLiteral("line=%1")
                               .arg(current->data(0, Qt::UserRole + 1).toInt()));
            columns.append(QStringLiteral("column=%1")
                               .arg(current->data(0, Qt::UserRole + 2).toInt()));
        }

        pathParts.prepend(columns.join(QLatin1Char('|')));
    }
    return pathParts.join(QLatin1Char('/'));
}

bool treeHasExpandableItems(QTreeWidgetItem* item)
{
    if (!item)
        return false;
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* child = item->child(i);
        if (child->childCount() > 0 || treeHasExpandableItems(child))
            return true;
    }
    return false;
}

void collectExpandedKeys(QTreeWidgetItem* item, QSet<QString>& keys)
{
    if (!item)
        return;
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* child = item->child(i);
        if (child->isExpanded())
            keys.insert(expansionKeyForItem(child));
        collectExpandedKeys(child, keys);
    }
}

int restoreExpandedKeys(QTreeWidgetItem* item, const QSet<QString>& keys)
{
    if (!item)
        return 0;

    int restored = 0;
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* child = item->child(i);
        const bool expanded = keys.contains(expansionKeyForItem(child));
        child->setExpanded(expanded);
        if (expanded)
            restored++;
        restored += restoreExpandedKeys(child, keys);
    }
    return restored;
}

} // namespace

namespace SemanticPanelUtils {

QString relationshipTypeText(SymbolRelationshipEngine::RelationType type)
{
    switch (type) {
    case SymbolRelationshipEngine::CONTAINS:
        return QStringLiteral("Contains");
    case SymbolRelationshipEngine::REFERENCES:
        return QStringLiteral("References");
    case SymbolRelationshipEngine::INSTANTIATES:
        return QStringLiteral("Instantiates");
    case SymbolRelationshipEngine::CALLS:
        return QStringLiteral("Calls");
    case SymbolRelationshipEngine::INHERITS:
        return QStringLiteral("Inherits");
    case SymbolRelationshipEngine::IMPLEMENTS:
        return QStringLiteral("Implements");
    case SymbolRelationshipEngine::ASSIGNS_TO:
        return QStringLiteral("Assigns To");
    case SymbolRelationshipEngine::READS_FROM:
        return QStringLiteral("Reads From");
    case SymbolRelationshipEngine::CLOCKS:
        return QStringLiteral("Clocks");
    case SymbolRelationshipEngine::RESETS:
        return QStringLiteral("Resets");
    case SymbolRelationshipEngine::GENERATES:
        return QStringLiteral("Generates");
    case SymbolRelationshipEngine::CONSTRAINS:
        return QStringLiteral("Constrains");
    }
    return QStringLiteral("Relationship");
}

QString countLabel(const QString& text, int count)
{
    return QStringLiteral("%1 (%2)").arg(text).arg(count);
}

bool treeHasExpandableItems(QTreeWidget* tree)
{
    return tree && ::treeHasExpandableItems(tree->invisibleRootItem());
}

QSet<QString> collectExpandedKeys(QTreeWidget* tree)
{
    QSet<QString> keys;
    if (tree)
        ::collectExpandedKeys(tree->invisibleRootItem(), keys);
    return keys;
}

void restoreTreeExpansion(QTreeWidget* tree,
                          bool hadExpandableItems,
                          const QSet<QString>& expandedKeys)
{
    if (!tree)
        return;

    const int restored = restoreExpandedKeys(tree->invisibleRootItem(), expandedKeys);
    if (!hadExpandableItems || (!expandedKeys.isEmpty() && restored == 0))
        tree->expandAll();
}

} // namespace SemanticPanelUtils
