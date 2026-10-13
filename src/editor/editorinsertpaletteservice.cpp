#include "editorinsertpaletteservice.h"

#include "completioncommandkindadapter.h"
#include "completionservice.h"
#include "usertemplateservice.h"

#include <QHash>
#include <QSet>
#include <Qt>

#include <algorithm>

namespace {

struct ParsedSymbolQuery {
    bool explicitSelector = false;
    QString filter;
    QList<CompletionCommandKind> kinds;
};

ParsedSymbolQuery parseSymbolQuery(const QString& text)
{
    struct Selector {
        const char* token;
        QList<CompletionCommandKind> kinds;
    };
    const QList<Selector> selectors{
        {"lp", {CompletionCommandKind::Localparam}},
        {"td", {CompletionCommandKind::Typedef,
                 CompletionCommandKind::EnumType,
                 CompletionCommandKind::PackedStructType,
                 CompletionCommandKind::UnpackedStructType}},
        {"et", {CompletionCommandKind::EnumType}},
        {"ee", {CompletionCommandKind::EnumValue}},
        {"ev", {CompletionCommandKind::EnumVariable}},
        {"st", {CompletionCommandKind::PackedStructType,
                 CompletionCommandKind::UnpackedStructType}},
        {"sv", {CompletionCommandKind::PackedStructVariable,
                 CompletionCommandKind::UnpackedStructVariable}},
        {"sm", {CompletionCommandKind::StructMember}},
        {"w", {CompletionCommandKind::Wire}},
        {"r", {CompletionCommandKind::Reg}},
        {"l", {CompletionCommandKind::Logic}},
        {"p", {CompletionCommandKind::Parameter}},
        {"t", {CompletionCommandKind::Task}},
        {"f", {CompletionCommandKind::Function}},
        {"m", {CompletionCommandKind::Module}},
        {"e", {CompletionCommandKind::EnumValue,
                CompletionCommandKind::EnumType,
                CompletionCommandKind::EnumVariable}},
        {"s", {CompletionCommandKind::StructMember,
                CompletionCommandKind::PackedStructType,
                CompletionCommandKind::UnpackedStructType,
                CompletionCommandKind::PackedStructVariable,
                CompletionCommandKind::UnpackedStructVariable}},
    };

    for (const Selector& selector : selectors) {
        const QString token = QString::fromLatin1(selector.token);
        if (text.size() <= token.size()
            || text.left(token.size()).compare(
                   token, Qt::CaseInsensitive) != 0
            || !text.at(token.size()).isSpace()) {
            continue;
        }
        ParsedSymbolQuery parsed;
        parsed.explicitSelector = true;
        parsed.kinds = selector.kinds;
        parsed.filter = text.mid(token.size()).trimmed();
        return parsed;
    }

    ParsedSymbolQuery parsed;
    parsed.filter = text.trimmed();
    parsed.kinds = {CompletionCommandKind::VisibleSymbol,
                    CompletionCommandKind::EnumValue};
    return parsed;
}

CommandCompletionQuery completionQuery(
    const GlobalControlQueryContext& context,
    CompletionCommandKind kind,
    const QString& prefix)
{
    CommandCompletionQuery query;
    query.prefix = prefix;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.packageName = context.packageName;
    query.documentText = context.documentText;
    query.cursorLine = context.cursorLine;
    query.cursorPosition = context.cursorPosition;
    query.commandKind = kind;
    return query;
}

QList<GlobalControlItem> symbolItems(
    const QString& text,
    const GlobalControlQueryContext& context)
{
    if (!context.editorAvailable)
        return {};

    const ParsedSymbolQuery parsed = parseSymbolQuery(text);
    CompletionService* service = CompletionService::getInstance();
    QList<GlobalControlItem> result;
    QSet<QString> seen;
    // A query has one filter, but record names and prepared item titles are
    // separate score inputs. Cache by the exact text used by each sort.
    QHash<QString, int> scores;
    const auto scoreForText = [&](const QString& candidate) {
        const auto found = scores.constFind(candidate);
        if (found != scores.cend())
            return found.value();
        const int score = service->completionItemScore(candidate, parsed.filter);
        scores.insert(candidate, score);
        return score;
    };
    auto append = [&](const QList<SemanticSymbolRecord>& records,
                      CompletionCommandKind kind) {
        QList<SemanticSymbolRecord> rankedRecords = records;
        if (!parsed.filter.isEmpty()) {
            std::stable_sort(
                rankedRecords.begin(),
                rankedRecords.end(),
                [&scoreForText](const SemanticSymbolRecord& left,
                                const SemanticSymbolRecord& right) {
                    return scoreForText(left.name) > scoreForText(right.name);
                });
        }
        for (const SemanticSymbolRecord& record : rankedRecords) {
            const QString stable = record.stableKey.isValid()
                ? symbolStableKeyText(record.stableKey)
                : QStringLiteral("%1|%2|%3")
                      .arg(record.owner.name,
                           QString::number(
                               static_cast<int>(record.declarationKind)),
                           record.name);
            if (seen.contains(stable))
                continue;
            seen.insert(stable);
            const CommandSymbolCompletionItem completion =
                service->commandSymbolCompletionItem(
                    record, kind, parsed.filter);
            GlobalControlItem item;
            item.kind = GlobalControlItemKind::Symbol;
            item.id = stable;
            item.title = completion.text;
            item.typeBadge = SymbolTaxonomy::symbolTypeLabel({record.declarationKind, record.usageRole, record.owner.kind, record.visibility, record.sourceRole, record.collectorKind});
            item.subtitle = completion.description;
            item.insertionText = completion.defaultValue.isEmpty()
                ? completion.text : completion.defaultValue;
            item.selectionStart = completion.selectionStart;
            item.selectionLength = completion.selectionLength;
            item.templateSlots = completion.templateSlots;
            item.replacementStart = context.replacementStart;
            item.replacementLength = context.replacementLength;
            item.sourceDocumentRevision = context.documentRevision;
            item.sourceDocumentInstance = context.documentInstance;
            item.sourceFileName = context.fileName;
            item.sourceCursorPosition = context.cursorPosition;
            if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Task
                || record.declarationKind == SymbolTaxonomy::DeclarationKind::Function) {
                item.requiresCurrentSemantic = true;
                item.sourceSemanticSnapshot = SemanticIndex::getInstance()->snapshotToken();
                if (SemanticIndex::getInstance()->getCachedFileContent(context.fileName) != context.documentText
                    || !record.presentation.callableSignatureAvailable)
                    item.disabledReason = QStringLiteral("Wait for the current task/function Slang analysis.");
            }
            result.append(item);
            if (result.size() >= 120)
                return;
        }
    };
    const auto rankMatches = [&]() {
        if (parsed.filter.isEmpty())
            return;
        std::stable_sort(
            result.begin(),
            result.end(),
            [&scoreForText](const GlobalControlItem& left,
                            const GlobalControlItem& right) {
                return scoreForText(left.title) > scoreForText(right.title);
            });
    };

