#include "syminfo.h"
#include "mycodeeditor.h"
#include "completionmanager.h"
#include "symbolrelationshipengine.h"

#include <QDebug>
#include <QRegExp>
#include <QFile>
#include <algorithm>
#include <memory>

std::unique_ptr<sym_list> sym_list::instance = nullptr;

sym_list::sym_list()
{
    symbolDatabase.reserve(1000);
    commentRegions.reserve(100);

    symbolTypeIndex.reserve(50);
    symbolNameIndex.reserve(500);
    fileNameIndex.reserve(50);
    symbolIdToIndex.reserve(1000);
}

sym_list::~sym_list()
{
}

// UPDATED: Smart pointer singleton implementation
sym_list* sym_list::getInstance()
{
    if (!instance) {
        instance = std::unique_ptr<sym_list>(new sym_list());
    }
    return instance.get();
}

int sym_list::allocateSymbolId()
{
    return nextSymbolId++;
}


void sym_list::addSymbol(const SymbolInfo& symbol)
{
    // 🚀 分配全局唯一ID
    SymbolInfo newSymbol = symbol;
    if (newSymbol.symbolId <= 0) {
        newSymbol.symbolId = allocateSymbolId();
    }

    // 🔧 FIX: 确保模块作用域正确设置（但不要覆盖struct变量的moduleScope）
    // 对于struct变量，moduleScope存储的是struct类型名，不应该被覆盖
    if (newSymbol.moduleScope.isEmpty() &&
        (newSymbol.symbolType == sym_reg ||
         newSymbol.symbolType == sym_wire ||
         newSymbol.symbolType == sym_logic)) {
        newSymbol.moduleScope = getCurrentModuleScope(newSymbol.fileName, newSymbol.startLine);
    }
    // 对于struct变量和struct成员，moduleScope已经存储了类型名，不要覆盖

    symbolDatabase.append(newSymbol);
    int newIndex = symbolDatabase.size() - 1;

    // 🚀 建立ID到索引的映射
    symbolIdToIndex[newSymbol.symbolId] = newIndex;

    addToIndexes(newIndex);
    updateLineBasedSymbols(newSymbol);

    // 🚀 NEW: 通知关系引擎有新符号添加
    if (relationshipEngine) {
        // 延迟构建关系，等所有符号添加完成后批量处理
        // 这里只是标记需要重建关系
    }

    // Mark cache as dirty
    indexesDirty = true;

    // 失效相关缓存
    CompletionManager::getInstance()->invalidateCommandModeCache();
}

sym_list::SymbolInfo sym_list::getSymbolById(int symbolId) const
{
    if (symbolIdToIndex.contains(symbolId)) {
        int index = symbolIdToIndex[symbolId];
        if (index < symbolDatabase.size()) {
            return symbolDatabase[index];
        }
    }

    // 返回空符号
    SymbolInfo emptySymbol;
    emptySymbol.symbolId = -1;
    return emptySymbol;
}

bool sym_list::hasSymbol(int symbolId) const
{
    return symbolIdToIndex.contains(symbolId);
}

SymbolRelationshipEngine* sym_list::getRelationshipEngine() const
{
    return relationshipEngine;
}

void sym_list::setRelationshipEngine(SymbolRelationshipEngine* engine)
{
    relationshipEngine = engine;

    // 如果已有符号数据，重建所有关系
    if (engine && !symbolDatabase.isEmpty()) {
        rebuildAllRelationships();
    }
}

void sym_list::rebuildAllRelationships()
{
    if (!relationshipEngine) return;

    // 清除现有关系
    relationshipEngine->clearAllRelationships();

    // 按文件分组重建关系
    QHash<QString, QList<SymbolInfo>> symbolsByFile;
    for (const SymbolInfo& symbol : symbolDatabase) {
        symbolsByFile[symbol.fileName].append(symbol);
    }

    for (auto it = symbolsByFile.begin(); it != symbolsByFile.end(); ++it) {
        buildSymbolRelationships(it.key());
    }
}

void sym_list::buildSymbolRelationships(const QString& fileName)
{
    if (!relationshipEngine) return;

    QList<SymbolInfo> fileSymbols = findSymbolsByFileName(fileName);
    if (fileSymbols.isEmpty()) return;

    // 🚀 1. 构建模块包含关系
    analyzeModuleContainment(fileName);

    // 🚀 2. 分析变量引用关系 (基础实现)
    // 这里可以后续扩展为更复杂的代码解析

    // 🚀 3. 通知关系引擎重建该文件的关系
    relationshipEngine->buildFileRelationships(fileName);
}

QList<sym_list::SymbolInfo> sym_list::findSymbolsByType(sym_type_e symbolType)
{
    QList<SymbolInfo> result;

    if (symbolTypeIndex.contains(symbolType)) {
        const QList<int>& indices = symbolTypeIndex[symbolType];
        result.reserve(indices.size());

        for (int index : indices) {
            if (index < symbolDatabase.size()) {
                result.append(symbolDatabase[index]);
            }
        }
    }

    return result;
}

void sym_list::analyzeModuleContainment(const QString& fileName)
{
    if (!relationshipEngine) return;

    QList<SymbolInfo> fileSymbols = findSymbolsByFileName(fileName);

    // 查找所有模块
    QList<SymbolInfo> modules;
    for (const SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType == sym_module) {
            modules.append(symbol);
        }
    }

    // 为每个模块找到它包含的符号
    for (const SymbolInfo& module : modules) {
        for (const SymbolInfo& symbol : fileSymbols) {
            if (symbol.symbolId != module.symbolId &&
                isSymbolInModule(symbol, module)) {

                // 🚀 建立包含关系
                relationshipEngine->addRelationship(
                    module.symbolId,
                    symbol.symbolId,
                    SymbolRelationshipEngine::CONTAINS
                );

                // 🚀 更新符号的模块作用域信息 - 这是关键！
                // 但是不要覆盖struct变量和struct成员的moduleScope（它们存储的是类型名）
                int symbolIndex = symbolIdToIndex[symbol.symbolId];
                if (symbolIndex < symbolDatabase.size()) {
                    // 对于struct变量和struct成员，moduleScope已经存储了类型名，不要覆盖
                    if (symbol.symbolType != sym_packed_struct_var &&
                        symbol.symbolType != sym_unpacked_struct_var &&
                        symbol.symbolType != sym_struct_member) {
                        symbolDatabase[symbolIndex].moduleScope = module.symbolName;
                        symbolDatabase[symbolIndex].scopeLevel = 1;
                    }
                }
            }
        }
    }
}

QList<sym_list::SymbolInfo> sym_list::findSymbolsByName(const QString& symbolName)
{
    QList<SymbolInfo> result;

    if (symbolNameIndex.contains(symbolName)) {
        const QList<int>& indices = symbolNameIndex[symbolName];
        result.reserve(indices.size());

        for (int index : indices) {
            if (index < symbolDatabase.size()) {
                result.append(symbolDatabase[index]);
            }
        }
    }

    return result;
}

// NEW: 🚀 超高性能的符号名称列表获取
QStringList sym_list::getSymbolNamesByType(sym_type_e symbolType)
{
    updateCachedData();

    if (cachedSymbolNamesByType.contains(symbolType)) {
        return cachedSymbolNamesByType[symbolType];
    }

    return QStringList(); // 空列表
}

QSet<QString> sym_list::getUniqueSymbolNames()
{
    updateCachedData();
    return cachedUniqueNames;
}

int sym_list::getSymbolCountByType(sym_type_e symbolType)
{
    if (symbolTypeIndex.contains(symbolType)) {
        return symbolTypeIndex[symbolType].size();
    }
    return 0;
}

QList<sym_list::SymbolInfo> sym_list::findSymbolsByFileName(const QString& fileName)
{
    QList<SymbolInfo> result;

    if (fileNameIndex.contains(fileName)) {
        const QList<int>& indices = fileNameIndex[fileName];
        result.reserve(indices.size());

        for (int index : indices) {
            if (index < symbolDatabase.size()) {
                result.append(symbolDatabase[index]);
            }
        }
    }

    return result;
}

QList<sym_list::SymbolInfo> sym_list::getAllSymbols()
{
    return symbolDatabase;
}

