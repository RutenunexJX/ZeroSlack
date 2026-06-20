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

namespace {
QString stableDedupeKeyForModuleContextRecord(
    const SemanticSymbolRecord& record)
{
    const QString stableKey = symbolStableKeyText(record.stableKey);
    if (!stableKey.isEmpty())
        return stableKey;
    return QStringLiteral("%1|%2|%3")
        .arg(record.owner.name,
             QString::number(static_cast<int>(record.declarationKind)),
             record.name);
}

sym_list::SymbolInfo moduleContextSymbolInfoForRecord(
    const SemanticSymbolRecord& record)
{
    return semanticSymbolInfoCarrierForRecord(record);
}

int moduleContextLocalHandleForSymbol(const sym_list::SymbolInfo& symbol)
{
    return symbol.symbolId;
}
}

QList<SemanticSymbolRecord> SemanticIndex::getModuleInternalSymbolRecordsByType(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    bool useRelationshipFallback) const
{
    QList<SemanticSymbolRecord> result;
    if (moduleName.isEmpty())
        return result;

    const QList<SemanticSymbolRecord> allRecords = getSymbolRecords();
    SemanticSymbolRecord moduleRecord;
    bool foundModule = false;
    for (const SemanticSymbolRecord& record : allRecords) {
        if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Module
            && record.name == moduleName) {
            moduleRecord = record;
            foundModule = true;
            break;
        }
    }

    int moduleEndLineExclusive = std::numeric_limits<int>::max();
    if (foundModule) {
        QList<SemanticSymbolRecord> fileModules;
        const QList<SemanticSymbolRecord> fileRecords =
            getSymbolRecords(moduleRecord.location.fileName);
        for (const SemanticSymbolRecord& record : fileRecords) {
            if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Module
                && record.location.fileName == moduleRecord.location.fileName) {
                fileModules.append(record);
            }
        }
        std::sort(fileModules.begin(), fileModules.end(),
                  [](const SemanticSymbolRecord& left,
                     const SemanticSymbolRecord& right) {
                      return left.location.startLine < right.location.startLine;
                  });
        for (int i = 0; i < fileModules.size(); ++i) {
            if (fileModules.at(i).localHandle == moduleRecord.localHandle
                && i + 1 < fileModules.size()) {
                moduleEndLineExclusive = fileModules.at(i + 1).location.startLine;
                break;
            }
        }
    }

    QSet<QString> seenStableKeys;
    auto appendIfMatches = [&](const SemanticSymbolRecord& record, bool fuzzyPrefix) {
        if (!moduleContextSymbolTypeMatches(record, symbolType)) {
            return;
        }
        const bool nameMatches = fuzzyPrefix
            ? moduleContextNameMatches(record.name, prefix)
            : (prefix.isEmpty()
               || record.name.startsWith(prefix, Qt::CaseInsensitive));
        if (!nameMatches)
            return;
        const QString dedupeKey =
            stableDedupeKeyForModuleContextRecord(record);
        if (dedupeKey.isEmpty() || seenStableKeys.contains(dedupeKey))
            return;
        seenStableKeys.insert(dedupeKey);
        result.append(record);
    };

    for (const SemanticSymbolRecord& record : allRecords) {
        bool correctModule = false;
        if (isModuleRangeSymbolType(symbolType)) {
            correctModule = foundModule
                && record.location.fileName == moduleRecord.location.fileName
                && record.location.startLine > moduleRecord.location.startLine
                && record.location.startLine < moduleEndLineExclusive;
        } else {
            correctModule = record.owner.name == moduleName;
        }

        if (correctModule)
            appendIfMatches(record, true);
    }

    if (useRelationshipFallback && result.isEmpty()) {
        const SymbolStableKey moduleStableKey = moduleRecord.stableKey;
        const QList<SemanticRelationshipResult> relationships =
            moduleStableKey.isValid()
                ? getRelationshipResults(moduleStableKey, true)
                : QList<SemanticRelationshipResult>();
        for (const SemanticRelationshipResult& relationship : relationships) {
            if (relationship.relationship.type != SymbolRelationshipEngine::CONTAINS)
                continue;
            const SemanticSymbolRecord targetRecord = relationship.toSymbolRecord;
            const SymbolStableKey targetKey = targetRecord.stableKey.isValid()
                ? targetRecord.stableKey
                : relationship.toStableKey;
            const SemanticSymbolRecord resolvedTargetRecord = targetRecord.isValid()
                ? targetRecord
                : getSymbolRecordByStableKey(targetKey);
            if (resolvedTargetRecord.localHandle >= 0)
                appendIfMatches(resolvedTargetRecord, false);
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
    const QList<sym_list::SymbolInfo> fileSymbols =
        semanticSymbolInfoCarriersForRecords(getSymbolRecords(fileName));
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
        if (moduleContextLocalHandleForSymbol(symbol)
            == moduleContextLocalHandleForSymbol(moduleSymbol)) {
            continue;
        }
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

    QSet<QString> seenStableKeys;
    auto appendSymbol = [&](const sym_list::SymbolInfo& symbol) {
        const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
        if (!moduleContextSymbolTypeMatches(record, symbolType)) {
            return;
        }
        if (!moduleContextNameMatches(record.name, prefix))
            return;
        const QString dedupeKey =
            stableDedupeKeyForModuleContextRecord(record);
        if (dedupeKey.isEmpty() || seenStableKeys.contains(dedupeKey))
            return;
        seenStableKeys.insert(dedupeKey);
        result.append(symbol);
    };

    const QList<sym_list::SymbolInfo> allSymbols =
        semanticSymbolInfoCarriersForRecords(getSymbolRecords());
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        bool isCorrectModule = false;
        if (isModuleRangeSymbolType(symbolType)) {
            isCorrectModule = inModuleRange(symbol);
        } else {
            const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
            isCorrectModule = record.owner.name == moduleName;
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
                    semanticSymbolInfoCarriersForRecords(
                        getSymbolRecords(absoluteIncludePath));
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
            const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
            bool imported = starPackages.contains(record.owner.name);
            if (!imported) {
                auto it = importedSymbolsByPackage.constFind(record.owner.name);
                imported = it != importedSymbolsByPackage.constEnd()
                    && it->contains(record.name);
            }
            if (imported)
                appendSymbol(symbol);
        }
    }

    if (result.isEmpty()) {
        const SemanticSymbolRecord moduleRecord =
            semanticSymbolRecordForSymbol(moduleSymbol);
        const SymbolStableKey moduleStableKey = moduleRecord.stableKey.isValid()
            ? moduleRecord.stableKey
            : symbolStableKeyForSymbol(moduleSymbol);
        const QList<SemanticRelationshipResult> relationships =
            moduleStableKey.isValid()
                ? getRelationshipResults(moduleStableKey, true)
                : QList<SemanticRelationshipResult>();
        for (const SemanticRelationshipResult& relationship : relationships) {
            if (relationship.relationship.type != SymbolRelationshipEngine::CONTAINS)
                continue;
            const SemanticSymbolRecord targetRecord = relationship.toSymbolRecord;
            const SymbolStableKey targetKey = targetRecord.stableKey.isValid()
                ? targetRecord.stableKey
                : relationship.toStableKey;
            const SemanticSymbolRecord resolvedTargetRecord = targetRecord.isValid()
                ? targetRecord
                : getSymbolRecordByStableKey(targetKey);
            const sym_list::SymbolInfo targetSymbol =
                moduleContextSymbolInfoForRecord(resolvedTargetRecord);
            if (moduleContextLocalHandleForSymbol(targetSymbol) >= 0)
                appendSymbol(targetSymbol);
        }
    }

    sortModuleContextSymbols(result);
    return result;
}
