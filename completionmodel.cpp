#include "completionmodel.h"
#include <QFont>
#include <QFontMetrics>
#include <QColor>
#include <QSize>
#include <QStringList>
#include <algorithm>

static const int CompletionItemMetaTypeId = qRegisterMetaType<CompletionModel::CompletionItem>("CompletionModel::CompletionItem");

CompletionModel::CompletionModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    completions.reserve(50);
}

QModelIndex CompletionModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    return createIndex(row, column);
}

QModelIndex CompletionModel::parent(const QModelIndex &child) const
{
    Q_UNUSED(child)
    return QModelIndex();
}

int CompletionModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return completions.size();
}

int CompletionModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 1;
}

QVariant CompletionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= completions.size())
        return QVariant();

    const CompletionItem &item = completions.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        return item.displayText.isEmpty() ? item.text : item.displayText;

    case Qt::ToolTipRole:
        return item.toolTipText.isEmpty() ? item.description : item.toolTipText;

    case Qt::BackgroundRole:
        switch (item.visualKind) {
        case KeywordVisual:
            return QColor(255, 255, 255);
        case SymbolHeaderVisual:
            return QColor(100, 150, 200);
        case SymbolDefaultVisual:
            return QColor(200, 255, 200);
        case SymbolVisual:
            return QColor(240, 250, 240);
        case CommandHeaderVisual:
            return QColor(80, 80, 200);
        case CommandEmptyVisual:
            return QColor(255, 200, 200);
        case CommandVisual:
            return QColor(240, 240, 250);
        }
        break;

    case Qt::ForegroundRole:
        switch (item.visualKind) {
        case SymbolHeaderVisual:
        case CommandHeaderVisual:
            return QColor(255, 255, 255);
        case SymbolDefaultVisual:
        case SymbolVisual:
            return QColor(0, 100, 0);
        case CommandEmptyVisual:
            return QColor(100, 100, 100);
        case CommandVisual:
            return QColor(0, 0, 150);
        case KeywordVisual:
        default:
            return QColor(0, 0, 0);
        }
        break;

    case Qt::FontRole:
        {
            QFont font("Consolas", 9);
            if (item.emphasized)
                font.setBold(true);
            return font;
        }

    case Qt::SizeHintRole:
        {
            QFont font("Consolas", 9);
            if (item.emphasized)
                font.setBold(true);
            const QString text =
                item.displayText.isEmpty() ? item.text : item.displayText;
            return QSize(QFontMetrics(font).horizontalAdvance(text) + 16,
                         item.rowHeight);
        }

    case Qt::UserRole:
        return QVariant::fromValue(item);
    }

    return QVariant();
}

CompletionModel::CompletionItem CompletionModel::getItem(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= completions.size()) {
        return CompletionItem();
    }
    return completions.at(index.row());
}

void CompletionModel::fillDisplayMetadata(CompletionItem &item)
{
    item.displayText = item.text;
    item.toolTipText = item.description;
    item.rowHeight = 18;
    item.selectable = true;
    item.emphasized = false;

    switch (item.type) {
    case KeywordCompletion:
        item.visualKind = KeywordVisual;
        return;
    case SymbolCompletion:
        if (item.text.contains(QStringLiteral("::"))) {
            item.visualKind = SymbolHeaderVisual;
            item.selectable = false;
            item.emphasized = true;
        } else if (item.text.startsWith(QStringLiteral("[DEFAULT]"))) {
            item.visualKind = SymbolDefaultVisual;
            item.emphasized = true;
            const QString defaultType = item.description.split(' ').value(0);
            item.toolTipText =
                QStringLiteral("No matching %1 found. Press Enter/Tab to insert default value.")
                    .arg(defaultType);
        } else {
            item.visualKind = SymbolVisual;
            const QString typeText = item.typeDisplayName.isEmpty()
                ? item.description
                : item.typeDisplayName;
            if (!typeText.isEmpty())
                item.displayText = QStringLiteral("%1 (%2)")
                    .arg(item.text, typeText);
            QStringList tooltipParts;
            if (!typeText.isEmpty())
                tooltipParts.append(typeText);
            if (!item.ownerScopeName.isEmpty())
                tooltipParts.append(QStringLiteral("owner: %1").arg(item.ownerScopeName));
            if (!item.sourceRoleDisplayName.isEmpty())
                tooltipParts.append(item.sourceRoleDisplayName);
            if (!tooltipParts.isEmpty())
                item.toolTipText = tooltipParts.join(QStringLiteral(" | "));
        }
        return;
    case CommandCompletion:
        if (item.text.contains(QStringLiteral("::"))) {
            item.visualKind = CommandHeaderVisual;
            item.selectable = false;
            item.emphasized = true;
        } else if (item.text == QStringLiteral("No matching commands")
                   || item.text == QStringLiteral("No matching symbols")) {
            item.visualKind = CommandEmptyVisual;
            item.selectable = false;
        } else {
            item.visualKind = CommandVisual;
        }
        if (!item.description.isEmpty())
            item.displayText = QStringLiteral("%1 - %2")
                .arg(item.text, item.description);
        return;
    }
}

bool CompletionModel::isSelectableIndex(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= completions.size())
        return false;
    return isSelectableItem(completions.at(index.row()));
}

QModelIndex CompletionModel::firstSelectableIndex() const
{
    for (int row = 0; row < completions.size(); ++row) {
        if (isSelectableItem(completions.at(row)))
            return index(row, 0);
    }
    return QModelIndex();
}

void CompletionModel::sortCompletionsByScore()
{
    std::sort(completions.begin(), completions.end(),
              [](const CompletionItem &a, const CompletionItem &b) {
                  return a.score > b.score;
              });
}

bool CompletionModel::isSelectableItem(const CompletionItem &item) const
{
    return item.selectable;
}