void sym_list::clearSymbolsForFile(const QString& fileName)
{
    int beforeCount = symbolDatabase.size();

    // 🚀 NEW: 通知关系引擎失效该文件的关系
    if (relationshipEngine) {
        relationshipEngine->invalidateFileRelationships(fileName);
    }

    // NEW: 使用索引进行高效删除
    if (fileNameIndex.contains(fileName)) {
        QList<int> indicesToRemove = fileNameIndex[fileName];

        // 🚀 NEW: 从symbolId映射中移除
        for (int index : indicesToRemove) {
            if (index < symbolDatabase.size()) {
                int symbolId = symbolDatabase[index].symbolId;
                symbolIdToIndex.remove(symbolId);
            }
        }

        // 按降序排列索引，从后往前删除
        std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<int>());

        for (int index : indicesToRemove) {
            if (index < symbolDatabase.size()) {
                // 从所有索引中移除
                removeFromIndexes(index);
                symbolDatabase.removeAt(index);
            }
        }

        // 重建索引（因为数组索引发生了变化）
        rebuildAllIndexes();
    }

    int afterCount = symbolDatabase.size();

    // UPDATED: Invalidate all symbol caches when symbols are removed
    if (beforeCount != afterCount) {
        CompletionManager::getInstance()->invalidateSymbolCaches();
        invalidateCache();
    }
}

void sym_list::rebuildAllIndexes()
{
    // 清空所有索引
    symbolTypeIndex.clear();
    symbolNameIndex.clear();
    fileNameIndex.clear();
    symbolIdToIndex.clear(); // 🚀 NEW: 清空ID映射

    // 重建索引
    for (int i = 0; i < symbolDatabase.size(); ++i) {
        addToIndexes(i);
        // 🚀 NEW: 重建ID映射
        symbolIdToIndex[symbolDatabase[i].symbolId] = i;
    }

    invalidateCache();
}

// NEW: 🚀 添加到索引
void sym_list::addToIndexes(int symbolIndex)
{
    if (symbolIndex >= symbolDatabase.size()) return;

    const SymbolInfo& symbol = symbolDatabase[symbolIndex];

    // 类型索引
    symbolTypeIndex[symbol.symbolType].append(symbolIndex);

    // 名称索引
    symbolNameIndex[symbol.symbolName].append(symbolIndex);

    // 文件名索引
    fileNameIndex[symbol.fileName].append(symbolIndex);
}

// NEW: 🚀 从索引中移除
void sym_list::removeFromIndexes(int symbolIndex)
{
    if (symbolIndex >= symbolDatabase.size()) return;

    const SymbolInfo& symbol = symbolDatabase[symbolIndex];

    // 从类型索引中移除
    if (symbolTypeIndex.contains(symbol.symbolType)) {
        symbolTypeIndex[symbol.symbolType].removeAll(symbolIndex);
        if (symbolTypeIndex[symbol.symbolType].isEmpty()) {
            symbolTypeIndex.remove(symbol.symbolType);
        }
    }

    // 从名称索引中移除
    if (symbolNameIndex.contains(symbol.symbolName)) {
        symbolNameIndex[symbol.symbolName].removeAll(symbolIndex);
        if (symbolNameIndex[symbol.symbolName].isEmpty()) {
            symbolNameIndex.remove(symbol.symbolName);
        }
    }

    // 从文件名索引中移除
    if (fileNameIndex.contains(symbol.fileName)) {
        fileNameIndex[symbol.fileName].removeAll(symbolIndex);
        if (fileNameIndex[symbol.fileName].isEmpty()) {
            fileNameIndex.remove(symbol.fileName);
        }
    }
}

// NEW: 🚀 使缓存失效
void sym_list::invalidateCache()
{
    cachedSymbolNamesByType.clear();
    cachedUniqueNames.clear();
    indexesDirty = true;
}

// NEW: 🚀 更新缓存数据
void sym_list::updateCachedData() const
{
    if (!indexesDirty) return;

    // 清空缓存
    cachedSymbolNamesByType.clear();
    cachedUniqueNames.clear();

    // 为每种符号类型构建名称列表
    for (auto it = symbolTypeIndex.begin(); it != symbolTypeIndex.end(); ++it) {
        sym_type_e symbolType = it.key();
        const QList<int>& indices = it.value();

        QStringList names;
        names.reserve(indices.size());

        for (int index : indices) {
            if (index < symbolDatabase.size()) {
                const QString& name = symbolDatabase[index].symbolName;
                names.append(name);
                cachedUniqueNames.insert(name);
            }
        }

        // 排序并去重
        names.removeDuplicates();
        names.sort();

        cachedSymbolNamesByType[symbolType] = names;
    }

    indexesDirty = false;
}

bool sym_list::isPositionInComment(int position)
{
    // Binary search since commentRegions is sorted
    auto it = std::lower_bound(commentRegions.begin(), commentRegions.end(), position,
        [](const CommentRegion& region, int pos) {
            return region.endPos <= pos;
        });

    return it != commentRegions.end() && position >= it->startPos;
}

