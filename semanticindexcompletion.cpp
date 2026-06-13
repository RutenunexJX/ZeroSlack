#include "semanticindex.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <limits>

namespace {
QString normalizedCompletionFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

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

bool isModuleRangeSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_packed_struct_var
        || type == sym_list::sym_unpacked_struct_var;
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

QStringList SemanticIndex::getEnumValueCompletionNames(
    const QString& prefix,
    const QString& enumTypeName) const
{
    QList<sym_list::SymbolInfo> result;
    const QList<sym_list::SymbolInfo> symbols =
        getSymbolsByType(sym_list::sym_enum_value);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!enumTypeName.isEmpty() && symbol.moduleScope != enumTypeName)
            continue;
        if (!semanticCompletionNameMatches(symbol.symbolName, prefix))
            continue;
        result.append(symbol);
    }
    return uniqueSortedSymbolNames(result);
}

QString SemanticIndex::enumTypeForVariable(
    const QString& variableName,
    const QString& moduleName) const
{
    if (!moduleName.isEmpty()) {
        const QList<sym_list::SymbolInfo> moduleSymbols =
            getModuleInternalSymbolsByType(moduleName, sym_list::sym_enum_var);
        for (const sym_list::SymbolInfo& symbol : moduleSymbols) {
            if (symbol.symbolName == variableName)
                return symbol.moduleScope;
        }
    }

    const QList<sym_list::SymbolInfo> symbols =
        getSymbolsByType(sym_list::sym_enum_var);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.symbolName == variableName)
            return symbol.moduleScope;
    }

    return QString();
}

QStringList SemanticIndex::getModulePortCompletionNames(
    const QString& prefix,
    const QString& moduleTypeName) const
{
    if (moduleTypeName.isEmpty())
        return {};

    bool moduleExists = false;
    const QList<sym_list::SymbolInfo> modules =
        getSymbolsByType(sym_list::sym_module);
    for (const sym_list::SymbolInfo& symbol : modules) {
        if (symbol.symbolName == moduleTypeName) {
            moduleExists = true;
            break;
        }
    }
    if (!moduleExists)
        return {};

    QList<sym_list::SymbolInfo> portSymbols;
    portSymbols.append(getCommandCompletionSymbols(moduleTypeName,
                                                   sym_list::sym_wire,
                                                   prefix));
    portSymbols.append(getCommandCompletionSymbols(moduleTypeName,
                                                   sym_list::sym_reg,
                                                   prefix));
    portSymbols.append(getCommandCompletionSymbols(moduleTypeName,
                                                   sym_list::sym_logic,
                                                   prefix));
    return uniqueSortedSymbolNames(portSymbols);
}

QStringList SemanticIndex::getRelationshipCompletionNames(
    const QString& symbolName,
    const QList<SymbolRelationshipEngine::RelationType>& types,
    bool outgoing,
    const QString& prefix) const
{
    if (symbolName.isEmpty())
        return {};

    const int symbolId = findSymbolId(symbolName);
    if (symbolId < 0)
        return {};

    QList<sym_list::SymbolInfo> symbols;
    const QList<SemanticRelationship> relationships = getRelationships(symbolId, outgoing);
    for (const SemanticRelationship& relationship : relationships) {
        if (!types.isEmpty() && !types.contains(relationship.type))
            continue;

        const int peerId = outgoing ? relationship.toId : relationship.fromId;
        const sym_list::SymbolInfo symbol = getSymbolById(peerId);
        if (symbol.symbolId < 0)
            continue;
        if (!semanticCompletionNameMatches(symbol.symbolName, prefix))
            continue;
        symbols.append(symbol);
    }

    return uniqueSortedSymbolNames(symbols);
}

