#include "smartrelationshipbuilder.h"
#include <QRegularExpression>
#include <QApplication>
#include <algorithm>
#include <utility>

SmartRelationshipBuilder::SmartRelationshipBuilder(SymbolRelationshipEngine* engine,
                                                 sym_list* symbolDatabase,
                                                 SlangManager* slangManager,
                                                 QObject *parent)
    : QObject(parent), relationshipEngine(engine), symbolDatabase(symbolDatabase), m_slangManager(slangManager)
{
    initializePatterns();
}

SmartRelationshipBuilder::~SmartRelationshipBuilder()
{
}

void SmartRelationshipBuilder::initializePatterns()
{
    patterns.variableAssignment = QRegularExpression("([a-zA-Z_][a-zA-Z0-9_]*)\\s*=\\s*([^;]+);");
    patterns.variableReference = QRegularExpression("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b");
    patterns.taskCall = QRegularExpression("([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(.*\\)\\s*;|([a-zA-Z_][a-zA-Z0-9_]*)\\s*;");
    patterns.functionCall = QRegularExpression("([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(.*\\)");
    patterns.alwaysBlock = QRegularExpression("always\\s*(@.*)?\\s*begin");
    patterns.generateBlock = QRegularExpression("generate\\s*begin");
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

void SmartRelationshipBuilder::analyzeModuleInstantiations(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    QVector<ModuleInstantiationInfo> insts = m_slangManager->extractModuleInstantiations(context.currentFileName, content);
    for (const ModuleInstantiationInfo& info : std::as_const(insts)) {
        // lineMin/lineMax are 0-based; info.lineNumber is 1-based
        if (lineMin >= 0 && (info.lineNumber - 1 < lineMin || info.lineNumber - 1 > lineMax))
            continue;

        int moduleTypeId = findSymbolIdByName(info.moduleName, context);
        if (moduleTypeId != -1 && context.currentModuleId != -1) {
            addRelationshipWithContext(
                context.currentModuleId,
                moduleTypeId,
                SymbolRelationshipEngine::INSTANTIATES,
                QString("Instance: %1 at line %2").arg(info.instanceName).arg(info.lineNumber),
                90
            );
        }
    }
}

void SmartRelationshipBuilder::analyzeVariableAssignments(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        if (lineMin >= 0 && (lineNum < lineMin || lineNum > lineMax))
            continue;
        const QString& line = lines[lineNum].trimmed();

        if (line.isEmpty() || line.startsWith("//")) continue;

        QRegularExpressionMatchIterator assignIt = patterns.variableAssignment.globalMatch(line);
        while (assignIt.hasNext()) {
            QRegularExpressionMatch match = assignIt.next();
            QString leftVar = match.captured(1);
            QString rightExpr = match.captured(2);

            int leftVarId = findSymbolIdByName(leftVar, context);
            if (leftVarId != -1) {
                QStringList rightVars = extractVariablesFromExpression(rightExpr);

                for (const QString& rightVar : std::as_const(rightVars)) {
                    int rightVarId = findSymbolIdByName(rightVar, context);
                    if (rightVarId != -1 && rightVarId != leftVarId) {
                        addRelationshipWithContext(
                            leftVarId,
                            rightVarId,
                            SymbolRelationshipEngine::REFERENCES,
                            QString("Assignment at line %1").arg(lineNum + 1),
                            85
                        );

                        addRelationshipWithContext(
                            rightVarId,
                            leftVarId,
                            SymbolRelationshipEngine::ASSIGNS_TO,
                            QString("Assigned to %1 at line %2").arg(leftVar).arg(lineNum + 1),
                            85
                        );
                    }
                }
            }
        }
    }
}

void SmartRelationshipBuilder::analyzeVariableReferences(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        if (lineMin >= 0 && (lineNum < lineMin || lineNum > lineMax))
            continue;
        const QString& line = lines[lineNum].trimmed();

        static const QRegularExpression declPattern("\\b(reg|wire|logic|input|output)\\b");
        if (line.isEmpty() || line.startsWith("//") || line.contains(declPattern)) {
            continue;
        }

        static const QRegularExpression condCheckPattern("\\b(if|case|while)\\s*\\(");
        if (line.contains(condCheckPattern)) {
            static const QRegularExpression conditionRegex("\\b(if|case|while)\\s*\\(([^)]+)\\)");
            QRegularExpressionMatch match = conditionRegex.match(line);
            if (match.hasMatch()) {
                QString condition = match.captured(2);
                QStringList referencedVars = extractVariablesFromExpression(condition);

                for (const QString& varName : std::as_const(referencedVars)) {
                    int varId = findSymbolIdByName(varName, context);
                    if (varId != -1 && context.currentModuleId != -1) {
                        addRelationshipWithContext(
                            context.currentModuleId,
                            varId,
                            SymbolRelationshipEngine::READS_FROM,
                            QString("Condition check at line %1").arg(lineNum + 1),
                            70
                        );
                    }
                }
            }
        }
    }
}

