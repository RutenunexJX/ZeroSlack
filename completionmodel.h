#ifndef COMPLETIONMODEL_H
#define COMPLETIONMODEL_H

#include <QMetaType>
#include <QAbstractItemModel>
#include <QStringList>
#include "completiontypes.h"

class CompletionModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum CompletionType {
        KeywordCompletion,
        SymbolCompletion,
        CommandCompletion
    };

    enum CompletionVisualKind {
        KeywordVisual,
        SymbolVisual,
        SymbolHeaderVisual,
        SymbolDefaultVisual,
        CommandVisual,
        CommandHeaderVisual,
        CommandEmptyVisual
    };

    struct CompletionItem {
        QString text;
        QString description;
        QString displayText;
        QString toolTipText;
        CompletionType type = KeywordCompletion;
        CompletionVisualKind visualKind = KeywordVisual;
        sym_list::sym_type_e symbolType = sym_list::sym_user;
        SymbolStableKey symbolStableKey;
        QString defaultValue;
        int score = 0;
        int rowHeight = 18;
        bool selectable = true;
        bool emphasized = false;
    };

    explicit CompletionModel(QObject *parent = nullptr);

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Custom methods
    void updateCompletions(const QStringList &keywords,
                          const QList<sym_list::SymbolInfo> &symbols,
                          const QString &prefix,
                          CompletionType type = KeywordCompletion);
    void updateCompletions(const CompletionResult &completion,
                           const QString &prefix);
    void updateCommandCompletions(const QStringList &commands, const QString &prefix);
    void clear();

    CompletionItem getItem(const QModelIndex &index) const;
    bool isSelectableIndex(const QModelIndex &index) const;
    QModelIndex firstSelectableIndex() const;

    void updateSymbolCompletions(const QList<sym_list::SymbolInfo> &symbols,
                               const QString &prefix,
                               sym_list::sym_type_e symbolType);


private:
    QList<CompletionItem> completions;

    static const int MaxCompletionItems = 500;

    static void fillDisplayMetadata(CompletionItem &item);
    void sortCompletionsByScore();
    bool isSelectableItem(const CompletionItem &item) const;
};

Q_DECLARE_METATYPE(CompletionModel::CompletionItem)

#endif // COMPLETIONMODEL_H