QStringList SemanticIndex::getBidirectionalRelationshipCompletionNames(
    const QString& symbolName,
    const QList<SymbolRelationshipEngine::RelationType>& types,
    const QString& prefix) const
{
    QStringList result = getRelationshipCompletionNames(symbolName, types, true, prefix);
    result.append(getRelationshipCompletionNames(symbolName, types, false, prefix));
    result.removeDuplicates();
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList SemanticIndex::getSymbolsWithOutgoingRelationshipCompletionNames(
    SymbolRelationshipEngine::RelationType type,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!semanticCompletionNameMatches(symbol.symbolName, prefix))
            continue;

        const QList<SemanticRelationship> relationships =
            getRelationships(symbol.symbolId, true);
        for (const SemanticRelationship& relationship : relationships) {
            if (relationship.type == type) {
                result.append(symbol);
                break;
            }
        }
    }

    return uniqueSortedSymbolNames(result);
}

QString SemanticIndex::getStructTypeForVariable(const QString& variableName,
                                                const QString& moduleName) const
{
    if (variableName.isEmpty())
        return QString();

    QList<sym_list::SymbolInfo> structVariables =
        getSymbolsByType(sym_list::sym_packed_struct_var);
    structVariables.append(getSymbolsByType(sym_list::sym_unpacked_struct_var));

    if (!moduleName.isEmpty()) {
        for (const sym_list::SymbolInfo& symbol : std::as_const(structVariables)) {
            if (symbol.symbolName == variableName
                && symbol.moduleScope == moduleName
                && !symbol.dataType.isEmpty()) {
                return symbol.dataType;
            }
        }
    }

    for (const sym_list::SymbolInfo& symbol : std::as_const(structVariables)) {
        if (symbol.symbolName == variableName && !symbol.dataType.isEmpty())
            return symbol.dataType;
    }

    return QString();
}

