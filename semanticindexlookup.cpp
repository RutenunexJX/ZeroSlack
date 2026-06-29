#include "semanticindex.h"

#include "semanticindexlookuphelpers.h"
#include "semanticindexsnapshot.h"
#include "symboltaxonomy.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <functional>

using namespace semantic_index_lookup;

namespace {

SemanticDefinitionResult combinedDefinitionMissEvidence(
    const SemanticDefinitionResult& local,
    const SemanticDefinitionResult& global)
{
    SemanticDefinitionResult combined;
    combined.inspectedCandidateCount =
        local.inspectedCandidateCount + global.inspectedCandidateCount;
    combined.matchingNameCandidateCount =
        local.matchingNameCandidateCount + global.matchingNameCandidateCount;
    combined.typeCompatibleCandidateCount =
        local.typeCompatibleCandidateCount + global.typeCompatibleCandidateCount;
    combined.visibleCandidateCount =
        local.visibleCandidateCount + global.visibleCandidateCount;

    if (combined.inspectedCandidateCount == 0) {
        combined.missReason = SemanticDefinitionMissReason::NoCandidateSymbols;
    } else if (combined.matchingNameCandidateCount == 0) {
        combined.missReason = SemanticDefinitionMissReason::NoMatchingName;
    } else if (combined.typeCompatibleCandidateCount == 0) {
        combined.missReason = SemanticDefinitionMissReason::StructMemberTypeMismatch;
    } else {
        combined.missReason = SemanticDefinitionMissReason::NotVisibleInContext;
    }
    return combined;
}

SymbolTaxonomy::SemanticMetadata metadataForRecord(
    const SemanticSymbolRecord& record)
{
    return semanticMetadataForSymbolRecord(record);
}

bool definitionRecordVisibleInContext(
    const SemanticSymbolRecord& record,
    const SemanticQueryContext& context,
    const SemanticIndex* index)
{
    const SymbolTaxonomy::SemanticMetadata metadata = metadataForRecord(record);
    if (record.visibility == SymbolTaxonomy::SymbolVisibility::PackageVisible)
        return index && index->packageVisibleRecordImported(record, context);

    return SymbolTaxonomy::isDefinitionVisibleInContext(
        metadata,
        record.owner.name,
        context.moduleName);
}

int definitionRecordContextPriorityAdjustment(
    const SemanticSymbolRecord& record,
    const QString& moduleName)
{
    if (!moduleName.isEmpty() && record.owner.name == moduleName)
        return -100;
    if (record.visibility == SymbolTaxonomy::SymbolVisibility::PackageVisible)
        return -20;
    return 0;
}

SemanticQueryContext semanticContextForDefinitionQuery(
    const SemanticDefinitionQuery& query)
{
    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;
    context.cursorLine = query.cursorLine;
    context.cursorPosition = query.cursorPosition;
    context.importedPackageNames = query.importedPackageNames;
    return context;
}

bool isPackageWildcardImportLine(const QString& line, QStringList* packages)
{
    static const QRegularExpression importKeyword(
        QStringLiteral("\\bimport\\b"));
    if (!importKeyword.match(line).hasMatch())
        return false;

    static const QRegularExpression wildcardPackage(
        QStringLiteral("\\b([A-Za-z_$][A-Za-z0-9_$]*)\\s*::\\s*\\*"));
    QRegularExpressionMatchIterator it = wildcardPackage.globalMatch(line);
    bool matched = false;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (packages)
            packages->append(match.captured(1));
        matched = true;
    }
    return matched;
}

QString codeLineWithoutComments(const QString& line, bool* inBlockComment)
{
    QString result;
    result.reserve(line.size());
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        const QChar next = (i + 1 < line.size()) ? line.at(i + 1) : QChar();

        if (inBlockComment && *inBlockComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                *inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (inString) {
            result.append(ch);
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('/'))
            break;
        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            if (inBlockComment)
                *inBlockComment = true;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('"'))
            inString = true;
        result.append(ch);
    }
    return result;
}

