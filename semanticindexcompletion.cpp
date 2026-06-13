#include "semanticindex.h"

#include "completionservice.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {
bool commandSymbolTypeMatches(sym_list::sym_type_e symbolType,
                              sym_list::sym_type_e commandType,
                              const QString& dataType = QString())
{
    if (symbolType == commandType)
        return true;
    return commandType == sym_list::sym_enum
        && symbolType == sym_list::sym_typedef
        && dataType == QLatin1String("enum");
}

bool semanticCompletionNameMatches(const QString& name, const QString& prefix)
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

bool internalCompletionSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_reg
        || type == sym_list::sym_wire
        || type == sym_list::sym_logic
        || type == sym_list::sym_localparam
        || type == sym_list::sym_parameter;
}

bool globalCompletionSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_task
        || type == sym_list::sym_function
        || type == sym_list::sym_interface
        || type == sym_list::sym_package;
}

bool commandGlobalCompletionSymbolType(sym_list::sym_type_e type)
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

bool globalSymbolInfoType(sym_list::sym_type_e type)
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
        || type == sym_list::sym_packed_struct_var
        || type == sym_list::sym_unpacked_struct_var;
}

bool alwaysGlobalSymbolInfoType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct;
}

bool alwaysGlobalCommandSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_def_define;
}

void sortSymbolsByName(QList<sym_list::SymbolInfo>& symbols)
{
    std::sort(symbols.begin(), symbols.end(),
              [](const sym_list::SymbolInfo& left,
                 const sym_list::SymbolInfo& right) {
                  return QString::compare(left.symbolName,
                                          right.symbolName,
                                          Qt::CaseInsensitive) < 0;
              });
}

QStringList uniqueSortedSymbolNames(const QList<sym_list::SymbolInfo>& symbols)
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

int endModulePositionInContent(const QString& fileContent,
                               const sym_list::SymbolInfo& moduleSymbol)
{
    int searchStart = moduleSymbol.position;
    int moduleDepth = 0;
    bool foundModule = false;

    static const QRegularExpression moduleStartPattern(QStringLiteral("\\bmodule\\s+"));
    static const QRegularExpression moduleEndPattern(QStringLiteral("\\bendmodule\\b"));

    int pos = searchStart;
    while (pos < fileContent.length()) {
        const QRegularExpressionMatch startMatch = moduleStartPattern.match(fileContent, pos);
        const QRegularExpressionMatch endMatch = moduleEndPattern.match(fileContent, pos);
        const int nextModuleStart = startMatch.hasMatch() ? startMatch.capturedStart(0) : -1;
        const int nextModuleEnd = endMatch.hasMatch() ? endMatch.capturedStart(0) : -1;

        if (nextModuleStart != -1
            && (nextModuleEnd == -1 || nextModuleStart < nextModuleEnd)) {
            if (foundModule || nextModuleStart == moduleSymbol.position) {
                ++moduleDepth;
                foundModule = true;
            }
            pos = nextModuleStart + startMatch.capturedLength(0);
        } else if (nextModuleEnd != -1) {
            if (foundModule) {
                --moduleDepth;
                if (moduleDepth == 0)
                    return nextModuleEnd + endMatch.capturedLength(0);
            }
            pos = nextModuleEnd + endMatch.capturedLength(0);
        } else {
            break;
        }
    }

    return -1;
}

QString moduleNameAtPositionInContent(const QList<sym_list::SymbolInfo>& modules,
                                      int cursorPosition,
                                      const QString& fileContent)
{
    if (fileContent.isEmpty())
        return QString();

    int cursorLine = 0;
    int pos = 0;
    while (pos < cursorPosition && pos < fileContent.length()) {
        if (fileContent.at(pos) == QLatin1Char('\n'))
            ++cursorLine;
        ++pos;
    }

    for (const sym_list::SymbolInfo& module : modules) {
        if (cursorPosition < module.position)
            continue;
        if (!sym_list::isValidModuleName(module.symbolName))
            continue;

        if (module.endLine > 0) {
            if (cursorLine >= module.startLine && cursorLine <= module.endLine)
                return module.symbolName;
            continue;
        }

        const int moduleEndPosition = endModulePositionInContent(fileContent, module);
        if (moduleEndPosition >= 0 && cursorPosition < moduleEndPosition)
            return module.symbolName;
    }

    return QString();
}
}

