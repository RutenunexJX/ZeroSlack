#include "syminfo.h"
#include "scope_tree.h"
#include "completionmanager.h"
#include "symbolrelationshipengine.h"
#include "sv_symbol_parser.h"

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

// 前向声明：在 parseModulePorts / parseInstanceConnections / analyzeModuleInstantiations 之前使用
static int findMatchingParen(const QString &text, int openParenPos);

std::unique_ptr<sym_list> sym_list::instance = nullptr;

// 供 setContentIncremental 持写锁时避免 findSymbolsByFileName 内再次加读锁导致死锁
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

// UPDATED: Smart pointer singleton implementation，多线程安全（阶段 B）
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
    if (s_holdingWriteLock) {
        // 调用方已持写锁，不再加读锁，避免同一线程死锁
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

// 单遍合并：从 startPos 起找下一个结构匹配（不在注释内），返回最早的一个
// maxSearchLen > 0 时仅在 text.mid(startPos, maxSearchLen) 内匹配，避免长文本灾难性回溯卡死
sym_list::StructuralMatchResult sym_list::findNextStructuralMatch(const QString& text, int startPos,
                                                                  const QList<StructRange>& structRanges,
                                                                  int maxSearchLen)
{
    StructuralMatchResult best;
    best.position = -1;
    const bool useWindow = maxSearchLen > 0 && (startPos + maxSearchLen) < text.length();
    const int searchLen = useWindow ? maxSearchLen : (text.length() - startPos);
    if (searchLen <= 0) return best;

    const QString searchText = useWindow ? text.mid(startPos, searchLen) : text;
    const int matchStart = useWindow ? 0 : startPos;
    const int posOffset = useWindow ? startPos : 0;

    // 方括号内用 {0,500} 限定长度，避免 [^\\]]* 在长文本上灾难性回溯导致卡死
    static const QRegularExpression modulePattern("\\bmodule\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    static const QRegularExpression endmodulePattern("\\bendmodule\\b");
    static const QRegularExpression endtaskPattern("\\bendtask\\b");
    static const QRegularExpression endfunctionPattern("\\bendfunction\\b");
    static const QRegularExpression beginPattern("\\bbegin\\b");
    static const QRegularExpression endOnlyPattern("\\bend\\b");
    static const QRegularExpression regPattern("\\breg\\s+(?:\\[[^\\]]{0,500}\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    static const QRegularExpression wirePattern("\\bwire\\s+(?:\\[[^\\]]{0,500}\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    static const QRegularExpression logicPattern("\\blogic\\s+(?:\\[[^\\]]{0,500}\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    static const QRegularExpression taskPattern("\\btask\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    // 返回值用 \S+\s+ 单 token，避免 \w{0,150} 灾难性回溯导致卡死
    static const QRegularExpression functionPattern("\\bfunction\\s+(?:\\S+\\s+)?([a-zA-Z_][a-zA-Z0-9_]*)");

    auto tryPattern = [&](const QRegularExpression& pattern, int type, int capGroup) -> void {
        QRegularExpressionMatch m = pattern.match(searchText, matchStart);
        if (!m.hasMatch()) return;
        int pos = m.capturedStart(0) + posOffset;
        if (best.position >= 0 && pos > best.position) return;
        if (isMatchInComment(pos, m.capturedLength(0))) return;
        if (type == 4) {  // logic：排除 struct 内部（在 struct 范围内的不加入 logic 池）
            int capPos = (capGroup > 0 && m.lastCapturedIndex() >= capGroup) ? m.capturedStart(capGroup) + posOffset : pos;
            if (capPos >= 0 && isPositionInStructRange(capPos, structRanges)) return;
        }
        best.position = pos;
        best.length = m.capturedLength(0);
        best.matchType = type;
        best.capturedName = (capGroup > 0 && m.lastCapturedIndex() >= capGroup) ? m.captured(capGroup) : QString();
        best.capturePos = (capGroup > 0 && m.lastCapturedIndex() >= capGroup) ? m.capturedStart(capGroup) + posOffset : pos;
    };

    tryPattern(modulePattern, 0, 1);
    tryPattern(endmodulePattern, 1, 0);
    tryPattern(endtaskPattern, 7, 0);
    tryPattern(endfunctionPattern, 8, 0);
    tryPattern(beginPattern, 9, 0);
    tryPattern(endOnlyPattern, 10, 0);
    tryPattern(regPattern, 2, 1);
    tryPattern(wirePattern, 3, 1);
    tryPattern(logicPattern, 4, 1);
    tryPattern(taskPattern, 5, 1);
    tryPattern(functionPattern, 6, 1);

    return best;
}

// 后台 onePass 单次匹配窗口大小，限制正则输入长度避免灾难性回溯
static const int kBackgroundOnePassWindow = 1024;

// 判断该行在类型关键字（reg/wire/logic）之前是否包含端口方向；若是则不应作为模块级变量加入（端口已由 parseModulePorts 添加）
static bool isVariableDeclarationAPort(const QString& text, int typeKeywordPos)
{
    if (typeKeywordPos <= 0 || typeKeywordPos >= text.length()) return false;
    int lineStart = typeKeywordPos;
    while (lineStart > 0 && text[lineStart - 1] != '\n') lineStart--;
    QString prefix = text.mid(lineStart, typeKeywordPos - lineStart);
    QRegularExpression portDir("\\b(?:input|output|inout|ref)\\b");
    return portDir.match(prefix).hasMatch();
}

void sym_list::extractSymbolsAndContainsOnePass(const QString& text)
{
    const bool isBackground = (QThread::currentThread() != QCoreApplication::instance()->thread());
    // 始终计算 struct 范围，从根源排除 struct 内 reg/wire/logic（首次加载工作区与打开文件一致）
    QList<StructRange> structRanges = findStructRanges(text);
    int maxSearchWindow = isBackground ? kBackgroundOnePassWindow : 0;
    extractSymbolsAndContainsOnePassImpl(text, structRanges, maxSearchWindow);
}

void sym_list::extractSymbolsAndContainsOnePassImpl(const QString& text, const QList<StructRange>& structRanges,
                                                    int maxSearchWindow)
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

    SVSymbolParser parser(text, currentFileName);
    QList<SymbolInfo> parsed = parser.parse();

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
            if (isPositionInStructRange(sym.position, structRanges))
                continue;
            addSymbol(sym);
            SymbolInfo added = symbolDatabase.last();
            if (relationshipEngine && !moduleStack.isEmpty())
                relationshipEngine->addRelationship(moduleStack.last(), added.symbolId, SymbolRelationshipEngine::CONTAINS);
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbols[added.symbolName] = added;
        }
    }
}

// 从 position 起跳过空白与注释，返回下一个非空白位置
static int skipWhitespaceAndComments(const QString& text, int pos)
{
    const int n = text.length();
    while (pos < n) {
        if (text[pos].isSpace()) { pos++; continue; }
        if (pos + 1 < n && text[pos] == '/' && text[pos + 1] == '/') {
            while (pos < n && text[pos] != '\n') pos++;
            continue;
        }
        if (pos + 1 < n && text[pos] == '/' && text[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < n && !(text[pos] == '*' && text[pos + 1] == '/')) pos++;
            if (pos + 1 < n) pos += 2;
            continue;
        }
        break;
    }
    return pos;
}

void sym_list::parseModulePorts(const QString& text, int moduleKeywordPos, const QString& moduleName, int moduleId,
                                  const QVector<int>& lineStarts)
{
    auto posToLineColumn = [&lineStarts](int position, int &line, int &column) {
        auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), position);
        int lineIdx = qBound(0, (int)(it - lineStarts.begin()) - 1, lineStarts.size() - 1);
        line = lineIdx;
        column = position - lineStarts[lineIdx];
    };

    int p = moduleKeywordPos;
    const int n = text.length();
    if (p + 6 >= n || text.mid(p, 6) != QLatin1String("module")) return;
    p = skipWhitespaceAndComments(text, p + 6);
    // 跳过模块名（标识符）
    while (p < n && (text[p].isLetterOrNumber() || text[p] == '_')) p++;
    p = skipWhitespaceAndComments(text, p);
    // 可选 #( ... )
    if (p < n && text[p] == '#') {
        p++;
        p = skipWhitespaceAndComments(text, p);
        if (p < n && text[p] == '(') {
            int close = findMatchingParen(text, p);
            if (close < 0) return;
            p = skipWhitespaceAndComments(text, close + 1);
        }
    }
    if (p >= n || text[p] != '(') return;
    int portListStart = p + 1;
    int portListEnd = findMatchingParen(text, p);
    if (portListEnd < 0) return;
    QString portListStr;
    for (int i = portListStart; i < portListEnd; i++) {
        QChar c = text[i];
        if (c == '/' && i + 1 < portListEnd) {
            if (text[i + 1] == '/') {
                while (i < portListEnd && text[i] != '\n') { portListStr += ' '; i++; }
                i--;
                continue;
            }
            if (text[i + 1] == '*') {
                portListStr += "  ";
                i += 2;
                while (i + 1 < portListEnd && !(text[i] == '*' && text[i + 1] == '/')) { portListStr += ' '; i++; }
                if (i + 1 < portListEnd) i += 2;
                continue;
            }
        }
        portListStr += c;
    }

    // 按顶层逗号分割（尊重括号/方括号深度）
    QList<QString> segments;
    int depth = 0, start = 0;
    for (int i = 0; i <= portListStr.length(); i++) {
        QChar c = (i < portListStr.length()) ? portListStr[i] : QChar(',');
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') depth--;
        else if ((c == ',' || i == portListStr.length()) && depth == 0) {
            segments.append(portListStr.mid(start, i - start).trimmed());
            start = i + 1;
        }
    }

    sym_type_e lastPortType = sym_port_input;
    QString lastDataType;
    const QRegularExpression idRx("^[a-zA-Z_][a-zA-Z0-9_]*$");
    const QRegularExpression idDotRx("^[a-zA-Z_][a-zA-Z0-9_]*\\.[a-zA-Z_][a-zA-Z0-9_]*$");

    for (const QString& seg : qAsConst(segments)) {
        if (seg.isEmpty()) continue;
        QStringList tokens;
        for (int i = 0; i < seg.length(); ) {
            i = skipWhitespaceAndComments(seg, i);
            if (i >= seg.length()) break;
            if (seg[i] == '[') {
                int j = i + 1, d = 1;
                while (j < seg.length() && d > 0) {
                    if (seg[j] == '[') d++; else if (seg[j] == ']') d--;
                    j++;
                }
                tokens.append(seg.mid(i, j - i));
                i = j;
                continue;
            }
            if (seg[i].isLetterOrNumber() || seg[i] == '_' || seg[i] == '.') {
                int j = i;
                while (j < seg.length() && (seg[j].isLetterOrNumber() || seg[j] == '_' || seg[j] == '.')) j++;
                tokens.append(seg.mid(i, j - i));
                i = j;
                continue;
            }
            i++;
        }
        if (tokens.isEmpty()) continue;

        sym_type_e portType = lastPortType;
        QString dataType = lastDataType;
        QStringList names;
        int tokenIdx = 0;

        if (tokenIdx < tokens.size()) {
            const QString& t = tokens[tokenIdx];
            if (t == QLatin1String("input"))  { portType = sym_port_input;  tokenIdx++; }
            else if (t == QLatin1String("output")) { portType = sym_port_output; tokenIdx++; }
            else if (t == QLatin1String("inout"))  { portType = sym_port_inout;  tokenIdx++; }
            else if (t == QLatin1String("ref"))    { portType = sym_port_ref;    tokenIdx++; }
        }
        if (tokenIdx < tokens.size() && tokens[tokenIdx] == QLatin1String("virtual")) {
            portType = sym_port_interface;
            tokenIdx++;
            if (tokenIdx < tokens.size()) { dataType = tokens[tokenIdx]; tokenIdx++; }
        } else if (tokenIdx < tokens.size() && idDotRx.match(tokens[tokenIdx]).hasMatch()) {
            portType = sym_port_interface_modport;
            dataType = tokens[tokenIdx];
            tokenIdx++;
        } else {
            // 类型部分：保留至少一个 token 作为端口名（继承时可能只剩一个标识符）
            while (tokenIdx < tokens.size() - 1) {
                const QString& t = tokens[tokenIdx];
                if (t == QLatin1String("logic") || t == QLatin1String("reg") || t == QLatin1String("wire") ||
                    t.startsWith(QLatin1Char('[')) || idRx.match(t).hasMatch()) {
                    if (!dataType.isEmpty()) dataType += QLatin1Char(' ');
                    dataType += t;
                    tokenIdx++;
                } else
                    break;
            }
        }
        while (tokenIdx < tokens.size() && idRx.match(tokens[tokenIdx]).hasMatch()) {
            names.append(tokens[tokenIdx]);
            tokenIdx++;
        }
        if (names.isEmpty()) continue;
        lastPortType = portType;
        lastDataType = dataType;

        int nameSearchStart = 0;
        for (const QString& portName : qAsConst(names)) {
            SymbolInfo portSymbol;
            portSymbol.fileName = currentFileName;
            portSymbol.symbolName = portName;
            portSymbol.symbolType = portType;
            portSymbol.moduleScope = moduleName;
            portSymbol.scopeLevel = 1;
            portSymbol.dataType = dataType;
            int namePos = portListStr.indexOf(portName, nameSearchStart);
            if (namePos >= 0) {
                int absPos = portListStart + namePos;
                portSymbol.position = absPos;
                portSymbol.length = portName.length();
                posToLineColumn(absPos, portSymbol.startLine, portSymbol.startColumn);
                nameSearchStart = namePos + portName.length();
            } else {
                portSymbol.position = portListStart;
                portSymbol.length = portName.length();
                posToLineColumn(portListStart, portSymbol.startLine, portSymbol.startColumn);
            }
            portSymbol.endLine = portSymbol.startLine;
            portSymbol.endColumn = portSymbol.startColumn + portName.length();
            addSymbol(portSymbol);
            int portId = symbolDatabase.last().symbolId;
            if (relationshipEngine)
                relationshipEngine->addRelationship(moduleId, portId, SymbolRelationshipEngine::CONTAINS);
        }
    }
}

