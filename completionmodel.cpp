#include "completionmodel.h"
#include <QFont>
#include <QColor>
#include <QSize>
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
        if (item.type == SymbolCompletion) {
            if (item.text.contains("::")) {
                return item.text;
            } else if (item.text.startsWith("[DEFAULT]")) {
                return item.text;
            } else {
                // UPDATED: Show proper type information for symbols
                return QString("%1 (%2)").arg(item.text, item.description);
            }
        } else if (item.type == CommandCompletion && !item.description.isEmpty()) {
            return QString("%1 - %2").arg(item.text, item.description);
        }
        return item.text;

    case Qt::ToolTipRole:
        if (item.type == SymbolCompletion && item.text.startsWith("[DEFAULT]")) {
            return QString("No matching %1 found. Press Enter/Tab to insert default value.").arg(item.description.split(' ')[0]);
        }
        return item.description;

    case Qt::BackgroundRole:
        switch (item.type) {
        case KeywordCompletion:
            return QColor(255, 255, 255);
        case SymbolCompletion:
            if (item.text.contains("::")) {
                return QColor(100, 150, 200);
            } else if (item.text.startsWith("[DEFAULT]")) {
                return QColor(200, 255, 200);
            }
            return QColor(240, 250, 240);
        case CommandCompletion:
            if (item.text.contains("::")) {
                return QColor(80, 80, 200);
            } else if (item.text == "No matching commands") {
                return QColor(255, 200, 200);
            }
            return QColor(240, 240, 250);
        }
        break;

    case Qt::ForegroundRole:
        switch (item.type) {
        case SymbolCompletion:
            if (item.text.contains("::")) {
                return QColor(255, 255, 255);
            } else if (item.text.startsWith("[DEFAULT]")) {
                return QColor(0, 100, 0);
            }
            return QColor(0, 100, 0);
        case CommandCompletion:
            if (item.text.contains("::")) {
                return QColor(255, 255, 255);
            } else if (item.text == "No matching commands") {
                return QColor(100, 100, 100);
            }
            return QColor(0, 0, 150);
        default:
            return QColor(0, 0, 0);
        }
        break;

    case Qt::FontRole:
        {
            QFont font("Consolas", 9);
            if ((item.type == CommandCompletion || item.type == SymbolCompletion) &&
                (item.text.contains("::") || item.text.startsWith("[DEFAULT]"))) {
                font.setBold(true);
            }
            return font;
        }

    case Qt::SizeHintRole:
        return QSize(0, 18);

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
    if (item.text.contains(QStringLiteral("::")))
        return false;
    if (item.text == QStringLiteral("No matching commands")
        || item.text == QStringLiteral("No matching symbols")) {
        return false;
    }
    return true;
}
