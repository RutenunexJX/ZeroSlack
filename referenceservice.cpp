#include "referenceservice.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

std::unique_ptr<ReferenceService> ReferenceService::instance = nullptr;

namespace {
QString normalizedReferenceFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QStringList normalizedReferenceFileNames(const QStringList& fileNames)
{
    QStringList normalized;
    QSet<QString> seen;
    for (const QString& fileName : fileNames) {
        const QString path = normalizedReferenceFileName(fileName);
        if (path.isEmpty() || seen.contains(path))
            continue;
        seen.insert(path);
        normalized.append(path);
    }
    return normalized;
}

QString referenceRecordFileName(const SemanticSymbolRecord& record,
                                const sym_list::SymbolInfo& fallbackSymbol)
{
    return record.location.fileName.isEmpty()
        ? fallbackSymbol.fileName
        : record.location.fileName;
}

int referenceRecordLine(const SemanticSymbolRecord& record,
                        const sym_list::SymbolInfo& fallbackSymbol)
{
    return record.location.startLine > 0
        ? record.location.startLine
        : fallbackSymbol.startLine;
}

int referenceRecordColumn(const SemanticSymbolRecord& record,
                          const sym_list::SymbolInfo& fallbackSymbol)
{
    return record.location.startColumn > 0
        ? record.location.startColumn
        : fallbackSymbol.startColumn;
}

QString referenceRecordName(const SemanticSymbolRecord& record,
                            const sym_list::SymbolInfo& fallbackSymbol)
{
    return record.name.isEmpty()
        ? fallbackSymbol.symbolName
        : record.name;
}

bool referenceLocationLess(const ReferenceResult& lhs,
                           const ReferenceResult& rhs)
{
    const QString leftFile =
        referenceRecordFileName(lhs.referencingSymbolRecord, lhs.referencingSymbol);
    const QString rightFile =
        referenceRecordFileName(rhs.referencingSymbolRecord, rhs.referencingSymbol);
    const int fileCompare = QString::compare(normalizedReferenceFileName(leftFile),
                                             normalizedReferenceFileName(rightFile),
                                             Qt::CaseInsensitive);
    if (fileCompare != 0)
        return fileCompare < 0;
    const int leftLine =
        referenceRecordLine(lhs.referencingSymbolRecord, lhs.referencingSymbol);
    const int rightLine =
        referenceRecordLine(rhs.referencingSymbolRecord, rhs.referencingSymbol);
    if (leftLine != rightLine)
        return leftLine < rightLine;
    const int leftColumn =
        referenceRecordColumn(lhs.referencingSymbolRecord, lhs.referencingSymbol);
    const int rightColumn =
        referenceRecordColumn(rhs.referencingSymbolRecord, rhs.referencingSymbol);
    if (leftColumn != rightColumn)
        return leftColumn < rightColumn;
    return QString::compare(referenceRecordName(lhs.referencingSymbolRecord,
                                                lhs.referencingSymbol),
                            referenceRecordName(rhs.referencingSymbolRecord,
                                                rhs.referencingSymbol),
                            Qt::CaseInsensitive) < 0;
}

QString referenceTypeDisplayName(SymbolRelationshipEngine::RelationType type)
{
    switch (type) {
    case SymbolRelationshipEngine::CONTAINS:
        return QStringLiteral("Contains");
    case SymbolRelationshipEngine::REFERENCES:
        return QStringLiteral("References");
    case SymbolRelationshipEngine::INSTANTIATES:
        return QStringLiteral("Instantiates");
    case SymbolRelationshipEngine::CALLS:
        return QStringLiteral("Calls");
    case SymbolRelationshipEngine::INHERITS:
        return QStringLiteral("Inherits");
    case SymbolRelationshipEngine::IMPLEMENTS:
        return QStringLiteral("Implements");
    case SymbolRelationshipEngine::ASSIGNS_TO:
        return QStringLiteral("Assigns To");
    case SymbolRelationshipEngine::READS_FROM:
        return QStringLiteral("Reads From");
    case SymbolRelationshipEngine::CLOCKS:
        return QStringLiteral("Clocks");
    case SymbolRelationshipEngine::RESETS:
        return QStringLiteral("Resets");
    case SymbolRelationshipEngine::GENERATES:
        return QStringLiteral("Generates");
    case SymbolRelationshipEngine::CONSTRAINS:
        return QStringLiteral("Constrains");
    }
    return QStringLiteral("Relationship");
}

QString reportNotFoundReasonDisplayName(ReferenceReportNotFoundReason reason)
{
    switch (reason) {
    case ReferenceReportNotFoundReason::None:
        return QString();
    case ReferenceReportNotFoundReason::NoSubjectSymbol:
        return QStringLiteral("no subject symbol");
    case ReferenceReportNotFoundReason::NoReferences:
        return QStringLiteral("no references");
    }
    return QStringLiteral("reference report unavailable");
}
}

