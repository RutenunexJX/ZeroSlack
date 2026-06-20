#include "semanticindex.h"

#include "completioncommandkindadapter.h"
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

}

QList<SemanticSymbolRecord> SemanticIndex::getModuleInternalSymbolRecordsByType(
    const QString& moduleName,
    CompletionCommandKind commandKind,
    const QString& prefix,
    bool useRelationshipFallback) const
{
    QList<SemanticSymbolRecord> result;
    if (moduleName.isEmpty())
        return result;

    const sym_list::sym_type_e symbolType =
        rawCollectorKindForCompletionCommandKind(commandKind);
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

QList<SemanticSymbolRecord> SemanticIndex::getModuleContextSymbolRecordsByType(
    const QString& moduleName,
    const QString& fileName,
    CompletionCommandKind commandKind,
    const QString& prefix) const
{
    QList<SemanticSymbolRecord> result;
    if (moduleName.isEmpty() || fileName.isEmpty())
        return result;

    const sym_list::sym_type_e symbolType =
        rawCollectorKindForCompletionCommandKind(commandKind);
    const QString normalizedTargetFile = normalizedModuleContextFileName(fileName);
    const QList<SemanticSymbolRecord> fileRecords = getSymbolRecords(fileName);
    SemanticSymbolRecord moduleRecord;
    bool foundModule = false;
    for (const SemanticSymbolRecord& record : fileRecords) {
        if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Module
            && record.name == moduleName
            && normalizedModuleContextFileName(record.location.fileName)
                == normalizedTargetFile) {
            moduleRecord = record;
            foundModule = true;
            break;
        }
    }
    if (!foundModule)
        return result;

    int moduleEndLineExclusive = std::numeric_limits<int>::max();
    for (const SemanticSymbolRecord& record : fileRecords) {
        if (record.declarationKind != SymbolTaxonomy::DeclarationKind::Module)
            continue;
        if (record.localHandle == moduleRecord.localHandle) {
            continue;
        }
        if (record.location.startLine > moduleRecord.location.startLine
            && record.location.startLine < moduleEndLineExclusive) {
            moduleEndLineExclusive = record.location.startLine;
        }
    }

    auto inModuleRange = [&moduleRecord, moduleEndLineExclusive](
                             const SemanticSymbolRecord& record) {
        return record.location.fileName == moduleRecord.location.fileName
            && record.location.startLine > moduleRecord.location.startLine
            && record.location.startLine < moduleEndLineExclusive;
    };

    QSet<QString> seenStableKeys;
    auto appendRecord = [&](const SemanticSymbolRecord& record) {
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
        result.append(record);
    };

    const QList<SemanticSymbolRecord> allRecords = getSymbolRecords();
    for (const SemanticSymbolRecord& record : allRecords) {
        bool isCorrectModule = false;
        if (isModuleRangeSymbolType(symbolType)) {
            isCorrectModule = inModuleRange(record);
        } else {
            isCorrectModule = record.owner.name == moduleName;
        }
        if (isCorrectModule)
            appendRecord(record);
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
            if (lineNumber < moduleRecord.location.startLine
                || lineNumber >= moduleEndLineExclusive) {
                continue;
            }

            const QString line = lines.at(i);
            const QRegularExpressionMatch includeMatch = includeRegex.match(line);
            if (includeMatch.hasMatch()) {
                const QString includePath = includeMatch.captured(1).trimmed();
                const QString absoluteIncludePath =
                    QDir(baseDir).absoluteFilePath(includePath);
                const QList<SemanticSymbolRecord> includeRecords =
                    getSymbolRecords(absoluteIncludePath);
                for (const SemanticSymbolRecord& record : includeRecords)
                    appendRecord(record);
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

        for (const SemanticSymbolRecord& record : allRecords) {
            bool imported = starPackages.contains(record.owner.name);
            if (!imported) {
                auto it = importedSymbolsByPackage.constFind(record.owner.name);
                imported = it != importedSymbolsByPackage.constEnd()
                    && it->contains(record.name);
            }
            if (imported)
                appendRecord(record);
        }
    }

    if (result.isEmpty()) {
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
                appendRecord(resolvedTargetRecord);
        }
    }

    sortModuleContextSymbolRecords(result);
    return result;
}
