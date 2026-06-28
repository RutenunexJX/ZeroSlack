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

CompletionResult completionBandSummaryForVisibleSymbolCompletions(
    const QList<CompletionModel::CompletionItem>& items)
{
    CompletionResult result;
    for (const CompletionModel::CompletionItem& item : items) {
        if (!isVisibleSymbolCompletionBandItem(item))
            continue;

        CompletionResult::SemanticCompletionItem summaryItem;
        summaryItem.analysisBand = item.analysisBand;
        result.items.append(summaryItem);
    }
    return result;
}
}

void CompletionModel::updateCompletions(const CompletionResult &completion,
                                        const QString &prefix)
{
    beginResetModel();
    completions.clear();

    CompletionService* completionService = CompletionService::getInstance();
    for (const CompletionResult::SemanticCompletionItem &semanticItem : completion.items) {
        CompletionItem item;
        item.text = semanticItem.insertText.isEmpty()
            ? semanticItem.label
            : semanticItem.insertText;
        item.type = SymbolCompletion;
        item.symbolRecord = semanticItem.symbolRecord;
        item.symbolStableKey = item.symbolRecord.stableKey.isValid()
            ? item.symbolRecord.stableKey
            : semanticItem.symbolStableKey;
        item.score = completionService->completionItemScore(semanticItem.label, prefix);
        item.description = semanticItem.typeDisplayName;
        item.typeDisplayName = semanticItem.typeDisplayName;
        item.ownerScopeName = semanticItem.ownerScopeName;
        item.sourceRoleDisplayName = semanticItem.sourceRoleDisplayName;
        item.analysisBand = semanticItem.analysisBand;
        item.analysisBandDisplayName = semanticItem.analysisBandDisplayName;
        item.declarationKind = semanticItem.declarationKind;
        item.usageRole = semanticItem.usageRole;
        item.ownerScope = semanticItem.ownerScope;
        item.sourceRole = semanticItem.sourceRole;

        fillDisplayMetadata(item);
        completions.append(item);
    }

    sortCompletionsByScore();
    if (completions.size() > MaxCompletionItems) {
        completions = completions.mid(0, MaxCompletionItems);
    }
    if (completions.size() > 15) {
        completions = completions.mid(0, 15);
    }
    const CompletionResult visibleBandSummary =
        completionBandSummaryForVisibleSymbolCompletions(completions);
    if (visibleBandSummary.analysisBandGroupCount() > 1) {
        CompletionItem bandHeader;
        bandHeader.text = QStringLiteral(":: COMPLETION BANDS - %1 ::")
                              .arg(visibleBandSummary.analysisBandSummaryText());
        bandHeader.type = SymbolCompletion;
        bandHeader.description =
            QStringLiteral("Completion analysis bands");
        bandHeader.score = 1000;
        fillDisplayMetadata(bandHeader);
        completions.prepend(bandHeader);
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
    fillDisplayMetadata(headerItem);
    completions.append(headerItem);

    CompletionService* completionService = CompletionService::getInstance();
    int matchCount = 0;
    for (const QString &command : commands) {
        if (prefix.isEmpty() || command.startsWith(prefix, Qt::CaseInsensitive)) {
            CompletionItem item;
            item.text = command;
            item.type = CommandCompletion;
            item.description = QString("Execute %1 command").arg(command);
            item.score = completionService->completionItemScore(command, prefix);
            fillDisplayMetadata(item);
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
        fillDisplayMetadata(noMatchItem);
        completions.append(noMatchItem);
    }

    sortCompletionsByScore();
    if (completions.size() > MaxCompletionItems) {
        completions = completions.mid(0, MaxCompletionItems);
    }
    endResetModel();
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

void CompletionModel::updateCommandHelpCompletions(
    const QList<CommandModeCommand>& commands)
{
    beginResetModel();
    completions.clear();

    CompletionItem headerItem;
    headerItem.text = QStringLiteral(":: COMMAND HELP - ;cmd + Space ::");
    headerItem.type = CommandCompletion;
    headerItem.description = QStringLiteral("Select a command to start it");
    headerItem.score = 1000;
    fillDisplayMetadata(headerItem);
    completions.append(headerItem);

    int score = 999;
    for (const CommandModeCommand& command : commands) {
        CompletionItem item;
        item.text = command.prefix.trimmed();
        item.type = CommandCompletion;
        item.description =
            QStringLiteral("%1 -> %2").arg(command.description, command.defaultValue);
        item.defaultValue = command.prefix;
        item.score = score--;
        fillDisplayMetadata(item);
        completions.append(item);
    }

    if (completions.size() > MaxCompletionItems)
        completions = completions.mid(0, MaxCompletionItems);

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
    CompletionCommandKind requestedKind)
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

    CompletionItem defaultItem;
    defaultItem.text = QString("[DEFAULT] %1").arg(presentation.defaultValue);
    defaultItem.type = SymbolCompletion;
    defaultItem.description =
        QString("Default %1 declaration").arg(presentation.typeDescription.split(' ').value(0));
    defaultItem.defaultValue = presentation.defaultValue;
    defaultItem.score = 999;
    fillDisplayMetadata(defaultItem);
    completions.append(defaultItem);

    QSet<QString> addedItems;

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
    }

    sortCompletionsByScore();
    if (completions.size() > MaxCompletionItems) {
        completions = completions.mid(0, MaxCompletionItems);
    }
    if (completions.size() > 32) {
        completions = completions.mid(0, 32);
    }
    const CompletionResult bandSummary =
        completionBandSummaryForVisibleSymbolCompletions(completions);
    if (bandSummary.analysisBandGroupCount() > 1) {
        CompletionItem bandHeader;
        bandHeader.text = QStringLiteral(":: COMMAND SYMBOL BANDS - %1 ::")
                              .arg(bandSummary.analysisBandSummaryText());
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