void sym_list::getModuleName(const QString &text)
{
    QRegExp moduleName("\\bmodule\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> matches = findMatchesOutsideComments(text, moduleName);

    for (const RegexMatch &match : qAsConst(matches)) {
        // Reuse the regex to get capture group
        if (moduleName.indexIn(text, match.position) != -1) {
            const QString moduleNameCaptured = moduleName.cap(1);

            // Create symbol info and add to database
            SymbolInfo moduleSymbol;
            moduleSymbol.fileName = currentFileName;
            moduleSymbol.symbolName = moduleNameCaptured;
            moduleSymbol.symbolType = sym_module;
            moduleSymbol.position = match.position;
            moduleSymbol.length = match.length;

            int captureGroupPos = moduleName.pos(1);
            calculateLineColumn(text, captureGroupPos, moduleSymbol.startLine, moduleSymbol.startColumn);

            moduleSymbol.endLine = moduleSymbol.startLine;
            moduleSymbol.endColumn = moduleSymbol.startColumn + moduleNameCaptured.length();

            addSymbol(moduleSymbol);
        }
    }
}

void sym_list::buildCommentRegions(const QString &text)
{
    commentRegions.clear();
    findSingleLineComments(text);
    findMultiLineComments(text);

    // Sort once at the end for binary search optimization
    std::sort(commentRegions.begin(), commentRegions.end(),
              [](const CommentRegion &a, const CommentRegion &b) {
                  return a.startPos < b.startPos;
              });
}

void sym_list::findSingleLineComments(const QString &text)
{
    const QStringList lines = text.split('\n');
    int currentPos = 0;

    for (int lineNum = 0; lineNum < lines.size(); lineNum++) {
        const QString &line = lines[lineNum];

        int commentPos = line.indexOf("//");
        if (commentPos != -1 && !isPositionInMultiLineComment(currentPos + commentPos)) {
            CommentRegion region;
            region.startPos = currentPos + commentPos;
            region.endPos = currentPos + line.length();
            region.startLine = lineNum;
            region.startColumn = commentPos;
            region.endLine = lineNum;
            region.endColumn = line.length();
            commentRegions.append(region);
        }

        currentPos += line.length() + 1;
    }
}

void sym_list::findMultiLineComments(const QString &text)
{
    static const QRegExp multiLineStart("\\/\\*");
    static const QRegExp multiLineEnd("\\*\\/");

    int pos = 0;
    while ((pos = multiLineStart.indexIn(text, pos)) != -1) {
        int endPos = multiLineEnd.indexIn(text, pos + 2);

        CommentRegion region;
        region.startPos = pos;
        region.endPos = (endPos != -1) ? endPos + 2 : text.length();

        calculateLineColumn(text, region.startPos, region.startLine, region.startColumn);
        calculateLineColumn(text, region.endPos, region.endLine, region.endColumn);

        commentRegions.append(region);
        pos = region.endPos;
    }
}

void sym_list::calculateLineColumn(const QString &text, int position, int &line, int &column)
{
    line = 0;
    column = 0;
    const int maxPos = qMin(position, text.length());

    for (int i = 0; i < maxPos; i++) {
        if (text[i] == '\n') {
            line++;
            column = 0;
        } else {
            column++;
        }
    }
}
/*
bool sym_list::isMatchInComment(int matchStart, int matchLength)
{
    const int matchEnd = matchStart + matchLength;

    // Use binary search since commentRegions is sorted
    auto it = std::lower_bound(commentRegions.begin(), commentRegions.end(), matchStart,
        [](const CommentRegion& region, int pos) {
            return region.endPos <= pos;
        });

    // Check if any overlapping regions exist
    for (auto iter = it; iter != commentRegions.end() && iter->startPos < matchEnd; ++iter) {
        if (!(matchEnd <= iter->startPos || matchStart >= iter->endPos)) {
            return true;
        }
    }
    return false;
}*/

bool sym_list::isMatchInComment(int matchStart, int matchLength)
{
    const int matchEnd = matchStart + matchLength;

    // Use binary search since commentRegions is sorted
    auto it = std::lower_bound(commentRegions.begin(), commentRegions.end(), matchStart,
        [](const CommentRegion& region, int pos) {
            return region.endPos <= pos;
        });

    // Check if any overlapping regions exist
    for (auto iter = it; iter != commentRegions.end() && iter->startPos < matchEnd; ++iter) {
        // 简单清晰的重叠检测：匹配项与注释区域有重叠
        if (matchStart < iter->endPos && matchEnd > iter->startPos) {
            return true;
        }
    }
    return false;
}

bool sym_list::isPositionInMultiLineComment(int pos)
{
    return isPositionInComment(pos); // Reuse optimized function
}

QList<sym_list::CommentRegion> sym_list::getCommentRegions() const
{
    return commentRegions;
}

QList<sym_list::RegexMatch> sym_list::findMatchesOutsideComments(const QString &text, const QRegExp &pattern)
{
    QList<RegexMatch> validMatches;
    validMatches.reserve(50); // Reasonable estimate

    QRegExp regExp(pattern);
    int pos = 0;

    while ((pos = regExp.indexIn(text, pos)) != -1) {
        const int matchStart = pos;
        const int matchLength = regExp.matchedLength();

        if (!isMatchInComment(matchStart, matchLength)) {
            RegexMatch match;
            match.position = matchStart;
            match.length = matchLength;
            match.captured = regExp.cap(0);

            calculateLineColumn(text, matchStart, match.lineNumber, match.columnNumber);
            validMatches.append(match);
        }

        pos += matchLength;
    }

    return validMatches;
}

void sym_list::setCodeEditor(MyCodeEditor* codeEditor)
{
    if (!codeEditor) {
        return;
    }

    currentFileName = codeEditor->getFileName();

    // Clear existing symbols for this file before analysis
    clearSymbolsForFile(currentFileName);

    const QString text = codeEditor->document()->toPlainText();

    // Build comment regions first
    buildCommentRegions(text);

    // Extract all symbol types
    getModuleName(text);
    getVariableDeclarations(text);
    getTasksAndFunctions(text);

    // 🚀 NEW: 构建符号关系
    buildSymbolRelationships(currentFileName);

    // UPDATED: Force refresh all caches to ensure normal mode completion works
    CompletionManager::getInstance()->forceRefreshSymbolCaches();
}

void sym_list::getVariableDeclarations(const QString &text)
{
    int symbolsFound = 0;

    // Extract reg declarations
    QRegExp regPattern("\\breg\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> regMatches = findMatchesOutsideComments(text, regPattern);

    for (const RegexMatch &match : qAsConst(regMatches)) {
        if (regPattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = regPattern.cap(1);
            symbol.symbolType = sym_reg;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, regPattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    // Extract wire declarations
    QRegExp wirePattern("\\bwire\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> wireMatches = findMatchesOutsideComments(text, wirePattern);

    for (const RegexMatch &match : qAsConst(wireMatches)) {
        if (wirePattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = wirePattern.cap(1);
            symbol.symbolType = sym_wire;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, wirePattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    // Extract logic declarations
    // 先找到所有struct的范围，排除struct内部的logic
    QList<StructRange> structRanges = findStructRanges(text);
    
    QRegExp logicPattern("\\blogic\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> logicMatches = findMatchesOutsideComments(text, logicPattern);

    for (const RegexMatch &match : qAsConst(logicMatches)) {
        if (logicPattern.indexIn(text, match.position) != -1) {
            // 检查logic是否在struct范围内
            int logicPos = logicPattern.pos(1); // logic变量名的位置
            if (!isPositionInStructRange(logicPos, structRanges)) {
                SymbolInfo symbol;
                symbol.fileName = currentFileName;
                symbol.symbolName = logicPattern.cap(1);
                symbol.symbolType = sym_logic;
                symbol.position = match.position;
                symbol.length = match.length;
                calculateLineColumn(text, logicPattern.pos(1), symbol.startLine, symbol.startColumn);
                symbol.endLine = symbol.startLine;
                symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
                addSymbol(symbol);
                symbolsFound++;
            }
        }
    }

    getAdditionalSymbols(text);
}

void sym_list::getAdditionalSymbols(const QString &text)
{
    // 分析interface声明
    analyzeInterfaces(text);

    // 分析package声明
    analyzePackages(text);

    // 分析struct/enum/typedef声明
    analyzeDataTypes(text);

    // 分析预处理器指令
    analyzePreprocessorDirectives(text);

    // 分析always块和assign语句
    //analyzeAlwaysAndAssign(text);

    // 分析参数声明
    analyzeParameters(text);

    // 分析约束相关
    analyzeConstraints(text);

    // [DEBUG] Ctrl+S 保存后打印当前文件的 struct 表，便于调试
    QList<SymbolInfo> all = getAllSymbols();
    QList<SymbolInfo> structTypes;
    QList<SymbolInfo> structVars;
    for (const SymbolInfo &s : all) {
        if (s.fileName != currentFileName) continue;
        if (s.symbolType == sym_packed_struct || s.symbolType == sym_unpacked_struct)
            structTypes.append(s);
        if (s.symbolType == sym_packed_struct_var || s.symbolType == sym_unpacked_struct_var)
            structVars.append(s);
    }
    qDebug() << "[struct表]" << currentFileName;
    qDebug() << "  struct类型:" << structTypes.size();
    for (const SymbolInfo &s : structTypes)
        qDebug() << "    " << (s.symbolType == sym_packed_struct ? "packed" : "unpacked") << s.symbolName;
    qDebug() << "  struct变量:" << structVars.size();
    for (const SymbolInfo &s : structVars)
        qDebug() << "    " << s.symbolName << "(" << s.moduleScope << ")" << (s.symbolType == sym_packed_struct_var ? "packed" : "unpacked");
}

void sym_list::analyzePackages(const QString &text)
{
    // package 声明: package package_name;
    QRegExp packagePattern("\\bpackage\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> packageMatches = findMatchesOutsideComments(text, packagePattern);

    for (const RegexMatch &match : qAsConst(packageMatches)) {
        if (packagePattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = packagePattern.cap(1);
            symbol.symbolType = sym_package;
            symbol.position = match.position;
            symbol.length = match.length;

            // 计算包名在行中的精确位置
            int capPos = packagePattern.pos(1);
            calculateLineColumn(text, capPos, symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();

            addSymbol(symbol);
        }
    }
}

void sym_list::getTasksAndFunctions(const QString &text)
{
    int symbolsFound = 0;

    // Extract task declarations
    QRegExp taskPattern("\\btask\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> taskMatches = findMatchesOutsideComments(text, taskPattern);

    for (const RegexMatch &match : qAsConst(taskMatches)) {
        if (taskPattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = taskPattern.cap(1);
            symbol.symbolType = sym_task;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, taskPattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    // Extract function declarations
    QRegExp functionPattern("\\bfunction\\s+(?:\\w+\\s+)?([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> functionMatches = findMatchesOutsideComments(text, functionPattern);

    for (const RegexMatch &match : qAsConst(functionMatches)) {
        if (functionPattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = functionPattern.cap(1);
            symbol.symbolType = sym_function;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, functionPattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

}

void sym_list::setCodeEditorIncremental(MyCodeEditor* codeEditor)
{
    if (!codeEditor) return;

    currentFileName = codeEditor->getFileName();
    QString content = codeEditor->document()->toPlainText();

    if (!needsAnalysis(currentFileName, content)) {
        return;
    }

    FileState& state = fileStates[currentFileName];

    // FIXED: 更清晰的分支逻辑
    bool isFirstTime = !fileStates.contains(currentFileName) || state.needsFullAnalysis;

    if (isFirstTime) {
        clearSymbolsForFile(currentFileName);
        buildCommentRegions(content);
        getModuleName(content);
        getVariableDeclarations(content);
        getTasksAndFunctions(content);

        // 🚀 NEW: 构建符号关系
        buildSymbolRelationships(currentFileName);

        state.needsFullAnalysis = false;

        // FIXED: 第一次分析后立即缓存内容
        previousFileContents[currentFileName] = content;
    } else {
        QList<int> changedLines = detectChangedLines(currentFileName, content);
        if (!changedLines.isEmpty()) {
            analyzeSpecificLines(currentFileName, content, changedLines);

            // 🚀 NEW: 增量更新关系
            buildSymbolRelationships(currentFileName);
        }
    }

    state.contentHash = calculateContentHash(content);
    state.lastModified = QDateTime::currentDateTime();

    QList<SymbolInfo> fileSymbols = findSymbolsByFileName(currentFileName);

    CompletionManager::getInstance()->invalidateSymbolCaches();
}

QString sym_list::calculateContentHash(const QString& content)
{
    return QString::number(qHash(content));
}

bool sym_list::needsAnalysis(const QString& fileName, const QString& content)
{
    if (!fileStates.contains(fileName)) return true;

    QString newHash = calculateContentHash(content);
    return newHash != fileStates[fileName].contentHash;
}

void sym_list::updateLineBasedSymbols(const SymbolInfo& symbol)
{
    lineBasedSymbols[symbol.fileName][symbol.startLine].append(symbol);
}

QList<int> sym_list::detectChangedLines(const QString& fileName, const QString& newContent)
{
    QList<int> changedLines;

    if (!previousFileContents.contains(fileName)) {
        previousFileContents[fileName] = newContent;

        // 第一次分析时，返回空列表，让完整分析处理
        return changedLines; // 返回空列表，触发完整分析
    }

    QString oldContent = previousFileContents[fileName];
    QStringList oldLines = oldContent.split('\n');
    QStringList newLines = newContent.split('\n');

    int maxLines = qMax(oldLines.size(), newLines.size());
    int actualChanges = 0;

    for (int i = 0; i < maxLines; ++i) {
        QString oldLine = (i < oldLines.size()) ? oldLines[i] : QString();
        QString newLine = (i < newLines.size()) ? newLines[i] : QString();

        if (oldLine.trimmed() != newLine.trimmed()) {
            changedLines.append(i);
            actualChanges++;

            // 检查声明关键字，标记相邻行
            static QStringList declarationKeywords = {"module", "reg", "wire", "logic", "task", "function"};
            for (const QString &keyword : declarationKeywords) {
                if (newLine.contains(QRegExp("\\b" + keyword + "\\b"))) {
                    if (i > 0 && !changedLines.contains(i - 1)) {
                        changedLines.append(i - 1);
                    }
                    if (i < maxLines - 1 && !changedLines.contains(i + 1)) {
                        changedLines.append(i + 1);
                    }
                    break;
                }
            }
        }
    }

    // 更新缓存内容
    previousFileContents[fileName] = newContent;

    // 去重并排序
    changedLines = changedLines.toSet().toList();
    std::sort(changedLines.begin(), changedLines.end());

    return changedLines;
}

void sym_list::analyzeSpecificLines(const QString& fileName, const QString& content, const QList<int>& lines)
{
    if (lines.isEmpty()) {
        return;
    }

    // 清除旧符号
    clearSymbolsForLines(fileName, lines);

    // 重建注释区域
    buildCommentRegions(content);

    QStringList contentLines = content.split('\n');
    int newSymbolsFound = 0;

    for (int lineNum : lines) {
        if (lineNum >= contentLines.size()) {
            continue;
        }

        QString lineText = contentLines[lineNum];
        QString trimmedLine = lineText.trimmed();

        if (trimmedLine.isEmpty()) {
            continue;
        }

        // 计算行在文件中的起始位置
        int lineStartPos = 0;
        for (int i = 0; i < lineNum; ++i) {
            lineStartPos += contentLines[i].length() + 1; // +1 for '\n'
        }

        int symbolsBeforeLine = symbolDatabase.size();

        // 分析各种符号类型
        analyzeModulesInLine(lineText, lineStartPos, lineNum);
        analyzeVariablesInLine(lineText, lineStartPos, lineNum, content);
        analyzeTasksFunctionsInLine(lineText, lineStartPos, lineNum);

        int symbolsAfterLine = symbolDatabase.size();
        int symbolsFoundInLine = symbolsAfterLine - symbolsBeforeLine;
        newSymbolsFound += symbolsFoundInLine;

        if (symbolsFoundInLine > 0) {
        } else {
        }
    }

    // 增量更新后，用完整内容重新收集 struct/typedef/enum 及对应变量，避免只识别到部分结构体
    clearStructTypedefEnumSymbolsForFile(fileName);
    currentFileName = fileName;
    analyzeDataTypes(content);
}

void sym_list::analyzeTasksFunctionsInLine(const QString& lineText, int lineStartPos, int lineNum)
{
    // 分析 task 声明
    QRegExp taskPattern("\\btask\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    int tasksBefore = symbolDatabase.size();
    analyzeTaskFunctionPattern(lineText, lineStartPos, lineNum, taskPattern, sym_task);
    int tasksAfter = symbolDatabase.size();
    int tasksFound = tasksAfter - tasksBefore;

    // 分析 function 声明
    QRegExp functionPattern("\\bfunction\\s+(?:\\w+\\s+)?([a-zA-Z_][a-zA-Z0-9_]*)");
    int functionsBefore = symbolDatabase.size();
    analyzeTaskFunctionPattern(lineText, lineStartPos, lineNum, functionPattern, sym_function);
    int functionsAfter = symbolDatabase.size();
    int functionsFound = functionsAfter - functionsBefore;

    if (tasksFound > 0 || functionsFound > 0) {
    } else {
    }
}

void sym_list::clearSymbolsForLines(const QString& fileName, const QList<int>& lines)
{
    int removedCount = 0;

    // 从行级映射中清除
    if (lineBasedSymbols.contains(fileName)) {
        for (int lineNum : lines) {
            if (lineBasedSymbols[fileName].contains(lineNum)) {
                removedCount += lineBasedSymbols[fileName][lineNum].size();
                lineBasedSymbols[fileName].remove(lineNum);
            }
        }
    }

    // 🚀 使用索引优化的删除方法
    QList<int> indicesToRemove;

    if (fileNameIndex.contains(fileName)) {
        const QList<int>& fileIndices = fileNameIndex[fileName];

        for (int index : fileIndices) {
            if (index < symbolDatabase.size()) {
                const SymbolInfo& symbol = symbolDatabase[index];
                if (lines.contains(symbol.startLine)) {
                    indicesToRemove.append(index);
                }
            }
        }
    }

    // 按降序删除以避免索引混乱
    std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<int>());

    for (int index : indicesToRemove) {
        if (index < symbolDatabase.size()) {
            removeFromIndexes(index);
            symbolDatabase.removeAt(index);
        }
    }

    // 重建索引
    if (!indicesToRemove.isEmpty()) {
        rebuildAllIndexes();
    }
}

void sym_list::clearStructTypedefEnumSymbolsForFile(const QString &fileName)
{
    if (!fileNameIndex.contains(fileName)) return;

    static const QList<sym_type_e> typesToClear = {
        sym_packed_struct, sym_unpacked_struct,
        sym_packed_struct_var, sym_unpacked_struct_var, sym_struct_member,
        sym_typedef, sym_enum, sym_enum_var, sym_enum_value
    };
    QSet<sym_type_e> typeSet;
    for (sym_type_e t : typesToClear) typeSet.insert(t);

    QList<int> indicesToRemove;
    const QList<int> &fileIndices = fileNameIndex[fileName];
    for (int index : fileIndices) {
        if (index < symbolDatabase.size() && typeSet.contains(symbolDatabase[index].symbolType)) {
            indicesToRemove.append(index);
        }
    }
    if (indicesToRemove.isEmpty()) return;

    for (int index : indicesToRemove) {
        if (index < symbolDatabase.size()) {
            int symbolId = symbolDatabase[index].symbolId;
            symbolIdToIndex.remove(symbolId);
        }
    }
    std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<int>());
    for (int index : indicesToRemove) {
        if (index < symbolDatabase.size()) {
            removeFromIndexes(index);
            symbolDatabase.removeAt(index);
        }
    }
    rebuildAllIndexes();
    CompletionManager::getInstance()->invalidateSymbolCaches();
}

void sym_list::refreshStructTypedefEnumForFile(const QString &fileName, const QString &content)
{
    previousFileContents[fileName] = content;
    buildCommentRegions(content);
    clearStructTypedefEnumSymbolsForFile(fileName);
    currentFileName = fileName;
    analyzeDataTypes(content);
}

void sym_list::analyzeModulesInLine(const QString& lineText, int lineStartPos, int lineNum)
{
    QRegExp modulePattern("\\bmodule\\s+([a-zA-Z_][a-zA-Z0-9_]*)");

    int pos = 0;
    while ((pos = modulePattern.indexIn(lineText, pos)) != -1) {
        int absolutePos = lineStartPos + pos;

        if (!isMatchInComment(absolutePos, modulePattern.matchedLength())) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = modulePattern.cap(1);
            symbol.symbolType = sym_module;
            symbol.startLine = lineNum;
            symbol.startColumn = pos;
            symbol.endLine = lineNum;
            symbol.endColumn = pos + symbol.symbolName.length();
            symbol.position = absolutePos;
            symbol.length = modulePattern.matchedLength();

            addSymbol(symbol);
        }

        pos += modulePattern.matchedLength();
    }
}

void sym_list::analyzeVariablesInLine(const QString& lineText, int lineStartPos, int lineNum, const QString& fullText)
{
    // reg 变量
    QRegExp regPattern("\\breg\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    analyzeVariablePattern(lineText, lineStartPos, lineNum, regPattern, sym_reg);

    // wire 变量
    QRegExp wirePattern("\\bwire\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    analyzeVariablePattern(lineText, lineStartPos, lineNum, wirePattern, sym_wire);

    // logic 变量 - 需要排除struct内部的logic
    QString textToUse = fullText;
    if (textToUse.isEmpty()) {
        // 尝试从缓存获取
        if (previousFileContents.contains(currentFileName)) {
            textToUse = previousFileContents[currentFileName];
        } else {
            // 尝试从文件读取
            QFile file(currentFileName);
            if (file.open(QIODevice::ReadOnly | QFile::Text)) {
                textToUse = file.readAll();
                file.close();
            }
        }
    }
    
    if (!textToUse.isEmpty()) {
        QList<StructRange> structRanges = findStructRanges(textToUse);
        QRegExp logicPattern("\\blogic\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
        QRegExp regExp(logicPattern);
        int pos = 0;

        while ((pos = regExp.indexIn(lineText, pos)) != -1) {
            int absolutePos = lineStartPos + pos;
            int logicNamePos = lineStartPos + regExp.pos(1);

            if (!isMatchInComment(absolutePos, regExp.matchedLength())) {
                // 检查logic是否在struct范围内
                if (!isPositionInStructRange(logicNamePos, structRanges)) {
                    SymbolInfo symbol;
                    symbol.fileName = currentFileName;
                    symbol.symbolName = regExp.cap(1);
                    symbol.symbolType = sym_logic;
                    symbol.startLine = lineNum;
                    symbol.startColumn = regExp.pos(1);
                    symbol.endLine = lineNum;
                    symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
                    symbol.position = absolutePos;
                    symbol.length = regExp.matchedLength();

                    symbol.moduleScope = getCurrentModuleScope(symbol.fileName, symbol.startLine);
                    addSymbol(symbol);
                }
            }

            pos += regExp.matchedLength();
        }
    } else {
        // 如果无法获取完整文件内容，使用原来的方法（可能不够准确）
        QRegExp logicPattern("\\blogic\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
        analyzeVariablePattern(lineText, lineStartPos, lineNum, logicPattern, sym_logic);
    }
}

// 新增：获取指定位置的模块作用域
QString sym_list::getCurrentModuleScope(const QString& fileName, int lineNumber) {
    // 查找包含该行的模块
    QList<SymbolInfo> modules = findSymbolsByType(sym_module);
    for (const SymbolInfo& moduleSymbol : modules) {
        if (moduleSymbol.fileName == fileName) {
            // 查找模块的结束位置
            int moduleEndLine = findEndModuleLine(fileName, moduleSymbol);
            if (lineNumber > moduleSymbol.startLine && lineNumber < moduleEndLine) {
                return moduleSymbol.symbolName;
            }
        }
    }
    return QString(); // 不在任何模块内
}

void sym_list::analyzeVariablePattern(const QString& lineText, int lineStartPos, int lineNum,
                                     const QRegExp& pattern, sym_type_e symbolType)
{
    QRegExp regExp(pattern);
    int pos = 0;

    while ((pos = regExp.indexIn(lineText, pos)) != -1) {
        int absolutePos = lineStartPos + pos;

        if (!isMatchInComment(absolutePos, regExp.matchedLength())) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = regExp.cap(1);
            symbol.symbolType = symbolType;
            symbol.startLine = lineNum;
            symbol.startColumn = regExp.pos(1) - lineStartPos + lineStartPos;
            symbol.endLine = lineNum;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            symbol.position = absolutePos;
            symbol.length = regExp.matchedLength();

            symbol.moduleScope = getCurrentModuleScope(symbol.fileName, symbol.startLine);
            addSymbol(symbol);
        }

        pos += regExp.matchedLength();
    }
}

int sym_list::findEndModuleLine(const QString &fileName, const SymbolInfo &moduleSymbol)
{
    if (moduleSymbol.symbolType != sym_module) {
        return -1;
    }

    // Check if the file content is already cached
    QString content;
    if (previousFileContents.contains(fileName)) {
        content = previousFileContents[fileName];
    } else {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QFile::Text)) {
            return -1; // Cannot open file
        }
        content = file.readAll();
        file.close();
        previousFileContents[fileName] = content;
    }

    QStringList lines = content.split('\n');
    int moduleDepth = 0;

    for (int i = moduleSymbol.startLine; i < lines.size(); ++i) {
        const QString &line = lines[i];
        int lineStartPos = 0;
        for (int j = 0; j < i; ++j) {
            lineStartPos += lines[j].length() + 1;
        }
        if (line.contains(QRegExp("\\bmodule\\b")) && !isMatchInComment(lineStartPos, line.length())) {
            moduleDepth++;
        }
        if (line.contains(QRegExp("\\bendmodule\\b")) && !isMatchInComment(lineStartPos, line.length())) {
            moduleDepth--;
            if (moduleDepth == 0) {
                return i;
            }
        }
    }

    return -1; // endmodule not found
}

void sym_list::analyzeTaskFunctionPattern(const QString& lineText, int lineStartPos, int lineNum,
                                         const QRegExp& pattern, sym_type_e symbolType)
{
    QRegExp regExp(pattern);
    int pos = 0;

    while ((pos = regExp.indexIn(lineText, pos)) != -1) {
        int absolutePos = lineStartPos + pos;

        if (!isMatchInComment(absolutePos, regExp.matchedLength())) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = regExp.cap(1);
            symbol.symbolType = symbolType;
            symbol.startLine = lineNum;

            // 计算捕获组在行中的位置
            int capturePos = regExp.pos(1);
            symbol.startColumn = capturePos;
            symbol.endLine = lineNum;
            symbol.endColumn = capturePos + symbol.symbolName.length();
            symbol.position = lineStartPos + capturePos;
            symbol.length = symbol.symbolName.length();

            addSymbol(symbol);
        }

        pos += regExp.matchedLength();
    }
}

bool isSymbolInModule(const sym_list::SymbolInfo& symbol, const sym_list::SymbolInfo& module)
{
    // 简单实现：检查符号是否在模块的行范围内
    // 更复杂的实现需要解析模块的endmodule位置
    return symbol.fileName == module.fileName &&
           symbol.startLine > module.startLine;
}

QString getModuleNameContainingSymbol(const sym_list::SymbolInfo& symbol,
                                     const QList<sym_list::SymbolInfo>& allSymbols)
{
    for (const sym_list::SymbolInfo& moduleSymbol : allSymbols) {
        if (moduleSymbol.symbolType == sym_list::sym_module &&
            isSymbolInModule(symbol, moduleSymbol)) {
            return moduleSymbol.symbolName;
        }
    }
    return QString(); // 没有找到包含的模块
}


// 🚀 Interface 分析
void sym_list::analyzeInterfaces(const QString &text)
{
    int symbolsFound = 0;

    // Interface 声明: interface interfaceName;
    QRegExp interfacePattern("\\binterface\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*[;(]");
    QList<RegexMatch> interfaceMatches = findMatchesOutsideComments(text, interfacePattern);

    for (const RegexMatch &match : qAsConst(interfaceMatches)) {
        if (interfacePattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = interfacePattern.cap(1);
            symbol.symbolType = sym_interface;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, interfacePattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    // Modport 声明: modport portName(input sig1, output sig2);
    QRegExp modportPattern("\\bmodport\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(");
    QList<RegexMatch> modportMatches = findMatchesOutsideComments(text, modportPattern);

    for (const RegexMatch &match : qAsConst(modportMatches)) {
        if (modportPattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = modportPattern.cap(1);
            symbol.symbolType = sym_interface_modport;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, modportPattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }
}

// 🚀 数据类型分析 (struct, enum, typedef)
void sym_list::analyzeDataTypes(const QString &text)
{
    int symbolsFound = 0;

    // Packed struct: typedef struct packed { ... } structName;
    QRegExp packedStructPattern("\\btypedef\\s+struct\\s+packed\\s*\\{([^}]+)\\}\\s*([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> packedStructMatches = findMatchesOutsideComments(text, packedStructPattern);

    for (const RegexMatch &match : qAsConst(packedStructMatches)) {
        if (packedStructPattern.indexIn(text, match.position) != -1) {
            // cap(1)=花括号内成员内容, cap(2)=结构体类型名(如 rst_s)
            QString structTypeName = packedStructPattern.cap(2);
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = structTypeName;
            symbol.symbolType = sym_packed_struct;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, packedStructPattern.pos(2), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + structTypeName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    // Unpacked struct: typedef struct { ... } structName;
    // 使用更完整的正则表达式，匹配多行struct定义
    QRegExp unpackedStructPattern("\\btypedef\\s+struct\\s*\\{([^}]+)\\}\\s*([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> unpackedStructMatches = findMatchesOutsideComments(text, unpackedStructPattern);

    for (const RegexMatch &match : qAsConst(unpackedStructMatches)) {
        if (unpackedStructPattern.indexIn(text, match.position) != -1) {
            QString structMembers = unpackedStructPattern.cap(1);
            QString structName = unpackedStructPattern.cap(2);
            
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = structName;
            symbol.symbolType = sym_unpacked_struct;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, unpackedStructPattern.pos(2), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + structName.length();
            addSymbol(symbol);
            symbolsFound++;
            
            // 解析结构体成员
            analyzeStructMembers(structMembers, structName, match.position, text);
        }
    }

    // Enum: typedef enum { ... } enumName;
    QRegExp enumPattern("typedef\\s+enum\\s*(?:\\{[^}]*\\})?\\s*([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> enumMatches = findMatchesOutsideComments(text, enumPattern);

    for (const RegexMatch &match : qAsConst(enumMatches)) {
        if (enumPattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = enumPattern.cap(1);
            symbol.symbolType = sym_enum;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, enumPattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    // Typedef: typedef existingType newTypeName;
    QRegExp typedefPattern("\\btypedef\\s+(?:(?:struct|enum|union)\\s+)?[a-zA-Z_][a-zA-Z0-9_\\[\\]:]*\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*;");
    QList<RegexMatch> typedefMatches = findMatchesOutsideComments(text, typedefPattern);

    for (const RegexMatch &match : qAsConst(typedefMatches)) {
        if (typedefPattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = typedefPattern.cap(1);
            symbol.symbolType = sym_typedef;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, typedefPattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }
    
    // 分析结构体变量声明（需要在struct类型识别之后）
    analyzeStructVariables(text);
}

// 🚀 预处理器指令分析
void sym_list::analyzePreprocessorDirectives(const QString &text)
{
    int symbolsFound = 0;

    // `define 宏定义: `define MACRO_NAME value
    QRegExp definePattern("`define\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> defineMatches = findMatchesOutsideComments(text, definePattern);

    for (const RegexMatch &match : qAsConst(defineMatches)) {
        if (definePattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = definePattern.cap(1);
            symbol.symbolType = sym_def_define;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, definePattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    // `ifdef 条件编译
    QRegExp ifdefPattern("`ifdef\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> ifdefMatches = findMatchesOutsideComments(text, ifdefPattern);

    for (const RegexMatch &match : qAsConst(ifdefMatches)) {
        if (ifdefPattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = ifdefPattern.cap(1);
            symbol.symbolType = sym_def_ifdef;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, ifdefPattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    // `ifndef 条件编译
    QRegExp ifndefPattern("`ifndef\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> ifndefMatches = findMatchesOutsideComments(text, ifndefPattern);

    for (const RegexMatch &match : qAsConst(ifndefMatches)) {
        if (ifndefPattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = ifndefPattern.cap(1);
            symbol.symbolType = sym_def_ifndef;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, ifndefPattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }
}

// 🚀 Always块和Assign语句分析
void sym_list::analyzeAlwaysAndAssign(const QString &text)
{
    int symbolsFound = 0;

    // always_ff 块
    QRegExp alwaysFFPattern("\\balways_ff\\s*@\\s*\\([^)]*\\)");
    QList<RegexMatch> alwaysFFMatches = findMatchesOutsideComments(text, alwaysFFPattern);

    for (const RegexMatch &match : qAsConst(alwaysFFMatches)) {
        SymbolInfo symbol;
        symbol.fileName = currentFileName;
        symbol.symbolName = QString("always_ff_%1").arg(symbol.startLine);
        symbol.symbolType = sym_always_ff;
        symbol.position = match.position;
        symbol.length = match.length;
        calculateLineColumn(text, match.position, symbol.startLine, symbol.startColumn);
        symbol.endLine = symbol.startLine;
        symbol.endColumn = symbol.startColumn + match.length;
        addSymbol(symbol);
        symbolsFound++;
    }

    // always_comb 块
    QRegExp alwaysCombPattern("\\balways_comb\\b");
    QList<RegexMatch> alwaysCombMatches = findMatchesOutsideComments(text, alwaysCombPattern);

    for (const RegexMatch &match : qAsConst(alwaysCombMatches)) {
        SymbolInfo symbol;
        symbol.fileName = currentFileName;
        symbol.symbolName = QString("always_comb_%1").arg(symbol.startLine);
        symbol.symbolType = sym_always_comb;
        symbol.position = match.position;
        symbol.length = match.length;
        calculateLineColumn(text, match.position, symbol.startLine, symbol.startColumn);
        symbol.endLine = symbol.startLine;
        symbol.endColumn = symbol.startColumn + match.length;
        addSymbol(symbol);
        symbolsFound++;
    }

    // always 块 (通用)
    QRegExp alwaysPattern("\\balways\\s*@\\s*\\([^)]*\\)");
    QList<RegexMatch> alwaysMatches = findMatchesOutsideComments(text, alwaysPattern);

    for (const RegexMatch &match : qAsConst(alwaysMatches)) {
        SymbolInfo symbol;
        symbol.fileName = currentFileName;
        symbol.symbolName = QString("always_%1").arg(symbol.startLine);
        symbol.symbolType = sym_always;
        symbol.position = match.position;
        symbol.length = match.length;
        calculateLineColumn(text, match.position, symbol.startLine, symbol.startColumn);
        symbol.endLine = symbol.startLine;
        symbol.endColumn = symbol.startColumn + match.length;
        addSymbol(symbol);
        symbolsFound++;
    }

    // assign 语句
    QRegExp assignPattern("\\bassign\\s+([a-zA-Z_][a-zA-Z0-9_\\[\\]]*)\\s*=");
    QList<RegexMatch> assignMatches = findMatchesOutsideComments(text, assignPattern);

    for (const RegexMatch &match : qAsConst(assignMatches)) {
        if (assignPattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = assignPattern.cap(1);
            symbol.symbolType = sym_assign;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, assignPattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }
}

// 🚀 参数分析
void sym_list::analyzeParameters(const QString &text)
{
    int symbolsFound = 0;

    // parameter 声明
    QRegExp parameterPattern("\\bparameter\\s+(?:[a-zA-Z_][a-zA-Z0-9_]*\\s*=\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> parameterMatches = findMatchesOutsideComments(text, parameterPattern);

    for (const RegexMatch &match : qAsConst(parameterMatches)) {
        if (parameterPattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = parameterPattern.cap(1);
            symbol.symbolType = sym_parameter;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, parameterPattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    // localparam 声明
    QRegExp localparamPattern("\\blocalparam\\s+(?:[a-zA-Z_][a-zA-Z0-9_]*\\s*=\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> localparamMatches = findMatchesOutsideComments(text, localparamPattern);

    for (const RegexMatch &match : qAsConst(localparamMatches)) {
        if (localparamPattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = localparamPattern.cap(1);
            symbol.symbolType = sym_localparam;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, localparamPattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }
}

// 🚀 约束分析 (Xilinx等)
void sym_list::analyzeConstraints(const QString &text)
{
    int symbolsFound = 0;

    // Xilinx 约束: (* KEEP = "TRUE" *) 等
    QRegExp xilinxConstraintPattern("\\(\\*\\s*([A-Z_]+)\\s*=");
    QList<RegexMatch> constraintMatches = findMatchesOutsideComments(text, xilinxConstraintPattern);

    for (const RegexMatch &match : qAsConst(constraintMatches)) {
        if (xilinxConstraintPattern.indexIn(text, match.position) != -1) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = xilinxConstraintPattern.cap(1);
            symbol.symbolType = sym_xilinx_constraint;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, xilinxConstraintPattern.pos(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }
}

void sym_list::analyzeEnumsAndStructs(const QString &text)
{
    int symbolsFound = 0;

    // ===== 枚举类型分析 =====

    // 1. 基本枚举: enum { VALUE1, VALUE2 } var_name;
    QRegExp basicEnumPattern("\\benum\\s*\\{([^}]+)\\}\\s*([a-zA-Z_][a-zA-Z0-9_,\\s]*)");
    QList<RegexMatch> basicEnumMatches = findMatchesOutsideComments(text, basicEnumPattern);

    for (const RegexMatch &match : qAsConst(basicEnumMatches)) {
        if (basicEnumPattern.indexIn(text, match.position) != -1) {
            QString enumValues = basicEnumPattern.cap(1);
            QString variables = basicEnumPattern.cap(2);

            // 解析枚举值
            QStringList valueList = enumValues.split(',', QString::SkipEmptyParts);
            for (const QString &value : valueList) {
                QString cleanValue = value.trimmed();
                // 移除赋值部分 (例如 VALUE1 = 1)
                int assignPos = cleanValue.indexOf('=');
                if (assignPos >= 0) {
                    cleanValue = cleanValue.left(assignPos).trimmed();
                }

                if (!cleanValue.isEmpty()) {
                    SymbolInfo enumValueSymbol;
                    enumValueSymbol.fileName = currentFileName;
                    enumValueSymbol.symbolName = cleanValue;
                    enumValueSymbol.symbolType = sym_enum_value;
                    enumValueSymbol.position = match.position;
                    enumValueSymbol.length = cleanValue.length();
                    calculateLineColumn(text, match.position, enumValueSymbol.startLine, enumValueSymbol.startColumn);
                    enumValueSymbol.endLine = enumValueSymbol.startLine;
                    enumValueSymbol.endColumn = enumValueSymbol.startColumn + cleanValue.length();
                    addSymbol(enumValueSymbol);
                    symbolsFound++;
                }
            }

            // 解析枚举变量
            QStringList varList = variables.split(',', QString::SkipEmptyParts);
            for (const QString &var : varList) {
                QString cleanVar = var.trimmed().remove(';');
                if (!cleanVar.isEmpty()) {
                    SymbolInfo enumVarSymbol;
                    enumVarSymbol.fileName = currentFileName;
                    enumVarSymbol.symbolName = cleanVar;
                    enumVarSymbol.symbolType = sym_enum_var;
                    enumVarSymbol.position = match.position;
                    enumVarSymbol.length = cleanVar.length();
                    calculateLineColumn(text, match.position, enumVarSymbol.startLine, enumVarSymbol.startColumn);
                    enumVarSymbol.endLine = enumVarSymbol.startLine;
                    enumVarSymbol.endColumn = enumVarSymbol.startColumn + cleanVar.length();
                    addSymbol(enumVarSymbol);
                    symbolsFound++;
                }
            }
        }
    }

    // 2. Typedef枚举: typedef enum { VALUE1, VALUE2 } enum_name_t;
    QRegExp typedefEnumPattern("\\btypedef\\s+enum\\s*\\{([^}]+)\\}\\s*([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> typedefEnumMatches = findMatchesOutsideComments(text, typedefEnumPattern);

    for (const RegexMatch &match : qAsConst(typedefEnumMatches)) {
        if (typedefEnumPattern.indexIn(text, match.position) != -1) {
            QString enumValues = typedefEnumPattern.cap(1);
            QString typeName = typedefEnumPattern.cap(2);

            // 添加枚举类型定义
            SymbolInfo enumTypeSymbol;
            enumTypeSymbol.fileName = currentFileName;
            enumTypeSymbol.symbolName = typeName;
            enumTypeSymbol.symbolType = sym_enum;
            enumTypeSymbol.position = match.position;
            enumTypeSymbol.length = match.length;
            calculateLineColumn(text, typedefEnumPattern.pos(2), enumTypeSymbol.startLine, enumTypeSymbol.startColumn);
            enumTypeSymbol.endLine = enumTypeSymbol.startLine;
            enumTypeSymbol.endColumn = enumTypeSymbol.startColumn + typeName.length();
            addSymbol(enumTypeSymbol);
            symbolsFound++;

            // 解析枚举值
            QStringList valueList = enumValues.split(',', QString::SkipEmptyParts);
            for (const QString &value : valueList) {
                QString cleanValue = value.trimmed();
                int assignPos = cleanValue.indexOf('=');
                if (assignPos >= 0) {
                    cleanValue = cleanValue.left(assignPos).trimmed();
                }

                if (!cleanValue.isEmpty()) {
                    SymbolInfo enumValueSymbol;
                    enumValueSymbol.fileName = currentFileName;
                    enumValueSymbol.symbolName = cleanValue;
                    enumValueSymbol.symbolType = sym_enum_value;
                    enumValueSymbol.position = match.position;
                    enumValueSymbol.length = cleanValue.length();
                    // 关联到枚举类型
                    enumValueSymbol.moduleScope = typeName;  // 使用moduleScope存储所属枚举类型
                    calculateLineColumn(text, match.position, enumValueSymbol.startLine, enumValueSymbol.startColumn);
                    enumValueSymbol.endLine = enumValueSymbol.startLine;
                    enumValueSymbol.endColumn = enumValueSymbol.startColumn + cleanValue.length();
                    addSymbol(enumValueSymbol);
                    symbolsFound++;
                }
            }
        }
    }

    // 3. 枚举变量声明: enum_name_t variable_name;
    QRegExp enumVarPattern("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*[;,]");
    // 这个需要结合类型信息来判断是否为枚举变量，在后续处理中完善

    // ===== 结构体分析 =====

    // 1. Packed struct: typedef struct packed { members } struct_name_t;
    QRegExp packedStructPattern("\\btypedef\\s+struct\\s+packed\\s*\\{([^}]+)\\}\\s*([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> packedStructMatches = findMatchesOutsideComments(text, packedStructPattern);

    for (const RegexMatch &match : qAsConst(packedStructMatches)) {
        if (packedStructPattern.indexIn(text, match.position) != -1) {
            QString structMembers = packedStructPattern.cap(1);
            QString structName = packedStructPattern.cap(2);

            // 添加结构体类型定义
            SymbolInfo structTypeSymbol;
            structTypeSymbol.fileName = currentFileName;
            structTypeSymbol.symbolName = structName;
            structTypeSymbol.symbolType = sym_packed_struct;
            structTypeSymbol.position = match.position;
            structTypeSymbol.length = match.length;
            calculateLineColumn(text, packedStructPattern.pos(2), structTypeSymbol.startLine, structTypeSymbol.startColumn);
            structTypeSymbol.endLine = structTypeSymbol.startLine;
            structTypeSymbol.endColumn = structTypeSymbol.startColumn + structName.length();
            addSymbol(structTypeSymbol);
            symbolsFound++;

            // 解析结构体成员
            analyzeStructMembers(structMembers, structName, match.position, text);
        }
    }

    // 2. Unpacked struct: typedef struct { members } struct_name_t;
    QRegExp unpackedStructPattern("\\btypedef\\s+struct\\s*\\{([^}]+)\\}\\s*([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> unpackedStructMatches = findMatchesOutsideComments(text, unpackedStructPattern);

    for (const RegexMatch &match : qAsConst(unpackedStructMatches)) {
        if (unpackedStructPattern.indexIn(text, match.position) != -1) {
            QString structMembers = unpackedStructPattern.cap(1);
            QString structName = unpackedStructPattern.cap(2);

            // 添加结构体类型定义
            SymbolInfo structTypeSymbol;
            structTypeSymbol.fileName = currentFileName;
            structTypeSymbol.symbolName = structName;
            structTypeSymbol.symbolType = sym_unpacked_struct;
            structTypeSymbol.position = match.position;
            structTypeSymbol.length = match.length;
            calculateLineColumn(text, unpackedStructPattern.pos(2), structTypeSymbol.startLine, structTypeSymbol.startColumn);
            structTypeSymbol.endLine = structTypeSymbol.startLine;
            structTypeSymbol.endColumn = structTypeSymbol.startColumn + structName.length();
            addSymbol(structTypeSymbol);
            symbolsFound++;

            // 解析结构体成员
            analyzeStructMembers(structMembers, structName, match.position, text);
        }
    }

    // 3. 结构体变量声明分析
    analyzeStructVariables(text);
}

// 新增：分析结构体成员的辅助方法
void sym_list::analyzeStructMembers(const QString &membersText, const QString &structName,
                                   int basePosition, const QString &fullText)
{
    // 解析结构体成员
    QStringList lines = membersText.split(';', QString::SkipEmptyParts);

    for (const QString &line : lines) {
        QString cleanLine = line.trimmed();
        if (cleanLine.isEmpty()) continue;

        // 基本成员模式: type member_name [array_size]
        QRegExp memberPattern("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s+([a-zA-Z_][a-zA-Z0-9_]*)(?:\\s*\\[[^\\]]*\\])?");

        if (memberPattern.indexIn(cleanLine) != -1) {
            QString memberType = memberPattern.cap(1);
            QString memberName = memberPattern.cap(2);

            SymbolInfo memberSymbol;
            memberSymbol.fileName = currentFileName;
            memberSymbol.symbolName = memberName;
            memberSymbol.symbolType = sym_struct_member;
            memberSymbol.position = basePosition;
            memberSymbol.length = memberName.length();
            memberSymbol.moduleScope = structName;  // 使用moduleScope存储所属结构体名称
            calculateLineColumn(fullText, basePosition, memberSymbol.startLine, memberSymbol.startColumn);
            memberSymbol.endLine = memberSymbol.startLine;
            memberSymbol.endColumn = memberSymbol.startColumn + memberName.length();
            addSymbol(memberSymbol);
        }
    }
}

// 新增：分析结构体变量声明
void sym_list::analyzeStructVariables(const QString &text)
{
    // 获取所有已知的结构体类型
    QSet<QString> structTypes;
    for (const SymbolInfo &symbol : getAllSymbols()) {
        if (symbol.symbolType == sym_packed_struct || symbol.symbolType == sym_unpacked_struct) {
            structTypes.insert(symbol.symbolName);
        }
    }

    // 用于去重：记录已经添加的struct变量（文件名+变量名+类型名）
    QSet<QString> addedStructVars;
    
    // 查找结构体变量声明
    for (const QString &structType : structTypes) {
        QString pattern = QString("\\b%1\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*[;,]").arg(structType);
        QRegExp structVarPattern(pattern);
        QList<RegexMatch> structVarMatches = findMatchesOutsideComments(text, structVarPattern);

        for (const RegexMatch &match : qAsConst(structVarMatches)) {
            if (structVarPattern.indexIn(text, match.position) != -1) {
                QString varName = structVarPattern.cap(1);
                
                // 检查是否已经添加过（去重）
                QString uniqueKey = QString("%1:%2:%3").arg(currentFileName).arg(varName).arg(structType);
                if (addedStructVars.contains(uniqueKey)) {
                    continue; // 跳过重复的
                }
                addedStructVars.insert(uniqueKey);

                SymbolInfo varSymbol;
                varSymbol.fileName = currentFileName;
                varSymbol.symbolName = varName;
                // 根据结构体类型确定变量类型
                bool isPacked = false;
                for (const SymbolInfo &symbol : getAllSymbols()) {
                    if (symbol.symbolName == structType) {
                        isPacked = (symbol.symbolType == sym_packed_struct);
                        break;
                    }
                }
                varSymbol.symbolType = isPacked ? sym_packed_struct_var : sym_unpacked_struct_var;
                varSymbol.position = match.position;
                varSymbol.length = match.length;
                varSymbol.moduleScope = structType;  // 存储结构体类型名称
                calculateLineColumn(text, structVarPattern.pos(1), varSymbol.startLine, varSymbol.startColumn);
                varSymbol.endLine = varSymbol.startLine;
                varSymbol.endColumn = varSymbol.startColumn + varName.length();
                addSymbol(varSymbol);
            }
        }
    }
}

// 辅助函数：找到匹配的闭括号
static int findMatchingBrace(const QString &text, int openBracePos)
{
    if (openBracePos < 0 || openBracePos >= text.length() || text[openBracePos] != '{') {
        return -1;
    }
    
    int depth = 1;
    int pos = openBracePos + 1;
    
    while (pos < text.length() && depth > 0) {
        QChar ch = text[pos];
        if (ch == '{') {
            depth++;
        } else if (ch == '}') {
            depth--;
            if (depth == 0) {
                return pos;
            }
        } else if (ch == '"') {
            // 跳过字符串
            pos++;
            while (pos < text.length() && text[pos] != '"') {
                if (text[pos] == '\\' && pos + 1 < text.length()) {
                    pos += 2; // 跳过转义字符
                } else {
                    pos++;
                }
            }
        } else if (ch == '/' && pos + 1 < text.length()) {
            // 跳过注释
            if (text[pos + 1] == '/') {
                // 单行注释，跳到行尾
                while (pos < text.length() && text[pos] != '\n') {
                    pos++;
                }
            } else if (text[pos + 1] == '*') {
                // 多行注释
                pos += 2;
                while (pos + 1 < text.length()) {
                    if (text[pos] == '*' && text[pos + 1] == '/') {
                        pos += 2;
                        break;
                    }
                    pos++;
                }
            }
        }
        pos++;
    }
    
    return -1; // 未找到匹配的闭括号
}

// 查找所有struct的范围（包括packed和unpacked）
QList<sym_list::StructRange> sym_list::findStructRanges(const QString &text)
{
    QList<StructRange> ranges;
    
    // 1. 查找packed struct: typedef struct packed { ... } structName;
    QRegExp packedStructPattern("\\btypedef\\s+struct\\s+packed\\s*\\{");
    int pos = 0;
    while ((pos = packedStructPattern.indexIn(text, pos)) != -1) {
        // 检查是否在注释中
        if (!isMatchInComment(pos, packedStructPattern.matchedLength())) {
            // 找到'{'的位置
            int braceStart = text.indexOf('{', pos + packedStructPattern.matchedLength() - 1);
            if (braceStart != -1) {
                // 找到匹配的'}'
                int braceEnd = findMatchingBrace(text, braceStart);
                if (braceEnd != -1) {
                    StructRange range;
                    range.startPos = braceStart;
                    range.endPos = braceEnd;
                    ranges.append(range);
                }
            }
        }
        pos += packedStructPattern.matchedLength();
    }
    
    // 2. 查找unpacked struct: typedef struct { ... } structName;
    QRegExp unpackedStructPattern("\\btypedef\\s+struct\\s*\\{");
    pos = 0;
    while ((pos = unpackedStructPattern.indexIn(text, pos)) != -1) {
        // 检查是否在注释中
        if (!isMatchInComment(pos, unpackedStructPattern.matchedLength())) {
            // 找到'{'的位置
            int braceStart = text.indexOf('{', pos + unpackedStructPattern.matchedLength() - 1);
            if (braceStart != -1) {
                // 找到匹配的'}'
                int braceEnd = findMatchingBrace(text, braceStart);
                if (braceEnd != -1) {
                    StructRange range;
                    range.startPos = braceStart;
                    range.endPos = braceEnd;
                    ranges.append(range);
                }
            }
        }
        pos += unpackedStructPattern.matchedLength();
    }
    
    return ranges;
}

// 检查位置是否在struct范围内
bool sym_list::isPositionInStructRange(int position, const QList<StructRange> &structRanges)
{
    for (const StructRange &range : structRanges) {
        if (position >= range.startPos && position <= range.endPos) {
            return true;
        }
    }
    return false;
}
