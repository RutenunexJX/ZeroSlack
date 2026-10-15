#include "moduleinstantiationtemplate.h"
#include "completionservice.h"

#include "codetemplatecontextanalyzer.h"
#include "codetemplateservice.h"
#include "completioncommandmode.h"
#include "completioncommandkindadapter.h"
#include "completionmatcher.h"
#include "completionsemanticquery.h"
#include "inlinecommandmode.h"
#include "packagetoolservice.h"
#include "symboltaxonomy.h"
#include "usertemplateservice.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace {
QString editorActionToken(const InlineCommandDescriptor& descriptor)
{
    return descriptor.label.startsWith(QStringLiteral(";:"))
        ? descriptor.label.mid(2)
        : descriptor.label;
}

QList<CodeTemplateItem> matchingEditorActions(const QString& prefix)
{
    QList<CodeTemplateItem> items;
    for (const InlineCommandDescriptor& descriptor :
         InlineCommandMode::descriptorsForIntent(InlineCommandIntent::EditorAction)) {
        const QString token = editorActionToken(descriptor);
        if (!prefix.isEmpty()
            && !token.startsWith(prefix, Qt::CaseInsensitive)) {
            continue;
        }

        CodeTemplateItem item;
        item.commandToken = descriptor.label;
        item.label = token;
        item.description = descriptor.description.isEmpty()
            ? QStringLiteral("reserved editor action")
            : descriptor.description;
        item.defaultValue = descriptor.defaultValue;
        item.insertText = descriptor.defaultValue;
        items.append(item);
    }
    return items;
}

InlineCommandDescriptor userTemplateDescriptor(
    const CodeTemplateItem& item)
{
    InlineCommandDescriptor descriptor;
    descriptor.prefix = item.commandToken + QLatin1Char(' ');
    descriptor.intent = InlineCommandIntent::CodeTemplate;
    descriptor.semanticKind = CompletionCommandKind::User;
    descriptor.label = item.commandToken;
    descriptor.description = item.description.isEmpty()
        ? QStringLiteral("user template")
        : item.description;
    descriptor.defaultValue = item.insertText.isEmpty()
        ? item.defaultValue
        : item.insertText;
    return descriptor;
}

InlineCommandMatch matchUserTemplateCommand(
    const QString& lineUpToCursor,
    UserTemplateService* service)
{
    if (!service)
        return {};

    QList<InlineCommandDescriptor> descriptors;
    const QList<CodeTemplateItem> userTemplates = service->catalog();
    for (const CodeTemplateItem& item : userTemplates) {
        const QString commandToken = item.commandToken.trimmed();
        if (!commandToken.startsWith(QStringLiteral(";;"))
            || commandToken.size() <= 2) {
            continue;
        }

        descriptors.append(userTemplateDescriptor(item));
    }

    return InlineCommandMode::matchAbbreviationBeforeCursor(lineUpToCursor,
                                                            descriptors);
}

CommandModeMatch commandModeMatchFromInlineMatch(
    const InlineCommandMatch& match)
{
    CommandModeMatch result;
    if (!match.matched)
        return result;

    result.matched = true;
    result.helpRequested = match.helpRequested;
    result.intent = match.intent;
    result.prefixPosition = match.prefixPosition;
    result.input = match.input;
    result.descriptor = match.descriptor;
    result.command = InlineCommandMode::toCommandModeCommand(match.descriptor);
    return result;
}

bool isBetterCommandModeMatch(const CommandModeMatch& candidate,
                              const CommandModeMatch& best)
{
    if (!candidate.matched)
        return false;
    if (!best.matched)
        return true;
    if (candidate.prefixPosition != best.prefixPosition)
        return candidate.prefixPosition > best.prefixPosition;
    if (candidate.descriptor.label.size() != best.descriptor.label.size())
        return candidate.descriptor.label.size() > best.descriptor.label.size();
    if (candidate.descriptor.prefix.size() != best.descriptor.prefix.size())
        return candidate.descriptor.prefix.size() > best.descriptor.prefix.size();
    if (candidate.intent != best.intent)
        return candidate.intent == InlineCommandIntent::CodeTemplate;
    return false;
}

