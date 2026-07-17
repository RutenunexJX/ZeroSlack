#include "smartrelationshipbuilder.h"
#include "semanticindex.h"
#include <utility>

SmartRelationshipBuilder::SmartRelationshipBuilder(
    SymbolRelationshipEngine* engine,
    SlangManager* slangManager,
    SymbolRecordProvider symbolRecordProvider,
    QObject *parent)
    : QObject(parent),
      relationshipEngine(engine),
      m_slangManager(slangManager),
      m_symbolRecordProvider(std::move(symbolRecordProvider))
{
}
SmartRelationshipBuilder::~SmartRelationshipBuilder() = default;

QVector<RelationshipToAdd> SmartRelationshipBuilder::computeRelationships(
    const QString& fileName,
    const QString& content,
    const QList<SemanticSymbolRecord>& fileSymbolRecords,
    const SemanticIndexSnapshot* snapshot)
{
    return computeRelationships(fileName,
                                content,
                                fileSymbolRecords,
                                snapshot,
                                QStringList(),
                                QHash<QString, QString>());
}

QVector<RelationshipToAdd> SmartRelationshipBuilder::computeRelationships(
    const QString& fileName,
    const QString& content,
    const QList<SemanticSymbolRecord>& fileSymbolRecords,
    const SemanticIndexSnapshot* snapshot,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines,
    const RelationshipExtractionInfo* precomputedRelationshipInfo)
{
    QVector<RelationshipToAdd> result;
    if (checkCancellation(fileName))
        return result;
    if (fileSymbolRecords.isEmpty())
        return result;

    try {
        AnalysisContext context;
        setupAnalysisContextFromRecords(fileName,
                                        fileSymbolRecords,
                                        snapshot,
                                        context);
        context.includeDirs = includeDirs;
        context.defines = defines;
        if (precomputedRelationshipInfo) {
            context.relationshipInfo = *precomputedRelationshipInfo;
            context.relationshipInfoLoaded = true;
        }

        collectResults = &result;

        analyzeModuleInstantiations(content, context);
        if (checkCancellation(fileName)) { collectResults = nullptr; return result; }

        analyzeVariableAssignments(content, context);
        if (checkCancellation(fileName)) { collectResults = nullptr; return result; }

        analyzeVariableReferences(content, context);
        if (checkCancellation(fileName)) { collectResults = nullptr; return result; }

        analyzeTaskFunctionCalls(content, context);
        if (checkCancellation(fileName)) { collectResults = nullptr; return result; }

        if (enableAdvancedAnalysis) {
            analyzeAlwaysBlocks(content, context);
            if (checkCancellation(fileName)) { collectResults = nullptr; return result; }

            analyzeClockResetRelationships(content, context);
            if (checkCancellation(fileName)) { collectResults = nullptr; return result; }
        }

        collectResults = nullptr;
    } catch (...) {
        collectResults = nullptr;
    }
    return result;
}

QHash<QString, RelationshipExtractionInfo>
SmartRelationshipBuilder::extractWorkspaceRelationshipInfo(
    const QStringList& filePaths,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines) const
{
    if (!m_slangManager)
        return {};
    return m_slangManager->extractWorkspaceRelationshipInfo(filePaths,
                                                            includeDirs,
                                                            defines,
                                                            [this]() {
                                                                return isCancelled();
                                                            });
}

void SmartRelationshipBuilder::cancelAnalysis()
{
    cancelled.store(true);
    emit analysisCancelled();
}

void SmartRelationshipBuilder::resetCancellation()
{
    cancelled.store(false);
}

bool SmartRelationshipBuilder::checkCancellation(const QString& currentFile)
{
    if (cancelled.load()) {
        if (!currentFile.isEmpty()) {
            emit analysisError(currentFile, "Analysis cancelled by user");
        }
        return true;
    }
    return false;
}