    SemanticQueryContext semanticContext;
    semanticContext.fileName = context.fileName;
    semanticContext.moduleName = context.moduleName;
    semanticContext.packageName = context.packageName;
    semanticContext.cursorLine = context.cursorLine;
    semanticContext.cursorPosition = context.cursorPosition;
    const auto expectedEnumValues = [&]() {
        return service->findExpectedEnumValueRecords(
            context.expectedTypeIdentifier,
            context.moduleName,
            context.packageName,
            parsed.filter, semanticContext);
    };
    const auto structMembers = [&]() {
        return service->findStructMemberCompletionRecords(
            context.memberPath,
            context.moduleName,
            parsed.filter, semanticContext);
    };
    QList<SemanticSymbolRecord> visibleRecords;
    bool visibleRecordsLoaded = false;
    const auto visibleSymbols = [&]() -> const QList<SemanticSymbolRecord>& {
        if (!visibleRecordsLoaded) {
            visibleRecords = service->findCommandCompletionSymbolRecords(
                completionQuery(context,
                                CompletionCommandKind::VisibleSymbol,
                                parsed.filter));
            visibleRecordsLoaded = true;
        }
        return visibleRecords;
    };

    if (!parsed.explicitSelector && context.memberAccess) {
        const QList<SemanticSymbolRecord> members = structMembers();
        if (!members.isEmpty()) {
            append(members, CompletionCommandKind::StructMember);
            rankMatches();
            return result;
        }
    }
    if (!parsed.explicitSelector
        && !context.expectedTypeIdentifier.isEmpty()) {
        append(expectedEnumValues(), CompletionCommandKind::EnumValue);
    }

