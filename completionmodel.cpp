#include "completionmodel.h"
#include "completionservice.h"
#include <QFont>
#include <QColor>
#include <algorithm>
#include <utility>

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
            QFont font("Consolas", 10);
            if ((item.type == CommandCompletion || item.type == SymbolCompletion) &&
                (item.text.contains("::") || item.text.startsWith("[DEFAULT]"))) {
                font.setBold(true);
            }
            return font;
        }

    case Qt::UserRole:
        return QVariant::fromValue(item);
    }

    return QVariant();
}

void CompletionModel::updateCompletions(const QStringList &keywords,
                                       const QList<sym_list::SymbolInfo> &symbols,
                                       const QString &prefix,
                                       CompletionType type)
{
    beginResetModel();
    completions.clear();

    if (type == KeywordCompletion) {
        for (const QString &keyword : keywords) {
            CompletionItem item;
            item.text = keyword;
            item.type = KeywordCompletion;
            item.score = calculateScore(keyword, prefix);
            completions.append(item);
        }
    } else if (type == SymbolCompletion) {
        CompletionService* completionService = CompletionService::getInstance();
        if (symbols.size() == keywords.size()) {
            for (int i = 0; i < keywords.size() && i < symbols.size(); i++) {
                CompletionItem item;
                item.text = keywords[i];
                item.type = SymbolCompletion;
                item.symbolType = symbols[i].symbolType;
                item.score = calculateScore(keywords[i], prefix);
                item.description =
                    completionService->symbolTypeDescription(symbols[i].symbolType);

                completions.append(item);
            }
        } else {
            for (const sym_list::SymbolInfo &symbol : symbols) {
                CompletionItem item;
                item.text = symbol.symbolName;
                item.type = SymbolCompletion;
                item.symbolType = symbol.symbolType;
                item.score = calculateScore(symbol.symbolName, prefix);
                item.description =
                    completionService->symbolTypeDescription(symbol.symbolType);

                completions.append(item);
            }
        }
    }

    // Sort by score, cap total to avoid UI stutter, then limit visible
    sortCompletionsByScore();
    if (completions.size() > MaxCompletionItems) {
        completions = completions.mid(0, MaxCompletionItems);
    }
    if (completions.size() > 15) {
        completions = completions.mid(0, 15);  // Normal mode: top 15 by score
    }

    endResetModel();
}

void CompletionModel::updateCommandCompletions(const QStringList &commands, const QString &prefix)
{
    beginResetModel();
    completions.clear();

    CompletionItem headerItem;
    headerItem.text = prefix.isEmpty() ? ":: ALTERNATE MODE - COMMAND INTERFACE ::"
                                      : QString(":: ALTERNATE MODE - Input: '%1' ::").arg(prefix);
    headerItem.type = CommandCompletion;
    headerItem.description = "Command Interface";
    headerItem.score = 1000;
    completions.append(headerItem);

    int matchCount = 0;
    for (const QString &command : commands) {
        if (prefix.isEmpty() || command.startsWith(prefix, Qt::CaseInsensitive)) {
            CompletionItem item;
            item.text = command;
            item.type = CommandCompletion;
            item.description = QString("Execute %1 command").arg(command);
            item.score = calculateScore(command, prefix);
            completions.append(item);
            matchCount++;
        }
    }

    if (matchCount == 0 && !prefix.isEmpty()) {
        CompletionItem noMatchItem;
        noMatchItem.text = "No matching commands";
        noMatchItem.type = CommandCompletion;
        noMatchItem.description = "No commands match your input";
        noMatchItem.score = 0;
        completions.append(noMatchItem);
    }

    sortCompletionsByScore();
    if (completions.size() > MaxCompletionItems) {
        completions = completions.mid(0, MaxCompletionItems);
    }
    endResetModel();
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

void CompletionModel::updateSymbolCompletions(const QList<sym_list::SymbolInfo> &symbols,
                                              const QString &prefix,
                                              sym_list::sym_type_e symbolType)
{
    beginResetModel();
    completions.clear();

    CompletionService* completionService = CompletionService::getInstance();
    const CommandSymbolPresentation presentation =
        completionService->commandSymbolPresentation(symbolType);

    CompletionItem descItem;
    descItem.text = QString(":: COMMAND MODE - %1 ::").arg(presentation.typeDescription);
    descItem.type = SymbolCompletion;
    descItem.description = "Command Mode";
    descItem.score = 1000;
    descItem.defaultValue = presentation.defaultValue;
    completions.append(descItem);

    CompletionItem defaultItem;
    defaultItem.text = QString("[DEFAULT] %1").arg(presentation.defaultValue);
    defaultItem.type = SymbolCompletion;
    defaultItem.symbolType = symbolType;
    defaultItem.description =
        QString("Default %1 declaration").arg(presentation.typeDescription.split(' ').value(0));
    defaultItem.defaultValue = presentation.defaultValue;
    defaultItem.score = 999;  // High score but less than header
    completions.append(defaultItem);

    QSet<QString> addedItems;

    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.symbolName == presentation.defaultValue) {
            continue;
        }

        const CommandSymbolCompletionItem serviceItem =
            completionService->commandSymbolCompletionItem(symbol, symbolType, prefix);
        if (addedItems.contains(serviceItem.uniqueKey))
            continue;
        addedItems.insert(serviceItem.uniqueKey);

        CompletionItem item;
        item.type = SymbolCompletion;
        item.symbolType = symbolType;
        item.text = serviceItem.text;
        item.description = serviceItem.description;
        item.defaultValue = serviceItem.defaultValue;
        item.score = serviceItem.score;

        completions.append(item);
    }

    sortCompletionsByScore();
    if (completions.size() > MaxCompletionItems) {
        completions = completions.mid(0, MaxCompletionItems);
    }
    if (completions.size() > 32) {
        completions = completions.mid(0, 32);
    }

    endResetModel();
}

void CompletionModel::sortCompletionsByScore()
{
    std::sort(completions.begin(), completions.end(),
              [](const CompletionItem &a, const CompletionItem &b) {
                  return a.score > b.score;
              });
}

int CompletionModel::calculateScore(const QString &text, const QString &prefix) const
{
    if (prefix.isEmpty()) return 100;

    const QString lowerText = text.toLower();
    const QString lowerPrefix = prefix.toLower();

    if (lowerText == lowerPrefix) return 1000;

    if (lowerText.startsWith(lowerPrefix)) {
        return 800 + (100 - prefix.length());
    }

    if (lowerText.contains(lowerPrefix)) {
        return 400 + (100 - text.length());
    }

    int score = 0;
    int textPos = 0;
    for (const QChar &ch : lowerPrefix) {
        int found = lowerText.indexOf(ch, textPos);
        if (found >= 0) {
            score += 10;
            textPos = found + 1;
        } else {
            return 0; // No match
        }
    }

    return score;
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