QString moduleNameAtLine(const QList<SemanticSymbolRecord>& records, int line)
{
    if (line <= 0)
        return {};

    QString best;
    int bestStartLine = -1;
    for (const SemanticSymbolRecord& record : records) {
        if (record.declarationKind != SymbolTaxonomy::DeclarationKind::Module)
            continue;
        if (record.location.startLine <= 0
            || record.location.startLine > line) {
            continue;
        }
        if (record.location.endLine > 0 && record.location.endLine < line)
            continue;
        if (record.location.startLine > bestStartLine) {
            best = record.name;
            bestStartLine = record.location.startLine;
        }
    }
    return best;
}

bool importLineVisibleForContext(
    const QString& importModule,
    const SemanticQueryContext& context)
{
    if (context.moduleName.isEmpty())
        return importModule.isEmpty();
    return importModule.isEmpty() || importModule == context.moduleName;
}

QString includeFileNameFromLine(const QString& line, const QString& parentFileName)
{
    static const QRegularExpression includePattern(
        QStringLiteral("^\\s*`include\\s+\"([^\"]+)\""));
    const QRegularExpressionMatch match = includePattern.match(line);
    if (!match.hasMatch())
        return {};

    const QString includeName = match.captured(1);
    if (includeName.isEmpty())
        return {};
    QFileInfo includeInfo(includeName);
    if (!includeInfo.isAbsolute()) {
        QDir dir = QFileInfo(parentFileName).dir();
        includeInfo = QFileInfo(dir, includeName);
        while (!includeInfo.exists() && dir.cdUp())
            includeInfo = QFileInfo(dir, includeName);
    }
    return normalizedLookupFileName(includeInfo.absoluteFilePath());
}

bool definitionRecordIsInQueryFile(
    const SemanticSymbolRecord& record,
    const QString& queryFileName)
{
    const QString recordFileName =
        normalizedLookupFileName(record.location.fileName);
    const QString queryFile =
        normalizedLookupFileName(queryFileName);
    return !recordFileName.isEmpty()
        && !queryFile.isEmpty()
        && recordFileName == queryFile;
}

}

QList<SemanticSymbolSearchResult> SemanticIndex::searchSymbols(
    const SemanticSymbolSearchQuery& query) const
{
    QList<SemanticSymbolSearchResult> result;
    const QList<SemanticSymbolRecord> records =
        getSymbolRecords(query.fileName);
    for (const SemanticSymbolRecord& record : records) {
        if (!symbolSearchTypeMatches(record,
                                     query.declarationKinds,
                                     query.intent))
            continue;

        const int score = symbolSearchMatchScore(record.name, query);
        if (score <= 0)
            continue;

        SemanticSymbolSearchResult item;
        item.symbolRecord = record;
        item.symbolStableKey = item.symbolRecord.stableKey;
        item.score = score;
        result.append(item);
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const SemanticSymbolSearchResult& a,
                        const SemanticSymbolSearchResult& b) {
        if (a.score != b.score)
            return a.score > b.score;
        const int aBandPriority =
            semanticSymbolAnalysisBandSortPriority(a.symbolRecord);
        const int bBandPriority =
            semanticSymbolAnalysisBandSortPriority(b.symbolRecord);
        if (aBandPriority != bBandPriority)
            return aBandPriority < bBandPriority;
        if (a.symbolRecord.location.fileName != b.symbolRecord.location.fileName)
            return a.symbolRecord.location.fileName
                < b.symbolRecord.location.fileName;
        if (a.symbolRecord.location.startLine != b.symbolRecord.location.startLine)
            return a.symbolRecord.location.startLine
                < b.symbolRecord.location.startLine;
        return a.symbolRecord.name < b.symbolRecord.name;
    });

    if (query.maxResults >= 0 && result.size() > query.maxResults)
        result = result.mid(0, query.maxResults);
    return result;
}

