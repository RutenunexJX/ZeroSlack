#include "completionmodel.h"

#include "completionservice.h"

#include <QSet>

#include <algorithm>

namespace {
QString visibleIncludeTemplatePreview(QString text)
{
    QStringList lines = text.trimmed().split(QLatin1Char('\n'));
    constexpr int kMaxPreviewLines = 8;
    if (lines.size() > kMaxPreviewLines) {
        lines = lines.mid(0, kMaxPreviewLines);
        lines.append(QStringLiteral("..."));
    }
    return lines.join(QLatin1Char('\n'));
}

QString ownerScopeNameForRecord(const SemanticSymbolRecord& record)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;

    switch (record.owner.kind) {
    case SymbolTaxonomy::SymbolOwnerScope::Global:
        return QStringLiteral("global");
    case SymbolTaxonomy::SymbolOwnerScope::Module:
        return QStringLiteral("module");
    case SymbolTaxonomy::SymbolOwnerScope::Interface:
        return QStringLiteral("interface");
    case SymbolTaxonomy::SymbolOwnerScope::Package:
        return QStringLiteral("package");
    case SymbolTaxonomy::SymbolOwnerScope::Struct:
        return QStringLiteral("struct");
    case SymbolTaxonomy::SymbolOwnerScope::Unknown:
        break;
    }
    return QString();
}

void fillSymbolMetadataFromRecord(
    CompletionModel::CompletionItem& item,
    const SemanticSymbolRecord& record)
{
    item.symbolRecord = record;
    item.symbolStableKey = record.stableKey;

    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    item.typeDisplayName = SymbolTaxonomy::symbolTypeLabel(metadata);
    item.ownerScopeName = ownerScopeNameForRecord(record);
    item.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
    item.analysisBand = record.analysisBand;
    item.analysisBandDisplayName =
        semanticAnalysisBandDisplayName(record.analysisBand);
    item.declarationKind = metadata.declarationKind;
    item.usageRole = metadata.usageRole;
    item.ownerScope = metadata.ownerScope;
    item.sourceRole = metadata.sourceRole;
}

bool isVisibleSymbolCompletionBandItem(
    const CompletionModel::CompletionItem& item)
{
    if (item.type != CompletionModel::SymbolCompletion)
        return false;
    if (!item.selectable)
        return false;
    if (item.text.startsWith(QStringLiteral("[DEFAULT]")))
        return false;
    return true;
}

struct CompletionAnalysisBandCount {
    SemanticAnalysisBandMetadata metadata;
    int count = 0;
};

SemanticAnalysisBandMetadata normalizedCompletionAnalysisBand(
    const SemanticAnalysisBandMetadata& metadata)
{
    if (metadata.isValid())
        return metadata;

    SemanticAnalysisBandMetadata unbanded;
    unbanded.label = QStringLiteral("unbanded");
    unbanded.displayName = QStringLiteral("unbanded");
    return unbanded;
}

QList<CompletionAnalysisBandCount> completionBandCountsForVisibleSymbols(
    const QList<CompletionModel::CompletionItem>& items)
{
    QList<CompletionAnalysisBandCount> counts;
    for (const CompletionModel::CompletionItem& item : items) {
        if (!isVisibleSymbolCompletionBandItem(item))
            continue;

        const SemanticAnalysisBandMetadata metadata =
            normalizedCompletionAnalysisBand(item.analysisBand);
        auto existing = std::find_if(
            counts.begin(),
            counts.end(),
            [&](const CompletionAnalysisBandCount& count) {
                return count.metadata.label == metadata.label;
            });
        if (existing == counts.end()) {
            CompletionAnalysisBandCount next;
            next.metadata = metadata;
            next.count = 1;
            counts.append(next);
        } else {
            ++existing->count;
        }
    }

    std::sort(counts.begin(),
              counts.end(),
              [](const CompletionAnalysisBandCount& left,
                 const CompletionAnalysisBandCount& right) {
                  const int leftPriority =
                      semanticAnalysisBandSortPriority(left.metadata);
                  const int rightPriority =
                      semanticAnalysisBandSortPriority(right.metadata);
                  if (leftPriority != rightPriority)
                      return leftPriority < rightPriority;
                  return QString::compare(left.metadata.label,
                                          right.metadata.label,
                                          Qt::CaseInsensitive) < 0;
              });
    return counts;
}

