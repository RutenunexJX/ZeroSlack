#include "semanticindex.h"

#include "semanticindexmodulecontexthelpers.h"
#include "symboltaxonomy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <limits>

using namespace semantic_index_module_context;

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
        if (SymbolTaxonomy::isModuleDeclaration(symbol)
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
            if (SymbolTaxonomy::isModuleDeclaration(symbol)
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
        if (!moduleContextSymbolTypeMatches(
                symbol.symbolType, symbolType, symbol.dataType)) {
            return;
        }
        const bool nameMatches = fuzzyPrefix
            ? moduleContextNameMatches(symbol.symbolName, prefix)
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

    const QString normalizedTargetFile = normalizedModuleContextFileName(fileName);
    const QList<sym_list::SymbolInfo> fileSymbols = getSymbols(fileName);
    sym_list::SymbolInfo moduleSymbol;
    bool foundModule = false;
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        if (SymbolTaxonomy::isModuleDeclaration(symbol)
            && symbol.symbolName == moduleName
            && normalizedModuleContextFileName(symbol.fileName) == normalizedTargetFile) {
            moduleSymbol = symbol;
            foundModule = true;
            break;
        }
    }
    if (!foundModule)
        return result;

    int moduleEndLineExclusive = std::numeric_limits<int>::max();
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        if (!SymbolTaxonomy::isModuleDeclaration(symbol))
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
        if (!moduleContextSymbolTypeMatches(
                symbol.symbolType, symbolType, symbol.dataType)) {
            return;
        }
        if (!moduleContextNameMatches(symbol.symbolName, prefix))
            return;
        if (seenIds.contains(symbol.symbolId))
            return;
        seenIds.insert(symbol.symbolId);
        result.append(symbol);
    };

    const QList<sym_list::SymbolInfo> allSymbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        bool isCorrectModule = false;
        if (isModuleRangeSymbolType(symbolType)) {
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

    sortModuleContextSymbols(result);
    return result;
}
