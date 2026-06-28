#include "completionservice.h"

#include "codetemplateservice.h"
#include "completioncommandmode.h"
#include "completionsemanticquery.h"
#include "completionsymbolquery.h"
#include "inlinecommandmode.h"
#include "symboltaxonomy.h"
#include "usertemplateservice.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace {
bool isCommandSafePrefix(const QString& text)
{
    for (const QChar ch : text) {
        if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t'))
            return false;
    }
    return true;
}

bool isPositionInCommentOrString(const QString& line, int position)
{
    bool inString = false;
    bool inBlockComment = false;
    bool escaped = false;

    for (int i = 0; i < position && i < line.size(); ++i) {
        const QChar ch = line.at(i);
        const QChar next = (i + 1 < line.size()) ? line.at(i + 1) : QChar();

        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }

        if (inBlockComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('/'))
            return true;
        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            inBlockComment = true;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('"'))
            inString = true;
    }

    return inString || inBlockComment;
}

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
    InlineCommandMatch result;
    if (!service)
        return result;

    const QList<CodeTemplateItem> userTemplates = service->catalog();
    for (const CodeTemplateItem& item : userTemplates) {
        const QString commandToken = item.commandToken.trimmed();
        if (!commandToken.startsWith(QStringLiteral(";;"))
            || commandToken.size() <= 2) {
            continue;
        }

        const QString commandPrefix = commandToken + QLatin1Char(' ');
        const int prefixPosition = lineUpToCursor.lastIndexOf(commandPrefix);
        if (prefixPosition < 0)
            continue;
        if (!isCommandSafePrefix(lineUpToCursor.left(prefixPosition)))
            continue;
        if (isPositionInCommentOrString(lineUpToCursor, prefixPosition))
            continue;

        result.matched = true;
        result.intent = InlineCommandIntent::CodeTemplate;
        result.prefixPosition = prefixPosition;
        result.commandToken = commandToken;
        result.descriptor = userTemplateDescriptor(item);
        result.input = lineUpToCursor.mid(prefixPosition + commandPrefix.size());
        return result;
    }

    return result;
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

QList<CodeTemplateItem> matchingCodeTemplateItems(
    UserTemplateService* userTemplates,
    const QString& commandToken,
    const QString& seedText)
{
    QList<CodeTemplateItem> result =
        CodeTemplateService::getInstance()->matchingTemplates(
            commandToken,
            seedText);
    if (userTemplates)
        result.append(userTemplates->matchingTemplates(commandToken));
    return result;
}

QString moduleInstanceStem(const QString& moduleName)
{
    QString stem;
    for (const QChar ch : moduleName) {
        if (ch.isLetterOrNumber()
            || ch == QLatin1Char('_')
            || ch == QLatin1Char('$')) {
            stem.append(ch);
        } else if (!stem.endsWith(QLatin1Char('_'))) {
            stem.append(QLatin1Char('_'));
        }
    }

    while (stem.startsWith(QLatin1Char('_')))
        stem.remove(0, 1);
    while (stem.endsWith(QLatin1Char('_')))
        stem.chop(1);
    if (stem.isEmpty())
        stem = QStringLiteral("module");
    if (stem.at(0).isDigit())
        stem.prepend(QStringLiteral("module_"));
    return stem;
}

QString simpleModuleInstantiationText(const QString& moduleName)
{
    return QStringLiteral("%1 u_%2 (\n);")
        .arg(moduleName, moduleInstanceStem(moduleName));
}

QString normalizedInstantiationRecordFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool moduleMemberOrderLess(const SemanticSymbolRecord& left,
                           const SemanticSymbolRecord& right)
{
    if (left.location.startLine != right.location.startLine)
        return left.location.startLine < right.location.startLine;
    if (left.location.startColumn != right.location.startColumn)
        return left.location.startColumn < right.location.startColumn;
    if (left.location.position != right.location.position)
        return left.location.position < right.location.position;
    if (left.localHandle != right.localHandle)
        return left.localHandle < right.localHandle;
    return QString::compare(left.name, right.name, Qt::CaseInsensitive) < 0;
}