ReferenceService* ReferenceService::getInstance()
{
    if (!instance)
        instance = std::make_unique<ReferenceService>();
    return instance.get();
}

ReferenceService::ReferenceService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance()),
      relationshipService(index)
{
}

ReferenceService::~ReferenceService() = default;

void ReferenceService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
    relationshipService.setSemanticIndex(index);
}

QList<ReferenceResult> ReferenceService::findReferences(const ReferenceQuery& query) const
{
    const ReferenceQuery normalized = normalizedQuery(query);
    const sym_list::SymbolInfo subjectSymbol = resolveSubjectSymbol(normalized);
    const SymbolStableKey subjectStableKey = normalized.symbolStableKey.isValid()
        ? normalized.symbolStableKey
        : symbolStableKeyForSymbol(subjectSymbol);
    if (!subjectStableKey.isValid() && subjectSymbol.symbolId < 0)
        return {};

    RelationshipQuery relationshipQuery;
    relationshipQuery.symbolStableKey = subjectStableKey;
    relationshipQuery.outgoing = false;
    relationshipQuery.types = effectiveTypes(normalized);

    QList<ReferenceResult> result;
    const QList<RelationshipResult> relationships =
        relationshipService.findRelationships(relationshipQuery);
    for (const RelationshipResult& rel : relationships) {
        ReferenceResult reference = toReferenceResult(rel);
        if (!scopeMatches(normalized,
                          reference.referencingSymbolRecord,
                          reference.referencingSymbol))
            continue;
        result.append(reference);
    }
    std::sort(result.begin(), result.end(),
              [](const ReferenceResult& lhs, const ReferenceResult& rhs) {
                  if (lhs.relationship.relationship.type != rhs.relationship.relationship.type) {
                      return static_cast<int>(lhs.relationship.relationship.type)
                          < static_cast<int>(rhs.relationship.relationship.type);
                  }
                  return referenceLocationLess(lhs, rhs);
              });
    return result;
}

ReferenceReport ReferenceService::findReferenceReport(const ReferenceQuery& query) const
{
    const ReferenceQuery normalized = normalizedQuery(query);
    ReferenceReport report;
    report.subjectSymbol = resolveSubjectSymbol(normalized);
    if (report.subjectSymbol.symbolId < 0) {
        report.notFoundReason = ReferenceReportNotFoundReason::NoSubjectSymbol;
        report.notFoundReasonDisplayName =
            reportNotFoundReasonDisplayName(report.notFoundReason);
        return report;
    }
    report.subjectSymbolRecord = semanticSymbolRecordForSymbol(report.subjectSymbol);
    report.subjectStableKey = report.subjectSymbolRecord.stableKey.isValid()
        ? report.subjectSymbolRecord.stableKey
        : symbolStableKeyForSymbol(report.subjectSymbol);

    report.references = findReferences(normalized);
    report.totalCount = report.references.size();
    QMap<QString, int> fileGroupIndexes;
    QMap<QString, QMap<SymbolRelationshipEngine::RelationType, int>> typeGroupIndexes;
    for (const ReferenceResult& reference : report.references) {
        const QString referenceFile =
            referenceRecordFileName(reference.referencingSymbolRecord,
                                    reference.referencingSymbol);
        const QString normalizedFile =
            normalizedReferenceFileName(referenceFile);
        const QString fileKey = normalizedFile.isEmpty()
            ? referenceFile
            : normalizedFile;
        const SymbolRelationshipEngine::RelationType type =
            reference.relationship.relationship.type;
        report.fileCounts[fileKey]++;
        report.typeCounts[type]++;
        report.fileTypeCounts[fileKey][type]++;

        if (!fileGroupIndexes.contains(fileKey)) {
            ReferenceFileGroup fileGroup;
            fileGroup.fileName = referenceFile;
            fileGroup.fileKey = fileKey;
            fileGroup.displayName =
                referenceFileDisplayName(referenceFile);
            fileGroupIndexes.insert(fileKey, report.fileGroups.size());
            report.fileGroups.append(fileGroup);
        }

        ReferenceFileGroup& fileGroup =
            report.fileGroups[fileGroupIndexes.value(fileKey)];
        fileGroup.count++;

        if (!typeGroupIndexes[fileKey].contains(type)) {
            ReferenceTypeGroup typeGroup;
            typeGroup.type = type;
            typeGroup.displayName = referenceTypeDisplayName(type);
            typeGroupIndexes[fileKey].insert(type, fileGroup.typeGroups.size());
            fileGroup.typeGroups.append(typeGroup);
        }

        ReferenceTypeGroup& typeGroup =
            fileGroup.typeGroups[typeGroupIndexes[fileKey].value(type)];
        typeGroup.references.append(reference);
        typeGroup.count++;
    }
    if (report.totalCount == 0) {
        report.notFoundReason = ReferenceReportNotFoundReason::NoReferences;
        report.notFoundReasonDisplayName =
            reportNotFoundReasonDisplayName(report.notFoundReason);
    } else {
        report.notFoundReason = ReferenceReportNotFoundReason::None;
    }
    return report;
}