QString completionBandSummaryText(
    const QList<CompletionAnalysisBandCount>& counts)
{
    QStringList parts;
    for (const CompletionAnalysisBandCount& count : counts) {
        const QString itemCount = QStringLiteral("%1 %2")
            .arg(count.count)
            .arg(count.count == 1
                     ? QStringLiteral("item")
                     : QStringLiteral("items"));
        parts.append(QStringLiteral("%1 %2")
                         .arg(semanticAnalysisBandDisplayName(count.metadata),
                              itemCount));
    }
    return QStringLiteral("bands %1")
        .arg(parts.join(QStringLiteral(", ")));
}
}

void CompletionModel::updateIncludeFileCompletions(
    const QStringList& filePaths,
    const QString& prefix)
{
    beginResetModel();
    completions.clear();

    CompletionItem headerItem;
    headerItem.text = prefix.isEmpty()
        ? QStringLiteral(":: INCLUDE FILES ::")
        : QStringLiteral(":: INCLUDE FILES - %1 ::").arg(prefix);
    headerItem.type = CommandCompletion;
    headerItem.description = QStringLiteral("Workspace include files");
    headerItem.selectable = false;
    headerItem.score = 1000;
    fillDisplayMetadata(headerItem);
    completions.append(headerItem);

    CompletionService* completionService = CompletionService::getInstance();
    for (const QString& filePath : filePaths) {
        if (!prefix.isEmpty()
            && !filePath.contains(prefix, Qt::CaseInsensitive)
            && !completionService->matchesCompletionAbbreviation(filePath, prefix)) {
            continue;
        }

        CompletionItem item;
        item.text = filePath;
        item.type = CommandCompletion;
        item.description = QStringLiteral("include file");
        item.score = prefix.isEmpty()
            ? 1
            : completionService->completionItemScore(filePath, prefix);
        fillDisplayMetadata(item);
        completions.append(item);
    }

    if (completions.size() == 1) {
        CompletionItem emptyItem;
        emptyItem.text = QStringLiteral("No matching include files");
        emptyItem.type = CommandCompletion;
        emptyItem.description = QStringLiteral("No workspace files match");
        emptyItem.selectable = false;
        fillDisplayMetadata(emptyItem);
        completions.append(emptyItem);
    }

    sortCompletionsByScore();
    if (completions.size() > MaxCompletionItems)
        completions = completions.mid(0, MaxCompletionItems);
    if (completions.size() > 80)
        completions = completions.mid(0, 80);

    endResetModel();
}

void CompletionModel::updateIncludeNewHeaderCompletions(
    const QList<IncludeNewHeaderChoice>& choices,
    const QString& title)
{
    beginResetModel();
    completions.clear();

    CompletionItem headerItem;
    headerItem.text = title.isEmpty()
        ? QStringLiteral(":: INCLUDE NEW HEADER ::")
        : QStringLiteral(":: INCLUDE NEW HEADER - %1 ::").arg(title);
    headerItem.type = CommandCompletion;
    headerItem.description = QStringLiteral("Create workspace header");
    headerItem.selectable = false;
    headerItem.score = 1000;
    fillDisplayMetadata(headerItem);
    completions.append(headerItem);

    int score = 999;
    for (const IncludeNewHeaderChoice& choice : choices) {
        CompletionItem item;
        item.text = choice.text;
        item.type = CommandCompletion;
        item.description = choice.description;
        item.toolTipText = choice.previewText.isEmpty()
            ? choice.description
            : choice.previewText;
        item.score = score--;
        fillDisplayMetadata(item);
        if (!choice.previewText.isEmpty()) {
            const QString preview = visibleIncludeTemplatePreview(choice.previewText);
            item.displayText =
                QStringLiteral("%1 - %2\n%3")
                    .arg(choice.text,
                         choice.description,
                         preview);
            item.toolTipText = choice.previewText;
            item.rowHeight = 18 * (item.displayText.count(QLatin1Char('\n')) + 1)
                + 4;
        }
        completions.append(item);
    }

    if (choices.isEmpty()) {
        CompletionItem emptyItem;
        emptyItem.text = QStringLiteral("No include-new choices");
        emptyItem.type = CommandCompletion;
        emptyItem.description = QStringLiteral("Type -n followed by a file name");
        emptyItem.selectable = false;
        fillDisplayMetadata(emptyItem);
        completions.append(emptyItem);
    }

    endResetModel();
}

