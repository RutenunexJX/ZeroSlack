#include "syminfo.h"
#include "scope_tree.h"
#include "completionmanager.h"
#include "symbolrelationshipengine.h"
#include "sv_symbol_parser.h"
#include "sv_treesitter_parser.h"

#include <QDebug>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QReadLocker>
#include <QThread>
#include <QCoreApplication>
#include <QWriteLocker>
#include <QMutex>
#include <algorithm>
#include <memory>
#include <QVector>

std::unique_ptr<sym_list> sym_list::instance = nullptr;

static thread_local bool s_holdingWriteLock = false;

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
    delete m_scopeManager;
    m_scopeManager = nullptr;
}

ScopeManager* sym_list::getScopeManager() const
{
    if (!m_scopeManager)
        m_scopeManager = new ScopeManager();
    return m_scopeManager;
}

sym_list* sym_list::getInstance()
{
    static QMutex instanceMutex;
    QMutexLocker lock(&instanceMutex);
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
    SymbolInfo newSymbol = symbol;
    if (newSymbol.symbolId <= 0) {
        newSymbol.symbolId = allocateSymbolId();
    }

    if (newSymbol.moduleScope.isEmpty() &&
        (newSymbol.symbolType == sym_reg ||
         newSymbol.symbolType == sym_wire ||
         newSymbol.symbolType == sym_logic)) {
        newSymbol.moduleScope = getCurrentModuleScope(newSymbol.fileName, newSymbol.startLine);
    }

    symbolDatabase.append(newSymbol);
    int newIndex = symbolDatabase.size() - 1;
    symbolIdToIndex[newSymbol.symbolId] = newIndex;
    addToIndexes(newIndex);
    updateLineBasedSymbols(newSymbol);
    indexesDirty = true;
    CompletionManager::getInstance()->invalidateCommandModeCache();
}

sym_list::SymbolInfo sym_list::getSymbolById(int symbolId) const
{
    if (s_holdingWriteLock) {
        if (symbolIdToIndex.contains(symbolId)) {
            int index = symbolIdToIndex[symbolId];
            if (index < symbolDatabase.size()) {
                return symbolDatabase[index];
            }
        }
        SymbolInfo emptySymbol;
        emptySymbol.symbolId = -1;
        return emptySymbol;
    }
    QReadLocker lock(&symbolDbLock);
    if (symbolIdToIndex.contains(symbolId)) {
        int index = symbolIdToIndex[symbolId];
        if (index < symbolDatabase.size()) {
            return symbolDatabase[index];
        }
    }

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

    relationshipEngine->clearAllRelationships();
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

    analyzeModuleContainment(fileName);
    relationshipEngine->buildFileRelationships(fileName);
}

static const int kBackgroundOnePassWindow = 1024;

void sym_list::extractSymbolsAndContainsOnePass(const QString& text)
{
    const bool isBackground = (QThread::currentThread() != QCoreApplication::instance()->thread());
    int maxSearchWindow = isBackground ? kBackgroundOnePassWindow : 0;
    extractSymbolsAndContainsOnePassImpl(text, maxSearchWindow);
}

