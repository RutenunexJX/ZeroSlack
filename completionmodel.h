#ifndef COMPLETIONMODEL_H
#define COMPLETIONMODEL_H

#include <QMetaType>
#include <QAbstractItemModel>
#include <QList>
#include <QStringList>
#include "completiontypes.h"
#include "includeheaderworkflowtypes.h"

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
        QString typeDisplayName;
        QString ownerScopeName;
        QString sourceRoleDisplayName;
        QString analysisBandDisplayName;
        SemanticAnalysisBandMetadata analysisBand;
        CompletionType type = KeywordCompletion;
        CompletionVisualKind visualKind = KeywordVisual;
        SemanticSymbolRecord symbolRecord;
        SymbolStableKey symbolStableKey;
        SymbolTaxonomy::DeclarationKind declarationKind =
            SymbolTaxonomy::DeclarationKind::Unknown;
        SymbolTaxonomy::SymbolUsageRole usageRole =
            SymbolTaxonomy::SymbolUsageRole::Unknown;
        SymbolTaxonomy::SymbolOwnerScope ownerScope =
            SymbolTaxonomy::SymbolOwnerScope::Unknown;
        SymbolTaxonomy::SourceRole sourceRole =
            SymbolTaxonomy::SourceRole::Unknown;
        QString defaultValue;
        int selectionStart = -1;
        int selectionLength = 0;
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
    void updateCompletions(const CompletionResult &completion,
                           const QString &prefix);
    void updateCommandCompletions(const QStringList &commands, const QString &prefix);
    void updateIncludeFileCompletions(const QStringList& filePaths,
                                      const QString& prefix);
    void updateIncludeNewHeaderCompletions(
        const QList<IncludeNewHeaderChoice>& choices,
        const QString& title);
    void updateCommandHelpCompletions(const QList<CommandModeCommand>& commands);
    void updateInlineCommandCompletions(
        const CommandModeCompletionState& state);
    void clear();

    CompletionItem getItem(const QModelIndex &index) const;
    bool isSelectableIndex(const QModelIndex &index) const;
    QModelIndex firstSelectableIndex() const;

    void updateSymbolRecordCompletions(
                               const QList<SemanticSymbolRecord> &records,
                               const QString &prefix,
                               CompletionCommandKind requestedKind);


private:
    QList<CompletionItem> completions;

    static const int MaxCompletionItems = 500;

    static void fillDisplayMetadata(CompletionItem &item);
    void sortCompletionsByScore();
    bool isSelectableItem(const CompletionItem &item) const;
};

Q_DECLARE_METATYPE(CompletionModel::CompletionItem)

#endif // COMPLETIONMODEL_H