bool isModuleInstantiationParameterRecord(
    const SemanticSymbolRecord& record,
    const QString& moduleName)
{
    return record.owner.name == moduleName
        && record.declarationKind == SymbolTaxonomy::DeclarationKind::Parameter
        && record.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration
        && !record.name.isEmpty();
}

bool isModuleInstantiationPortRecord(
    const SemanticSymbolRecord& record,
    const QString& moduleName)
{
    return record.owner.name == moduleName
        && record.declarationKind == SymbolTaxonomy::DeclarationKind::Port
        && record.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration
        && !record.name.isEmpty();
}

QList<SemanticSymbolRecord> recordsForSelectedModule(
    SemanticIndex* semanticIndex,
    const SemanticSymbolRecord& moduleRecord)
{
    if (!semanticIndex || moduleRecord.name.isEmpty())
        return {};

    const QList<SemanticSymbolRecord> ownerRecords =
        semanticIndex->getSymbolRecordsByOwner(moduleRecord.name);
    const QString moduleFile =
        normalizedInstantiationRecordFileName(moduleRecord.location.fileName);
    if (moduleFile.isEmpty())
        return ownerRecords;

    QList<SemanticSymbolRecord> sameFileRecords;
    for (const SemanticSymbolRecord& record : ownerRecords) {
        if (normalizedInstantiationRecordFileName(record.location.fileName)
            == moduleFile) {
            sameFileRecords.append(record);
        }
    }
    return sameFileRecords.isEmpty() ? ownerRecords : sameFileRecords;
}

QList<SemanticSymbolRecord> uniqueOrderedModuleMembers(
    QList<SemanticSymbolRecord> records)
{
    std::stable_sort(records.begin(), records.end(), moduleMemberOrderLess);

    QList<SemanticSymbolRecord> result;
    QSet<QString> seenNames;
    for (const SemanticSymbolRecord& record : records) {
        const QString key = record.name.toCaseFolded();
        if (key.isEmpty() || seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(record);
    }
    return result;
}

CodeTemplateSlot instantiationSlot(const QString& name, int start, int length)
{
    CodeTemplateSlot slot;
    slot.name = name;
    slot.start = start;
    slot.length = length;
    return slot;
}

struct ModuleInstantiationTemplate {
    QString text;
    CodeTemplateSlotList slotMetadata;
};

ModuleInstantiationTemplate moduleInstantiationTemplateForRecord(
    SemanticIndex* semanticIndex,
    const SemanticSymbolRecord& moduleRecord)
{
    ModuleInstantiationTemplate result;
    const QString moduleName = moduleRecord.name;
    result.text = simpleModuleInstantiationText(moduleName);
    if (!semanticIndex || moduleName.isEmpty())
        return result;

    QList<SemanticSymbolRecord> parameters;
    QList<SemanticSymbolRecord> ports;
    const QList<SemanticSymbolRecord> memberRecords =
        recordsForSelectedModule(semanticIndex, moduleRecord);
    for (const SemanticSymbolRecord& record : memberRecords) {
        if (isModuleInstantiationParameterRecord(record, moduleName)) {
            parameters.append(record);
        } else if (isModuleInstantiationPortRecord(record, moduleName)) {
            ports.append(record);
        }
    }

    parameters = uniqueOrderedModuleMembers(parameters);
    ports = uniqueOrderedModuleMembers(ports);
    if (ports.isEmpty())
        return result;

    result.text.clear();
    result.slotMetadata.clear();
    const QString instanceName =
        QStringLiteral("u_%1").arg(moduleInstanceStem(moduleName));
    int instanceSlotStart = -1;

    result.text.append(moduleName);
    if (!parameters.isEmpty()) {
        result.text.append(QStringLiteral(" #(\n"));
        for (int i = 0; i < parameters.size(); ++i) {
            const QString name = parameters.at(i).name;
            result.text.append(QStringLiteral("    .%1(").arg(name));
            const int slotStart = result.text.size();
            result.text.append(name);
            result.slotMetadata.append(instantiationSlot(
                QStringLiteral("parameter:%1").arg(name),
                slotStart,
                name.size()));
            result.text.append(i + 1 == parameters.size()
                                   ? QStringLiteral(")\n")
                                   : QStringLiteral("),\n"));
        }
        result.text.append(QStringLiteral(") "));
    } else {
        result.text.append(QLatin1Char(' '));
    }

    instanceSlotStart = result.text.size();
    result.text.append(instanceName);
    result.text.append(QStringLiteral(" (\n"));
    for (int i = 0; i < ports.size(); ++i) {
        const QString name = ports.at(i).name;
        result.text.append(QStringLiteral("    .%1(").arg(name));
        const int slotStart = result.text.size();
        result.text.append(name);
        result.slotMetadata.append(instantiationSlot(
            QStringLiteral("port:%1").arg(name),
            slotStart,
            name.size()));
        result.text.append(i + 1 == ports.size()
                               ? QStringLiteral(")\n")
                               : QStringLiteral("),\n"));
    }
    result.text.append(QStringLiteral(");"));
    result.slotMetadata.prepend(instantiationSlot(QStringLiteral("instance"),
                                                  instanceSlotStart,
                                                  instanceName.size()));
    return result;
}
}

