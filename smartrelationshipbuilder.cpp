#include "smartrelationshipbuilder.h"
#include "semanticindex.h"
#include <QApplication>
#include <algorithm>
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

SmartRelationshipBuilder::~SmartRelationshipBuilder()
{
}

void SmartRelationshipBuilder::analyzeFile(const QString& fileName, const QString& content)
{
    if (checkCancellation(fileName)) {
        return;
    }

    if (!relationshipEngine) {
        emit analysisError(fileName, "Missing relationship engine");
        return;
    }

    try {
        AnalysisContext context;
        setupAnalysisContext(fileName, context);

        analyzeModuleInstantiations(content, context);
        if (checkCancellation(fileName)) return;

        analyzeVariableAssignments(content, context);
        if (checkCancellation(fileName)) return;

        analyzeVariableReferences(content, context);
        if (checkCancellation(fileName)) return;

        analyzeTaskFunctionCalls(content, context);
        if (checkCancellation(fileName)) return;

        if (enableAdvancedAnalysis) {
            analyzeAlwaysBlocks(content, context);
            if (checkCancellation(fileName)) return;

            analyzeClockResetRelationships(content, context);
            if (checkCancellation(fileName)) return;
        }

        int relationshipsFound = relationshipEngine->getRelationshipCount();
        emit analysisCompleted(fileName, relationshipsFound);

    } catch (const std::exception& e) {
        if (!checkCancellation()) {
            emit analysisError(fileName, QString("Analysis failed: %1").arg(e.what()));
        }
    }
}

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

void SmartRelationshipBuilder::analyzeFileIncremental(const QString& fileName, const QString& content,
                                                     const QList<int>& changedLines)
{
    if (changedLines.isEmpty()) {
        return;
    }

    if (!relationshipEngine) {
        emit analysisError(fileName, "Missing relationship engine");
        return;
    }

    AnalysisContext context;
    setupAnalysisContext(fileName, context);

    QStringList lines = content.split('\n');
    int numLines = lines.size();
    if (numLines == 0) return;

    int minChanged = *std::min_element(changedLines.begin(), changedLines.end());
    int maxChanged = *std::max_element(changedLines.begin(), changedLines.end());
    int rangeLines = maxChanged - minChanged + 1;
    if (rangeLines > numLines * 3 / 10) {
        analyzeFile(fileName, content);
        return;
    }

    int minLine = qMax(0, minChanged - 2);
    int maxLine = qMin(numLines - 1, maxChanged + 2);

    QSet<int> affectedHandles =
        getAffectedSymbolLocalHandles(content, changedLines, context);
    for (int localHandle : affectedHandles) {
        relationshipEngine->removeAllRelationships(localHandle);
    }

    try {
        analyzeModuleInstantiations(content, context, minLine, maxLine);
        if (checkCancellation(fileName)) return;

        analyzeVariableAssignments(content, context, minLine, maxLine);
        if (checkCancellation(fileName)) return;

        analyzeVariableReferences(content, context, minLine, maxLine);
        if (checkCancellation(fileName)) return;

        analyzeTaskFunctionCalls(content, context, minLine, maxLine);
        if (checkCancellation(fileName)) return;

        if (enableAdvancedAnalysis) {
            analyzeAlwaysBlocks(content, context, minLine, maxLine);
            if (checkCancellation(fileName)) return;

            analyzeClockResetRelationships(content, context, minLine, maxLine);
        }

        int relationshipsFound = relationshipEngine->getRelationshipCount();
        emit analysisCompleted(fileName, relationshipsFound);
    } catch (const std::exception& e) {
        if (!checkCancellation()) {
            emit analysisError(fileName, QString("Incremental analysis failed: %1").arg(e.what()));
        }
    }
}

void SmartRelationshipBuilder::analyzeModuleRelationships(const QString& fileName, const QString& content)
{
    AnalysisContext context;
    setupAnalysisContext(fileName, context);
    analyzeModuleInstantiations(content, context);
}

void SmartRelationshipBuilder::analyzeVariableRelationships(const QString& fileName, const QString& content)
{
    AnalysisContext context;
    setupAnalysisContext(fileName, context);
    analyzeVariableAssignments(content, context);
    analyzeVariableReferences(content, context);
}

void SmartRelationshipBuilder::analyzeTaskFunctionRelationships(const QString& fileName, const QString& content)
{
    AnalysisContext context;
    setupAnalysisContext(fileName, context);
    analyzeTaskFunctionCalls(content, context);
}

void SmartRelationshipBuilder::analyzeAssignmentRelationships(const QString& fileName, const QString& content)
{
    AnalysisContext context;
    setupAnalysisContext(fileName, context);
    analyzeVariableAssignments(content, context);
}

void SmartRelationshipBuilder::analyzeInstantiationRelationships(const QString& fileName, const QString& content)
{
    AnalysisContext context;
    setupAnalysisContext(fileName, context);
    analyzeModuleInstantiations(content, context);
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

void SmartRelationshipBuilder::analyzeMultipleFiles(const QStringList& fileNames,
                                                   const QHash<QString, QString>& fileContents)
{
    cancelled.store(false);

    int totalFiles = fileNames.size();
    int processedFiles = 0;

    for (const QString& fileName : fileNames) {
        if (checkCancellation()) {
            emit analysisError("", QString("Analysis cancelled after processing %1/%2 files")
                              .arg(processedFiles).arg(totalFiles));
            return;
        }

        if (!fileContents.contains(fileName)) {
            continue;
        }

        const QString& content = fileContents[fileName];

        analyzeFile(fileName, content);

        processedFiles++;

        if (processedFiles % 5 == 0) {
            QApplication::processEvents();

            if (checkCancellation()) {
                emit analysisError("", QString("Analysis cancelled after processing %1/%2 files")
                                  .arg(processedFiles).arg(totalFiles));
                return;
            }
        }
    }
}
