#include "smartrelationshipbuilder.h"
#include <QRegExp>
#include <QApplication>

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

// 🚀 初始化分析模式
void SmartRelationshipBuilder::initializePatterns()
{
    // 🚀 模块实例化模式: module_name instance_name (
    patterns.moduleInstantiation = QRegExp("([a-zA-Z_][a-zA-Z0-9_]*)\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(");

    // 🚀 变量赋值模式: variable = expression
    patterns.variableAssignment = QRegExp("([a-zA-Z_][a-zA-Z0-9_]*)\\s*=\\s*([^;]+);");

    // 🚀 变量引用模式: 在表达式中的变量名
    patterns.variableReference = QRegExp("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b");

    // 🚀 task调用模式: task_name(args) 或 task_name;
    patterns.taskCall = QRegExp("([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(.*\\)\\s*;|([a-zA-Z_][a-zA-Z0-9_]*)\\s*;");

    // 🚀 function调用模式: function_name(args)
    patterns.functionCall = QRegExp("([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(.*\\)");

    // 🚀 always块模式
    patterns.alwaysBlock = QRegExp("always\\s*(@.*)?\\s*begin");

    // 🚀 generate块模式
    patterns.generateBlock = QRegExp("generate\\s*begin");
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

// 🚀 分析模块实例化关系
void SmartRelationshipBuilder::analyzeModuleInstantiations(const QString& content, AnalysisContext& context)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        const QString& line = lines[lineNum].trimmed();

        if (line.isEmpty() || line.startsWith("//")) continue;

        // 🚀 查找模块实例化
        int pos = 0;
        while ((pos = patterns.moduleInstantiation.indexIn(line, pos)) != -1) {
            QString moduleTypeName = patterns.moduleInstantiation.cap(1);
            QString instanceName = patterns.moduleInstantiation.cap(2);

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

            pos += patterns.moduleInstantiation.matchedLength();
        }
    }
}

// 🚀 分析变量赋值关系
void SmartRelationshipBuilder::analyzeVariableAssignments(const QString& content, AnalysisContext& context)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        const QString& line = lines[lineNum].trimmed();

        if (line.isEmpty() || line.startsWith("//")) continue;

        // 🚀 查找赋值语句
        int pos = 0;
        while ((pos = patterns.variableAssignment.indexIn(line, pos)) != -1) {
            QString leftVar = patterns.variableAssignment.cap(1);
            QString rightExpr = patterns.variableAssignment.cap(2);

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

            pos += patterns.variableAssignment.matchedLength();
        }
    }
}

