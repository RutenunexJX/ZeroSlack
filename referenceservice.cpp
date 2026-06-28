#include "referenceservice.h"

#include "svmacrosemantics.h"

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

QString referenceRecordFileName(const SemanticSymbolRecord& record)
{
    return record.location.fileName;
}

int referenceRecordLine(const SemanticSymbolRecord& record)
{
    return record.location.startLine;
}

QString referenceRecordName(const SemanticSymbolRecord& record)
{
    return record.name.isEmpty() ? QStringLiteral("<unnamed>") : record.name;
}

QString reportSubjectDisplayName(const SemanticSymbolRecord& record,
                                 const QString& fallbackName)
{
    return record.name.isEmpty() ? fallbackName : record.name;
}

bool referenceLocationLess(const ReferenceResult& lhs,
                           const ReferenceResult& rhs)
{
    const QString leftFile = lhs.referencingSymbolRecord.location.fileName;
    const QString rightFile = rhs.referencingSymbolRecord.location.fileName;
    const int fileCompare = QString::compare(normalizedReferenceFileName(leftFile),
                                             normalizedReferenceFileName(rightFile),
                                             Qt::CaseInsensitive);
    if (fileCompare != 0)
        return fileCompare < 0;
    const int leftLine = lhs.referencingSymbolRecord.location.startLine;
    const int rightLine = rhs.referencingSymbolRecord.location.startLine;
    if (leftLine != rightLine)
        return leftLine < rightLine;
    const int leftColumn = lhs.referencingSymbolRecord.location.startColumn;
    const int rightColumn = rhs.referencingSymbolRecord.location.startColumn;
    if (leftColumn != rightColumn)
        return leftColumn < rightColumn;
    return QString::compare(lhs.referencingSymbolRecord.name,
                            rhs.referencingSymbolRecord.name,
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

bool isMacroDefinitionRecord(const SemanticSymbolRecord& record)
{
    return record.declarationKind == SymbolTaxonomy::DeclarationKind::Macro
        && record.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration;
}

SymbolStableKey macroReferenceStableKey(const SvMacroSemantics::MacroReference& reference)
{
    SymbolStableKey key;
    key.fileName = normalizedReferenceFileName(reference.fileName);
    key.symbolName = reference.name;
    key.declarationKind = SymbolTaxonomy::DeclarationKind::Macro;
    key.ownerScope = QStringLiteral("macro-reference:%1:%2")
                         .arg(reference.line)
                         .arg(reference.column);
    return key;
}

SemanticSymbolRecord macroReferenceRecord(
    const SvMacroSemantics::MacroReference& reference)
{
    SemanticSymbolRecord record;
    record.name = reference.name;
    record.location.fileName = reference.fileName;
    record.location.startLine = reference.line;
    record.location.startColumn = reference.column;
    record.location.endLine = reference.line;
    record.location.endColumn = reference.column + reference.length;
    record.location.position = reference.position;
    record.location.length = reference.length;
    record.declarationKind = SymbolTaxonomy::DeclarationKind::Macro;
    record.usageRole = SymbolTaxonomy::SymbolUsageRole::Reference;
    record.visibility = SymbolTaxonomy::SymbolVisibility::Global;
    record.sourceRole = SymbolTaxonomy::sourceRoleForFileName(reference.fileName);
    record.collectorKind = SymbolTaxonomy::CollectorKind::DefDefine;
    record.owner.kind = SymbolTaxonomy::SymbolOwnerScope::Global;
    record.stableKey = macroReferenceStableKey(reference);
    return record;
}

ReferenceResult macroReferenceResult(
    const SemanticSymbolRecord& referencingRecord,
    const SemanticSymbolRecord& referencedRecord)
{
    ReferenceResult result;
    result.relationshipType = SymbolRelationshipEngine::REFERENCES;
    result.referencingSymbolRecord = referencingRecord;
    result.referencedSymbolRecord = referencedRecord;
    result.referencingStableKey = referencingRecord.stableKey;
    result.referencedStableKey = referencedRecord.stableKey;
    result.symbolDisplayName = referenceRecordName(referencingRecord);
    result.fileDisplayName = QFileInfo(
        referencingRecord.location.fileName).fileName();
    if (result.fileDisplayName.isEmpty())
        result.fileDisplayName = referencingRecord.location.fileName;
    result.lineDisplayName =
        QString::number(referencingRecord.location.startLine);
    result.relationshipTypeDisplayName =
        referenceTypeDisplayName(result.relationshipType);
    return result;
}

QStringList macroReferenceScanFiles(const ReferenceQuery& query,
                                    const QList<SemanticSymbolRecord>& records)
{
    QStringList files;
    QSet<QString> seen;
    auto appendFile = [&](const QString& fileName) {
        const QString normalized = normalizedReferenceFileName(fileName);
        if (normalized.isEmpty() || seen.contains(normalized))
            return;
        seen.insert(normalized);
        files.append(fileName);
    };

    if (query.currentFileOnly) {
        appendFile(query.fileName);
        return files;
    }
    if (query.workspaceFilesOnly) {
        for (const QString& fileName : query.workspaceFiles)
            appendFile(fileName);
        return files;
    }

    appendFile(query.fileName);
    for (const SemanticSymbolRecord& record : records)
        appendFile(record.location.fileName);
    return files;
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
    const SemanticSymbolRecord subjectRecord =
        resolveSubjectSymbolRecord(normalized);
    const SymbolStableKey subjectStableKey = normalized.symbolStableKey.isValid()
        ? normalized.symbolStableKey
        : subjectRecord.stableKey;
    if (!subjectStableKey.isValid())
        return {};
    if (isMacroDefinitionRecord(subjectRecord))
        return findMacroReferences(normalized, subjectRecord);

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
                          reference.referencingSymbolRecord))
            continue;
        result.append(reference);
    }
    std::sort(result.begin(), result.end(),
              [](const ReferenceResult& lhs, const ReferenceResult& rhs) {
                  if (lhs.relationshipType != rhs.relationshipType) {
                      return static_cast<int>(lhs.relationshipType)
                          < static_cast<int>(rhs.relationshipType);
                  }
                  return referenceLocationLess(lhs, rhs);
              });
    return result;
}