void sym_list::extractSymbolsAndContainsOnePassImpl(const QString& text, int maxSearchWindow)
{
    Q_UNUSED(maxSearchWindow);
    QList<int> moduleStack;
    QList<QString> moduleNameStack;

    ScopeManager* scopeMgr = getScopeManager();
    scopeMgr->clearFile(currentFileName);
    ScopeNode* fileRoot = new ScopeNode(ScopeType::Global, 0);
    fileRoot->endLine = 0;
    scopeMgr->setFileRoot(currentFileName, fileRoot);
    QStack<ScopeNode*> scopeStack;
    scopeStack.push(fileRoot);

    // 调试：每次分析文件时用 SVTreeSitterParser 解析并打印（加载工作区、打开文件、编辑器内容变更都会走到这里）
    {
        SVTreeSitterParser tsParser;
        tsParser.parse(text);
        QList<SymbolInfo> tsSymbols = tsParser.getSymbols();
        qDebug() << "SVTreeSitterParser [file]" << currentFileName << "symbols:" << tsSymbols.size();
        if (!tsSymbols.isEmpty()) {
            const SymbolInfo &first = tsSymbols.first();
            qDebug() << "  first: name=" << first.symbolName << "type=" << first.symbolType
                     << "line=" << first.startLine << "col=" << first.startColumn;
        }
    }

    QSet<QString> knownTypes;
    for (const SymbolInfo &s : symbolDatabase) {
        if (s.symbolType == sym_packed_struct || s.symbolType == sym_unpacked_struct
            || s.symbolType == sym_typedef || s.symbolType == sym_enum) {
            knownTypes.insert(s.symbolName);
        }
    }

    SVSymbolParser parser(text, currentFileName, knownTypes);
    QList<SymbolInfo> parsed = parser.parse();
    commentRegions = parser.takeComments();

    for (const SymbolInfo &sym : qAsConst(parsed)) {
        while (scopeStack.size() > 1 && scopeStack.top()->endLine > 0 && sym.startLine > scopeStack.top()->endLine) {
            ScopeNode* node = scopeStack.pop();
            if (node->type == ScopeType::Module && !moduleStack.isEmpty()) {
                moduleStack.removeLast();
                moduleNameStack.removeLast();
            }
        }

        if (sym.symbolType == sym_module) {
            addSymbol(sym);
            int moduleId = symbolDatabase.last().symbolId;
            SymbolInfo added = symbolDatabase.last();
            moduleStack.append(moduleId);
            moduleNameStack.append(sym.symbolName);
            ScopeNode* modNode = new ScopeNode(ScopeType::Module, sym.startLine);
            modNode->endLine = sym.endLine;
            modNode->parent = scopeStack.top();
            scopeStack.top()->children.append(modNode);
            modNode->symbols[sym.symbolName] = added;
            scopeStack.push(modNode);
            continue;
        }

        if (sym.symbolType == sym_task || sym.symbolType == sym_function) {
            addSymbol(sym);
            SymbolInfo added = symbolDatabase.last();
            if (relationshipEngine && !moduleStack.isEmpty())
                relationshipEngine->addRelationship(moduleStack.last(), added.symbolId, SymbolRelationshipEngine::CONTAINS);
            ScopeType st = (sym.symbolType == sym_task) ? ScopeType::Task : ScopeType::Function;
            ScopeNode* subNode = new ScopeNode(st, sym.startLine);
            subNode->endLine = sym.endLine;
            subNode->parent = scopeStack.top();
            scopeStack.top()->children.append(subNode);
            subNode->symbols[sym.symbolName] = added;
            scopeStack.push(subNode);
            continue;
        }

        if (sym.symbolType == sym_port_input || sym.symbolType == sym_port_output
            || sym.symbolType == sym_port_inout || sym.symbolType == sym_port_ref
            || sym.symbolType == sym_port_interface || sym.symbolType == sym_port_interface_modport) {
            addSymbol(sym);
            SymbolInfo added = symbolDatabase.last();
            if (relationshipEngine && !moduleStack.isEmpty())
                relationshipEngine->addRelationship(moduleStack.last(), added.symbolId, SymbolRelationshipEngine::CONTAINS);
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbols[added.symbolName] = added;
            continue;
        }

        if (sym.symbolType == sym_reg || sym.symbolType == sym_wire || sym.symbolType == sym_logic) {
            addSymbol(sym);
            SymbolInfo added = symbolDatabase.last();
            if (relationshipEngine && !moduleStack.isEmpty())
                relationshipEngine->addRelationship(moduleStack.last(), added.symbolId, SymbolRelationshipEngine::CONTAINS);
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbols[added.symbolName] = added;
            continue;
        }

        if (sym.symbolType == sym_always || sym.symbolType == sym_always_ff || sym.symbolType == sym_always_comb
            || sym.symbolType == sym_always_latch || sym.symbolType == sym_assign
            || sym.symbolType == sym_inst || sym.symbolType == sym_inst_pin) {
            addSymbol(sym);
            SymbolInfo added = symbolDatabase.last();
            if (relationshipEngine && !moduleStack.isEmpty())
                relationshipEngine->addRelationship(moduleStack.last(), added.symbolId, SymbolRelationshipEngine::CONTAINS);
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbols[added.symbolName] = added;
            continue;
        }

        if (sym.symbolType == sym_typedef || sym.symbolType == sym_enum
            || sym.symbolType == sym_packed_struct || sym.symbolType == sym_unpacked_struct
            || sym.symbolType == sym_enum_value || sym.symbolType == sym_enum_var
            || sym.symbolType == sym_struct_member
            || sym.symbolType == sym_packed_struct_var || sym.symbolType == sym_unpacked_struct_var) {
            addSymbol(sym);
            SymbolInfo added = symbolDatabase.last();
            if (relationshipEngine && !moduleStack.isEmpty())
                relationshipEngine->addRelationship(moduleStack.last(), added.symbolId, SymbolRelationshipEngine::CONTAINS);
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbols[added.symbolName] = added;
        }
    }
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

    QList<SymbolInfo> modules;
    for (const SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType == sym_module) {
            modules.append(symbol);
        }
    }

    for (const SymbolInfo& module : modules) {
        for (const SymbolInfo& symbol : fileSymbols) {
            if (symbol.symbolId != module.symbolId &&
                isSymbolInModule(symbol, module)) {

                relationshipEngine->addRelationship(
                    module.symbolId,
                    symbol.symbolId,
                    SymbolRelationshipEngine::CONTAINS
                );

                int symbolIndex = symbolIdToIndex[symbol.symbolId];
                if (symbolIndex < symbolDatabase.size()) {
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
    QReadLocker lock(&symbolDbLock);
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

int sym_list::findSymbolIdByName(const QString& symbolName) const
{
    if (s_holdingWriteLock) {
        // 调用方已持写锁，不再加读锁，避免同一线程死锁
        if (symbolNameIndex.contains(symbolName)) {
            const QList<int>& indices = symbolNameIndex[symbolName];
            if (!indices.isEmpty() && indices.first() < symbolDatabase.size()) {
                return symbolDatabase[indices.first()].symbolId;
            }
        }
        return -1;
    }
    QReadLocker lock(&symbolDbLock);
    if (symbolNameIndex.contains(symbolName)) {
        const QList<int>& indices = symbolNameIndex[symbolName];
        if (!indices.isEmpty() && indices.first() < symbolDatabase.size()) {
            return symbolDatabase[indices.first()].symbolId;
        }
    }
    return -1;
}

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
    if (s_holdingWriteLock) {
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
    QReadLocker lock(&symbolDbLock);
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
    if (s_holdingWriteLock) {
        return symbolDatabase;
    }
    QReadLocker lock(&symbolDbLock);
    return symbolDatabase;
}

void sym_list::clearSymbolsForFile(const QString& fileName)
{
    int beforeCount = symbolDatabase.size();

    getScopeManager()->clearFile(fileName);

    if (relationshipEngine) {
        relationshipEngine->invalidateFileRelationships(fileName);
    }
    if (fileNameIndex.contains(fileName)) {
        QList<int> indicesToRemove = fileNameIndex[fileName];
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
    }
    int afterCount = symbolDatabase.size();
    if (beforeCount != afterCount) {
        CompletionManager::getInstance()->invalidateSymbolCaches();
        invalidateCache();
    }
}

void sym_list::rebuildAllIndexes()
{
    symbolTypeIndex.clear();
    symbolNameIndex.clear();
    fileNameIndex.clear();
    symbolIdToIndex.clear();
    for (int i = 0; i < symbolDatabase.size(); ++i) {
        addToIndexes(i);
        symbolIdToIndex[symbolDatabase[i].symbolId] = i;
    }

    invalidateCache();
}

void sym_list::addToIndexes(int symbolIndex)
{
    if (symbolIndex >= symbolDatabase.size()) return;

    const SymbolInfo& symbol = symbolDatabase[symbolIndex];
    symbolTypeIndex[symbol.symbolType].append(symbolIndex);
    symbolNameIndex[symbol.symbolName].append(symbolIndex);
    fileNameIndex[symbol.fileName].append(symbolIndex);
}

void sym_list::removeFromIndexes(int symbolIndex)
{
    if (symbolIndex >= symbolDatabase.size()) return;

    const SymbolInfo& symbol = symbolDatabase[symbolIndex];

    if (symbolTypeIndex.contains(symbol.symbolType)) {
        symbolTypeIndex[symbol.symbolType].removeAll(symbolIndex);
        if (symbolTypeIndex[symbol.symbolType].isEmpty()) {
            symbolTypeIndex.remove(symbol.symbolType);
        }
    }

    if (symbolNameIndex.contains(symbol.symbolName)) {
        symbolNameIndex[symbol.symbolName].removeAll(symbolIndex);
        if (symbolNameIndex[symbol.symbolName].isEmpty()) {
            symbolNameIndex.remove(symbol.symbolName);
        }
    }

    if (fileNameIndex.contains(symbol.fileName)) {
        fileNameIndex[symbol.fileName].removeAll(symbolIndex);
        if (fileNameIndex[symbol.fileName].isEmpty()) {
            fileNameIndex.remove(symbol.fileName);
        }
    }
}

void sym_list::invalidateCache()
{
    cachedSymbolNamesByType.clear();
    cachedUniqueNames.clear();
    indexesDirty = true;
}

void sym_list::updateCachedData() const
{
    if (!indexesDirty) return;
    cachedSymbolNamesByType.clear();
    cachedUniqueNames.clear();

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

        names.removeDuplicates();
        names.sort();

        cachedSymbolNamesByType[symbolType] = names;
    }

    indexesDirty = false;
}

bool sym_list::isPositionInComment(int position)
{
    auto it = std::lower_bound(commentRegions.begin(), commentRegions.end(), position,
        [](const CommentRegion& region, int pos) {
            return region.endPos <= pos;
        });

    return it != commentRegions.end() && position >= it->startPos;
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

bool sym_list::isMatchInComment(int matchStart, int matchLength)
{
    const int matchEnd = matchStart + matchLength;
    auto it = std::lower_bound(commentRegions.begin(), commentRegions.end(), matchStart,
        [](const CommentRegion& region, int pos) {
            return region.endPos <= pos;
        });
    for (auto iter = it; iter != commentRegions.end() && iter->startPos < matchEnd; ++iter) {
        if (matchStart < iter->endPos && matchEnd > iter->startPos) {
            return true;
        }
    }
    return false;
}

bool sym_list::isPositionInMultiLineComment(int pos)
{
    return isPositionInComment(pos);
}

QList<sym_list::CommentRegion> sym_list::getCommentRegions() const
{
    return commentRegions;
}

void sym_list::setContentIncremental(const QString& fileName, const QString& content)
{
    QWriteLocker lock(&symbolDbLock);
    s_holdingWriteLock = true;

    currentFileName = fileName;

    if (!needsAnalysis(currentFileName, content)) {
        s_holdingWriteLock = false;
        return;
    }

    FileState& state = fileStates[currentFileName];

    bool hasNoSymbols = !fileNameIndex.contains(currentFileName) || fileNameIndex.value(currentFileName).isEmpty();
    bool isFirstTime = !fileStates.contains(currentFileName) || state.needsFullAnalysis || hasNoSymbols;

    if (isFirstTime) {
        clearSymbolsForFile(currentFileName);
        // 先缓存当前内容，供 extractSymbolsAndContainsOnePass 内 getCurrentModuleScope -> findEndModuleLine 使用，避免读到磁盘旧内容导致 moduleScope 为空、补全“l ”无 logic
        previousFileContents[currentFileName] = content;
        extractSymbolsAndContainsOnePass(content);
        buildSymbolRelationships(currentFileName);
        state.needsFullAnalysis = false;
    } else {
        clearSymbolsForFile(currentFileName);
        previousFileContents[currentFileName] = content;
        extractSymbolsAndContainsOnePass(content);
        buildSymbolRelationships(currentFileName);
    }

    state.contentHash = calculateContentHash(content);
    state.symbolRelevantHash = calculateSymbolRelevantHash(content);
    state.lastAnalyzedLineCount = content.count('\n') + 1;
    state.lastModified = QDateTime::currentDateTime();
    previousFileContents[currentFileName] = content;
    s_holdingWriteLock = false;
}

QString sym_list::calculateContentHash(const QString& content)
{
    return QString::number(qHash(content));
}

QString sym_list::calculateSymbolRelevantHash(const QString& content)
{
    QString work = content;
    int i = 0;
    while (i < work.length()) {
        int start = work.indexOf("/*", i);
        if (start < 0) break;
        int end = work.indexOf("*/", start + 2);
        if (end < 0) end = work.length();
        work.replace(start, end - start + 2, " ");
        i = start + 1;
    }
    QStringList lines = work.split('\n');
    QStringList kept;
    for (const QString& line : qAsConst(lines)) {
        QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith("//")) continue;
        kept.append(QString(t).replace(QRegularExpression("\\s+"), " "));
    }
    QString joined = kept.join(" ").trimmed();
    return QString::number(qHash(joined));
}

bool sym_list::needsAnalysis(const QString& fileName, const QString& content)
{
    if (!fileStates.contains(fileName)) return true;

    int currentLineCount = content.count('\n') + 1;
    if (fileStates[fileName].lastAnalyzedLineCount != currentLineCount)
        return true;

    QString newSymbolHash = calculateSymbolRelevantHash(content);
    const QString& stored = fileStates[fileName].symbolRelevantHash;
    if (stored.isEmpty()) return true;
    return newSymbolHash != stored;
}

bool sym_list::contentAffectsSymbols(const QString& fileName, const QString& content)
{
    return needsAnalysis(fileName, content);
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

        return changedLines;
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

            static QStringList declarationKeywords = {"module", "reg", "wire", "logic", "task", "function"};
            for (const QString &keyword : declarationKeywords) {
                if (newLine.contains(QRegularExpression("\\b" + QRegularExpression::escape(keyword) + "\\b"))) {
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

    previousFileContents[fileName] = newContent;
    changedLines = changedLines.toSet().toList();
    std::sort(changedLines.begin(), changedLines.end());

    return changedLines;
}

void sym_list::clearSymbolsForLines(const QString& fileName, const QList<int>& lines)
{
    int removedCount = 0;

    if (lineBasedSymbols.contains(fileName)) {
        for (int lineNum : lines) {
            if (lineBasedSymbols[fileName].contains(lineNum)) {
                removedCount += lineBasedSymbols[fileName][lineNum].size();
                lineBasedSymbols[fileName].remove(lineNum);
            }
        }
    }

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

    std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<int>());

    for (int index : indicesToRemove) {
        if (index < symbolDatabase.size()) {
            removeFromIndexes(index);
            symbolDatabase.removeAt(index);
        }
    }

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
}

bool sym_list::isValidModuleName(const QString& name) {
    if (name.isEmpty()) return false;
    static const QRegularExpression svIdentifier(QStringLiteral("^[a-zA-Z_][a-zA-Z0-9_]*$"));
    return svIdentifier.match(name).hasMatch();
}

QString sym_list::getCurrentModuleScope(const QString& fileName, int lineNumber) {
    QList<SymbolInfo> modules = findSymbolsByType(sym_module);
    for (const SymbolInfo& moduleSymbol : modules) {
        if (moduleSymbol.fileName != fileName) continue;
        if (!isValidModuleName(moduleSymbol.symbolName)) continue;
        int moduleEndLine = findEndModuleLine(fileName, moduleSymbol);
        if (moduleEndLine < 0) continue;
        if (lineNumber > moduleSymbol.startLine && lineNumber < moduleEndLine)
            return moduleSymbol.symbolName;
    }
    return QString();
}

QString sym_list::getCachedFileContent(const QString& fileName) const
{
    QReadLocker lock(&symbolDbLock);
    return previousFileContents.value(fileName, QString());
}

int sym_list::findEndModuleLine(const QString &fileName, const SymbolInfo &moduleSymbol)
{
    if (moduleSymbol.symbolType != sym_module) {
        return -1;
    }

    QString content;
    if (previousFileContents.contains(fileName)) {
        content = previousFileContents[fileName];
    } else {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QFile::Text)) {
            return -1;
        }
        content = file.readAll();
        file.close();
        previousFileContents[fileName] = content;
    }

    QStringList lines = content.split('\n');
    int moduleDepth = 0;
    // 增量维护行首偏移，避免 O(lines^2) 导致大文件卡死
    int lineStartPos = 0;
    for (int j = 0; j < moduleSymbol.startLine && j < lines.size(); ++j)
        lineStartPos += lines[j].length() + 1;

    for (int i = moduleSymbol.startLine; i < lines.size(); ++i) {
        const QString &line = lines[i];
        static const QRegularExpression moduleWord("\\bmodule\\b");
        static const QRegularExpression endmoduleWord("\\bendmodule\\b");
        if (line.contains(moduleWord) && !isMatchInComment(lineStartPos, line.length())) {
            moduleDepth++;
        }
        if (line.contains(endmoduleWord) && !isMatchInComment(lineStartPos, line.length())) {
            moduleDepth--;
            if (moduleDepth == 0) {
                return i;
            }
        }
        lineStartPos += line.length() + 1;
    }

    return -1;
}

bool isSymbolInModule(const sym_list::SymbolInfo& symbol, const sym_list::SymbolInfo& module)
{
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
    return QString();
}