    for (const CompletionCommandKind kind : parsed.kinds) {
        if (result.size() >= 120)
            break;
        if (kind == CompletionCommandKind::StructMember) {
            if (context.memberAccess)
                append(structMembers(), kind);
            else
                append(service->findVisibleStructMemberRecords(
                           completionQuery(context, kind, parsed.filter)),
                       kind);
            continue;
        }
        if (kind == CompletionCommandKind::EnumValue) {
            append(expectedEnumValues(), kind);
            append(service->findVisibleEnumValueRecords(
                       completionQuery(context, kind, parsed.filter)),
                   kind);
            continue;
        }
        if (parsed.explicitSelector) {
            if (kind == CompletionCommandKind::Module) {
                append(service->findCommandCompletionSymbolRecords(
                           completionQuery(context,
                                           kind,
                                           parsed.filter)),
                       kind);
                continue;
            }
            QList<SemanticSymbolRecord> matchingRecords;
            for (const SemanticSymbolRecord& record : visibleSymbols()) {
                if (completionCommandKindMatchesCommandRecord(record, kind))
                    matchingRecords.append(record);
            }
            append(matchingRecords, kind);
            continue;
        }
        append(service->findCommandCompletionSymbolRecords(
                   completionQuery(context, kind, parsed.filter)),
               kind);
    }
    rankMatches();
    return result;
}

bool matchesTemplate(const CodeTemplateItem& item,
                     const QString& filter)
{
    const QString needle = filter.trimmed();
    return needle.isEmpty()
        || item.label.contains(needle, Qt::CaseInsensitive)
        || item.commandToken.contains(needle, Qt::CaseInsensitive)
        || item.description.contains(needle, Qt::CaseInsensitive)
        || item.command.value(QStringLiteral("symbol")).toString().contains(needle, Qt::CaseInsensitive)
        || item.command.value(QStringLiteral("aliases")).toStringList().join(QLatin1Char(' ')).contains(needle, Qt::CaseInsensitive);
}

QList<GlobalControlItem> simulationItems(
    const QString& text, const GlobalControlQueryContext& context)
{
    if (!context.editorAvailable)
        return {};
    auto* service = UserTemplateService::getInstance();
    const auto templates = service->catalogForFile(context.fileName, true);
    const auto catalogRevision = service->catalogRevision();
    QList<GlobalControlItem> result;
    for (const auto& templateItem : templates) {
        if (!matchesTemplate(templateItem, text))
            continue;
        GlobalControlItem item;
        item.kind = GlobalControlItemKind::Template;
        item.operation = GlobalControlItemOperation::InsertSimulationCommand;
        item.id = templateItem.commandToken;
        item.title = templateItem.label;
        item.subtitle = templateItem.description;
        item.insertionText = templateItem.insertText.isEmpty()
            ? templateItem.defaultValue : templateItem.insertText;
        item.selectionStart = templateItem.selectionStart;
        item.selectionLength = templateItem.selectionLength;
        item.templateSlots = templateItem.templateSlots;
        item.parameters = templateItem.command;
        item.parameterSummary = templateItem.command.value(QStringLiteral("summary")).toString();
        if (item.parameterSummary.isEmpty()) {
            QStringList names;
            for (const auto& slot : item.templateSlots)
                if (!names.contains(slot.name)) names << slot.name;
            item.parameterSummary = names.mid(0, 3).join(QStringLiteral(" · "));
        }
        item.replacementStart = context.replacementStart;
        item.replacementLength = context.replacementLength;
        item.sourceDocumentRevision = context.documentRevision;
        item.sourceDocumentInstance = context.documentInstance;
        item.sourceDocumentTextRevision = context.documentTextRevision;
        item.sourceFileName = context.fileName;
        item.sourceCursorPosition = context.cursorPosition;
        item.sourceTemplateCatalogRevision = catalogRevision;
        if (context.readOnly)
            item.disabledReason = QStringLiteral("This document is read-only.");
        result.append(item);
        if (result.size() >= 120)
            break;
    }
    return result;
}

} // namespace

QList<GlobalControlItem> EditorInsertPaletteService::query(
    GlobalControlCategory category,
    const QString& text,
    const GlobalControlQueryContext& context) const
{
    if (context.simulationCommands || category == GlobalControlCategory::Templates)
        return simulationItems(text, context);
    return category == GlobalControlCategory::Symbols ? symbolItems(text, context)
                                                      : QList<GlobalControlItem>();
}