ReferenceReport ReferenceService::findReferenceReport(const ReferenceQuery& query) const
{
    const ReferenceQuery normalized = normalizedQuery(query);
    ReferenceReport report;
    report.subjectSymbolRecord = resolveSubjectSymbolRecord(normalized);
    report.subjectStableKey = report.subjectSymbolRecord.stableKey;
    report.subjectDisplayName =
        reportSubjectDisplayName(report.subjectSymbolRecord,
                                 normalized.symbolName);
    if (!report.subjectStableKey.isValid()) {
        report.notFoundReason = ReferenceReportNotFoundReason::NoSubjectSymbol;
        report.notFoundReasonDisplayName =
            reportNotFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    report.references = findReferences(normalized);
    report.totalCount = report.references.size();
    QMap<QString, int> fileGroupIndexes;
    QMap<QString, QMap<SymbolRelationshipEngine::RelationType, int>> typeGroupIndexes;
    for (const ReferenceResult& reference : report.references) {
        const QString referenceFile =
            reference.referencingSymbolRecord.location.fileName;
        const QString normalizedFile =
            normalizedReferenceFileName(referenceFile);
        const QString fileKey = normalizedFile.isEmpty()
            ? referenceFile
            : normalizedFile;
        const SymbolRelationshipEngine::RelationType type =
            reference.relationshipType;
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

SemanticSymbolRecord ReferenceService::resolveSubjectSymbolRecord(
    const ReferenceQuery& query) const
{
    if (query.symbolStableKey.isValid())
        return semanticIndex()->getSymbolRecordByStableKey(query.symbolStableKey);

    if (query.symbolName.isEmpty())
        return {};

    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;
    const QList<SemanticSymbolRecord> definitions =
        semanticIndex()->findDefinitionRecords(query.symbolName, context);
    if (definitions.isEmpty())
        return {};
    return definitions.first();
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
                                    const SemanticSymbolRecord& record) const
{
    const QString normalizedSource = normalizedReferenceFileName(
        record.location.fileName);
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

QList<ReferenceResult> ReferenceService::findMacroReferences(
    const ReferenceQuery& query,
    const SemanticSymbolRecord& subjectRecord) const
{
    if (!isMacroDefinitionRecord(subjectRecord))
        return {};
    if (!query.types.isEmpty()
        && !query.types.contains(SymbolRelationshipEngine::REFERENCES)) {
        return {};
    }

    QList<ReferenceResult> result;
    QSet<QString> seen;
    const QList<SemanticSymbolRecord> records = semanticIndex()->getSymbolRecords();
    auto appendResult = [&](const SemanticSymbolRecord& referencingRecord) {
        if (!scopeMatches(query, referencingRecord))
            return;
        const QString key = QStringLiteral("%1:%2:%3:%4")
                                .arg(normalizedReferenceFileName(
                                    referencingRecord.location.fileName))
                                .arg(referencingRecord.location.startLine)
                                .arg(referencingRecord.location.startColumn)
                                .arg(static_cast<int>(
                                    referencingRecord.usageRole));
        if (seen.contains(key))
            return;
        seen.insert(key);
        result.append(macroReferenceResult(referencingRecord, subjectRecord));
    };

    for (const SemanticSymbolRecord& record :
         semanticIndex()->getSymbolRecordsByName(subjectRecord.name)) {
        if (isMacroDefinitionRecord(record)
            && record.name == subjectRecord.name) {
            appendResult(record);
        }
    }

    const QStringList files = macroReferenceScanFiles(query, records);
    for (const QString& fileName : files) {
        const QString content = semanticIndex()->getCachedFileContent(fileName);
        if (content.isEmpty())
            continue;
        for (const SvMacroSemantics::MacroReference& reference :
             SvMacroSemantics::collectMacroReferences(fileName, content)) {
            if (reference.name != subjectRecord.name)
                continue;
            appendResult(macroReferenceRecord(reference));
        }
    }

    std::sort(result.begin(), result.end(), referenceLocationLess);
    return result;
}

ReferenceResult ReferenceService::toReferenceResult(
    const RelationshipResult& relationship) const
{
    ReferenceResult result;
    result.relationshipType = relationship.relationship.type;
    result.referencingSymbolRecord = relationship.fromSymbolRecord;
    result.referencedSymbolRecord = relationship.toSymbolRecord;
    result.referencingStableKey = result.referencingSymbolRecord.stableKey.isValid()
        ? result.referencingSymbolRecord.stableKey
        : relationship.fromStableKey;
    result.referencedStableKey = result.referencedSymbolRecord.stableKey.isValid()
        ? result.referencedSymbolRecord.stableKey
        : relationship.toStableKey;
    const QString sourceFile =
        referenceRecordFileName(result.referencingSymbolRecord);
    result.symbolDisplayName =
        referenceRecordName(result.referencingSymbolRecord);
    result.fileDisplayName = referenceFileDisplayName(sourceFile);
    result.lineDisplayName = referenceLineDisplayName(
        referenceRecordLine(result.referencingSymbolRecord));
    result.relationshipTypeDisplayName =
        referenceTypeDisplayName(result.relationshipType);
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