QStringList SemanticIndex::findCompletions(const SemanticQueryContext& context) const
{
    CompletionQuery query;
    query.prefix = context.prefix;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.cursorLine = context.cursorLine;
    query.cursorPosition = context.cursorPosition;

    CompletionService completions(const_cast<SemanticIndex*>(this));
    if (!context.fileName.isEmpty() && context.cursorLine > 0) {
        return completions.findScopeCompletions(query);
    }
    return completions.findCompletions(query);
}

QList<sym_list::SymbolInfo> SemanticIndex::getModuleCompletionSymbols(
    const QString& moduleName,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    if (moduleName.isEmpty())
        return result;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.moduleScope != moduleName
            || !internalCompletionSymbolType(symbol.symbolType)
            || !semanticCompletionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    sortSymbolsByName(result);
    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getGlobalCompletionSymbols(
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!globalCompletionSymbolType(symbol.symbolType)
            || !semanticCompletionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    sortSymbolsByName(result);
    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getCommandCompletionSymbols(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    if (moduleName.isEmpty() && !commandGlobalCompletionSymbolType(symbolType))
        return result;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const bool useGlobalScope = moduleName.isEmpty()
            || alwaysGlobalCommandSymbolType(symbolType);
        const bool scopeMatches = useGlobalScope
            ? symbol.moduleScope.isEmpty()
            : symbol.moduleScope == moduleName;
        if (!scopeMatches
            || !commandSymbolTypeMatches(symbol.symbolType,
                                        symbolType,
                                        symbol.dataType)
            || !semanticCompletionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    sortSymbolsByName(result);
    return result;
}

QStringList SemanticIndex::getCompletionSymbolNames() const
{
    QSet<QString> uniqueNames;
    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!symbol.symbolName.isEmpty())
            uniqueNames.insert(symbol.symbolName);
    }

    QStringList names(uniqueNames.begin(), uniqueNames.end());
    names.sort(Qt::CaseInsensitive);
    return names;
}

QList<sym_list::SymbolInfo> SemanticIndex::getTypedCompletionSymbols(
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<int> seenIds;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    auto appendIfMatches = [&](const sym_list::SymbolInfo& symbol) {
        if (seenIds.contains(symbol.symbolId))
            return;
        if (!semanticCompletionNameMatches(symbol.symbolName, prefix))
            return;
        seenIds.insert(symbol.symbolId);
        result.append(symbol);
    };

    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.symbolType == symbolType)
            appendIfMatches(symbol);
    }

    if (symbolType == sym_list::sym_enum) {
        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (symbol.symbolType == sym_list::sym_typedef
                && symbol.dataType == QLatin1String("enum")) {
                appendIfMatches(symbol);
            }
        }
    }

    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getGlobalSymbolInfosByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    if (!globalSymbolInfoType(symbolType))
        return result;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!commandSymbolTypeMatches(symbol.symbolType,
                                      symbolType,
                                      symbol.dataType)
            || !semanticCompletionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const bool global = alwaysGlobalSymbolInfoType(symbolType)
            || symbol.moduleScope.isEmpty();
        if (global)
            result.append(symbol);
    }

    return result;
}

QString SemanticIndex::currentModuleAt(const QString& fileName, int cursorPosition) const
{
    if (fileName.isEmpty() || cursorPosition < 0)
        return QString();

    QList<sym_list::SymbolInfo> modules;
    const QList<sym_list::SymbolInfo> fileSymbols = getSymbols(fileName);
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType == sym_list::sym_module)
            modules.append(symbol);
    }

    if (modules.isEmpty())
        return QString();

    std::sort(modules.begin(), modules.end(),
              [](const sym_list::SymbolInfo& left,
                 const sym_list::SymbolInfo& right) {
                  return left.position < right.position;
              });

    QString content = getCachedFileContent(fileName);
    if (content.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            content = QString::fromUtf8(file.readAll());
    }

    return moduleNameAtPositionInContent(modules, cursorPosition, content);
}