QList<CodeTemplateItem> matchingCodeTemplateItems(
    UserTemplateService* userTemplates,
    const QString& commandToken,
    const QString& seedText,
    const QString& documentText,
    int cursorPosition,
    const QString& fileName)
{
    const CodeTemplateSignalContext signalContext =
        commandToken == QStringLiteral(";;af")
        ? CodeTemplateContextAnalyzer::analyze(documentText,
                                               cursorPosition, fileName)
        : CodeTemplateSignalContext();
    QList<CodeTemplateItem> result =
        CodeTemplateService::getInstance()->matchingTemplates(
            commandToken,
            seedText,
            signalContext);
    if (userTemplates)
        result.append(userTemplates->matchingTemplates(commandToken));
    return result;
}


}

QList<CommandModeCommand> CompletionService::commandModeCommands() const
{
    return CompletionCommandMode::commands();
}

CommandModeMatch CompletionService::matchCommandMode(const QString& lineUpToCursor) const
{
    CommandModeMatch result =
        CompletionCommandMode::matchCommandMode(lineUpToCursor);
    if (result.matched
        && InlineCommandMode::isPositionInCommentOrString(
            lineUpToCursor,
            result.prefixPosition)) {
        result = {};
    }

    const CommandModeMatch userTemplateMatch =
        commandModeMatchFromInlineMatch(
            matchUserTemplateCommand(lineUpToCursor, userTemplateService()));
    if (userTemplateMatch.matched
        && InlineCommandMode::isPositionInCommentOrString(
            lineUpToCursor,
            userTemplateMatch.prefixPosition)) {
        // The parser is intentionally lexical-context agnostic; the service
        // applies the editability guard before a match can become active.
    } else if (isBetterCommandModeMatch(userTemplateMatch, result)) {
        result = userTemplateMatch;
    }
    return result;
}

CommandModeInputState CompletionService::commandModeInputState(
    const QString& lineUpToCursor) const
{
    const CommandModeMatch match = matchCommandMode(lineUpToCursor);
    CommandModeInputState state;
    if (!match.matched)
        return state;

    state.matched = true;
    state.helpRequested = match.helpRequested;
    state.intent = match.intent;
    state.prefixPosition = match.prefixPosition;
    state.input = match.input;
    state.command = match.command;
    state.descriptor = match.descriptor;
    return state;
}

