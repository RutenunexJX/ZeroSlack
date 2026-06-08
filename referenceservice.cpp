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

bool referenceLocationLess(const sym_list::SymbolInfo& lhs,
                           const sym_list::SymbolInfo& rhs)
{
    const int fileCompare = QString::compare(normalizedReferenceFileName(lhs.fileName),
                                             normalizedReferenceFileName(rhs.fileName),
                                             Qt::CaseInsensitive);
    if (fileCompare != 0)
        return fileCompare < 0;
    if (lhs.startLine != rhs.startLine)
        return lhs.startLine < rhs.startLine;
    if (lhs.startColumn != rhs.startColumn)
        return lhs.startColumn < rhs.startColumn;
    return QString::compare(lhs.symbolName, rhs.symbolName, Qt::CaseInsensitive) < 0;
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
    const int id = resolveSymbolId(query);
    if (id < 0)
        return {};

    RelationshipQuery relationshipQuery;
    relationshipQuery.symbolId = id;
    relationshipQuery.outgoing = false;
    relationshipQuery.types = effectiveTypes(query);

    QList<ReferenceResult> result;
    const QList<RelationshipResult> relationships =
        relationshipService.findRelationships(relationshipQuery);
    for (const RelationshipResult& rel : relationships) {
        ReferenceResult reference = toReferenceResult(rel);
        if (!scopeMatches(query, reference.referencingSymbol))
            continue;
        result.append(reference);
    }
    std::sort(result.begin(), result.end(),
              [](const ReferenceResult& lhs, const ReferenceResult& rhs) {
                  if (lhs.relationship.relationship.type != rhs.relationship.relationship.type) {
                      return static_cast<int>(lhs.relationship.relationship.type)
                          < static_cast<int>(rhs.relationship.relationship.type);
                  }
                  return referenceLocationLess(lhs.referencingSymbol, rhs.referencingSymbol);
              });
    return result;
}

ReferenceReport ReferenceService::findReferenceReport(const ReferenceQuery& query) const
{
    ReferenceReport report;
    report.references = findReferences(query);
    report.totalCount = report.references.size();
    QMap<QString, int> fileGroupIndexes;
    QMap<QString, QMap<SymbolRelationshipEngine::RelationType, int>> typeGroupIndexes;
    for (const ReferenceResult& reference : report.references) {
        const QString normalizedFile =
            normalizedReferenceFileName(reference.referencingSymbol.fileName);
        const QString fileKey = normalizedFile.isEmpty()
            ? reference.referencingSymbol.fileName
            : normalizedFile;
        const SymbolRelationshipEngine::RelationType type =
            reference.relationship.relationship.type;
        report.fileCounts[fileKey]++;
        report.typeCounts[type]++;
        report.fileTypeCounts[fileKey][type]++;

        if (!fileGroupIndexes.contains(fileKey)) {
            ReferenceFileGroup fileGroup;
            fileGroup.fileName = reference.referencingSymbol.fileName;
            fileGroup.fileKey = fileKey;
            fileGroup.displayName = QFileInfo(reference.referencingSymbol.fileName).fileName();
            if (fileGroup.displayName.isEmpty())
                fileGroup.displayName = reference.referencingSymbol.fileName;
            fileGroupIndexes.insert(fileKey, report.fileGroups.size());
            report.fileGroups.append(fileGroup);
        }

        ReferenceFileGroup& fileGroup =
            report.fileGroups[fileGroupIndexes.value(fileKey)];
        fileGroup.count++;

        if (!typeGroupIndexes[fileKey].contains(type)) {
            ReferenceTypeGroup typeGroup;
            typeGroup.type = type;
            typeGroupIndexes[fileKey].insert(type, fileGroup.typeGroups.size());
            fileGroup.typeGroups.append(typeGroup);
        }

        ReferenceTypeGroup& typeGroup =
            fileGroup.typeGroups[typeGroupIndexes[fileKey].value(type)];
        typeGroup.references.append(reference);
        typeGroup.count++;
    }
    return report;
}

bool ReferenceService::hasReferences(const ReferenceQuery& query) const
{
    return !findReferences(query).isEmpty();
}

SemanticIndex* ReferenceService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

int ReferenceService::resolveSymbolId(const ReferenceQuery& query) const
{
    if (query.symbolId >= 0)
        return query.symbolId;
    if (query.symbolName.isEmpty())
        return -1;

    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;
    return semanticIndex()->findSymbolId(query.symbolName, context);
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
                                    const sym_list::SymbolInfo& symbol) const
{
    const QString normalizedSource = normalizedReferenceFileName(symbol.fileName);
    if (query.currentFileOnly) {
        const QString normalizedCurrent = normalizedReferenceFileName(query.fileName);
        if (normalizedSource != normalizedCurrent)
            return false;
    }

    if (query.workspaceFilesOnly) {
        QSet<QString> workspaceFiles;
        for (const QString& file : query.workspaceFiles)
            workspaceFiles.insert(normalizedReferenceFileName(file));
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
    return result;
}