void CompletionModel::updateInlineCommandCompletions(
    const CommandModeCompletionState& state)
{
    beginResetModel();
    completions.clear();

    CompletionItem headerItem;
    if (state.helpRequested) {
        switch (state.intent) {
        case InlineCommandIntent::SemanticCompletion:
            headerItem.text = QStringLiteral(":: COMMAND HELP - ;cmd + Space ::");
            break;
        case InlineCommandIntent::CodeTemplate:
            headerItem.text = QStringLiteral(":: TEMPLATE HELP - ;;cmd + Space ::");
            break;
        case InlineCommandIntent::EditorAction:
            headerItem.text = QStringLiteral(":: ACTION HELP - ;:cmd ::");
            break;
        case InlineCommandIntent::HeaderInclude:
            headerItem.text = QStringLiteral(":: HEADER INCLUDE HELP - ;h ::");
            break;
        case InlineCommandIntent::PackageImport:
            headerItem.text = QStringLiteral(":: PACKAGE IMPORT HELP - ;pk ::");
            break;
        }
    } else {
        headerItem.text = state.headerText.isEmpty()
            ? QStringLiteral(":: INLINE COMMAND ::")
            : state.headerText;
    }
    headerItem.type = CommandCompletion;
    headerItem.description = QStringLiteral("Inline Command");
    headerItem.score = 1000;
    fillDisplayMetadata(headerItem);
    completions.append(headerItem);

    int score = 999;
    if (state.helpRequested) {
        for (const InlineCommandDescriptor& descriptor : state.helpDescriptors) {
            CompletionItem item;
            item.text = descriptor.label;
            item.type = CommandCompletion;
            item.description =
                QStringLiteral("%1 -> %2")
                    .arg(descriptor.description, descriptor.defaultValue);
            item.defaultValue = descriptor.prefix;
            item.score = score--;
            fillDisplayMetadata(item);
            completions.append(item);
        }
    } else {
        for (const CodeTemplateItem& templateItem : state.templateItems) {
            CompletionItem item;
            item.text = templateItem.label.isEmpty()
                ? templateItem.commandToken
                : templateItem.label;
            item.type = CommandCompletion;
            item.description = templateItem.description;
            item.defaultValue = templateItem.insertText.isEmpty()
                ? templateItem.defaultValue
                : templateItem.insertText;
            item.selectionStart = templateItem.selectionStart;
            item.selectionLength = templateItem.selectionLength;
            item.templateSlots = templateItem.templateSlots;
            item.score = score--;
            fillDisplayMetadata(item);
            completions.append(item);
        }
    }

    if (completions.size() == 1) {
        CompletionItem emptyItem;
        emptyItem.text = QStringLiteral("No matching commands");
        emptyItem.type = CommandCompletion;
        emptyItem.description = QStringLiteral("No inline command entries");
        emptyItem.selectable = false;
        fillDisplayMetadata(emptyItem);
        completions.append(emptyItem);
    }

    if (completions.size() > MaxCompletionItems)
        completions = completions.mid(0, MaxCompletionItems);

    endResetModel();
}

