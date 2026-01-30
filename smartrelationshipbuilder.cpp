#include "smartrelationshipbuilder.h"
#include <QRegularExpression>
#include <QApplication>
#include <algorithm>

SmartRelationshipBuilder::SmartRelationshipBuilder(SymbolRelationshipEngine* engine,
                                                 sym_list* symbolDatabase,
                                                 QObject *parent)
    : QObject(parent), relationshipEngine(engine), symbolDatabase(symbolDatabase)
{
    initializePatterns();
}

SmartRelationshipBuilder::~SmartRelationshipBuilder()
{
}

// 🚀 初始化分析模式（QRegularExpression 预编译，一次构造重复使用）
void SmartRelationshipBuilder::initializePatterns()
{
    // 🚀 模块实例化模式: module_name instance_name (
    patterns.moduleInstantiation = QRegularExpression("([a-zA-Z_][a-zA-Z0-9_]*)\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(");

    // 🚀 变量赋值模式: variable = expression
    patterns.variableAssignment = QRegularExpression("([a-zA-Z_][a-zA-Z0-9_]*)\\s*=\\s*([^;]+);");

    // 🚀 变量引用模式: 在表达式中的变量名
    patterns.variableReference = QRegularExpression("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b");

    // 🚀 task调用模式: task_name(args) 或 task_name;
    patterns.taskCall = QRegularExpression("([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(.*\\)\\s*;|([a-zA-Z_][a-zA-Z0-9_]*)\\s*;");

    // 🚀 function调用模式: function_name(args)
    patterns.functionCall = QRegularExpression("([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(.*\\)");

    // 🚀 always块模式
    patterns.alwaysBlock = QRegularExpression("always\\s*(@.*)?\\s*begin");

    // 🚀 generate块模式
    patterns.generateBlock = QRegularExpression("generate\\s*begin");
}

// 🚀 主要分析接口实现
void SmartRelationshipBuilder::analyzeFile(const QString& fileName, const QString& content)
{
    // 🚀 在开始前检查取消状态
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

        // 🚀 在各个分析步骤中检查取消状态
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

            analyzeInterfaceRelationships(content, context);
            if (checkCancellation(fileName)) return;

            analyzeClockResetRelationships(content, context);
            if (checkCancellation(fileName)) return;
        }

        // 计算发现的关系数量
        int relationshipsFound = relationshipEngine->getRelationshipCount();
        emit analysisCompleted(fileName, relationshipsFound);

    } catch (const std::exception& e) {
        if (!checkCancellation()) {
            emit analysisError(fileName, QString("Analysis failed: %1").arg(e.what()));
        }
    }
}

// 🚀 仅计算关系并返回，不写引擎（供后台线程调用；主线程用结果调用 engine->addRelationship）
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

            analyzeInterfaceRelationships(content, context);
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

// 🚀 设置分析上下文
void SmartRelationshipBuilder::setupAnalysisContext(const QString& fileName, AnalysisContext& context)
{
    context.currentFileName = fileName;
    context.fileSymbols = symbolDatabase->findSymbolsByFileName(fileName);
    context.localSymbolIds.clear();
    context.symbolIdToType.clear();

    // 🚀 构建本地符号映射
    for (const sym_list::SymbolInfo& symbol : qAsConst(context.fileSymbols)) {
        context.localSymbolIds[symbol.symbolName] = symbol.symbolId;
        context.symbolIdToType[symbol.symbolId] = symbol.symbolType;

        // 🚀 找到当前文件的主模块
        if (symbol.symbolType == sym_list::sym_module && context.currentModuleId == -1) {
            context.currentModuleName = symbol.symbolName;
            context.currentModuleId = symbol.symbolId;
        }
    }
}

// 🚀 从已有符号列表设置上下文（用于后台线程 computeRelationships，不访问 DB）
void SmartRelationshipBuilder::setupAnalysisContextFromSymbols(const QString& fileName,
                                                              const QList<sym_list::SymbolInfo>& fileSymbols,
                                                              AnalysisContext& context)
{
    context.currentFileName = fileName;
    context.fileSymbols = fileSymbols;
    context.localSymbolIds.clear();
    context.symbolIdToType.clear();

    for (const sym_list::SymbolInfo& symbol : qAsConst(fileSymbols)) {
        context.localSymbolIds[symbol.symbolName] = symbol.symbolId;
        context.symbolIdToType[symbol.symbolId] = symbol.symbolType;

        if (symbol.symbolType == sym_list::sym_module && context.currentModuleId == -1) {
            context.currentModuleName = symbol.symbolName;
            context.currentModuleId = symbol.symbolId;
        }
    }
}