CommandModeCompletionState CompletionService::commandModeCompletionState(
    const CommandModeCompletionQuery& query) const
{
    CommandModeInputState inputState;
    if (query.hasExplicitMatch && query.explicitMatch.matched) {
        inputState.matched = true;
        inputState.helpRequested = query.explicitMatch.helpRequested;
        inputState.intent = query.explicitMatch.intent;
        inputState.prefixPosition = query.explicitMatch.prefixPosition;
        inputState.input = query.explicitMatch.input;
        inputState.command =
            InlineCommandMode::toCommandModeCommand(
                query.explicitMatch.descriptor);
        inputState.descriptor = query.explicitMatch.descriptor;
    } else {
        inputState = commandModeInputState(query.lineUpToCursor);
    }

    CommandModeCompletionState state;
    if (!inputState.matched)
        return state;

    state.matched = true;
    state.helpRequested = inputState.helpRequested;
    state.intent = inputState.intent;
    state.prefixPosition = inputState.prefixPosition;
    state.input = inputState.input;
    state.completionPrefix = inputState.input.trimmed();
    state.command = inputState.command;
    state.descriptor = inputState.descriptor;
    state.commandKind = inputState.command.kind;
    state.headerText = InlineCommandMode::headerText(inputState.descriptor);

    if (state.helpRequested) {
        state.helpDescriptors =
            InlineCommandMode::descriptorsForIntent(state.intent);
        if (state.intent == InlineCommandIntent::SemanticCompletion)
            state.helpCommands = commandModeCommands();
        state.showCompletions = true;
        return state;
    }

    if (state.intent == InlineCommandIntent::CodeTemplate) {
        state.templateItems = matchingCodeTemplateItems(
            userTemplateService(),
            state.descriptor.label,
            state.completionPrefix,
            query.documentText,
            query.cursorPosition, query.fileName);
        state.showCompletions = true;
        return state;
    }

    if (state.intent == InlineCommandIntent::EditorAction) {
        state.templateItems = matchingEditorActions(state.completionPrefix);
        state.showCompletions = true;
        return state;
    }

    if (state.intent == InlineCommandIntent::HeaderInclude) {
        state.showCompletions = true;
        return state;
    }

    CommandCompletionQuery completionQuery;
    completionQuery.prefix = state.completionPrefix;
    completionQuery.fileName = query.fileName;
    completionQuery.moduleName = query.moduleName;
    completionQuery.packageName = query.packageName;
    completionQuery.documentText = query.documentText;
    completionQuery.cursorLine = query.cursorLine;
    completionQuery.cursorPosition = query.cursorPosition;
    completionQuery.commandKind = state.command.kind;

    state.symbolRecords = findCommandCompletionSymbolRecords(completionQuery);
    if (state.intent == InlineCommandIntent::PackageImport
        && query.hasExplicitMatch
        && query.explicitMatch.matched
        && !query.documentText.isEmpty()
        && query.cursorPosition >= 0) {
        const int replacementLength =
            query.explicitMatch.endPosition
            - query.explicitMatch.prefixPosition;
        const int replacementStart = query.cursorPosition;
        const int replacementEnd =
            replacementStart + replacementLength;
        const PackageImportSite site =
            PackageToolService::analyzePackageImportSite(
                query.documentText,
                replacementStart,
                replacementEnd);
        if (!site.valid) {
            state.symbolRecords.clear();
            state.hidePopup = true;
            return state;
        }

        state.symbolRecords.erase(
            std::remove_if(
                state.symbolRecords.begin(),
                state.symbolRecords.end(),
                [&site](const SemanticSymbolRecord& record) {
                    return !site.canImport(record.name);
                }),
            state.symbolRecords.end());
    }
    if (state.symbolRecords.isEmpty()
        && CompletionCommandMode::requiresModuleContext(state.command.kind)
        && completionQuery.moduleName.isEmpty()
        && completionQuery.packageName.isEmpty()) {
        state.hidePopup = true;
        return state;
    }

    state.showCompletions = true;
    state.symbolStableKeys.reserve(state.symbolRecords.size());
    for (const SemanticSymbolRecord& record : state.symbolRecords) {
        state.symbolStableKeys.append(record.stableKey);
    }
    return state;
}

CompletionActivationState CompletionService::completionActivationState(
    const CompletionActivationQuery& query) const
{
    return CompletionCommandMode::activationState(query);
}

CompletionPopupKeyState CompletionService::completionPopupKeyState(
    const CompletionPopupKeyQuery& query) const
{
    return CompletionCommandMode::popupKeyState(query);
}

CommandSymbolPresentation CompletionService::commandSymbolPresentation(
    CompletionCommandKind kind) const
{
    return CompletionCommandMode::symbolPresentation(kind);
}

CommandSymbolCompletionItem CompletionService::commandSymbolCompletionItem(
    const SemanticSymbolRecord& record,
    CompletionCommandKind requestedKind,
    const QString& prefix) const
{
    CommandSymbolCompletionItem item =
        CompletionCommandMode::symbolCompletionItem(record, requestedKind, prefix);
    if (requestedKind == CompletionCommandKind::Module) {
        const ModuleInstantiationTemplate instantiation =
            moduleInstantiationTemplateForRecord(semanticIndex(), record);
        item.defaultValue = instantiation.text;
        item.templateSlots = instantiation.slotMetadata;
        if (!item.templateSlots.isEmpty()) {
            item.selectionStart = item.templateSlots.first().start;
            item.selectionLength = item.templateSlots.first().length;
        }
    }
    if ((record.declarationKind == SymbolTaxonomy::DeclarationKind::Task
         || record.declarationKind == SymbolTaxonomy::DeclarationKind::Function)
        && record.presentation.callableSignatureAvailable) {
        item.defaultValue = record.name + QLatin1Char('(');
        item.templateSlots.clear();
        for (const auto& argument : record.presentation.callableArguments) {
            if (!item.templateSlots.isEmpty()) item.defaultValue += QStringLiteral(", ");
            item.defaultValue += QLatin1Char('.') + argument.name + QLatin1Char('(');
            const QString value = argument.hasDefault ? QString() : argument.name;
            item.templateSlots.append({argument.name, int(item.defaultValue.size()), int(value.size()),
                                       int(item.templateSlots.size()) + 1, true});
            item.defaultValue += value + QLatin1Char(')');
        }
        const bool task = record.declarationKind == SymbolTaxonomy::DeclarationKind::Task;
        item.defaultValue += task ? QStringLiteral(");") : QStringLiteral(")");
        item.description = task ? QStringLiteral("task call") : QStringLiteral("function call → %1").arg(record.presentation.callableReturnType);
        if (!item.templateSlots.isEmpty()) {
            item.selectionStart = item.templateSlots.first().start;
            item.selectionLength = item.templateSlots.first().length;
        }
    }
    return item;
}