QList<sym_list::SymbolInfo> SemanticIndex::getStructMembers(
    const QString& structTypeName) const
{
    QList<sym_list::SymbolInfo> result;
    const QList<sym_list::SymbolInfo> members =
        getSymbolsByType(sym_list::sym_struct_member);
    for (const sym_list::SymbolInfo& symbol : members) {
        if (!structTypeName.isEmpty() && symbol.moduleScope != structTypeName)
            continue;
        result.append(symbol);
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const sym_list::SymbolInfo& a,
                        const sym_list::SymbolInfo& b) {
        const int nameCompare = QString::compare(a.symbolName,
                                                 b.symbolName,
                                                 Qt::CaseInsensitive);
        if (nameCompare != 0)
            return nameCompare < 0;
        if (a.fileName != b.fileName)
            return a.fileName < b.fileName;
        if (a.startLine != b.startLine)
            return a.startLine < b.startLine;
        return a.symbolId < b.symbolId;
    });
    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getModuleInternalSymbolsByType(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    bool useRelationshipFallback) const
{
    QList<sym_list::SymbolInfo> result;
    if (moduleName.isEmpty())
        return result;

    const QList<sym_list::SymbolInfo> allSymbols = getSymbols();
    sym_list::SymbolInfo moduleSymbol;
    bool foundModule = false;
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (symbol.symbolType == sym_list::sym_module
            && symbol.symbolName == moduleName) {
            moduleSymbol = symbol;
            foundModule = true;
            break;
        }
    }

    int moduleEndLineExclusive = std::numeric_limits<int>::max();
    if (foundModule) {
        QList<sym_list::SymbolInfo> fileModules;
        const QList<sym_list::SymbolInfo> fileSymbols = getSymbols(moduleSymbol.fileName);
        for (const sym_list::SymbolInfo& symbol : fileSymbols) {
            if (symbol.symbolType == sym_list::sym_module
                && symbol.fileName == moduleSymbol.fileName) {
                fileModules.append(symbol);
            }
        }
        std::sort(fileModules.begin(), fileModules.end(),
                  [](const sym_list::SymbolInfo& left,
                     const sym_list::SymbolInfo& right) {
                      return left.startLine < right.startLine;
                  });
        for (int i = 0; i < fileModules.size(); ++i) {
            if (fileModules.at(i).symbolId == moduleSymbol.symbolId
                && i + 1 < fileModules.size()) {
                moduleEndLineExclusive = fileModules.at(i + 1).startLine;
                break;
            }
        }
    }

    auto appendIfMatches = [&](const sym_list::SymbolInfo& symbol, bool fuzzyPrefix) {
        if (!commandSymbolTypeMatches(symbol.symbolType, symbolType, symbol.dataType))
            return;
        const bool nameMatches = fuzzyPrefix
            ? semanticCompletionNameMatches(symbol.symbolName, prefix)
            : (prefix.isEmpty()
               || symbol.symbolName.startsWith(prefix, Qt::CaseInsensitive));
        if (nameMatches)
            result.append(symbol);
    };

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        bool correctModule = false;
        if (isModuleRangeSymbolType(symbolType)) {
            correctModule = foundModule
                && symbol.fileName == moduleSymbol.fileName
                && symbol.startLine > moduleSymbol.startLine
                && symbol.startLine < moduleEndLineExclusive;
        } else {
            correctModule = symbol.moduleScope == moduleName;
        }

        if (correctModule)
            appendIfMatches(symbol, true);
    }

    if (useRelationshipFallback && result.isEmpty()) {
        const int moduleId = findSymbolId(moduleName);
        const QList<SemanticRelationship> relationships = getRelationships(moduleId, true);
        for (const SemanticRelationship& relationship : relationships) {
            if (relationship.type != SymbolRelationshipEngine::CONTAINS)
                continue;
            const sym_list::SymbolInfo symbol = getSymbolById(relationship.toId);
            if (symbol.symbolId >= 0)
                appendIfMatches(symbol, false);
        }
    }

    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getModuleContextSymbolsByType(
    const QString& moduleName,
    const QString& fileName,
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    if (moduleName.isEmpty() || fileName.isEmpty())
        return result;

    const QString normalizedTargetFile = normalizedCompletionFileName(fileName);
    const QList<sym_list::SymbolInfo> fileSymbols = getSymbols(fileName);
    sym_list::SymbolInfo moduleSymbol;
    bool foundModule = false;
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType == sym_list::sym_module
            && symbol.symbolName == moduleName
            && normalizedCompletionFileName(symbol.fileName) == normalizedTargetFile) {
            moduleSymbol = symbol;
            foundModule = true;
            break;
        }
    }
    if (!foundModule)
        return result;

    int moduleEndLineExclusive = std::numeric_limits<int>::max();
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType != sym_list::sym_module)
            continue;
        if (symbol.symbolId == moduleSymbol.symbolId)
            continue;
        if (symbol.startLine > moduleSymbol.startLine
            && symbol.startLine < moduleEndLineExclusive) {
            moduleEndLineExclusive = symbol.startLine;
        }
    }

    auto inModuleRange = [&moduleSymbol, moduleEndLineExclusive](
                             const sym_list::SymbolInfo& symbol) {
        return symbol.fileName == moduleSymbol.fileName
            && symbol.startLine > moduleSymbol.startLine
            && symbol.startLine < moduleEndLineExclusive;
    };

    QSet<int> seenIds;
    auto appendSymbol = [&](const sym_list::SymbolInfo& symbol) {
        if (!commandSymbolTypeMatches(symbol.symbolType, symbolType, symbol.dataType))
            return;
        if (!semanticCompletionNameMatches(symbol.symbolName, prefix))
            return;
        if (seenIds.contains(symbol.symbolId))
            return;
        seenIds.insert(symbol.symbolId);
        result.append(symbol);
    };

    const QList<sym_list::SymbolInfo> allSymbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        bool isCorrectModule = false;
        if (symbolType == sym_list::sym_packed_struct
            || symbolType == sym_list::sym_unpacked_struct
            || symbolType == sym_list::sym_packed_struct_var
            || symbolType == sym_list::sym_unpacked_struct_var) {
            isCorrectModule = inModuleRange(symbol);
        } else {
            isCorrectModule = symbol.moduleScope == moduleName;
        }
        if (isCorrectModule)
            appendSymbol(symbol);
    }

    QString fileContent = getCachedFileContent(fileName);
    if (fileContent.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            fileContent = QString::fromUtf8(file.readAll());
    }

    if (!fileContent.isEmpty()) {
        const QString baseDir = QFileInfo(fileName).absolutePath();
        static const QRegularExpression includeRegex(
            QStringLiteral("`include\\s+\"([^\"]+)\""));
        static const QRegularExpression importStarRegex(
            QStringLiteral("import\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*::\\s*\\*\\s*;"));
        static const QRegularExpression importSymbolRegex(
            QStringLiteral("import\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*::\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*;"));

        QSet<QString> starPackages;
        QHash<QString, QSet<QString>> importedSymbolsByPackage;
        const QStringList lines = fileContent.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            const int lineNumber = i + 1;
            if (lineNumber < moduleSymbol.startLine
                || lineNumber >= moduleEndLineExclusive) {
                continue;
            }

            const QString line = lines.at(i);
            const QRegularExpressionMatch includeMatch = includeRegex.match(line);
            if (includeMatch.hasMatch()) {
                const QString includePath = includeMatch.captured(1).trimmed();
                const QString absoluteIncludePath =
                    QDir(baseDir).absoluteFilePath(includePath);
                const QList<sym_list::SymbolInfo> includeSymbols =
                    getSymbols(absoluteIncludePath);
                for (const sym_list::SymbolInfo& symbol : includeSymbols)
                    appendSymbol(symbol);
            }

            const QRegularExpressionMatch starMatch = importStarRegex.match(line);
            if (starMatch.hasMatch()) {
                starPackages.insert(starMatch.captured(1).trimmed());
                continue;
            }

            const QRegularExpressionMatch symbolMatch = importSymbolRegex.match(line);
            if (symbolMatch.hasMatch()) {
                importedSymbolsByPackage[symbolMatch.captured(1).trimmed()].insert(
                    symbolMatch.captured(2).trimmed());
            }
        }

        for (const sym_list::SymbolInfo& symbol : allSymbols) {
            bool imported = starPackages.contains(symbol.moduleScope);
            if (!imported) {
                auto it = importedSymbolsByPackage.constFind(symbol.moduleScope);
                imported = it != importedSymbolsByPackage.constEnd()
                    && it->contains(symbol.symbolName);
            }
            if (imported)
                appendSymbol(symbol);
        }
    }

    if (result.isEmpty()) {
        const int moduleId = findSymbolId(moduleName);
        const QList<SemanticRelationship> relationships =
            getRelationships(moduleId, true);
        for (const SemanticRelationship& relationship : relationships) {
            if (relationship.type != SymbolRelationshipEngine::CONTAINS)
                continue;
            appendSymbol(getSymbolById(relationship.toId));
        }
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const sym_list::SymbolInfo& a,
                        const sym_list::SymbolInfo& b) {
        const int nameCompare = QString::compare(a.symbolName,
                                                 b.symbolName,
                                                 Qt::CaseInsensitive);
        if (nameCompare != 0)
            return nameCompare < 0;
        if (a.startLine != b.startLine)
            return a.startLine < b.startLine;
        if (a.fileName != b.fileName)
            return a.fileName < b.fileName;
        return a.symbolId < b.symbolId;
    });
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

bool SemanticIndex::hasRelationshipFacts() const
{
    if (m_snapshot)
        return true;
    return symbolDatabase()->getRelationshipEngine() != nullptr;
}

int SemanticIndex::scopeScoreForSymbol(const QString& symbolName,
                                       const QString& moduleName) const
{
    if (symbolName.isEmpty() || moduleName.isEmpty())
        return 0;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& candidate : symbols) {
        if (candidate.symbolName == symbolName
            && candidate.moduleScope == moduleName) {
            return 20;
        }
    }
    return 0;
}