void CompletionModel::updateSymbolRecordCompletions(
    const QList<SemanticSymbolRecord> &records,
    const QString &prefix,
    CompletionCommandKind requestedKind,
    bool allowDefaultFallback)
{
    beginResetModel();
    completions.clear();

    CompletionService* completionService = CompletionService::getInstance();
    const CommandSymbolPresentation presentation =
        completionService->commandSymbolPresentation(requestedKind);

    CompletionItem descItem;
    descItem.text = QString(":: COMMAND MODE - %1 ::").arg(presentation.typeDescription);
    descItem.type = SymbolCompletion;
    descItem.description = "Command Mode";
    descItem.score = 1000;
    descItem.defaultValue = presentation.defaultValue;
    fillDisplayMetadata(descItem);
    completions.append(descItem);

    QSet<QString> addedItems;
    int realSymbolCount = 0;

    for (const SemanticSymbolRecord& record : records) {
        if (record.name == presentation.defaultValue) {
            continue;
        }

        const CommandSymbolCompletionItem serviceItem =
            completionService->commandSymbolCompletionItem(record, requestedKind, prefix);
        if (addedItems.contains(serviceItem.uniqueKey))
            continue;
        addedItems.insert(serviceItem.uniqueKey);

        CompletionItem item;
        item.type = SymbolCompletion;
        fillSymbolMetadataFromRecord(item, record);
        item.text = serviceItem.text;
        item.description = item.typeDisplayName.isEmpty()
            ? serviceItem.description
            : item.typeDisplayName;
        item.defaultValue = serviceItem.defaultValue;
        item.selectionStart = serviceItem.selectionStart;
        item.selectionLength = serviceItem.selectionLength;
        item.templateSlots = serviceItem.templateSlots;
        item.score = serviceItem.score;

        fillDisplayMetadata(item);
        completions.append(item);
        ++realSymbolCount;
    }

    if (realSymbolCount == 0 && allowDefaultFallback) {
        CompletionItem defaultItem;
        defaultItem.text =
            QString("[DEFAULT] %1").arg(presentation.defaultValue);
        defaultItem.type = SymbolCompletion;
        defaultItem.description =
            QString("Default %1 declaration")
                .arg(presentation.typeDescription.split(' ').value(0));
        defaultItem.defaultValue = presentation.defaultValue;
        defaultItem.score = 999;
        fillDisplayMetadata(defaultItem);
        completions.append(defaultItem);
    } else if (realSymbolCount == 0) {
        CompletionItem noMatchItem;
        noMatchItem.text = QStringLiteral("No matching symbols");
        noMatchItem.type = CommandCompletion;
        noMatchItem.description = prefix.isEmpty()
            ? QStringLiteral("No symbols are visible at the command anchor")
            : QStringLiteral("No symbols match \"%1\"").arg(prefix);
        noMatchItem.score = 999;
        fillDisplayMetadata(noMatchItem);
        completions.append(noMatchItem);
    }

    sortCompletionsByScore();
    if (completions.size() > MaxCompletionItems) {
        completions = completions.mid(0, MaxCompletionItems);
    }
    if (completions.size() > 32) {
        completions = completions.mid(0, 32);
    }
    const QList<CompletionAnalysisBandCount> bandCounts =
        completionBandCountsForVisibleSymbols(completions);
    if (bandCounts.size() > 1) {
        CompletionItem bandHeader;
        bandHeader.text = QStringLiteral(":: COMMAND SYMBOL BANDS - %1 ::")
                              .arg(completionBandSummaryText(bandCounts));
        bandHeader.type = SymbolCompletion;
        bandHeader.description =
            QStringLiteral("Command symbol analysis bands");
        bandHeader.score = 998;
        fillDisplayMetadata(bandHeader);
        const qsizetype insertIndex =
            std::min<qsizetype>(2, completions.size());
        completions.insert(insertIndex, bandHeader);
    }

    endResetModel();
}