void SmartRelationshipBuilder::analyzeTaskFunctionCalls(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        if (lineMin >= 0 && (lineNum < lineMin || lineNum > lineMax))
            continue;
        const QString& line = lines[lineNum].trimmed();

        if (line.isEmpty() || line.startsWith("//")) continue;

        QRegularExpressionMatchIterator taskIt = patterns.taskCall.globalMatch(line);
        while (taskIt.hasNext()) {
            QRegularExpressionMatch match = taskIt.next();
            QString taskName = match.captured(1);
            if (taskName.isEmpty()) {
                taskName = match.captured(2);
            }

            int taskId = findSymbolIdByName(taskName, context);
            if (taskId != -1) {
                sym_list::sym_type_e taskType = sym_list::sym_user;
                if (context.symbolIdToType.contains(taskId))
                    taskType = context.symbolIdToType[taskId];
                else
                    taskType = symbolDatabase->getSymbolById(taskId).symbolType;
                if (taskType == sym_list::sym_task || taskType == sym_list::sym_function) {

                    if (context.currentModuleId != -1) {
                        addRelationshipWithContext(
                            context.currentModuleId,
                            taskId,
                            SymbolRelationshipEngine::CALLS,
                            QString("Called at line %1").arg(lineNum + 1),
                            90
                        );
                    }
                }
            }
        }
    }
}

void SmartRelationshipBuilder::analyzeAlwaysBlocks(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    QStringList lines = content.split('\n');

    static const QRegularExpression sensitivityRegex("always\\s*@\\s*\\(([^)]+)\\)");
    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        if (lineMin >= 0 && (lineNum < lineMin || lineNum > lineMax))
            continue;
        const QString& line = lines[lineNum];

        if (patterns.alwaysBlock.match(line).hasMatch()) {
            QRegularExpressionMatch sensMatch = sensitivityRegex.match(line);
            if (sensMatch.hasMatch()) {
                QString sensitivityList = sensMatch.captured(1);
                QStringList signalNames = extractVariablesFromExpression(sensitivityList);

                for (const QString& signalName : std::as_const(signalNames)) {
                    int signalId = findSymbolIdByName(signalName, context);
                    if (signalId != -1 && context.currentModuleId != -1) {
                        addRelationshipWithContext(
                            context.currentModuleId,
                            signalId,
                            SymbolRelationshipEngine::READS_FROM,
                            QString("Always block sensitivity at line %1").arg(lineNum + 1),
                            80
                        );
                    }
                }
            }
        }
    }
}

void SmartRelationshipBuilder::analyzeClockResetRelationships(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        if (lineMin >= 0 && (lineNum < lineMin || lineNum > lineMax))
            continue;
        const QString& line = lines[lineNum].toLower();

        static const QRegularExpression clkPattern("\\b(clk|clock)\\b");
        static const QRegularExpression edgePattern("\\b(posedge|negedge)\\b");
        static const QRegularExpression clockRegex("(posedge|negedge)\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
        if (line.contains(clkPattern) && line.contains(edgePattern)) {
            QRegularExpressionMatch match = clockRegex.match(line);
            if (match.hasMatch()) {
                QString clockName = match.captured(2);
                int clockId = findSymbolIdByName(clockName, context);

                if (clockId != -1 && context.currentModuleId != -1) {
                    addRelationshipWithContext(
                        clockId,
                        context.currentModuleId,
                        SymbolRelationshipEngine::CLOCKS,
                        QString("Clock domain at line %1").arg(lineNum + 1),
                        95
                    );
                }
            }
        }

        static const QRegularExpression resetRegex("\\b(rst|reset|rstn|rst_n)\\b");
        if (line.contains(resetRegex)) {
            QRegularExpressionMatchIterator resetIt = resetRegex.globalMatch(line);
            while (resetIt.hasNext()) {
                QRegularExpressionMatch match = resetIt.next();
                QString resetName = match.captured(1);
                int resetId = findSymbolIdByName(resetName, context);

                if (resetId != -1 && context.currentModuleId != -1) {
                    addRelationshipWithContext(
                        resetId,
                        context.currentModuleId,
                        SymbolRelationshipEngine::RESETS,
                        QString("Reset signal at line %1").arg(lineNum + 1),
                        90
                    );
                }
            }
        }
    }
}

QStringList SmartRelationshipBuilder::extractVariablesFromExpression(const QString& expression)
{
    QStringList variables;
    QSet<QString> uniqueVars;

    static const QRegularExpression identifierRegex("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b");
    QRegularExpressionMatchIterator it = identifierRegex.globalMatch(expression);

    static const QSet<QString> svKeywords = {
        "and", "or", "not", "begin", "end", "if", "else", "case", "default",
        "posedge", "negedge", "assign", "always", "initial", "reg", "wire",
        "logic", "input", "output", "inout", "module", "endmodule"
    };

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString identifier = match.captured(1);

        if (!svKeywords.contains(identifier.toLower()) && !uniqueVars.contains(identifier)) {
            uniqueVars.insert(identifier);
            variables.append(identifier);
        }
    }

    return variables;
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