bool ReferenceService::hasReferences(const ReferenceQuery& query) const
{
    return !findReferences(query).isEmpty();
}

ReferenceQuery ReferenceService::queryForPanel(
    const ReferencePanelQueryOptions& options) const
{
    ReferenceQuery query;
    query.symbolName = options.symbolName;
    query.fileName = options.fileName;
    query.moduleName = options.moduleName;
    query.workspaceFiles = options.workspaceFiles;
    query.workspaceFilesOnly = options.scope == ReferencePanelScope::WorkspaceFiles;
    query.currentFileOnly = options.scope == ReferencePanelScope::CurrentFile;
    if (options.typeFilter >= 0) {
        query.types = {
            static_cast<SymbolRelationshipEngine::RelationType>(options.typeFilter)
        };
    }
    return normalizedQuery(query);
}

SemanticIndex* ReferenceService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

sym_list::SymbolInfo ReferenceService::resolveSubjectSymbol(
    const ReferenceQuery& query) const
{
    if (query.symbolStableKey.isValid())
        return semanticIndex()->getSymbolByStableKey(query.symbolStableKey);

    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    if (query.symbolName.isEmpty())
        return missing;

    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;
    const int symbolId = semanticIndex()->findSymbolId(query.symbolName, context);
    return semanticIndex()->getSymbolById(symbolId);
}

QList<SymbolRelationshipEngine::RelationType> ReferenceService::effectiveTypes(
    const ReferenceQuery& query) const
{
    if (!query.types.isEmpty())
        return query.types;
    return {
        SymbolRelationshipEngine::REFERENCES,
        SymbolRelationshipEngine::INSTANTIATES,
        SymbolRelationshipEngine::CALLS,
        SymbolRelationshipEngine::ASSIGNS_TO,
        SymbolRelationshipEngine::READS_FROM,
        SymbolRelationshipEngine::CLOCKS,
        SymbolRelationshipEngine::RESETS,
    };
}

bool ReferenceService::scopeMatches(const ReferenceQuery& query,
                                    const SemanticSymbolRecord& record,
                                    const sym_list::SymbolInfo& fallbackSymbol) const
{
    const QString normalizedSource = normalizedReferenceFileName(
        referenceRecordFileName(record, fallbackSymbol));
    if (query.currentFileOnly) {
        if (normalizedSource != query.fileName)
            return false;
    }

    if (query.workspaceFilesOnly) {
        QSet<QString> workspaceFiles;
        for (const QString& file : query.workspaceFiles)
            workspaceFiles.insert(file);
        if (!workspaceFiles.contains(normalizedSource))
            return false;
    }

    return true;
}

ReferenceResult ReferenceService::toReferenceResult(
    const RelationshipResult& relationship) const
{
    ReferenceResult result;
    result.relationship = relationship;
    result.referencingSymbol = relationship.fromSymbol;
    result.referencedSymbol = relationship.toSymbol;
    result.referencingSymbolRecord =
        semanticSymbolRecordForSymbol(result.referencingSymbol);
    result.referencedSymbolRecord =
        semanticSymbolRecordForSymbol(result.referencedSymbol);
    result.referencingStableKey = result.referencingSymbolRecord.stableKey.isValid()
        ? result.referencingSymbolRecord.stableKey
        : relationship.fromStableKey;
    result.referencedStableKey = result.referencedSymbolRecord.stableKey.isValid()
        ? result.referencedSymbolRecord.stableKey
        : relationship.toStableKey;
    const QString sourceFile =
        referenceRecordFileName(result.referencingSymbolRecord,
                                result.referencingSymbol);
    result.symbolDisplayName =
        referenceRecordName(result.referencingSymbolRecord, result.referencingSymbol);
    result.fileDisplayName = referenceFileDisplayName(sourceFile);
    result.lineDisplayName = referenceLineDisplayName(
        referenceRecordLine(result.referencingSymbolRecord, result.referencingSymbol));
    result.relationshipTypeDisplayName =
        referenceTypeDisplayName(relationship.relationship.type);
    return result;
}

ReferenceQuery ReferenceService::normalizedQuery(const ReferenceQuery& query)
{
    ReferenceQuery normalized = query;
    normalized.fileName = normalizedReferenceFileName(query.fileName);
    normalized.workspaceFiles = normalizedReferenceFileNames(query.workspaceFiles);
    return normalized;
}

QString ReferenceService::referenceFileDisplayName(const QString& fileName)
{
    QString displayName = QFileInfo(fileName).fileName();
    if (displayName.isEmpty())
        displayName = fileName;
    return displayName;
}

QString ReferenceService::referenceLineDisplayName(int line)
{
    return QString::number(line);
}