// 🚀 分析模块实例化关系（lineMin/lineMax >= 0 时仅处理该行范围）
void SmartRelationshipBuilder::analyzeModuleInstantiations(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        if (lineMin >= 0 && (lineNum < lineMin || lineNum > lineMax))
            continue;
        const QString& line = lines[lineNum].trimmed();

        if (line.isEmpty() || line.startsWith("//")) continue;

        // 🚀 查找模块实例化
        QRegularExpressionMatchIterator it = patterns.moduleInstantiation.globalMatch(line);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString moduleTypeName = match.captured(1);
            QString instanceName = match.captured(2);

            // 🚀 查找被实例化的模块
            int moduleTypeId = findSymbolIdByName(moduleTypeName, context);
            if (moduleTypeId != -1 && context.currentModuleId != -1) {
                // 🚀 建立实例化关系
                addRelationshipWithContext(
                    context.currentModuleId,
                    moduleTypeId,
                    SymbolRelationshipEngine::INSTANTIATES,
                    QString("Instance: %1 at line %2").arg(instanceName).arg(lineNum + 1),
                    90
                );
            }
        }
    }
}

// 🚀 分析变量赋值关系（lineMin/lineMax >= 0 时仅处理该行范围）
void SmartRelationshipBuilder::analyzeVariableAssignments(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        if (lineMin >= 0 && (lineNum < lineMin || lineNum > lineMax))
            continue;
        const QString& line = lines[lineNum].trimmed();

        if (line.isEmpty() || line.startsWith("//")) continue;

        // 🚀 查找赋值语句
        QRegularExpressionMatchIterator assignIt = patterns.variableAssignment.globalMatch(line);
        while (assignIt.hasNext()) {
            QRegularExpressionMatch match = assignIt.next();
            QString leftVar = match.captured(1);
            QString rightExpr = match.captured(2);

            int leftVarId = findSymbolIdByName(leftVar, context);
            if (leftVarId != -1) {
                // 🚀 提取右侧表达式中的变量
                QStringList rightVars = extractVariablesFromExpression(rightExpr);

                for (const QString& rightVar : qAsConst(rightVars)) {
                    int rightVarId = findSymbolIdByName(rightVar, context);
                    if (rightVarId != -1 && rightVarId != leftVarId) {
                        // 🚀 建立引用关系: leftVar 引用 rightVar
                        addRelationshipWithContext(
                            leftVarId,
                            rightVarId,
                            SymbolRelationshipEngine::REFERENCES,
                            QString("Assignment at line %1").arg(lineNum + 1),
                            85
                        );

                        // 🚀 建立赋值关系: rightVar 被赋值给 leftVar
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

// 🚀 分析变量引用关系（lineMin/lineMax >= 0 时仅处理该行范围）
void SmartRelationshipBuilder::analyzeVariableReferences(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        if (lineMin >= 0 && (lineNum < lineMin || lineNum > lineMax))
            continue;
        const QString& line = lines[lineNum].trimmed();

        // 🚀 跳过声明行和注释
        static const QRegularExpression declPattern("\\b(reg|wire|logic|input|output)\\b");
        if (line.isEmpty() || line.startsWith("//") || line.contains(declPattern)) {
            continue;
        }

        // 🚀 在条件语句、case语句等中查找变量引用
        static const QRegularExpression condCheckPattern("\\b(if|case|while)\\s*\\(");
        if (line.contains(condCheckPattern)) {
            static const QRegularExpression conditionRegex("\\b(if|case|while)\\s*\\(([^)]+)\\)");
            QRegularExpressionMatch match = conditionRegex.match(line);
            if (match.hasMatch()) {
                QString condition = match.captured(2);
                QStringList referencedVars = extractVariablesFromExpression(condition);

                for (const QString& varName : qAsConst(referencedVars)) {
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

// 🚀 分析task和function调用关系（lineMin/lineMax >= 0 时仅处理该行范围）
void SmartRelationshipBuilder::analyzeTaskFunctionCalls(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        if (lineMin >= 0 && (lineNum < lineMin || lineNum > lineMax))
            continue;
        const QString& line = lines[lineNum].trimmed();

        if (line.isEmpty() || line.startsWith("//")) continue;

        // 🚀 查找task调用
        QRegularExpressionMatchIterator taskIt = patterns.taskCall.globalMatch(line);
        while (taskIt.hasNext()) {
            QRegularExpressionMatch match = taskIt.next();
            QString taskName = match.captured(1);
            if (taskName.isEmpty()) {
                taskName = match.captured(2);
            }

            // 🚀 验证这确实是一个task或function
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

// 🚀 分析always块关系（lineMin/lineMax >= 0 时仅处理该行范围）
void SmartRelationshipBuilder::analyzeAlwaysBlocks(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    QStringList lines = content.split('\n');

    static const QRegularExpression sensitivityRegex("always\\s*@\\s*\\(([^)]+)\\)");
    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        if (lineMin >= 0 && (lineNum < lineMin || lineNum > lineMax))
            continue;
        const QString& line = lines[lineNum];

        if (patterns.alwaysBlock.match(line).hasMatch()) {
            // 🚀 分析敏感信号列表
            QRegularExpressionMatch sensMatch = sensitivityRegex.match(line);
            if (sensMatch.hasMatch()) {
                QString sensitivityList = sensMatch.captured(1);
                QStringList signalNames = extractVariablesFromExpression(sensitivityList); // 重命名避免与Qt宏冲突

                for (const QString& signalName : qAsConst(signalNames)) { // 重命名避免与Qt宏冲突
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

// 🚀 分析时钟和复位关系（lineMin/lineMax >= 0 时仅处理该行范围）
void SmartRelationshipBuilder::analyzeClockResetRelationships(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        if (lineMin >= 0 && (lineNum < lineMin || lineNum > lineMax))
            continue;
        const QString& line = lines[lineNum].toLower();

        // 🚀 查找时钟信号
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

        // 🚀 查找复位信号
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

// 🚀 辅助方法实现

QStringList SmartRelationshipBuilder::extractVariablesFromExpression(const QString& expression)
{
    QStringList variables;
    QSet<QString> uniqueVars; // 避免重复

    // 🚀 使用正则表达式提取标识符（QRegularExpression）
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
    // 🚀 首先在本地符号映射中查找
    if (context.localSymbolIds.contains(symbolName)) {
        return context.localSymbolIds[symbolName];
    }

    // 🚀 如果没找到，在全局符号数据库中通过索引直接查 symbolId，避免临时 QList 分配
    int id = symbolDatabase->findSymbolIdByName(symbolName);
    if (id >= 0)
        return id;

    return -1; // 未找到
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

// 🚀 返回包含指定行的最内层模块的 symbolId，不存在则 -1
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

// 🚀 根据变更行计算受影响的符号 ID 集合（需在 setupAnalysisContext 之后调用）
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

// 🚀 高级分析方法的基础实现
void SmartRelationshipBuilder::analyzeInterfaceRelationships(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    // 🚀 TODO: 实现interface关系分析
    Q_UNUSED(content)
    Q_UNUSED(context)
    Q_UNUSED(lineMin)
    Q_UNUSED(lineMax)
}

void SmartRelationshipBuilder::analyzeParameterRelationships(const QString& content, AnalysisContext& context)
{
    // 🚀 TODO: 实现parameter关系分析
    Q_UNUSED(content)
    Q_UNUSED(context)
}

void SmartRelationshipBuilder::analyzeConstraintRelationships(const QString& content, AnalysisContext& context)
{
    // 🚀 TODO: 实现constraint关系分析
    Q_UNUSED(content)
    Q_UNUSED(context)
}

// 🚀 增量分析实现：仅移除受影响符号的关系并仅对变更行范围重新分析
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
    // 若变更行数超过文件行数约 30%，退化为全量分析，避免增量逻辑复杂且收益小
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

            analyzeInterfaceRelationships(content, context, minLine, maxLine);
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

// 🚀 特定关系类型分析的公共接口
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
    // 🚀 重置取消状态
    cancelled.store(false);

    int totalFiles = fileNames.size();
    int processedFiles = 0;

    for (const QString& fileName : fileNames) {
        // 🚀 检查取消状态
        if (checkCancellation()) {
            emit analysisError("", QString("Analysis cancelled after processing %1/%2 files")
                              .arg(processedFiles).arg(totalFiles));
            return;
        }

        if (!fileContents.contains(fileName)) {
            continue;
        }

        const QString& content = fileContents[fileName];

        // 🚀 分析单个文件
        analyzeFile(fileName, content);

        processedFiles++;

        // 🚀 每处理5个文件让出CPU时间，保持UI响应
        if (processedFiles % 5 == 0) {
            QApplication::processEvents();

            // 🚀 处理事件后再次检查取消状态
            if (checkCancellation()) {
                emit analysisError("", QString("Analysis cancelled after processing %1/%2 files")
                                  .arg(processedFiles).arg(totalFiles));
                return;
            }
        }
    }
}