void sym_list::parseInstanceConnections(const QString& text, int instStartPos, const QString& moduleTypeName,
                                        int instanceSymbolId, const QVector<int>& lineStarts)
{
    Q_UNUSED(instanceSymbolId); // 预留：可建立 instance CONTAINS pin 关系
    if (!relationshipEngine) return;
    int moduleTypeId = findSymbolIdByName(moduleTypeName);
    if (moduleTypeId < 0) return;
    QList<int> moduleChildren = relationshipEngine->getModuleChildren(moduleTypeId);
    QHash<QString, int> portNameToId;
    for (int childId : qAsConst(moduleChildren)) {
        SymbolInfo child = getSymbolById(childId);
        if (child.symbolId < 0) continue;
        switch (child.symbolType) {
        case sym_port_input:
        case sym_port_output:
        case sym_port_inout:
        case sym_port_ref:
        case sym_port_interface:
        case sym_port_interface_modport:
            portNameToId[child.symbolName] = childId;
            break;
        default:
            break;
        }
    }

    auto posToLineColumn = [&lineStarts](int position, int &line, int &column) {
        auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), position);
        int lineIdx = qBound(0, (int)(it - lineStarts.begin()) - 1, lineStarts.size() - 1);
        line = lineIdx;
        column = position - lineStarts[lineIdx];
    };

    int p = instStartPos;
    const int n = text.length();
    p = skipWhitespaceAndComments(text, p);
    if (p >= n || text[p] != '(') return;
    int listStart = p + 1;
    int listEnd = findMatchingParen(text, p);
    if (listEnd < 0) return;

    static const QRegularExpression dotPinRx("\\.\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(");
    int searchPos = listStart;
    while (searchPos < listEnd) {
        QRegularExpressionMatch match = dotPinRx.match(text, searchPos);
        if (!match.hasMatch()) break;
        int dotPos = match.capturedStart(0);
        if (dotPos >= listEnd) break;
        QString pinName = match.captured(1);
        int openParen = match.capturedStart(0) + match.capturedLength(0) - 1;
        int closeParen = findMatchingParen(text, openParen);
        if (closeParen < 0) { searchPos = openParen + 1; continue; }
        if (isMatchInComment(dotPos, match.capturedLength(0))) { searchPos = closeParen + 1; continue; }
        int portId = portNameToId.value(pinName, -1);
        if (portId >= 0) {
            SymbolInfo pinSymbol;
            pinSymbol.fileName = currentFileName;
            pinSymbol.symbolName = pinName;
            pinSymbol.symbolType = sym_inst_pin;
            pinSymbol.position = dotPos;
            pinSymbol.length = match.capturedLength(0) - 1;
            posToLineColumn(dotPos, pinSymbol.startLine, pinSymbol.startColumn);
            pinSymbol.endLine = pinSymbol.startLine;
            pinSymbol.endColumn = pinSymbol.startColumn + pinName.length();
            addSymbol(pinSymbol);
            int pinId = symbolDatabase.last().symbolId;
            relationshipEngine->addRelationship(pinId, portId, SymbolRelationshipEngine::REFERENCES);
        }
        searchPos = closeParen + 1;
    }
}

