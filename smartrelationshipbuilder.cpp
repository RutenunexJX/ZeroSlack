#include "smartrelationshipbuilder.h"
#include <QApplication>
#include <algorithm>
#include <utility>

SmartRelationshipBuilder::SmartRelationshipBuilder(SymbolRelationshipEngine* engine,
                                                 sym_list* symbolDatabase,
                                                 SlangManager* slangManager,
                                                 QObject *parent)
    : QObject(parent), relationshipEngine(engine), symbolDatabase(symbolDatabase), m_slangManager(slangManager)
{
}

SmartRelationshipBuilder::~SmartRelationshipBuilder()
{
}

static bool isClockSignalName(const QString& signalName)
{
    const QString lower = signalName.toLower();
    return lower.contains(QLatin1String("clk")) || lower.contains(QLatin1String("clock"));
}

static bool isResetSignalName(const QString& signalName)
{
    const QString lower = signalName.toLower();
    return lower == QLatin1String("rst")
        || lower == QLatin1String("reset")
        || lower == QLatin1String("rstn")
        || lower == QLatin1String("rst_n")
        || lower.contains(QLatin1String("reset"))
        || lower.contains(QLatin1String("_rst"));
}

void SmartRelationshipBuilder::analyzeFile(const QString& fileName, const QString& content)
{
    if (checkCancellation(fileName)) {
        return;
    }

    if (!relationshipEngine || !symbolDatabase) {
        emit analysisError(fileName, "Missing relationship engine or symbol database");
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

QVector<RelationshipToAdd> SmartRelationshipBuilder::computeRelationships(const QString& fileName, const QString& content,
                                                                          const QList<sym_list::SymbolInfo>& fileSymbols)
{
    QVector<RelationshipToAdd> result;
    if (checkCancellation(fileName))
        return result;
    if (!symbolDatabase)
        return result;

    try {
        AnalysisContext context;
        setupAnalysisContextFromSymbols(fileName, fileSymbols, context);

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

void SmartRelationshipBuilder::setupAnalysisContext(const QString& fileName, AnalysisContext& context)
{
    context.currentFileName = fileName;
    context.fileSymbols = symbolDatabase->findSymbolsByFileName(fileName);
    context.localSymbolIds.clear();
    context.symbolIdToType.clear();

    for (const sym_list::SymbolInfo& symbol : std::as_const(context.fileSymbols)) {
        context.localSymbolIds[symbol.symbolName] = symbol.symbolId;
        context.symbolIdToType[symbol.symbolId] = symbol.symbolType;

        if (symbol.symbolType == sym_list::sym_module && context.currentModuleId == -1) {
            context.currentModuleName = symbol.symbolName;
            context.currentModuleId = symbol.symbolId;
        }
    }
}

void SmartRelationshipBuilder::setupAnalysisContextFromSymbols(const QString& fileName,
                                                              const QList<sym_list::SymbolInfo>& fileSymbols,
                                                              AnalysisContext& context)
{
    context.currentFileName = fileName;
    context.fileSymbols = fileSymbols;
    context.localSymbolIds.clear();
    context.symbolIdToType.clear();

    for (const sym_list::SymbolInfo& symbol : std::as_const(fileSymbols)) {
        context.localSymbolIds[symbol.symbolName] = symbol.symbolId;
        context.symbolIdToType[symbol.symbolId] = symbol.symbolType;

        if (symbol.symbolType == sym_list::sym_module && context.currentModuleId == -1) {
            context.currentModuleName = symbol.symbolName;
            context.currentModuleId = symbol.symbolId;
        }
    }
}

void SmartRelationshipBuilder::ensureRelationshipInfo(const QString& content, AnalysisContext& context)
{
    if (context.relationshipInfoLoaded || !m_slangManager)
        return;

    context.relationshipInfo = m_slangManager->extractRelationshipInfo(context.currentFileName, content);
    context.relationshipInfoLoaded = true;
}

void SmartRelationshipBuilder::analyzeModuleInstantiations(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    ensureRelationshipInfo(content, context);
    QSet<QString> emitted;
    for (const ModuleInstantiationInfo& info : std::as_const(context.relationshipInfo.moduleInstantiations)) {
        // lineMin/lineMax are 0-based; info.lineNumber is 1-based
        if (lineMin >= 0 && (info.lineNumber - 1 < lineMin || info.lineNumber - 1 > lineMax))
            continue;

        int moduleTypeId = findSymbolIdByName(info.moduleName, context);
        int ownerModuleId = getContainingModuleId(info.lineNumber, context);
        if (ownerModuleId == -1)
            ownerModuleId = context.currentModuleId;
        if (moduleTypeId != -1 && ownerModuleId != -1) {
            addRelationshipWithContext(
                ownerModuleId,
                moduleTypeId,
                SymbolRelationshipEngine::INSTANTIATES,
                QString("Instance: %1 at line %2").arg(info.instanceName).arg(info.lineNumber),
                90
            );
            emitted.insert(QStringLiteral("%1:%2").arg(ownerModuleId).arg(moduleTypeId));
        }
    }

    // Workspace-wide Slang symbol extraction can resolve cross-file instances even when
    // the single-file relationship parse treats the module type as unknown.
    for (const sym_list::SymbolInfo& symbol : std::as_const(context.fileSymbols)) {
        if (symbol.symbolType != sym_list::sym_inst || symbol.dataType.isEmpty())
            continue;
        if (lineMin >= 0 && (symbol.startLine - 1 < lineMin || symbol.startLine - 1 > lineMax))
            continue;

        int moduleTypeId = findSymbolIdByName(symbol.dataType, context);
        int ownerModuleId = getContainingModuleId(symbol.startLine, context);
        if (ownerModuleId == -1)
            ownerModuleId = context.currentModuleId;
        if (moduleTypeId != -1 && ownerModuleId != -1) {
            const QString key = QStringLiteral("%1:%2").arg(ownerModuleId).arg(moduleTypeId);
            if (emitted.contains(key))
                continue;

            addRelationshipWithContext(
                ownerModuleId,
                moduleTypeId,
                SymbolRelationshipEngine::INSTANTIATES,
                QString("Instance: %1 at line %2").arg(symbol.symbolName).arg(symbol.startLine),
                90
            );
            emitted.insert(key);
        }
    }
}

void SmartRelationshipBuilder::analyzeVariableAssignments(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    ensureRelationshipInfo(content, context);
    for (const AssignmentInfo& assignment : std::as_const(context.relationshipInfo.assignments)) {
        if (lineMin >= 0 && (assignment.lineNumber - 1 < lineMin || assignment.lineNumber - 1 > lineMax))
            continue;

        int leftVarId = findSymbolIdByName(assignment.leftName, context);
        if (leftVarId == -1)
            continue;

        for (const QString& rightVar : assignment.rightNames) {
            int rightVarId = findSymbolIdByName(rightVar, context);
            if (rightVarId != -1 && rightVarId != leftVarId) {
                addRelationshipWithContext(
                    leftVarId,
                    rightVarId,
                    SymbolRelationshipEngine::REFERENCES,
                    QString("Assignment at line %1").arg(assignment.lineNumber),
                    85
                );

                addRelationshipWithContext(
                    rightVarId,
                    leftVarId,
                    SymbolRelationshipEngine::ASSIGNS_TO,
                    QString("Assigned to %1 at line %2").arg(assignment.leftName).arg(assignment.lineNumber),
                    85
                );
            }
        }
    }
}

void SmartRelationshipBuilder::analyzeVariableReferences(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    ensureRelationshipInfo(content, context);
    for (const ConditionReferenceInfo& ref : std::as_const(context.relationshipInfo.conditionReferences)) {
        if (lineMin >= 0 && (ref.lineNumber - 1 < lineMin || ref.lineNumber - 1 > lineMax))
            continue;

        for (const QString& varName : ref.symbolNames) {
            int varId = findSymbolIdByName(varName, context);
            int ownerModuleId = getContainingModuleId(ref.lineNumber, context);
            if (ownerModuleId == -1)
                ownerModuleId = context.currentModuleId;
            if (varId != -1 && ownerModuleId != -1) {
                addRelationshipWithContext(
                    ownerModuleId,
                    varId,
                    SymbolRelationshipEngine::READS_FROM,
                    QString("Condition check at line %1").arg(ref.lineNumber),
                    70
                );
            }
        }
    }
}

void SmartRelationshipBuilder::analyzeTaskFunctionCalls(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    ensureRelationshipInfo(content, context);
    for (const SubroutineCallInfo& call : std::as_const(context.relationshipInfo.subroutineCalls)) {
        if (lineMin >= 0 && (call.lineNumber - 1 < lineMin || call.lineNumber - 1 > lineMax))
            continue;

        int taskId = findSymbolIdByName(call.subroutineName, context);
        if (taskId == -1)
            continue;

        sym_list::sym_type_e taskType = sym_list::sym_user;
        if (context.symbolIdToType.contains(taskId))
            taskType = context.symbolIdToType[taskId];
        else
            taskType = symbolDatabase->getSymbolById(taskId).symbolType;

        if (taskType != sym_list::sym_task && taskType != sym_list::sym_function)
            continue;

        int ownerModuleId = getContainingModuleId(call.lineNumber, context);
        if (ownerModuleId == -1)
            ownerModuleId = context.currentModuleId;
        if (ownerModuleId != -1) {
            addRelationshipWithContext(
                ownerModuleId,
                taskId,
                SymbolRelationshipEngine::CALLS,
                QString("Called at line %1").arg(call.lineNumber),
                95
            );
        }
    }
}

void SmartRelationshipBuilder::analyzeAlwaysBlocks(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    ensureRelationshipInfo(content, context);
    for (const TimingSignalInfo& signal : std::as_const(context.relationshipInfo.timingSignals)) {
        if (lineMin >= 0 && (signal.lineNumber - 1 < lineMin || signal.lineNumber - 1 > lineMax))
            continue;

        int signalId = findSymbolIdByName(signal.signalName, context);
        int ownerModuleId = getContainingModuleId(signal.lineNumber, context);
        if (ownerModuleId == -1)
            ownerModuleId = context.currentModuleId;
        if (signalId != -1 && ownerModuleId != -1) {
            addRelationshipWithContext(
                ownerModuleId,
                signalId,
                SymbolRelationshipEngine::READS_FROM,
                QString("Timing sensitivity at line %1").arg(signal.lineNumber),
                80
            );
        }
    }
}

void SmartRelationshipBuilder::analyzeClockResetRelationships(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    ensureRelationshipInfo(content, context);
    for (const TimingSignalInfo& signal : std::as_const(context.relationshipInfo.timingSignals)) {
        if (lineMin >= 0 && (signal.lineNumber - 1 < lineMin || signal.lineNumber - 1 > lineMax))
            continue;

        int ownerModuleId = getContainingModuleId(signal.lineNumber, context);
        if (ownerModuleId == -1)
            ownerModuleId = context.currentModuleId;
        if (ownerModuleId == -1)
            continue;

        if (signal.edgeSensitive && isClockSignalName(signal.signalName)) {
            int clockId = findSymbolIdByName(signal.signalName, context);
            if (clockId != -1) {
                addRelationshipWithContext(
                    clockId,
                    ownerModuleId,
                    SymbolRelationshipEngine::CLOCKS,
                    QString("Clock domain at line %1").arg(signal.lineNumber),
                    95
                );
            }
        }

        if (isResetSignalName(signal.signalName)) {
            int resetId = findSymbolIdByName(signal.signalName, context);
            if (resetId != -1) {
                addRelationshipWithContext(
                    resetId,
                    ownerModuleId,
                    SymbolRelationshipEngine::RESETS,
                    QString("Reset signal at line %1").arg(signal.lineNumber),
                    90
                );
            }
        }
    }
}

int SmartRelationshipBuilder::findSymbolIdByName(const QString& symbolName, const AnalysisContext& context)
{
    if (context.localSymbolIds.contains(symbolName)) {
        return context.localSymbolIds[symbolName];
    }

    int id = symbolDatabase->findSymbolIdByName(symbolName);
    if (id >= 0)
        return id;

    return -1;
}

void SmartRelationshipBuilder::addRelationshipWithContext(int fromId, int toId,
                                                        SymbolRelationshipEngine::RelationType type,
                                                        const QString& context, int confidence)
{
    if (confidence < confidenceThreshold)
        return;
    if (collectResults) {
        collectResults->append({fromId, toId, type, context, confidence});
        return;
    }
    if (relationshipEngine)
        relationshipEngine->addRelationship(fromId, toId, type, context, confidence);
}

int SmartRelationshipBuilder::getContainingModuleId(int lineNumber, const AnalysisContext& context)
{
    int foundId = -1;
    int foundStart = -1;
    for (const sym_list::SymbolInfo& s : context.fileSymbols) {
        if (s.symbolType == sym_list::sym_module
            && s.startLine <= lineNumber
            && s.endLine >= lineNumber
            && (foundId < 0 || s.startLine > foundStart)) {
            foundId = s.symbolId;
            foundStart = s.startLine;
        }
    }
    return foundId;
}

QString SmartRelationshipBuilder::findContainingModule(int lineNumber, const AnalysisContext& context)
{
    int id = getContainingModuleId(lineNumber, context);
    if (id < 0) return QString();
    for (const sym_list::SymbolInfo& s : context.fileSymbols) {
        if (s.symbolId == id) return s.symbolName;
    }
    return QString();
}

QSet<int> SmartRelationshipBuilder::getAffectedSymbolIds(const QString& content, const QList<int>& changedLines, AnalysisContext& context)
{
    QSet<int> affectedIds;
    if (changedLines.isEmpty()) return affectedIds;

    QStringList lines = content.split('\n');
    int numLines = lines.size();
    int minChanged = *std::min_element(changedLines.begin(), changedLines.end());
    int maxChanged = *std::max_element(changedLines.begin(), changedLines.end());
    int minLine = qMax(0, minChanged - 2);
    int maxLine = qMin(numLines - 1, maxChanged + 2);

    for (const sym_list::SymbolInfo& s : context.fileSymbols) {
        if (s.startLine >= minLine && s.startLine <= maxLine)
            affectedIds.insert(s.symbolId);
    }
    for (int lineNum : changedLines) {
        int mid = getContainingModuleId(lineNum, context);
        if (mid >= 0)
            affectedIds.insert(mid);
    }
    return affectedIds;
}

void SmartRelationshipBuilder::analyzeParameterRelationships(const QString& content, AnalysisContext& context)
{
    Q_UNUSED(content)
    Q_UNUSED(context)
}

void SmartRelationshipBuilder::analyzeConstraintRelationships(const QString& content, AnalysisContext& context)
{
    Q_UNUSED(content)
    Q_UNUSED(context)
}

void SmartRelationshipBuilder::analyzeFileIncremental(const QString& fileName, const QString& content,
                                                     const QList<int>& changedLines)
{
    if (changedLines.isEmpty()) {
        return;
    }

    if (!relationshipEngine || !symbolDatabase) {
        emit analysisError(fileName, "Missing relationship engine or symbol database");
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

    QSet<int> affectedIds = getAffectedSymbolIds(content, changedLines, context);
    for (int symbolId : affectedIds) {
        relationshipEngine->removeAllRelationships(symbolId);
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
