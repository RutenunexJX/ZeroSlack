#include "completionmodel.h"
#include "completionmanager.h"
#include <QFont>
#include <QColor>
#include <algorithm>
//#include <QDebug>

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
            // 处理特殊显示项
            if (item.text.contains("::")) {
                return item.text; // 命令模式标题
            } else if (item.text.startsWith("[DEFAULT]")) {
                return item.text; // 默认值选项
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
                return QColor(100, 150, 200); // 蓝色背景用于标题
            } else if (item.text.startsWith("[DEFAULT]")) {
                return QColor(200, 255, 200); // 绿色背景用于默认值
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
                return QColor(255, 255, 255); // 白色文字用于标题
            } else if (item.text.startsWith("[DEFAULT]")) {
                return QColor(0, 100, 0); // 深绿色文字用于默认值
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
        // Add keywords
        for (const QString &keyword : keywords) {
            CompletionItem item;
            item.text = keyword;
            item.type = KeywordCompletion;
            item.score = calculateScore(keyword, prefix);
            completions.append(item);
        }
    } else if (type == SymbolCompletion) {
        // UPDATED: Improved symbol handling for normal mode
        if (symbols.size() == keywords.size()) {
            // Normal mode: keywords and symbols should match 1:1
            for (int i = 0; i < keywords.size() && i < symbols.size(); i++) {
                CompletionItem item;
                item.text = keywords[i];
                item.type = SymbolCompletion;
                item.symbolType = symbols[i].symbolType;
                item.score = calculateScore(keywords[i], prefix);

                // UPDATED: Set proper description based on symbol type
                switch (symbols[i].symbolType) {
                case sym_list::sym_module:
                    item.description = "module";
                    break;
                case sym_list::sym_reg:
                    item.description = "reg";
                    break;
                case sym_list::sym_wire:
                    item.description = "wire";
                    break;
                case sym_list::sym_logic:
                    item.description = "logic";
                    break;
                case sym_list::sym_task:
                    item.description = "task";
                    break;
                case sym_list::sym_function:
                    item.description = "function";
                    break;
                default:
                    item.description = "symbol";
                    break;
                }

                completions.append(item);
            }
        } else {
            // Fallback: just add symbols
            for (const sym_list::SymbolInfo &symbol : symbols) {
                CompletionItem item;
                item.text = symbol.symbolName;
                item.type = SymbolCompletion;
                item.symbolType = symbol.symbolType;
                item.score = calculateScore(symbol.symbolName, prefix);

                // Add type description
                switch (symbol.symbolType) {
                case sym_list::sym_module:
                    item.description = "module";
                    break;
                case sym_list::sym_reg:
                    item.description = "reg";
                    break;
                case sym_list::sym_wire:
                    item.description = "wire";
                    break;
                case sym_list::sym_logic:
                    item.description = "logic";
                    break;
                case sym_list::sym_task:
                    item.description = "task";
                    break;
                case sym_list::sym_function:
                    item.description = "function";
                    break;
                default:
                    item.description = "symbol";
                    break;
                }

                completions.append(item);
            }
        }
    }

    // Sort by score and limit results
    sortCompletionsByScore();
    if (completions.size() > 15) {
        completions = completions.mid(0, 15);  // Use mid() instead of resize()
    }

    endResetModel();
}

void CompletionModel::updateCommandCompletions(const QStringList &commands, const QString &prefix)
{
    beginResetModel();
    completions.clear();

    // 添加标题项
    CompletionItem headerItem;
    headerItem.text = prefix.isEmpty() ? ":: ALTERNATE MODE - COMMAND INTERFACE ::"
                                      : QString(":: ALTERNATE MODE - Input: '%1' ::").arg(prefix);
    headerItem.type = CommandCompletion;
    headerItem.description = "Command Interface";
    headerItem.score = 1000; // 最高优先级
    completions.append(headerItem);

    // 添加匹配的命令
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
    endResetModel();
}

CompletionModel::CompletionItem CompletionModel::getItem(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= completions.size()) {
        return CompletionItem();
    }
    return completions.at(index.row());
}