void sym_list::analyzeModuleInstantiations(const QString& text)
{
    if (text.isEmpty()) return;
    QVector<int> lineStarts;
    lineStarts.append(0);
    for (int p = 0; p < text.length(); ) {
        int idx = text.indexOf('\n', p);
        if (idx < 0) break;
        p = idx + 1;
        lineStarts.append(p);
    }
    auto posToLineColumn = [&lineStarts](int position, int &line, int &column) {
        auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), position);
        int lineIdx = qBound(0, (int)(it - lineStarts.begin()) - 1, lineStarts.size() - 1);
        line = lineIdx;
        column = position - lineStarts[lineIdx];
    };

    // 匹配 "ModuleType inst_name" 且后续为可选 #(...) 再 (
    static const QRegularExpression instPattern("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*");
    int searchPos = 0;
    const int n = text.length();
    while (searchPos < n) {
        QRegularExpressionMatch m = instPattern.match(text, searchPos);
        if (!m.hasMatch()) break;
        int typeStart = m.capturedStart(0);
        int nameEnd = m.capturedStart(2) + m.capturedLength(2);
        QString moduleTypeName = m.captured(1);
        QString instanceName = m.captured(2);
        static const QSet<QString> skipTypes = {
            QLatin1String("module"), QLatin1String("endmodule"), QLatin1String("task"), QLatin1String("function"),
            QLatin1String("if"), QLatin1String("for"), QLatin1String("while"), QLatin1String("case"),
            QLatin1String("input"), QLatin1String("output"), QLatin1String("inout"), QLatin1String("ref"),
            QLatin1String("wire"), QLatin1String("reg"), QLatin1String("logic"), QLatin1String("var"),
            QLatin1String("parameter"), QLatin1String("localparam"), QLatin1String("const"),
            QLatin1String("typedef"), QLatin1String("enum"), QLatin1String("struct"), QLatin1String("interface")
        };
        if (skipTypes.contains(moduleTypeName)) {
            searchPos = nameEnd;
            continue;
        }
        if (isMatchInComment(typeStart, m.capturedLength(0))) { searchPos = nameEnd; continue; }
        int p = skipWhitespaceAndComments(text, nameEnd);
        if (p < n && text[p] == '#') {
            p++;
            p = skipWhitespaceAndComments(text, p);
            if (p < n && text[p] == '(') {
                int close = findMatchingParen(text, p);
                if (close < 0) { searchPos = nameEnd; continue; }
                p = skipWhitespaceAndComments(text, close + 1);
            }
        }
        if (p >= n || text[p] != '(') { searchPos = nameEnd; continue; }
        int openParenPos = p;
        SymbolInfo instSymbol;
        instSymbol.fileName = currentFileName;
        instSymbol.symbolName = instanceName;
        instSymbol.symbolType = sym_inst;
        instSymbol.dataType = moduleTypeName;
        instSymbol.position = m.capturedStart(2);
        instSymbol.length = instanceName.length();
        posToLineColumn(instSymbol.position, instSymbol.startLine, instSymbol.startColumn);
        instSymbol.endLine = instSymbol.startLine;
        instSymbol.endColumn = instSymbol.startColumn + instanceName.length();
        instSymbol.moduleScope = getCurrentModuleScope(currentFileName, instSymbol.startLine);
        addSymbol(instSymbol);
        int instanceId = symbolDatabase.last().symbolId;
        parseInstanceConnections(text, openParenPos, moduleTypeName, instanceId, lineStarts);
        searchPos = findMatchingParen(text, openParenPos);
        if (searchPos < 0) break;
        searchPos++;
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
    if (s_holdingWriteLock) {
        // 调用方已持写锁，不再加读锁，避免死锁
        if (fileNameIndex.contains(fileName)) {
            const QList<int>& indices = fileNameIndex[fileName];
            result.reserve(indices.size());
            for (int index : indices) {
                if (index < symbolDatabase.size()) {
                    result.append(symbolDatabase[index]);
                }
            }
        } else if (!fileName.isEmpty()) {
            QStringList keys = fileNameIndex.keys();
            qDebug("findSymbolsByFileName: fileName=%s not in index; keyCount=%d; sampleKeys=%s", qPrintable(fileName), keys.size(), qPrintable(keys.mid(0, 10).join(QLatin1String(" | "))));
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
    } else if (!fileName.isEmpty()) {
        QStringList keys = fileNameIndex.keys();
        qDebug("findSymbolsByFileName: fileName=%s not in index; keyCount=%d; sampleKeys=%s", qPrintable(fileName), keys.size(), qPrintable(keys.mid(0, 10).join(QLatin1String(" | "))));
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
    static const QRegularExpression moduleName("\\bmodule\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> matches = findMatchesOutsideComments(text, moduleName);

    for (const RegexMatch &match : qAsConst(matches)) {
        QRegularExpressionMatch m = moduleName.match(text, match.position);
        if (m.hasMatch()) {
            const QString moduleNameCaptured = m.captured(1);

            // Create symbol info and add to database
            SymbolInfo moduleSymbol;
            moduleSymbol.fileName = currentFileName;
            moduleSymbol.symbolName = moduleNameCaptured;
            moduleSymbol.symbolType = sym_module;
            moduleSymbol.position = match.position;
            moduleSymbol.length = match.length;

            int captureGroupPos = m.capturedStart(1);
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
    static const QRegularExpression multiLineStart("/\\*");
    static const QRegularExpression multiLineEnd("\\*/");

    int pos = 0;
    QRegularExpressionMatch startMatch = multiLineStart.match(text, pos);
    while (startMatch.hasMatch()) {
        pos = startMatch.capturedStart(0);
        QRegularExpressionMatch endMatch = multiLineEnd.match(text, pos + 2);
        int endPos = endMatch.hasMatch() ? endMatch.capturedStart(0) : -1;

        CommentRegion region;
        region.startPos = pos;
        region.endPos = (endPos >= 0) ? endPos + 2 : text.length();

        calculateLineColumn(text, region.startPos, region.startLine, region.startColumn);
        calculateLineColumn(text, region.endPos, region.endLine, region.endColumn);

        commentRegions.append(region);
        pos = region.endPos;
        startMatch = multiLineStart.match(text, pos);
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

QList<sym_list::RegexMatch> sym_list::findMatchesOutsideComments(const QString &text, const QRegularExpression &pattern)
{
    QList<RegexMatch> validMatches;
    validMatches.reserve(50); // Reasonable estimate

    QRegularExpressionMatchIterator it = pattern.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        const int matchStart = m.capturedStart(0);
        const int matchLength = m.capturedLength(0);

        if (!isMatchInComment(matchStart, matchLength)) {
            RegexMatch match;
            match.position = matchStart;
            match.length = matchLength;
            match.captured = m.captured(0);

            calculateLineColumn(text, matchStart, match.lineNumber, match.columnNumber);
            validMatches.append(match);
        }
    }

    return validMatches;
}

void sym_list::getVariableDeclarations(const QString &text)
{
    int symbolsFound = 0;

    static const QRegularExpression regPattern("\\breg\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> regMatches = findMatchesOutsideComments(text, regPattern);

    for (const RegexMatch &match : qAsConst(regMatches)) {
        QRegularExpressionMatch m = regPattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_reg;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    static const QRegularExpression wirePattern("\\bwire\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> wireMatches = findMatchesOutsideComments(text, wirePattern);

    for (const RegexMatch &match : qAsConst(wireMatches)) {
        QRegularExpressionMatch m = wirePattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_wire;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    // Extract logic declarations - 先找到所有struct的范围，排除struct内部的logic
    QList<StructRange> structRanges = findStructRanges(text);
    static const QRegularExpression logicPattern("\\blogic\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> logicMatches = findMatchesOutsideComments(text, logicPattern);

    for (const RegexMatch &match : qAsConst(logicMatches)) {
        QRegularExpressionMatch m = logicPattern.match(text, match.position);
        if (m.hasMatch()) {
            int logicPos = m.capturedStart(1);
            if (!isPositionInStructRange(logicPos, structRanges)) {
                SymbolInfo symbol;
                symbol.fileName = currentFileName;
                symbol.symbolName = m.captured(1);
                symbol.symbolType = sym_logic;
                symbol.position = match.position;
                symbol.length = match.length;
                calculateLineColumn(text, logicPos, symbol.startLine, symbol.startColumn);
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
    // 分析 module 实例化及 .pin -> 端口 REFERENCES
    analyzeModuleInstantiations(text);

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
}

void sym_list::analyzePackages(const QString &text)
{
    static const QRegularExpression packagePattern("\\bpackage\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> packageMatches = findMatchesOutsideComments(text, packagePattern);

    for (const RegexMatch &match : qAsConst(packageMatches)) {
        QRegularExpressionMatch m = packagePattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_package;
            symbol.position = match.position;
            symbol.length = match.length;

            int capPos = m.capturedStart(1);
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

    static const QRegularExpression taskPattern("\\btask\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> taskMatches = findMatchesOutsideComments(text, taskPattern);

    for (const RegexMatch &match : qAsConst(taskMatches)) {
        QRegularExpressionMatch m = taskPattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_task;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    static const QRegularExpression functionPattern("\\bfunction\\s+(?:\\w+\\s+)?([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> functionMatches = findMatchesOutsideComments(text, functionPattern);

    for (const RegexMatch &match : qAsConst(functionMatches)) {
        QRegularExpressionMatch m = functionPattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_function;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

}

void sym_list::setContentIncremental(const QString& fileName, const QString& content)
{
    QWriteLocker lock(&symbolDbLock);
    s_holdingWriteLock = true;

    currentFileName = fileName;
    qDebug("setContentIncremental: fileName=%s", qPrintable(fileName));

    if (!needsAnalysis(currentFileName, content)) {
        qDebug("setContentIncremental: SKIP needsAnalysis=false");
        s_holdingWriteLock = false;
        return;
    }

    FileState& state = fileStates[currentFileName];

    // 若该文件当前无符号（如 analyzeOpenTabs 刚 clearSymbolsForFile 后）必须做全量分析，否则增量只解析变更行无法恢复 module 等跨行结构
    bool hasNoSymbols = !fileNameIndex.contains(currentFileName) || fileNameIndex.value(currentFileName).isEmpty();
    bool isFirstTime = !fileStates.contains(currentFileName) || state.needsFullAnalysis || hasNoSymbols;
    qDebug("setContentIncremental: isFirstTime=%d, hasNoSymbols=%d", isFirstTime, hasNoSymbols);

    if (isFirstTime) {
        clearSymbolsForFile(currentFileName);
        buildCommentRegions(content);
        // 先缓存当前内容，供 extractSymbolsAndContainsOnePass 内 getCurrentModuleScope -> findEndModuleLine 使用，避免读到磁盘旧内容导致 moduleScope 为空、补全“l ”无 logic
        previousFileContents[currentFileName] = content;
        extractSymbolsAndContainsOnePass(content);
        {
            QList<SymbolInfo> fileSyms = findSymbolsByFileName(currentFileName);
            int nMod = 0;
            for (const auto& s : fileSyms)
                if (s.symbolType == sym_module) nMod++;
            qDebug("setContentIncremental: after onePass, file=%s, symbols=%d, modules=%d", qPrintable(currentFileName), fileSyms.size(), nMod);
        }
        getAdditionalSymbols(content);
        buildSymbolRelationships(currentFileName);
        state.needsFullAnalysis = false;
    } else {
        QList<int> changedLines = detectChangedLines(currentFileName, content);
        if (!changedLines.isEmpty()) {
            analyzeSpecificLines(currentFileName, content, changedLines);
            buildSymbolRelationships(currentFileName);
        }
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

// 规范化内容用于“是否影响符号”比较：去掉块注释、整行//注释、空白行，压缩空白后哈希
// 这样仅改注释、空格、空行时不会触发分析
QString sym_list::calculateSymbolRelevantHash(const QString& content)
{
    QString work = content;
    // 去掉 /* ... */ 块（简单实现：不处理字符串内的 /* */）
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
    static const QRegularExpression taskPattern("\\btask\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    int tasksBefore = symbolDatabase.size();
    analyzeTaskFunctionPattern(lineText, lineStartPos, lineNum, taskPattern, sym_task);
    int tasksAfter = symbolDatabase.size();
    int tasksFound = tasksAfter - tasksBefore;

    static const QRegularExpression functionPattern("\\bfunction\\s+(?:\\w+\\s+)?([a-zA-Z_][a-zA-Z0-9_]*)");
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
    static const QRegularExpression modulePattern("\\bmodule\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QRegularExpressionMatchIterator it = modulePattern.globalMatch(lineText);

    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        int pos = m.capturedStart(0);
        int absolutePos = lineStartPos + pos;

        if (!isMatchInComment(absolutePos, m.capturedLength(0))) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_module;
            symbol.startLine = lineNum;
            symbol.startColumn = pos;
            symbol.endLine = lineNum;
            symbol.endColumn = pos + symbol.symbolName.length();
            symbol.position = absolutePos;
            symbol.length = m.capturedLength(0);

            addSymbol(symbol);
        }
    }
}

// 判断当前行在类型关键字之前是否包含端口方向（input/output/inout/ref），用于排除端口声明避免重复
static bool isPortDeclarationInLine(const QString& lineText, int typeKeywordStartInLine)
{
    if (typeKeywordStartInLine <= 0) return false;
    QString prefix = lineText.left(typeKeywordStartInLine);
    QRegularExpression portDir("\\b(?:input|output|inout|ref)\\b");
    return portDir.match(prefix).hasMatch();
}

void sym_list::analyzeVariablesInLine(const QString& lineText, int lineStartPos, int lineNum, const QString& fullText)
{
    QString textToUse = fullText;
    if (textToUse.isEmpty()) {
        if (previousFileContents.contains(currentFileName)) {
            textToUse = previousFileContents[currentFileName];
        } else {
            QFile file(currentFileName);
            if (file.open(QIODevice::ReadOnly | QFile::Text)) {
                textToUse = file.readAll();
                file.close();
            }
        }
    }
    QList<StructRange> structRanges = textToUse.isEmpty() ? QList<StructRange>() : findStructRanges(textToUse);

    static const QRegularExpression regPattern("\\breg\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    static const QRegularExpression wirePattern("\\bwire\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    static const QRegularExpression logicPattern("\\blogic\\s+(?:\\[[^\\]]*\\]\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");

    auto addVariableIfNotPortOrStruct = [this, lineText, lineStartPos, lineNum, &structRanges](
        const QRegularExpression& pattern, sym_type_e symbolType)
    {
        QRegularExpressionMatchIterator it = pattern.globalMatch(lineText);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            int pos = m.capturedStart(0);
            int absolutePos = lineStartPos + pos;
            int namePosInLine = m.capturedStart(1);
            int absoluteNamePos = lineStartPos + namePosInLine;

            if (isMatchInComment(absolutePos, m.capturedLength(0)))
                continue;
            if (isPositionInStructRange(absoluteNamePos, structRanges))
                continue;
            if (isPortDeclarationInLine(lineText, pos))
                continue;

            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = symbolType;
            symbol.startLine = lineNum;
            symbol.startColumn = namePosInLine;
            symbol.endLine = lineNum;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            symbol.position = absolutePos;
            symbol.length = m.capturedLength(0);
            symbol.moduleScope = getCurrentModuleScope(symbol.fileName, symbol.startLine);
            addSymbol(symbol);
        }
    };

    addVariableIfNotPortOrStruct(regPattern, sym_reg);
    addVariableIfNotPortOrStruct(wirePattern, sym_wire);
    addVariableIfNotPortOrStruct(logicPattern, sym_logic);
}

// 判断是否为合法模块名：非空且符合 SV 标识符规范
bool sym_list::isValidModuleName(const QString& name) {
    if (name.isEmpty()) return false;
    static const QRegularExpression svIdentifier(QStringLiteral("^[a-zA-Z_][a-zA-Z0-9_]*$"));
    return svIdentifier.match(name).hasMatch();
}

// 新增：获取指定位置的模块作用域（公开接口，供跳转定义时优先同模块符号）
// 仅当存在配对 endmodule 且模块名合法时才认为在有效模块内
QString sym_list::getCurrentModuleScope(const QString& fileName, int lineNumber) {
    QList<SymbolInfo> modules = findSymbolsByType(sym_module);
    for (const SymbolInfo& moduleSymbol : modules) {
        if (moduleSymbol.fileName != fileName) continue;
        if (!isValidModuleName(moduleSymbol.symbolName)) continue;
        int moduleEndLine = findEndModuleLine(fileName, moduleSymbol);
        if (moduleEndLine < 0) continue; // 无配对 endmodule，不视为有效模块
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

void sym_list::analyzeVariablePattern(const QString& lineText, int lineStartPos, int lineNum,
                                     const QRegularExpression& pattern, sym_type_e symbolType)
{
    QRegularExpressionMatchIterator it = pattern.globalMatch(lineText);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        int pos = m.capturedStart(0);
        int absolutePos = lineStartPos + pos;

        if (!isMatchInComment(absolutePos, m.capturedLength(0))) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = symbolType;
            symbol.startLine = lineNum;
            symbol.startColumn = (m.lastCapturedIndex() >= 1) ? (m.capturedStart(1) - lineStartPos + lineStartPos) : pos;
            symbol.endLine = lineNum;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            symbol.position = absolutePos;
            symbol.length = m.capturedLength(0);

            symbol.moduleScope = getCurrentModuleScope(symbol.fileName, symbol.startLine);
            addSymbol(symbol);
        }
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

    return -1; // endmodule not found
}

void sym_list::analyzeTaskFunctionPattern(const QString& lineText, int lineStartPos, int lineNum,
                                         const QRegularExpression& pattern, sym_type_e symbolType)
{
    QRegularExpressionMatchIterator it = pattern.globalMatch(lineText);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        int pos = m.capturedStart(0);
        int absolutePos = lineStartPos + pos;

        if (!isMatchInComment(absolutePos, m.capturedLength(0))) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = symbolType;
            symbol.startLine = lineNum;

            int capturePos = (m.lastCapturedIndex() >= 1) ? m.capturedStart(1) : pos;
            symbol.startColumn = capturePos;
            symbol.endLine = lineNum;
            symbol.endColumn = capturePos + symbol.symbolName.length();
            symbol.position = lineStartPos + capturePos;
            symbol.length = symbol.symbolName.length();

            addSymbol(symbol);
        }
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

    static const QRegularExpression interfacePattern("\\binterface\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*[;(]");
    QList<RegexMatch> interfaceMatches = findMatchesOutsideComments(text, interfacePattern);

    for (const RegexMatch &match : qAsConst(interfaceMatches)) {
        QRegularExpressionMatch m = interfacePattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_interface;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    static const QRegularExpression modportPattern("\\bmodport\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(");
    QList<RegexMatch> modportMatches = findMatchesOutsideComments(text, modportPattern);

    for (const RegexMatch &match : qAsConst(modportMatches)) {
        QRegularExpressionMatch m = modportPattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_interface_modport;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
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

    static const QRegularExpression packedStructPattern("\\btypedef\\s+struct\\s+packed\\s*\\{([^}]+)\\}\\s*([a-zA-Z_][a-zA-Z0-9_]*)");
    QRegularExpressionMatchIterator packedIt = packedStructPattern.globalMatch(text);
    while (packedIt.hasNext()) {
        QRegularExpressionMatch m = packedIt.next();
        int packedPos = m.capturedStart(0);
        int typeNamePos = m.capturedStart(2);
        QString structTypeName = m.captured(2);
        // 用整段匹配位置判断，避免注释里的 typedef struct 被识别
        if (!isMatchInComment(packedPos, m.capturedLength(0))) {
            QString structMembers = m.captured(1);
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = structTypeName;
            symbol.symbolType = sym_packed_struct;
            symbol.position = packedPos;
            symbol.length = m.capturedLength(0);
            calculateLineColumn(text, typeNamePos, symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + structTypeName.length();
            symbol.moduleScope = getCurrentModuleScope(currentFileName, symbol.startLine);
            addSymbol(symbol);
            symbolsFound++;
            analyzeStructMembers(structMembers, structTypeName, packedPos, text);
        }
    }

    static const QRegularExpression unpackedStructPattern("\\btypedef\\s+struct\\s*\\{([^}]+)\\}\\s*([a-zA-Z_][a-zA-Z0-9_]*)");
    QRegularExpressionMatchIterator unpackedIt = unpackedStructPattern.globalMatch(text);
    while (unpackedIt.hasNext()) {
        QRegularExpressionMatch m = unpackedIt.next();
        int unpackedPos = m.capturedStart(0);
        int typeNamePos = m.capturedStart(2);
        QString structName = m.captured(2);
        // 用整段匹配位置判断，避免注释里的 typedef struct 被识别
        if (!isMatchInComment(unpackedPos, m.capturedLength(0))) {
            QString structMembers = m.captured(1);
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = structName;
            symbol.symbolType = sym_unpacked_struct;
            symbol.position = unpackedPos;
            symbol.length = m.capturedLength(0);
            calculateLineColumn(text, typeNamePos, symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + structName.length();
            symbol.moduleScope = getCurrentModuleScope(currentFileName, symbol.startLine);
            addSymbol(symbol);
            symbolsFound++;
            analyzeStructMembers(structMembers, structName, unpackedPos, text);
        }
    }

    static const QRegularExpression enumPattern("typedef\\s+enum\\s*(?:\\{[^}]*\\})?\\s*([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> enumMatches = findMatchesOutsideComments(text, enumPattern);

    for (const RegexMatch &match : qAsConst(enumMatches)) {
        QRegularExpressionMatch m = enumPattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_enum;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    static const QRegularExpression typedefPattern("\\btypedef\\s+(?:(?:struct|enum|union)\\s+)?[a-zA-Z_][a-zA-Z0-9_\\[\\]:]*\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*;");
    QList<RegexMatch> typedefMatches = findMatchesOutsideComments(text, typedefPattern);

    for (const RegexMatch &match : qAsConst(typedefMatches)) {
        QRegularExpressionMatch m = typedefPattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_typedef;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
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

    static const QRegularExpression definePattern("`define\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> defineMatches = findMatchesOutsideComments(text, definePattern);

    for (const RegexMatch &match : qAsConst(defineMatches)) {
        QRegularExpressionMatch m = definePattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_def_define;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    static const QRegularExpression ifdefPattern("`ifdef\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> ifdefMatches = findMatchesOutsideComments(text, ifdefPattern);

    for (const RegexMatch &match : qAsConst(ifdefMatches)) {
        QRegularExpressionMatch m = ifdefPattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_def_ifdef;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    static const QRegularExpression ifndefPattern("`ifndef\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> ifndefMatches = findMatchesOutsideComments(text, ifndefPattern);

    for (const RegexMatch &match : qAsConst(ifndefMatches)) {
        QRegularExpressionMatch m = ifndefPattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_def_ifndef;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
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

    static const QRegularExpression alwaysFFPattern("\\balways_ff\\s*@\\s*\\([^)]*\\)");
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

    static const QRegularExpression alwaysCombPattern("\\balways_comb\\b");
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

    static const QRegularExpression alwaysPattern("\\balways\\s*@\\s*\\([^)]*\\)");
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

    static const QRegularExpression assignPattern("\\bassign\\s+([a-zA-Z_][a-zA-Z0-9_\\[\\]]*)\\s*=");
    QList<RegexMatch> assignMatches = findMatchesOutsideComments(text, assignPattern);

    for (const RegexMatch &match : qAsConst(assignMatches)) {
        QRegularExpressionMatch m = assignPattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_assign;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
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

    static const QRegularExpression parameterPattern("\\bparameter\\s+(?:[a-zA-Z_][a-zA-Z0-9_]*\\s*=\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> parameterMatches = findMatchesOutsideComments(text, parameterPattern);

    for (const RegexMatch &match : qAsConst(parameterMatches)) {
        QRegularExpressionMatch m = parameterPattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_parameter;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
            symbol.endLine = symbol.startLine;
            symbol.endColumn = symbol.startColumn + symbol.symbolName.length();
            addSymbol(symbol);
            symbolsFound++;
        }
    }

    static const QRegularExpression localparamPattern("\\blocalparam\\s+(?:[a-zA-Z_][a-zA-Z0-9_]*\\s*=\\s*)?([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> localparamMatches = findMatchesOutsideComments(text, localparamPattern);

    for (const RegexMatch &match : qAsConst(localparamMatches)) {
        QRegularExpressionMatch m = localparamPattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_localparam;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
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

    static const QRegularExpression xilinxConstraintPattern("\\(\\*\\s*([A-Z_]+)\\s*=");
    QList<RegexMatch> constraintMatches = findMatchesOutsideComments(text, xilinxConstraintPattern);

    for (const RegexMatch &match : qAsConst(constraintMatches)) {
        QRegularExpressionMatch m = xilinxConstraintPattern.match(text, match.position);
        if (m.hasMatch()) {
            SymbolInfo symbol;
            symbol.fileName = currentFileName;
            symbol.symbolName = m.captured(1);
            symbol.symbolType = sym_xilinx_constraint;
            symbol.position = match.position;
            symbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(1), symbol.startLine, symbol.startColumn);
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

    static const QRegularExpression basicEnumPattern("\\benum\\s*\\{([^}]+)\\}\\s*([a-zA-Z_][a-zA-Z0-9_,\\s]*)");
    QList<RegexMatch> basicEnumMatches = findMatchesOutsideComments(text, basicEnumPattern);

    for (const RegexMatch &match : qAsConst(basicEnumMatches)) {
        QRegularExpressionMatch m = basicEnumPattern.match(text, match.position);
        if (m.hasMatch()) {
            QString enumValues = m.captured(1);
            QString variables = m.captured(2);

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

    static const QRegularExpression typedefEnumPattern("\\btypedef\\s+enum\\s*\\{([^}]+)\\}\\s*([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> typedefEnumMatches = findMatchesOutsideComments(text, typedefEnumPattern);

    for (const RegexMatch &match : qAsConst(typedefEnumMatches)) {
        QRegularExpressionMatch m = typedefEnumPattern.match(text, match.position);
        if (m.hasMatch()) {
            QString enumValues = m.captured(1);
            QString typeName = m.captured(2);

            // 添加枚举类型定义
            SymbolInfo enumTypeSymbol;
            enumTypeSymbol.fileName = currentFileName;
            enumTypeSymbol.symbolName = typeName;
            enumTypeSymbol.symbolType = sym_enum;
            enumTypeSymbol.position = match.position;
            enumTypeSymbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(2), enumTypeSymbol.startLine, enumTypeSymbol.startColumn);
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

    // 3. 枚举变量声明: enum_name_t variable_name; 需结合类型信息判断，后续完善

    // ===== 结构体分析 =====

    static const QRegularExpression packedStructPatternEnums("\\btypedef\\s+struct\\s+packed\\s*\\{([^}]+)\\}\\s*([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> packedStructMatches = findMatchesOutsideComments(text, packedStructPatternEnums);

    for (const RegexMatch &match : qAsConst(packedStructMatches)) {
        QRegularExpressionMatch m = packedStructPatternEnums.match(text, match.position);
        if (m.hasMatch()) {
            QString structMembers = m.captured(1);
            QString structName = m.captured(2);

            SymbolInfo structTypeSymbol;
            structTypeSymbol.fileName = currentFileName;
            structTypeSymbol.symbolName = structName;
            structTypeSymbol.symbolType = sym_packed_struct;
            structTypeSymbol.position = match.position;
            structTypeSymbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(2), structTypeSymbol.startLine, structTypeSymbol.startColumn);
            structTypeSymbol.endLine = structTypeSymbol.startLine;
            structTypeSymbol.endColumn = structTypeSymbol.startColumn + structName.length();
            addSymbol(structTypeSymbol);
            symbolsFound++;

            analyzeStructMembers(structMembers, structName, match.position, text);
        }
    }

    static const QRegularExpression unpackedStructPatternEnums("\\btypedef\\s+struct\\s*\\{([^}]+)\\}\\s*([a-zA-Z_][a-zA-Z0-9_]*)");
    QList<RegexMatch> unpackedStructMatches = findMatchesOutsideComments(text, unpackedStructPatternEnums);

    for (const RegexMatch &match : qAsConst(unpackedStructMatches)) {
        QRegularExpressionMatch m = unpackedStructPatternEnums.match(text, match.position);
        if (m.hasMatch()) {
            QString structMembers = m.captured(1);
            QString structName = m.captured(2);

            SymbolInfo structTypeSymbol;
            structTypeSymbol.fileName = currentFileName;
            structTypeSymbol.symbolName = structName;
            structTypeSymbol.symbolType = sym_unpacked_struct;
            structTypeSymbol.position = match.position;
            structTypeSymbol.length = match.length;
            calculateLineColumn(text, m.capturedStart(2), structTypeSymbol.startLine, structTypeSymbol.startColumn);
            structTypeSymbol.endLine = structTypeSymbol.startLine;
            structTypeSymbol.endColumn = structTypeSymbol.startColumn + structName.length();
            addSymbol(structTypeSymbol);
            symbolsFound++;

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

        static const QRegularExpression memberPattern("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s+([a-zA-Z_][a-zA-Z0-9_]*)(?:\\s*\\[[^\\]]*\\])?");
        QRegularExpressionMatch memberMatch = memberPattern.match(cleanLine);

        if (memberMatch.hasMatch()) {
            QString memberType = memberMatch.captured(1);
            QString memberName = memberMatch.captured(2);

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
    
    for (const QString &structType : structTypes) {
        // 支持 xxx_s name; / xxx_s name, / xxx_s name [4]; / xxx_s name [3:0];
        QString pattern = QString("\\b%1\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?:\\[[^\\]]*\\])?\\s*[;,]").arg(QRegularExpression::escape(structType));
        QRegularExpression structVarPattern(pattern);
        QList<RegexMatch> structVarMatches = findMatchesOutsideComments(text, structVarPattern);

        for (const RegexMatch &match : qAsConst(structVarMatches)) {
            QRegularExpressionMatch m = structVarPattern.match(text, match.position);
            if (m.hasMatch()) {
                QString varName = m.captured(1);

                QString uniqueKey = QString("%1:%2:%3").arg(currentFileName).arg(varName).arg(structType);
                if (addedStructVars.contains(uniqueKey)) {
                    continue;
                }
                addedStructVars.insert(uniqueKey);

                SymbolInfo varSymbol;
                varSymbol.fileName = currentFileName;
                varSymbol.symbolName = varName;
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
                varSymbol.moduleScope = structType;
                calculateLineColumn(text, m.capturedStart(1), varSymbol.startLine, varSymbol.startColumn);
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
    const int maxSteps = text.length() + 1;
    int steps = 0;

    int depth = 1;
    int pos = openBracePos + 1;

    while (pos < text.length() && depth > 0 && steps < maxSteps) {
        steps++;
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
                // 多行注释；限制步数以防未闭合 /* 导致扫描整文件
                pos += 2;
                const int commentMaxSteps = qMin(text.length() - pos, 500000);
                int commentSteps = 0;
                while (pos + 1 < text.length() && commentSteps < commentMaxSteps) {
                    commentSteps++;
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

// 辅助函数：找到匹配的圆括号 ')'
static int findMatchingParen(const QString &text, int openParenPos)
{
    if (openParenPos < 0 || openParenPos >= text.length() || text[openParenPos] != '(') {
        return -1;
    }
    const int maxSteps = text.length() + 1;
    int steps = 0;
    int depth = 1;
    int pos = openParenPos + 1;
    while (pos < text.length() && depth > 0 && steps < maxSteps) {
        steps++;
        QChar ch = text[pos];
        if (ch == '(') {
            depth++;
        } else if (ch == ')') {
            depth--;
            if (depth == 0)
                return pos;
        } else if (ch == '"') {
            pos++;
            while (pos < text.length() && text[pos] != '"') {
                if (text[pos] == '\\' && pos + 1 < text.length()) pos += 2;
                else pos++;
            }
        } else if (ch == '/' && pos + 1 < text.length()) {
            if (text[pos + 1] == '/') {
                while (pos < text.length() && text[pos] != '\n') pos++;
            } else if (text[pos + 1] == '*') {
                pos += 2;
                const int commentMaxSteps = qMin(text.length() - pos, 500000);
                int commentSteps = 0;
                while (pos + 1 < text.length() && commentSteps < commentMaxSteps) {
                    commentSteps++;
                    if (text[pos] == '*' && text[pos + 1] == '/') { pos += 2; break; }
                    pos++;
                }
            }
        }
        pos++;
    }
    return -1;
}

// 置为 1 时在“应用程序输出”中打印 struct 范围识别调试
#ifndef SV_SYMINFO_STRUCT_DEBUG
#define SV_SYMINFO_STRUCT_DEBUG 1
#endif

// 查找所有 struct/union 的范围（含 typedef struct、匿名 struct { }、union { }）；限制数量与迭代以防异常输入卡死
static const int kMaxStructRanges = 200;
static const int kMaxStructMatchIterations = 500;

QList<sym_list::StructRange> sym_list::findStructRanges(const QString &text)
{
    QList<StructRange> ranges;
    if (text.isEmpty()) return ranges;

    // 先基于当前文本构建注释区域，确保注释里的 struct/union 关键字不会误参与识别
    buildCommentRegions(text);

    // 规则：注释里的 struct/union 不参与分析（匹配起点在注释内则整段视为注释，不加入）。
    // 若该段因跨行匹配而包含“起点在代码区”的 struct（如下一行的 typedef struct{），则单独加入。
    QRegularExpression structUnionPattern("\\b(?:typedef\\s+)?(?:struct|union)\\b[^\\{]*\\{");
    structUnionPattern.optimize();
    QRegularExpressionMatchIterator it = structUnionPattern.globalMatch(text);
    int iterCount = 0;
    while (it.hasNext() && ranges.size() < kMaxStructRanges && iterCount < kMaxStructMatchIterations) {
        iterCount++;
        QRegularExpressionMatch m = it.next();
        int pos = m.capturedStart(0);
        int len = m.capturedLength(0);
        QString matched = text.mid(pos, len).left(50).replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (isMatchInComment(pos, len)) {
#if SV_SYMINFO_STRUCT_DEBUG
            qDebug() << "[findStructRanges] SKIP (start in comment) pos=" << pos << "len=" << len << "matched:" << matched;
#endif
            // 整段起点在注释内 → 不参与。段内用“只匹配关键字”的正则逐处找，避免贪婪匹配整段只得到一次匹配
            QString segment = text.mid(pos, len);
            static const QRegularExpression keywordOnlyPattern("\\b(?:typedef\\s+)?(?:struct|union)\\b");
            QRegularExpressionMatchIterator it2 = keywordOnlyPattern.globalMatch(segment);
            while (it2.hasNext() && ranges.size() < kMaxStructRanges) {
                QRegularExpressionMatch m2 = it2.next();
                int localStart = m2.capturedStart(0);
                int localLen = m2.capturedLength(0);
                int absStart = pos + localStart;
                if (isMatchInComment(absStart, localLen))
                    continue;
                int braceStart = text.indexOf(QLatin1Char('{'), absStart);
                if (braceStart < 0 || braceStart >= pos + len)
                    continue;
                int braceEnd = findMatchingBrace(text, braceStart);
                if (braceEnd != -1) {
                    StructRange range;
                    range.startPos = braceStart;
                    range.endPos = braceEnd;
                    ranges.append(range);
#if SV_SYMINFO_STRUCT_DEBUG
                    qDebug() << "[findStructRanges] ADD (from skipped span) range" << braceStart << "-" << braceEnd;
#endif
                }
            }
            continue;
        }
        // 匹配末尾即为 '{'
        int braceStart = pos + len - 1;
        if (text[braceStart] != '{')
            continue;
        int braceEnd = findMatchingBrace(text, braceStart);
        if (braceEnd != -1) {
            StructRange range;
            range.startPos = braceStart;
            range.endPos = braceEnd;
            ranges.append(range);
#if SV_SYMINFO_STRUCT_DEBUG
            qDebug() << "[findStructRanges] ADD range" << braceStart << "-" << braceEnd << "matched:" << matched;
#endif
        }
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