SemanticDefinitionResult SemanticIndex::resolveDefinition(
    const SemanticDefinitionQuery& query) const
{
    SemanticDefinitionResult empty;
    if (query.symbolName.isEmpty()) {
        empty.missReason = SemanticDefinitionMissReason::EmptySymbolName;
        return empty;
    }

    SemanticDefinitionResult local = bestDefinitionFromCandidates(
        getSymbolRecords(query.fileName),
        query,
        true);
    if (local.found) {
        local.localFile =
            definitionRecordIsInQueryFile(local.symbolRecord, query.fileName);
        return local;
    }
    if (local.missReason
        == SemanticDefinitionMissReason::AmbiguousImportedPackageSymbol) {
        return local;
    }

    const SemanticQueryContext context = semanticContextForDefinitionQuery(query);
    QList<SemanticSymbolRecord> globalCandidates =
        findDefinitionRecords(query.symbolName, context);
    const QString queryFile = normalizedLookupFileName(query.fileName);
    globalCandidates.erase(
        std::remove_if(globalCandidates.begin(), globalCandidates.end(),
                       [&queryFile](const SemanticSymbolRecord& record) {
                           return normalizedLookupFileName(
                                      record.location.fileName) == queryFile;
                       }),
        globalCandidates.end());
    SemanticDefinitionResult global =
        bestDefinitionFromCandidates(globalCandidates, query, false);
    if (global.found)
        return global;
    if (global.missReason
        == SemanticDefinitionMissReason::AmbiguousImportedPackageSymbol) {
        global.inspectedCandidateCount += local.inspectedCandidateCount;
        global.matchingNameCandidateCount += local.matchingNameCandidateCount;
        global.typeCompatibleCandidateCount += local.typeCompatibleCandidateCount;
        global.visibleCandidateCount += local.visibleCandidateCount;
        return global;
    }
    return combinedDefinitionMissEvidence(local, global);
}

QList<SemanticSymbolRecord> SemanticIndex::findDefinitionRecords(
    const QString& name,
    const SemanticQueryContext& context) const
{
    if (name.isEmpty())
        return {};

    if (m_snapshot)
        return m_snapshot->findDefinitionRecords(name, context);

    QList<SemanticSymbolRecord> sorted;
    for (const SemanticSymbolRecord& record : getSymbolRecords()) {
        if (record.name == name)
            sorted.append(record);
    }
    if (sorted.isEmpty())
        return {};
    const QString normalizedContextFile = normalizedLookupFileName(context.fileName);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&context, &normalizedContextFile](const SemanticSymbolRecord& a,
                                                        const SemanticSymbolRecord& b) {
        auto score = [&context, &normalizedContextFile](const SemanticSymbolRecord& s) {
            int value = 0;
            if (!normalizedContextFile.isEmpty()
                && normalizedLookupFileName(s.location.fileName) == normalizedContextFile)
                value += 100;
            if (!context.moduleName.isEmpty()
                && s.owner.name == context.moduleName)
                value += 50;
            if (SymbolTaxonomy::isGlobalDefinition(metadataForRecord(s))) {
                value += 10;
            }
            return value;
        };

        const int aScore = score(a);
        const int bScore = score(b);
        if (aScore != bScore)
            return aScore > bScore;
        const int aBandPriority =
            semanticSymbolAnalysisBandSortPriority(a);
        const int bBandPriority =
            semanticSymbolAnalysisBandSortPriority(b);
        if (aBandPriority != bBandPriority)
            return aBandPriority < bBandPriority;
        if (a.location.fileName != b.location.fileName)
            return a.location.fileName < b.location.fileName;
        if (a.location.startLine != b.location.startLine)
            return a.location.startLine < b.location.startLine;
        return a.localHandle < b.localHandle;
    });
    return sorted;
}

