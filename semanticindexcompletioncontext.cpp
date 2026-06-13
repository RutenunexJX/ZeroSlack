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
QString normalizedCompletionContextFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool commandCompletionContextSymbolTypeMatches(sym_list::sym_type_e symbolType,
                                               sym_list::sym_type_e commandType,
                                               const QString& dataType = QString())
{
    if (symbolType == commandType)
        return true;
    return commandType == sym_list::sym_enum
        && symbolType == sym_list::sym_typedef
        && dataType == QLatin1String("enum");
}

bool semanticCompletionContextNameMatches(const QString& name, const QString& prefix)
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

bool isCompletionContextModuleRangeSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_packed_struct_var
        || type == sym_list::sym_unpacked_struct_var;
}

QStringList uniqueSortedCompletionContextSymbolNames(
    const QList<sym_list::SymbolInfo>& symbols)
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
        if (!semanticCompletionContextNameMatches(symbol.symbolName, prefix))
            continue;
        result.append(symbol);
    }
    return uniqueSortedCompletionContextSymbolNames(result);
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
    return uniqueSortedCompletionContextSymbolNames(portSymbols);
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
        if (!commandCompletionContextSymbolTypeMatches(
                symbol.symbolType, symbolType, symbol.dataType)) {
            return;
        }
        const bool nameMatches = fuzzyPrefix
            ? semanticCompletionContextNameMatches(symbol.symbolName, prefix)
            : (prefix.isEmpty()
               || symbol.symbolName.startsWith(prefix, Qt::CaseInsensitive));
        if (nameMatches)
            result.append(symbol);
    };

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        bool correctModule = false;
        if (isCompletionContextModuleRangeSymbolType(symbolType)) {
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

    const QString normalizedTargetFile = normalizedCompletionContextFileName(fileName);
    const QList<sym_list::SymbolInfo> fileSymbols = getSymbols(fileName);
    sym_list::SymbolInfo moduleSymbol;
    bool foundModule = false;
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType == sym_list::sym_module
            && symbol.symbolName == moduleName
            && normalizedCompletionContextFileName(symbol.fileName) == normalizedTargetFile) {
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
        if (!commandCompletionContextSymbolTypeMatches(
                symbol.symbolType, symbolType, symbol.dataType)) {
            return;
        }
        if (!semanticCompletionContextNameMatches(symbol.symbolName, prefix))
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