QList<CommandModeCommand> CompletionService::commandModeCommands() const
{
    return CompletionCommandMode::commands();
}

CommandModeMatch CompletionService::matchCommandMode(const QString& lineUpToCursor) const
{
    CommandModeMatch match =
        CompletionCommandMode::matchCommandMode(lineUpToCursor);
    if (match.matched)
        return match;

    return commandModeMatchFromInlineMatch(
        matchUserTemplateCommand(lineUpToCursor, userTemplateService()));
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
    const CommandModeInputState inputState =
        commandModeInputState(query.lineUpToCursor);

    CommandModeCompletionState state;
    if (!inputState.matched)
        return state;

    state.matched = true;
    state.helpRequested = inputState.helpRequested;
    state.exitRequested = inputState.exitRequested;
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

    if (state.exitRequested)
        return state;

    if (state.intent == InlineCommandIntent::CodeTemplate) {
        state.templateItems = matchingCodeTemplateItems(
            userTemplateService(),
            state.descriptor.label,
            state.completionPrefix);
        state.showCompletions = true;
        return state;
    }

    if (state.intent == InlineCommandIntent::EditorAction) {
        state.templateItems = matchingEditorActions(state.completionPrefix);
        state.showCompletions = true;
        return state;
    }

    CommandCompletionQuery completionQuery;
    completionQuery.prefix = state.completionPrefix;
    completionQuery.fileName = query.fileName;
    completionQuery.moduleName = query.moduleName;
    completionQuery.documentText = query.documentText;
    completionQuery.commandKind = state.command.kind;

    state.symbolRecords = findCommandCompletionSymbolRecords(completionQuery);
    if (state.symbolRecords.isEmpty()
        && CompletionCommandMode::requiresModuleContext(state.command.kind)
        && completionQuery.moduleName.isEmpty()) {
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
    return item;
}

QStringList CompletionService::findCommandCompletions(const CommandCompletionQuery& query) const
{
    return CompletionSymbolQuery::namesFromRecords(
        findCommandCompletionSymbolRecords(query));
}

QList<SemanticSymbolRecord> CompletionService::findCommandCompletionSymbolRecords(
    const CommandCompletionQuery& query) const
{
    return CompletionSemanticQuery::commandSymbolRecords(semanticIndex(), query);
}