void CompletionModel::updateSymbolCompletions(const QList<sym_list::SymbolInfo> &symbols,
                                              const QString &prefix,
                                              sym_list::sym_type_e symbolType)
{
    beginResetModel();
    completions.clear();

    // 确定符号类型的默认值和描述（这部分逻辑不变）
    QString defaultValue, typeDescription;
    switch (symbolType) {
    case sym_list::sym_reg:
        defaultValue = "reg";
        typeDescription = "reg variables";
        break;
    case sym_list::sym_wire:
        defaultValue = "wire";
        typeDescription = "wire variables";
        break;
    case sym_list::sym_logic:
        defaultValue = "logic";
        typeDescription = "logic variables";
        break;
    case sym_list::sym_module:
        defaultValue = "module";
        typeDescription = "modules";
        break;
    case sym_list::sym_task:
        defaultValue = "task";
        typeDescription = "tasks";
        break;
    case sym_list::sym_function:
        defaultValue = "function";
        typeDescription = "functions";
        break;
    default:
        defaultValue = "symbol";
        typeDescription = "symbols";
        break;
    }

    // 添加命令描述作为第一项（不变）
    CompletionItem descItem;
    descItem.text = QString(":: COMMAND MODE - %1 ::").arg(typeDescription);
    descItem.type = SymbolCompletion;
    descItem.description = "Command Mode";
    descItem.score = 1000;
    descItem.defaultValue = defaultValue;
    completions.append(descItem);

    // Always add default value as first selectable item
    CompletionItem defaultItem;
    defaultItem.text = QString("[DEFAULT] %1").arg(defaultValue);
    defaultItem.type = SymbolCompletion;
    defaultItem.symbolType = symbolType;
    defaultItem.description = QString("Default %1 declaration").arg(typeDescription.split(' ')[0]);
    defaultItem.defaultValue = defaultValue;
    defaultItem.score = 999;  // High score but less than header
    completions.append(defaultItem);

    CompletionManager* manager = CompletionManager::getInstance();
/*
    // 一次性获取所有匹配的符号和分数（已排序）
    QVector<QPair<sym_list::SymbolInfo, int>> scoredMatches = manager->getScoredSymbolMatches(symbolType, prefix);

    int matchCount = 0;
    for (const auto& match : qAsConst(scoredMatches)) {
        // Skip if it's the same as default value to avoid duplicates
        if (match.first.symbolName == defaultValue) {
            continue;
        }

        CompletionItem item;
        item.text = match.first.symbolName;           // 符号名称
        item.type = SymbolCompletion;
        item.symbolType = symbolType;
        item.description = typeDescription.split(' ')[0]; // "reg", "wire", etc.
        item.defaultValue = match.first.symbolName;
        item.score = match.second;                    // 预计算的分数，无需重新计算

        completions.append(item);
        matchCount++;
    }
*/
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.symbolName == defaultValue) {
            continue;
        }

        CompletionItem item;
        item.text = symbol.symbolName;
        item.type = SymbolCompletion;
        item.symbolType = symbolType;
        item.description = typeDescription.split(' ')[0];
        item.defaultValue = symbol.symbolName;
        // 为传入的符号计算匹配分数
        item.score = manager->calculateMatchScore(symbol.symbolName, prefix);

        completions.append(item);
    }

    sortCompletionsByScore();

    // 限制结果数量（保持不变）
    if (completions.size() > 16) {
        completions = completions.mid(0, 16);
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

    // Exact match
    if (lowerText == lowerPrefix) return 1000;

    // Prefix match
    if (lowerText.startsWith(lowerPrefix)) {
        return 800 + (100 - prefix.length());
    }

    // Contains match
    if (lowerText.contains(lowerPrefix)) {
        return 400 + (100 - text.length());
    }

    // Abbreviation match (simplified)
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

QString CompletionModel::getTypeDescription(sym_list::sym_type_e symbolType)
{
    switch (symbolType) {
    case sym_list::sym_reg: return "reg variables";
    case sym_list::sym_wire: return "wire variables";
    case sym_list::sym_logic: return "logic variables";
    case sym_list::sym_module: return "modules";
    case sym_list::sym_task: return "tasks";
    case sym_list::sym_function: return "functions";

    case sym_list::sym_interface: return "interfaces";
    case sym_list::sym_interface_modport: return "interface modports";
    case sym_list::sym_packed_struct: return "packed structures";
    case sym_list::sym_unpacked_struct: return "unpacked structures";
    case sym_list::sym_enum: return "enumeration types";
    case sym_list::sym_typedef: return "type definitions";
    case sym_list::sym_def_define: return "macro definitions";
    case sym_list::sym_def_ifdef: return "conditional compilation";
    case sym_list::sym_def_ifndef: return "conditional compilation";
    case sym_list::sym_parameter: return "parameters";
    case sym_list::sym_localparam: return "local parameters";
    case sym_list::sym_always: return "always blocks";
    case sym_list::sym_always_ff: return "always_ff blocks";
    case sym_list::sym_always_comb: return "always_comb blocks";
    case sym_list::sym_assign: return "continuous assignments";
    case sym_list::sym_xilinx_constraint: return "synthesis constraints";

    case sym_list::sym_enum_var:          return "enumeration variables";
    case sym_list::sym_enum_value:        return "enumeration values";

    case sym_list::sym_packed_struct_var: return "packed struct variables";
    case sym_list::sym_unpacked_struct_var: return "unpacked struct variables";
    case sym_list::sym_struct_member:     return "structure members";

    default: return "symbols";
    }
}