QList<SemanticSymbolRecord> CompletionService::findCommandCompletionSymbolRecords(
    const CommandCompletionQuery& query) const
{
    return CompletionSemanticQuery::commandSymbolRecords(semanticIndex(), query);
}


namespace {
QList<SemanticSymbolRecord> matchingMembers(const QList<SemanticSymbolRecord>& records,
                                           const QString& prefix)
{
    QList<SemanticSymbolRecord> result;
    QSet<QString> seen;
    for (const auto& record : records) {
        if (!prefix.isEmpty() && CompletionMatcher::completionItemScore(record.name, prefix) <= 0)
            continue;
        const auto key = symbolStableKeyText(record.stableKey);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        result.append(record);
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QList<SemanticSymbolRecord> visibleTypeMembers(SemanticIndex* semantic,
    const CommandCompletionQuery& query, SymbolTaxonomy::DeclarationKind kind)
{
    if (!semantic)
        return {};
    auto typeQuery = query;
    typeQuery.prefix.clear();
    const auto groups = CompletionSemanticQuery::commandSymbolRecordGroups(semantic, typeQuery,
        {CompletionCommandKind::Typedef, CompletionCommandKind::PackedStructType,
         CompletionCommandKind::UnpackedStructType, CompletionCommandKind::EnumType,
         CompletionCommandKind::VisibleSymbol});
    QList<SemanticSymbolRecord> result;
    for (const auto& group : groups)
        for (const auto& subject : group)
            result.append(semantic->getTypeMemberRecords(subject, kind));
    return matchingMembers(result, query.prefix);
}
}

QList<SemanticSymbolRecord> CompletionService::findStructMemberCompletionRecords(
    const QStringList& memberPath, const QString& moduleName, const QString& prefix,
    const SemanticQueryContext& context) const
{
    auto* semantic = semanticIndex();
    if (!semantic || memberPath.isEmpty())
        return {};
    auto query = context;
    if (query.moduleName.isEmpty())
        query.moduleName = moduleName;
    const auto root = semantic->resolveVisibleValueRecord(memberPath.first(), query);
    const auto subject = semantic->resolveMemberPath(root, memberPath.mid(1));
    return matchingMembers(semantic->getTypeMemberRecords(subject,
        SymbolTaxonomy::DeclarationKind::StructMember), prefix);
}

QList<SemanticSymbolRecord> CompletionService::findVisibleStructMemberRecords(
    const CommandCompletionQuery& query) const
{
    return visibleTypeMembers(semanticIndex(), query, SymbolTaxonomy::DeclarationKind::StructMember);
}

QList<SemanticSymbolRecord> CompletionService::findExpectedEnumValueRecords(
    const QString& identifier, const QString& moduleName, const QString& packageName,
    const QString& prefix, const SemanticQueryContext& context) const
{
    auto* semantic = semanticIndex();
    if (!semantic || identifier.isEmpty())
        return {};
    auto query = context;
    if (query.moduleName.isEmpty())
        query.moduleName = moduleName;
    if (query.packageName.isEmpty())
        query.packageName = packageName;
    const auto path = identifier.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (path.isEmpty())
        return {};
    const auto root = semantic->resolveVisibleValueRecord(path.first(), query);
    const auto subject = semantic->resolveMemberPath(root, path.mid(1));
    return matchingMembers(semantic->getTypeMemberRecords(subject,
        SymbolTaxonomy::DeclarationKind::Enum), prefix);
}

QList<SemanticSymbolRecord> CompletionService::findVisibleEnumValueRecords(
    const CommandCompletionQuery& query) const
{
    return visibleTypeMembers(semanticIndex(), query, SymbolTaxonomy::DeclarationKind::Enum);
}
