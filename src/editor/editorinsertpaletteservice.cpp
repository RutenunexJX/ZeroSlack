#include "editorinsertpaletteservice.h"

#include "codetemplatecontextanalyzer.h"
#include "codetemplateservice.h"
#include "completioncommandkindadapter.h"
#include "completionservice.h"
#include "packagetoolservice.h"
#include "usertemplateservice.h"

#include <QFileInfo>
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
    auto append = [&](const QList<SemanticSymbolRecord>& records,
                      CompletionCommandKind kind) {
        QList<SemanticSymbolRecord> rankedRecords = records;
        if (!parsed.filter.isEmpty()) {
            std::stable_sort(
                rankedRecords.begin(),
                rankedRecords.end(),
                [service, &parsed](const SemanticSymbolRecord& left,
                                   const SemanticSymbolRecord& right) {
                    return service->completionItemScore(
                               left.name, parsed.filter)
                        > service->completionItemScore(
                               right.name, parsed.filter);
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
            item.subtitle = completion.description;
            item.insertionText = completion.defaultValue.isEmpty()
                ? completion.text : completion.defaultValue;
            item.selectionStart = completion.selectionStart;
            item.selectionLength = completion.selectionLength;
            item.templateSlots = completion.templateSlots;
            item.replacementStart = context.replacementStart;
            item.replacementLength = context.replacementLength;
            item.sourceDocumentRevision = context.documentRevision;
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
            [service, &parsed](const GlobalControlItem& left,
                               const GlobalControlItem& right) {
                return service->completionItemScore(
                           left.title, parsed.filter)
                    > service->completionItemScore(
                           right.title, parsed.filter);
            });
    };

    const auto expectedEnumValues = [&]() {
        return service->findExpectedEnumValueRecords(
            context.expectedTypeIdentifier,
            context.moduleName,
            context.packageName,
            parsed.filter);
    };
    const auto structMembers = [&]() {
        return service->findStructMemberCompletionRecords(
            context.memberPath,
            context.moduleName,
            parsed.filter);
    };
    const QList<SemanticSymbolRecord> visibleRecords =
        parsed.explicitSelector
        ? service->findCommandCompletionSymbolRecords(
              completionQuery(context,
                              CompletionCommandKind::VisibleSymbol,
                              parsed.filter))
        : QList<SemanticSymbolRecord>();

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
            for (const SemanticSymbolRecord& record : visibleRecords) {
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
        || item.description.contains(needle, Qt::CaseInsensitive);
}

bool matchesPaletteItem(const QString& title,
                        const QString& subtitle,
                        const QString& filter)
{
    const QString needle = filter.trimmed();
    return needle.isEmpty()
        || title.contains(needle, Qt::CaseInsensitive)
        || subtitle.contains(needle, Qt::CaseInsensitive);
}

QString semanticFilter(QString text,
                       const QStringList& leadingWords)
{
    text = text.trimmed();
    for (const QString& leadingWord : leadingWords) {
        if (text.compare(leadingWord, Qt::CaseInsensitive) == 0)
            return {};
        if (text.startsWith(leadingWord + QLatin1Char(' '),
                            Qt::CaseInsensitive)) {
            return text.mid(leadingWord.size()).trimmed();
        }
    }
    return text;
}

void appendSemanticTemplates(
    QList<GlobalControlItem>* result,
    const GlobalControlQueryContext& context,
    const QString& text,
    CompletionCommandKind kind,
    GlobalControlItemOperation operation,
    const QStringList& leadingWords,
    const PackageImportSite* importSite = nullptr)
{
    if (!result || !context.editorAvailable || result->size() >= 120)
        return;

    CommandCompletionQuery query;
    query.prefix = semanticFilter(text, leadingWords);
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.packageName = context.packageName;
    query.documentText = context.documentText;
    query.cursorLine = context.cursorLine;
    query.cursorPosition = context.cursorPosition;
    query.commandKind = kind;

    for (const SemanticSymbolRecord& record :
         CompletionService::getInstance()
             ->findCommandCompletionSymbolRecords(query)) {
        if (importSite && !importSite->canImport(record.name))
            continue;
        const CommandSymbolCompletionItem completion =
            CompletionService::getInstance()->commandSymbolCompletionItem(
                record, kind, query.prefix);
        GlobalControlItem item;
        item.kind = GlobalControlItemKind::Template;
        item.operation = operation;
        item.id = QStringLiteral("semantic:%1:%2")
                      .arg(static_cast<int>(kind))
                      .arg(completion.uniqueKey);
        item.title = kind == CompletionCommandKind::Module
            ? QStringLiteral("Instantiate %1").arg(completion.text)
            : QStringLiteral("Import %1::*").arg(completion.text);
        item.subtitle = completion.description;
        item.insertionText = completion.defaultValue;
        item.selectionStart = completion.selectionStart;
        item.selectionLength = completion.selectionLength;
        item.templateSlots = completion.templateSlots;
        if (kind == CompletionCommandKind::Package) {
            item.parameters.insert(QStringLiteral("packageName"),
                                   record.name);
        }
        result->append(item);
        if (result->size() >= 120)
            return;
    }
}

void appendIncludeTemplates(QList<GlobalControlItem>* result,
                            const QString& text,
                            const GlobalControlQueryContext& context)
{
    if (!result || !context.editorAvailable || result->size() >= 120)
        return;
    const QString filter = semanticFilter(
        text,
        {QStringLiteral("include"), QStringLiteral("header")});
    for (const QString& includePath : context.includeFiles) {
        const QString title = QStringLiteral("Include %1").arg(includePath);
        if (!matchesPaletteItem(title,
                                QStringLiteral("header include"),
                                text)
            && !includePath.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        GlobalControlItem item;
        item.kind = GlobalControlItemKind::Template;
        item.operation = GlobalControlItemOperation::InsertHeaderInclude;
        item.id = QStringLiteral("include:%1").arg(includePath);
        item.title = title;
        item.subtitle = QStringLiteral("header include");
        item.parameters.insert(QStringLiteral("includePath"), includePath);
        result->append(item);
        if (result->size() >= 120)
            return;
    }

    const QString normalized = text.simplified();
    const QStringList prefixes{
        QStringLiteral("new header "),
        QStringLiteral("create header "),
        QStringLiteral("header new "),
    };
    QString requestedName;
    for (const QString& prefix : prefixes) {
        if (normalized.startsWith(prefix, Qt::CaseInsensitive)) {
            requestedName = normalized.mid(prefix.size()).trimmed();
            break;
        }
    }
    if (requestedName.isEmpty() || result->size() >= 120)
        return;
    if (QFileInfo(requestedName).suffix().isEmpty())
        requestedName.append(QStringLiteral(".svh"));

    GlobalControlItem item;
    item.kind = GlobalControlItemKind::Template;
    item.operation = GlobalControlItemOperation::CreateHeader;
    item.id = QStringLiteral("header:new:%1").arg(requestedName);
    item.title = QStringLiteral("Create %1").arg(requestedName);
    item.subtitle = QStringLiteral("Create header and insert include");
    item.parameters.insert(QStringLiteral("fileName"), requestedName);
    result->append(item);
}

QList<GlobalControlItem> templateItems(
    const QString& text,
    const GlobalControlQueryContext& context)
{
    const CodeTemplateSignalContext signalContext =
        context.editorAvailable
        ? CodeTemplateContextAnalyzer::analyze(context.documentText,
                                               context.cursorPosition)
        : CodeTemplateSignalContext();

    QList<CodeTemplateItem> templates;
    for (const CodeTemplateItem& catalogItem :
         CodeTemplateService::getInstance()->catalog()) {
        templates.append(
            CodeTemplateService::getInstance()->matchingTemplates(
                catalogItem.commandToken,
                QString(),
                signalContext));
    }
    templates.append(UserTemplateService::getInstance()->catalog());

    QList<GlobalControlItem> result;
    for (const CodeTemplateItem& templateItem : templates) {
        if (!matchesTemplate(templateItem, text))
            continue;
        GlobalControlItem item;
        item.kind = GlobalControlItemKind::Template;
        item.id = templateItem.commandToken;
        item.title = templateItem.label;
        item.subtitle = templateItem.description;
        item.actionId = templateItem.actionId;
        item.executionRoute = templateItem.executionRoute;
        item.insertionText = templateItem.insertText.isEmpty()
            ? templateItem.defaultValue : templateItem.insertText;
        item.selectionStart = templateItem.selectionStart;
        item.selectionLength = templateItem.selectionLength;
        item.templateSlots = templateItem.templateSlots;
        result.append(item);
        if (result.size() >= 120)
            break;
    }

    appendSemanticTemplates(&result,
                            context,
                            text,
                            CompletionCommandKind::Module,
                            GlobalControlItemOperation::InsertText,
                            {QStringLiteral("module"),
                             QStringLiteral("instantiate")});
    if (context.editorAvailable && context.cursorPosition >= 0) {
        const PackageImportSite importSite =
            PackageToolService::analyzePackageImportSite(
                context.documentText,
                context.cursorPosition,
                context.cursorPosition);
        if (importSite.valid) {
            appendSemanticTemplates(
                &result,
                context,
                text,
                CompletionCommandKind::Package,
                GlobalControlItemOperation::InsertPackageImport,
                {QStringLiteral("package"), QStringLiteral("import")},
                &importSite);
        }
    }
    appendIncludeTemplates(&result, text, context);
    return result;
}

} // namespace

QList<GlobalControlItem> EditorInsertPaletteService::query(
    GlobalControlCategory category,
    const QString& text,
    const GlobalControlQueryContext& context) const
{
    switch (category) {
    case GlobalControlCategory::Symbols:
        return symbolItems(text, context);
    case GlobalControlCategory::Templates:
        return templateItems(text, context);
    case GlobalControlCategory::Commands:
        return {};
    }
    return {};
}
