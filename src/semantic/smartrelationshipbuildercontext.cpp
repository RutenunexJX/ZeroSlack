#include "smartrelationshipbuilder.h"

#include "semanticindex.h"
#include "symboltaxonomy.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace {
QString relationshipSourceKey(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString key = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
}

void SmartRelationshipBuilder::setupAnalysisContextFromRecords(
    const QString& fileName,
    const QList<SemanticSymbolRecord>& fileSymbolRecords,
    const SemanticIndexSnapshot* snapshot,
    AnalysisContext& context)
{
    context.currentFileName = fileName;
    context.fileSymbolRecords = fileSymbolRecords;
    context.localSymbolHandles.clear();
    context.recordsByName.clear();
    context.recordsByLocalHandle.clear();
    context.symbolRecordLookupCache.clear();
    context.containingModuleHandleByLine.clear();
    context.moduleRecords.clear();
    context.snapshot = snapshot;
    context.recordsByName.reserve(fileSymbolRecords.size());
    context.recordsByLocalHandle.reserve(fileSymbolRecords.size());

    for (const SemanticSymbolRecord& record :
         std::as_const(fileSymbolRecords)) {
        context.localSymbolHandles[record.name] = record.localHandle;
        context.recordsByName[record.name].append(record);
        if (record.localHandle >= 0)
            context.recordsByLocalHandle.insert(record.localHandle, record);

        if (SymbolTaxonomy::isModuleDeclaration(
                semanticMetadataForSymbolRecord(record))) {
            context.moduleRecords.append(record);
            if (context.currentModuleLocalHandle == -1) {
                context.currentModuleName = record.name;
                context.currentModuleLocalHandle = record.localHandle;
            }
        }
    }
}

void SmartRelationshipBuilder::ensureRelationshipInfo(
    const QString& content,
    AnalysisContext& context)
{
    if (context.relationshipInfoLoaded)
        return;
    context.relationshipInfoLoaded = true;
    if (!m_slangManager || context.currentFileName.isEmpty()
        || checkCancellation(context.currentFileName)) {
        return;
    }

    const QString absoluteFileName = QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(context.currentFileName).absoluteFilePath()));
    const QHash<QString, QString> contents{{absoluteFileName, content}};
    const QHash<QString, RelationshipExtractionInfo> extracted =
        extractOverlayWorkspaceRelationshipInfo(contents,
                                                context.includeDirs,
                                                context.defines,
                                                {absoluteFileName});
    const QString requestedKey = relationshipSourceKey(absoluteFileName);
    for (auto it = extracted.constBegin(); it != extracted.constEnd(); ++it) {
        if (relationshipSourceKey(it.key()) != requestedKey)
            continue;
        context.relationshipInfo = it.value();
        return;
    }
}