QSet<QString> SemanticIndex::activeImportedPackageNames(
    const SemanticQueryContext& context) const
{
    QSet<QString> result = context.importedPackageNames;
    if (context.fileName.isEmpty())
        return result;

    QSet<QString> visitedFiles;
    std::function<void(const SemanticQueryContext&)> scanContext;
    scanContext = [&](const SemanticQueryContext& scanContextValue) {
        const QString normalizedFile =
            normalizedLookupFileName(scanContextValue.fileName);
        if (normalizedFile.isEmpty() || visitedFiles.contains(normalizedFile))
            return;
        visitedFiles.insert(normalizedFile);

        const QString content = getCachedFileContent(scanContextValue.fileName);
        if (content.isEmpty())
            return;

        const QList<SemanticSymbolRecord> fileRecords =
            getSymbolRecords(scanContextValue.fileName);
        const QStringList lines = content.split(QLatin1Char('\n'));
        const int maxLine =
            scanContextValue.cursorLine > 0
                ? qMin(scanContextValue.cursorLine, lines.size())
                : lines.size();
        bool inBlockComment = false;
        for (int i = 0; i < maxLine; ++i) {
            const int lineNumber = i + 1;
            const QString code =
                codeLineWithoutComments(lines.at(i), &inBlockComment);

            const QString includeFileName =
                includeFileNameFromLine(code, scanContextValue.fileName);
            if (!includeFileName.isEmpty()) {
                SemanticQueryContext includeContext = scanContextValue;
                includeContext.fileName = includeFileName;
                includeContext.cursorLine = -1;
                scanContext(includeContext);
            }

            QStringList packages;
            if (!isPackageWildcardImportLine(code, &packages))
                continue;

            const QString importModule = moduleNameAtLine(fileRecords, lineNumber);
            if (!importLineVisibleForContext(importModule, scanContextValue))
                continue;

            for (const QString& packageName : packages) {
                if (!packageName.isEmpty())
                    result.insert(packageName);
            }
        }
    };

    scanContext(context);
    return result;
}

bool SemanticIndex::packageVisibleRecordImported(
    const SemanticSymbolRecord& record,
    const SemanticQueryContext& context) const
{
    if (record.visibility != SymbolTaxonomy::SymbolVisibility::PackageVisible)
        return true;
    if (record.owner.name.isEmpty())
        return false;
    return activeImportedPackageNames(context).contains(record.owner.name);
}

SemanticDefinitionResult SemanticIndex::bestDefinitionFromCandidates(
    const QList<SemanticSymbolRecord>& candidates,
    const SemanticDefinitionQuery& query,
    bool localFile) const
{
    SemanticDefinitionResult best;
    best.localFile = localFile;
    int bestPriority = 999;
    QSet<QString> bestImportedPackageOwners;
    const SemanticQueryContext context = semanticContextForDefinitionQuery(query);

    for (const SemanticSymbolRecord& record : candidates) {
        ++best.inspectedCandidateCount;
        if (!semanticDefinitionRecordMatches(record, query.symbolName))
            continue;
        ++best.matchingNameCandidateCount;
        if (semanticDefinitionSkipForStructMemberType(record, query))
            continue;
        ++best.typeCompatibleCandidateCount;
        if (!definitionRecordVisibleInContext(record, context, this)) {
            continue;
        }
        ++best.visibleCandidateCount;

        int priority = semanticDefinitionTypePriority(record)
            + definitionRecordContextPriorityAdjustment(record, query.moduleName);
        const bool importedPackageMember =
            record.visibility == SymbolTaxonomy::SymbolVisibility::PackageVisible;

        if (!best.found || priority < bestPriority) {
            best.found = true;
            best.localFile = localFile;
            best.symbolRecord = record;
            best.symbolStableKey = best.symbolRecord.stableKey;
            best.missReason = SemanticDefinitionMissReason::None;
            bestPriority = priority;
            bestImportedPackageOwners.clear();
            if (importedPackageMember)
                bestImportedPackageOwners.insert(record.owner.name);
        } else if (priority == bestPriority
                   && best.symbolRecord.visibility
                       == SymbolTaxonomy::SymbolVisibility::PackageVisible
                   && importedPackageMember) {
            bestImportedPackageOwners.insert(record.owner.name);
        }
    }

    if (best.found && bestImportedPackageOwners.size() > 1) {
        best.found = false;
        best.symbolRecord = {};
        best.symbolStableKey = {};
        best.missReason =
            SemanticDefinitionMissReason::AmbiguousImportedPackageSymbol;
        return best;
    }

    if (!best.found) {
        if (best.inspectedCandidateCount == 0) {
            best.missReason = SemanticDefinitionMissReason::NoCandidateSymbols;
        } else if (best.matchingNameCandidateCount == 0) {
            best.missReason = SemanticDefinitionMissReason::NoMatchingName;
        } else if (best.typeCompatibleCandidateCount == 0) {
            best.missReason = SemanticDefinitionMissReason::StructMemberTypeMismatch;
        } else {
            best.missReason = SemanticDefinitionMissReason::NotVisibleInContext;
        }
    }
    return best;
}
