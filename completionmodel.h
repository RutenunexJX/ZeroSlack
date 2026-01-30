#ifndef COMPLETIONMODEL_H
#define COMPLETIONMODEL_H

#include <QMetaType>
#include <QAbstractItemModel>
#include <QStringList>
#include "syminfo.h"

class CompletionModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum CompletionType {
        KeywordCompletion,
        SymbolCompletion,
        CommandCompletion
    };

    struct CompletionItem {
        QString text;
        QString description;
        CompletionType type;
        sym_list::sym_type_e symbolType;
        QString defaultValue;
        int score;
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
    void updateCommandCompletions(const QStringList &commands, const QString &prefix);
    void clear();

    CompletionItem getItem(const QModelIndex &index) const;

    void updateSymbolCompletions(const QList<sym_list::SymbolInfo> &symbols,
                               const QString &prefix,
                               sym_list::sym_type_e symbolType);


private:
    QList<CompletionItem> completions;

    /// 单次展示最大条数，避免候选过多导致弹窗卡顿（性能优化）
    static const int MaxCompletionItems = 500;

    void sortCompletionsByScore();
    int calculateScore(const QString &text, const QString &prefix) const;
    QString getTypeDescription(sym_list::sym_type_e symbolType);
};

Q_DECLARE_METATYPE(CompletionModel::CompletionItem)

#endif // COMPLETIONMODEL_H