// 🚀 分析变量引用关系
void SmartRelationshipBuilder::analyzeVariableReferences(const QString& content, AnalysisContext& context)
{
    // 🚀 这是一个更复杂的分析，需要识别各种上下文中的变量引用
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        const QString& line = lines[lineNum].trimmed();

        // 🚀 跳过声明行和注释
        if (line.isEmpty() || line.startsWith("//") ||
            line.contains(QRegExp("\\b(reg|wire|logic|input|output)\\b"))) {
            continue;
        }

        // 🚀 在条件语句、case语句等中查找变量引用
        if (line.contains(QRegExp("\\b(if|case|while)\\s*\\("))) {
            // 提取条件表达式中的变量
            QRegExp conditionRegex("\\b(if|case|while)\\s*\\(([^)]+)\\)");
            if (conditionRegex.indexIn(line) != -1) {
                QString condition = conditionRegex.cap(2);
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

// 🚀 分析task和function调用关系
void SmartRelationshipBuilder::analyzeTaskFunctionCalls(const QString& content, AnalysisContext& context)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        const QString& line = lines[lineNum].trimmed();

        if (line.isEmpty() || line.startsWith("//")) continue;

        // 🚀 查找task调用
        int pos = 0;
        while ((pos = patterns.taskCall.indexIn(line, pos)) != -1) {
            QString taskName = patterns.taskCall.cap(1);
            if (taskName.isEmpty()) {
                taskName = patterns.taskCall.cap(2);
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

            pos += patterns.taskCall.matchedLength();
        }
    }
}

// 🚀 分析always块关系
void SmartRelationshipBuilder::analyzeAlwaysBlocks(const QString& content, AnalysisContext& context)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        const QString& line = lines[lineNum];

        if (patterns.alwaysBlock.indexIn(line) != -1) {
            // 🚀 分析敏感信号列表
            QRegExp sensitivityRegex("always\\s*@\\s*\\(([^)]+)\\)");
            if (sensitivityRegex.indexIn(line) != -1) {
                QString sensitivityList = sensitivityRegex.cap(1);
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

// 🚀 分析时钟和复位关系
void SmartRelationshipBuilder::analyzeClockResetRelationships(const QString& content, AnalysisContext& context)
{
    QStringList lines = content.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        const QString& line = lines[lineNum].toLower();

        // 🚀 查找时钟信号
        if (line.contains(QRegExp("\\b(clk|clock)\\b")) &&
            line.contains(QRegExp("\\b(posedge|negedge)\\b"))) {

            QRegExp clockRegex("(posedge|negedge)\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
            if (clockRegex.indexIn(line) != -1) {
                QString clockName = clockRegex.cap(2);
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
        if (line.contains(QRegExp("\\b(rst|reset|rstn)\\b"))) {
            QRegExp resetRegex("\\b(rst|reset|rstn|rst_n)\\b");
            int pos = 0;
            while ((pos = resetRegex.indexIn(line, pos)) != -1) {
                QString resetName = resetRegex.cap(1);
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

                pos += resetRegex.matchedLength();
            }
        }
    }
}

// 🚀 辅助方法实现

QStringList SmartRelationshipBuilder::extractVariablesFromExpression(const QString& expression)
{
    QStringList variables;
    QSet<QString> uniqueVars; // 避免重复

    // 🚀 使用正则表达式提取标识符
    QRegExp identifierRegex("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b");
    int pos = 0;

    while ((pos = identifierRegex.indexIn(expression, pos)) != -1) {
        QString identifier = identifierRegex.cap(1);

        // 🚀 过滤掉SystemVerilog关键字
        static QSet<QString> svKeywords = {
            "and", "or", "not", "begin", "end", "if", "else", "case", "default",
            "posedge", "negedge", "assign", "always", "initial", "reg", "wire",
            "logic", "input", "output", "inout", "module", "endmodule"
        };

        if (!svKeywords.contains(identifier.toLower()) && !uniqueVars.contains(identifier)) {
            uniqueVars.insert(identifier);
            variables.append(identifier);
        }

        pos += identifierRegex.matchedLength();
    }

    return variables;
}

int SmartRelationshipBuilder::findSymbolIdByName(const QString& symbolName, const AnalysisContext& context)
{
    // 🚀 首先在本地符号映射中查找
    if (context.localSymbolIds.contains(symbolName)) {
        return context.localSymbolIds[symbolName];
    }

    // 🚀 如果没找到，在全局符号数据库中查找
    QList<sym_list::SymbolInfo> symbols = symbolDatabase->findSymbolsByName(symbolName);
    if (!symbols.isEmpty()) {
        return symbols.first().symbolId;
    }

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

// 🚀 高级分析方法的基础实现
void SmartRelationshipBuilder::analyzeInterfaceRelationships(const QString& content, AnalysisContext& context)
{
    // 🚀 TODO: 实现interface关系分析
    // 这需要更复杂的SystemVerilog语法解析
    Q_UNUSED(content)
    Q_UNUSED(context)
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

// 🚀 增量分析实现
void SmartRelationshipBuilder::analyzeFileIncremental(const QString& fileName, const QString& content,
                                                     const QList<int>& changedLines)
{
    if (changedLines.isEmpty()) {
        return;
    }

    // 🚀 对于增量分析，我们重新分析整个文件
    // 更复杂的实现可以只分析变化的行及其影响范围
    analyzeFile(fileName, content);
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
