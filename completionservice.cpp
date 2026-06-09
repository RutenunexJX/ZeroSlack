#include "completionservice.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>

std::unique_ptr<CompletionService> CompletionService::instance = nullptr;

CompletionService* CompletionService::getInstance()
{
    if (!instance)
        instance = std::make_unique<CompletionService>();
    return instance.get();
}

CompletionService::CompletionService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

CompletionService::~CompletionService() = default;

void CompletionService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

QStringList CompletionService::findCompletions(const CompletionQuery& query) const
{
    if (!query.structTypeNameForMember.isEmpty()) {
        return completionNamesFromSymbols(findStructMemberSymbols(query));
    }

    if (query.prefix.isEmpty())
        return {};

    if (!query.moduleName.isEmpty())
        return completionNamesFromSymbols(findModuleCompletionSymbols(query));

    return completionNamesFromSymbols(findGlobalCompletionSymbols(query));
}

QList<sym_list::SymbolInfo> CompletionService::findCompletionSymbols(
    const CompletionQuery& query) const
{
    if (!query.structTypeNameForMember.isEmpty())
        return findStructMemberSymbols(query);

    if (query.prefix.isEmpty())
        return {};

    if (!query.moduleName.isEmpty())
        return findModuleCompletionSymbols(query);

    return findGlobalCompletionSymbols(query);
}

QStringList CompletionService::findCommandCompletions(const CommandCompletionQuery& query) const
{
    return completionNamesFromSymbols(findCommandSymbolsFromIndex(query));
}

QList<sym_list::SymbolInfo> CompletionService::findCommandCompletionSymbols(
    const CommandCompletionQuery& query) const
{
    const bool useSymbolInfoDirectly =
        query.symbolType == sym_list::sym_packed_struct_var
        || query.symbolType == sym_list::sym_unpacked_struct_var
        || query.symbolType == sym_list::sym_packed_struct
        || query.symbolType == sym_list::sym_unpacked_struct;

    if (useSymbolInfoDirectly) {
        if (query.moduleName.isEmpty())
            return {};

        semanticIndex()->refreshStructTypedefEnumForFile(query.fileName, query.documentText);

        return semanticIndex()->getModuleContextSymbolsByType(
            query.moduleName,
            query.fileName,
            query.symbolType,
            query.prefix);
    }

    return findCommandSymbolsFromIndex(query);
}

QString CompletionService::getStructTypeForVariable(const QString& variableName,
                                                    const QString& moduleName) const
{
    return semanticIndex()->getStructTypeForVariable(variableName, moduleName);
}

bool CompletionService::tryParseStructMemberContext(const QString& line,
                                                    QString& outVariableName,
                                                    QString& outMemberPrefix) const
{
    static const QRegularExpression memberPattern(
        QStringLiteral("([a-zA-Z_][a-zA-Z0-9_]*)\\.([a-zA-Z0-9_]*)\\s*$"));
    const QRegularExpressionMatch match = memberPattern.match(line);
    if (!match.hasMatch())
        return false;

    outVariableName = match.captured(1);
    outMemberPrefix = match.captured(2);
    return true;
}

SemanticIndex* CompletionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

QList<sym_list::SymbolInfo> CompletionService::findStructMemberSymbols(
    const CompletionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> members =
        semanticIndex()->getStructMembers(query.structTypeNameForMember);
    for (const sym_list::SymbolInfo& member : members) {
        if (!completionNameMatches(member.symbolName, query.prefix))
            continue;
        const QString key = member.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(member);
    }
    return result;
}

QList<sym_list::SymbolInfo> CompletionService::findModuleCompletionSymbols(
    const CompletionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    if (query.moduleName.isEmpty())
        return result;

    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.moduleScope != query.moduleName
            || !isInternalCompletionType(symbol.symbolType)
            || !completionNameMatches(symbol.symbolName, query.prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    std::sort(result.begin(), result.end(),
              [](const sym_list::SymbolInfo& left,
                 const sym_list::SymbolInfo& right) {
                  return QString::compare(left.symbolName,
                                          right.symbolName,
                                          Qt::CaseInsensitive) < 0;
              });
    return result;
}

QList<sym_list::SymbolInfo> CompletionService::findGlobalCompletionSymbols(
    const CompletionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!isGlobalCompletionType(symbol.symbolType)
            || !completionNameMatches(symbol.symbolName, query.prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    std::sort(result.begin(), result.end(),
              [](const sym_list::SymbolInfo& left,
                 const sym_list::SymbolInfo& right) {
                  return QString::compare(left.symbolName,
                                          right.symbolName,
                                          Qt::CaseInsensitive) < 0;
              });
    return result;
}

QList<sym_list::SymbolInfo> CompletionService::findCommandSymbolsFromIndex(
    const CommandCompletionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    if (query.moduleName.isEmpty()
        && !isCommandGlobalCompletionType(query.symbolType)) {
        return result;
    }

    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const bool scopeMatches = query.moduleName.isEmpty()
            ? symbol.moduleScope.isEmpty()
            : symbol.moduleScope == query.moduleName;
        if (!scopeMatches
            || !commandSymbolTypeMatches(symbol.symbolType,
                                        symbol.dataType,
                                        query.symbolType)
            || !completionNameMatches(symbol.symbolName, query.prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    std::sort(result.begin(), result.end(),
              [](const sym_list::SymbolInfo& left,
                 const sym_list::SymbolInfo& right) {
                  return QString::compare(left.symbolName,
                                          right.symbolName,
                                          Qt::CaseInsensitive) < 0;
              });
    return result;
}

QStringList CompletionService::completionNamesFromSymbols(
    const QList<sym_list::SymbolInfo>& symbols) const
{
    QStringList result;
    QSet<QString> seenNames;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol.symbolName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool CompletionService::completionNameMatches(const QString& name,
                                              const QString& prefix) const
{
    if (prefix.isEmpty())
        return true;
    if (name.isEmpty())
        return false;

    const QString lowerName = name.toLower();
    const QString lowerPrefix = prefix.toLower();
    if (lowerName.startsWith(lowerPrefix))
        return true;

    int namePos = 0;
    int prefixPos = 0;
    while (prefixPos < lowerPrefix.length() && namePos < lowerName.length()) {
        if (lowerPrefix.at(prefixPos) == lowerName.at(namePos))
            ++prefixPos;
        ++namePos;
    }
    return prefixPos == lowerPrefix.length();
}

bool CompletionService::isInternalCompletionType(sym_list::sym_type_e type) const
{
    return type == sym_list::sym_reg
        || type == sym_list::sym_wire
        || type == sym_list::sym_logic
        || type == sym_list::sym_localparam
        || type == sym_list::sym_parameter;
}

bool CompletionService::isGlobalCompletionType(sym_list::sym_type_e type) const
{
    return type == sym_list::sym_module
        || type == sym_list::sym_task
        || type == sym_list::sym_function
        || type == sym_list::sym_interface
        || type == sym_list::sym_package;
}

bool CompletionService::isCommandGlobalCompletionType(sym_list::sym_type_e type) const
{
    return type == sym_list::sym_module
        || type == sym_list::sym_task
        || type == sym_list::sym_function
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_typedef
        || type == sym_list::sym_def_define
        || type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_enum;
}

bool CompletionService::commandSymbolTypeMatches(
    sym_list::sym_type_e symbolType,
    const QString& dataType,
    sym_list::sym_type_e requestedType) const
{
    if (symbolType == requestedType)
        return true;
    return requestedType == sym_list::sym_enum
        && symbolType == sym_list::sym_typedef
        && dataType == QLatin1String("enum");
}
