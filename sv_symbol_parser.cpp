#include "sv_symbol_parser.h"
#include <QSet>
#include <QRegularExpression>
#include <QDebug>

static QSet<QString> s_keywords;

static bool lineHasDirectionBefore(const QString &content, const QVector<int> &lineStarts, int line, int col)
{
    if (line < 0 || line >= lineStarts.size()) return false;
    int lineStart = lineStarts[line];
    int lineEnd = (line + 1 < lineStarts.size()) ? lineStarts[line + 1] : content.length();
    int span = qMin(col + 64, lineEnd - lineStart);
    QString lineText = content.mid(lineStart, span);
    static const QRegularExpression portDir(QLatin1String("\\b(?:input|output|inout|ref)\\b"));
    return portDir.match(lineText).hasMatch();
}

static int findMatchingParen(const QString &text, int openParenPos)
{
    if (openParenPos < 0 || openParenPos >= text.length() || text[openParenPos] != QLatin1Char('('))
        return -1;
    int depth = 1;
    for (int i = openParenPos + 1; i < text.length(); ++i) {
        QChar c = text[i];
        if (c == QLatin1Char('(')) depth++;
        else if (c == QLatin1Char(')')) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

SVSymbolParser::SVSymbolParser(const QString &content, const QString &fileName)
    : m_content(content)
    , m_fileName(fileName)
{
    m_lineStarts.append(0);
    for (int p = 0; p < m_content.length(); ) {
        int idx = m_content.indexOf(QLatin1Char('\n'), p);
        if (idx < 0) break;
        p = idx + 1;
        m_lineStarts.append(p);
    }
}

QSet<QString> SVSymbolParser::structuralKeywords()
{
    if (!s_keywords.isEmpty()) return s_keywords;
    s_keywords << QLatin1String("module") << QLatin1String("endmodule")
               << QLatin1String("task") << QLatin1String("endtask")
               << QLatin1String("function") << QLatin1String("endfunction")
               << QLatin1String("input") << QLatin1String("output") << QLatin1String("inout") << QLatin1String("ref")
               << QLatin1String("reg") << QLatin1String("wire") << QLatin1String("logic")
               << QLatin1String("begin") << QLatin1String("end");
    return s_keywords;
}

void SVSymbolParser::tokenize()
{
    m_tokens.clear();
    QStringList lines = m_content.split(QLatin1Char('\n'));
    int lexerState = 0;
    for (int lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        const QString &line = lines[lineIdx];
        SVLexer lexer(line);
        lexer.setState(lexerState);
        while (true) {
            Token t = lexer.nextToken();
            lexerState = lexer.getState();
            if (t.type == TokenType::EOF_SYMBOL) break;
            if (t.type == TokenType::Whitespace || t.type == TokenType::Comment)
                continue;
            SVToken st;
            st.token = t;
            st.line = lineIdx;
            st.col = t.offset;
            m_tokens.append(st);
        }
    }
}

void SVSymbolParser::advance()
{
    if (m_pos < m_tokens.size())
        ++m_pos;
}

const SVToken *SVSymbolParser::peek() const
{
    if (m_pos >= m_tokens.size()) return nullptr;
    return &m_tokens[m_pos];
}

bool SVSymbolParser::isAtEnd() const
{
    return m_pos >= m_tokens.size();
}

static QString tokenTextAt(const QString &content, const QVector<int> &lineStarts, const SVToken &st)
{
    int pos = lineStarts.value(st.line, 0) + st.col;
    return content.mid(pos, st.token.length);
}

bool SVSymbolParser::match(TokenType type, const QString &text) const
{
    const SVToken *t = peek();
    if (!t) return false;
    if (t->token.type != type) return false;
    if (!text.isEmpty()) {
        QString got = tokenTextAt(m_content, m_lineStarts, *t);
        if (got != text) return false;
    }
    return true;
}

bool SVSymbolParser::checkKeyword(const QString &text) const
{
    return structuralKeywords().contains(text);
}

struct PortNameLoc { QString name; int line; int col; };

static void emitPortSymbols(QList<sym_list::SymbolInfo> *out, const QList<PortNameLoc> &names,
                            const QString &fileName, const QString &moduleName,
                            sym_list::sym_type_e portType, const QString &dataType,
                            const QVector<int> &lineStarts)
{
    for (const PortNameLoc &pn : names) {
        sym_list::SymbolInfo portSymbol;
        portSymbol.fileName = fileName;
        portSymbol.symbolName = pn.name;
        portSymbol.symbolType = portType;
        portSymbol.moduleScope = moduleName;
        portSymbol.scopeLevel = 1;
        portSymbol.dataType = dataType;
        portSymbol.startLine = pn.line;
        portSymbol.startColumn = pn.col;
        portSymbol.endLine = pn.line;
        portSymbol.endColumn = pn.col + pn.name.length();
        portSymbol.position = lineStarts.value(pn.line, 0) + pn.col;
        portSymbol.length = pn.name.length();
        out->append(portSymbol);
    }
}

void SVSymbolParser::parsePortList(const QString &moduleName)
{
    const SVToken *t = peek();
    if (!t || t->token.type != TokenType::Error) return;
    QString s = tokenTextAt(m_content, m_lineStarts, *t);
    if (s != QLatin1String("(")) return;
    advance();
    int depth = 1;
    sym_list::sym_type_e lastPortType = sym_list::sym_port_input;
    QString lastDataType;
    QList<PortNameLoc> namesInSegment;
    sym_list::sym_type_e segmentPortType = sym_list::sym_port_input;
    QString segmentDataType;

    auto flushSegment = [&]() {
        emitPortSymbols(&m_symbols, namesInSegment, m_fileName, moduleName, segmentPortType, segmentDataType, m_lineStarts);
        namesInSegment.clear();
        segmentPortType = lastPortType;
        segmentDataType = lastDataType;
    };

    while (!isAtEnd()) {
        t = peek();
        if (!t) break;
        QString tok = tokenTextAt(m_content, m_lineStarts, *t);
        if (t->token.type == TokenType::Error && tok.length() == 1) {
            QChar c = tok[0];
            if (c == QLatin1Char('(')) { depth++; advance(); continue; }
            if (c == QLatin1Char(')')) {
                depth--;
                if (depth == 0) {
                    flushSegment();
                    advance();
                    return;
                }
                advance();
                continue;
            }
            if (c == QLatin1Char(',')) {
                if (depth == 1)
                    flushSegment();
                advance();
                continue;
            }
            if (c == QLatin1Char('[')) {
                if (!segmentDataType.isEmpty()) segmentDataType += QLatin1Char(' ');
                segmentDataType += tok;
                advance();
                continue;
            }
            if (c == QLatin1Char(']')) {
                if (!segmentDataType.isEmpty()) segmentDataType += QLatin1Char(' ');
                segmentDataType += tok;
                advance();
                continue;
            }
        }
        if (t->token.type == TokenType::Identifier) {
            if (tok == QLatin1String("input"))  { lastPortType = sym_list::sym_port_input;  segmentPortType = lastPortType; advance(); continue; }
            if (tok == QLatin1String("output")) { lastPortType = sym_list::sym_port_output; segmentPortType = lastPortType; advance(); continue; }
            if (tok == QLatin1String("inout"))  { lastPortType = sym_list::sym_port_inout;  segmentPortType = lastPortType; advance(); continue; }
            if (tok == QLatin1String("ref"))   { lastPortType = sym_list::sym_port_ref;    segmentPortType = lastPortType; advance(); continue; }
            if (tok == QLatin1String("logic") || tok == QLatin1String("wire") || tok == QLatin1String("reg")) {
                if (!segmentDataType.isEmpty()) segmentDataType += QLatin1Char(' ');
                segmentDataType += tok;
                advance();
                continue;
            }
            if (depth == 1 && !checkKeyword(tok)) {
                PortNameLoc pn;
                pn.name = tok;
                pn.line = t->line;
                pn.col = t->col;
                namesInSegment.append(pn);
                advance();
                continue;
            }
        }
        if (t->token.type == TokenType::Number) {
            if (!segmentDataType.isEmpty()) segmentDataType += QLatin1Char(' ');
            segmentDataType += tok;
            advance();
            continue;
        }
        advance();
    }
}

void SVSymbolParser::parseModule()
{
    if (!match(TokenType::Identifier, QLatin1String("module"))) return;
    const SVToken *modTok = peek();
    advance();
    if (isAtEnd()) return;
    const SVToken *nameTok = peek();
    if (!nameTok || nameTok->token.type != TokenType::Identifier) return;
    QString moduleName = tokenTextAt(m_content, m_lineStarts, *nameTok);
    if (checkKeyword(moduleName)) return;
    advance();

    sym_list::SymbolInfo moduleSymbol;
    moduleSymbol.fileName = m_fileName;
    moduleSymbol.symbolName = moduleName;
    moduleSymbol.symbolType = sym_list::sym_module;
    moduleSymbol.startLine = nameTok->line;
    moduleSymbol.startColumn = nameTok->col;
    moduleSymbol.endLine = nameTok->line;
    moduleSymbol.endColumn = nameTok->col + moduleName.length();
    moduleSymbol.position = m_lineStarts.value(nameTok->line, 0) + nameTok->col;
    moduleSymbol.length = moduleName.length();
    m_scopeStack.append(moduleName);
    int moduleSymbolIndex = m_symbols.size();
    m_symbols.append(moduleSymbol);

    while (!isAtEnd()) {
        const SVToken *t = peek();
        if (!t) break;
        QString tok = tokenTextAt(m_content, m_lineStarts, *t);
        if (t->token.type == TokenType::Error && tok.length() == 1 && tok[0] == QLatin1Char('#')) {
            advance();
            if (isAtEnd()) break;
            t = peek();
            if (t->token.type == TokenType::Error && tokenTextAt(m_content, m_lineStarts, *t) == QLatin1String("(")) {
                int pos = m_lineStarts.value(t->line, 0) + t->col;
                int close = findMatchingParen(m_content, pos);
                if (close >= 0) {
                    while (!isAtEnd()) {
                        const SVToken *cur = peek();
                        if (!cur) break;
                        int curPos = m_lineStarts.value(cur->line, 0) + cur->col;
                        if (curPos > close) break;
                        advance();
                    }
                }
            }
            continue;
        }
        if (t->token.type == TokenType::Error && tok.length() == 1 && tok[0] == QLatin1Char('(')) {
            parsePortList(moduleName);
            continue;
        }
        if (t->token.type == TokenType::Identifier && tok == QLatin1String("endmodule")) {
            if (moduleSymbolIndex >= 0 && moduleSymbolIndex < m_symbols.size())
                m_symbols[moduleSymbolIndex].endLine = t->line;
            if (!m_scopeStack.isEmpty()) m_scopeStack.removeLast();
            advance();
            return;
        }
        if (t->token.type == TokenType::Identifier && tok == QLatin1String("task")) {
            advance();
            if (isAtEnd()) { advance(); continue; }
            const SVToken *taskNameTok = peek();
            if (taskNameTok->token.type != TokenType::Identifier || checkKeyword(tokenTextAt(m_content, m_lineStarts, *taskNameTok))) {
                advance();
                continue;
            }
            QString taskName = tokenTextAt(m_content, m_lineStarts, *taskNameTok);
            sym_list::SymbolInfo taskSym;
            taskSym.fileName = m_fileName;
            taskSym.symbolName = taskName;
            taskSym.symbolType = sym_list::sym_task;
            taskSym.startLine = taskNameTok->line;
            taskSym.startColumn = taskNameTok->col;
            taskSym.endLine = taskNameTok->line;
            taskSym.endColumn = taskNameTok->col + taskName.length();
            taskSym.position = m_lineStarts.value(taskNameTok->line, 0) + taskNameTok->col;
            taskSym.length = taskName.length();
            if (!m_scopeStack.isEmpty()) taskSym.moduleScope = m_scopeStack.last();
            taskSym.scopeLevel = 1;
            advance();
            int depth = 0;
            while (!isAtEnd()) {
                const SVToken *cur = peek();
                if (!cur) break;
                QString curText = tokenTextAt(m_content, m_lineStarts, *cur);
                if (cur->token.type == TokenType::Identifier && curText == QLatin1String("endtask")) {
                    taskSym.endLine = cur->line;
                    m_symbols.append(taskSym);
                    advance();
                    break;
                }
                advance();
            }
            continue;
        }
        if (t->token.type == TokenType::Identifier && tok == QLatin1String("function")) {
            advance();
            while (!isAtEnd()) {
                const SVToken *cur = peek();
                if (!cur) break;
                QString curText = tokenTextAt(m_content, m_lineStarts, *cur);
                if (cur->token.type == TokenType::Identifier && !checkKeyword(curText)) {
                    QString funcName = curText;
                    sym_list::SymbolInfo funcSym;
                    funcSym.fileName = m_fileName;
                    funcSym.symbolName = funcName;
                    funcSym.symbolType = sym_list::sym_function;
                    funcSym.startLine = cur->line;
                    funcSym.startColumn = cur->col;
                    funcSym.endLine = cur->line;
                    funcSym.endColumn = cur->col + funcName.length();
                    funcSym.position = m_lineStarts.value(cur->line, 0) + cur->col;
                    funcSym.length = funcName.length();
                    if (!m_scopeStack.isEmpty()) funcSym.moduleScope = m_scopeStack.last();
                    funcSym.scopeLevel = 1;
                    advance();
                    while (!isAtEnd()) {
                        const SVToken *c2 = peek();
                        if (!c2) break;
                        QString c2Text = tokenTextAt(m_content, m_lineStarts, *c2);
                        if (c2->token.type == TokenType::Identifier && c2Text == QLatin1String("endfunction")) {
                            funcSym.endLine = c2->line;
                            m_symbols.append(funcSym);
                            advance();
                            break;
                        }
                        advance();
                    }
                    break;
                }
                advance();
            }
            continue;
        }
        if (t->token.type == TokenType::Identifier && (tok == QLatin1String("reg") || tok == QLatin1String("wire") || tok == QLatin1String("logic"))) {
            if (lineHasDirectionBefore(m_content, m_lineStarts, t->line, t->col)) {
                advance();
                continue;
            }
            sym_list::sym_type_e varType = (tok == QLatin1String("reg")) ? sym_list::sym_reg : (tok == QLatin1String("wire")) ? sym_list::sym_wire : sym_list::sym_logic;
            advance();
            while (!isAtEnd()) {
                const SVToken *cur = peek();
                if (!cur) break;
                QString curText = tokenTextAt(m_content, m_lineStarts, *cur);
                if (cur->token.type == TokenType::Error && (curText == QLatin1String("[") || curText == QLatin1String("]"))) {
                    advance();
                    continue;
                }
                if (cur->token.type == TokenType::Identifier && !checkKeyword(curText)) {
                    QString varName = curText;
                    sym_list::SymbolInfo varSym;
                    varSym.fileName = m_fileName;
                    varSym.symbolName = varName;
                    varSym.symbolType = varType;
                    varSym.startLine = cur->line;
                    varSym.startColumn = cur->col;
                    varSym.endLine = cur->line;
                    varSym.endColumn = cur->col + varName.length();
                    varSym.position = m_lineStarts.value(cur->line, 0) + cur->col;
                    varSym.length = varName.length();
                    if (!m_scopeStack.isEmpty()) varSym.moduleScope = m_scopeStack.last();
                    varSym.scopeLevel = 1;
                    m_symbols.append(varSym);
                    advance();
                    break;
                }
                advance();
            }
            continue;
        }
        advance();
    }
    if (!m_scopeStack.isEmpty() && m_scopeStack.last() == moduleName)
        m_scopeStack.removeLast();
}

QList<sym_list::SymbolInfo> SVSymbolParser::parse()
{
    m_symbols.clear();
    m_scopeStack.clear();
    m_pos = 0;
    tokenize();
    qDebug("SVSymbolParser::parse: file=%s, m_tokens.size()=%d, content.contains(module)=%d", qPrintable(m_fileName), m_tokens.size(), m_content.contains(QLatin1String("module")) ? 1 : 0);
    while (!isAtEnd()) {
        const SVToken *t = peek();
        if (!t) break;
        if (t->token.type == TokenType::Identifier) {
            QString tok = tokenTextAt(m_content, m_lineStarts, *t);
            if (tok == QLatin1String("module")) {
                parseModule();
                continue;
            }
        }
        advance();
    }
    {
        int nMod = 0, nLogic = 0;
        for (const auto& s : m_symbols) {
            if (s.symbolType == sym_list::sym_module) nMod++;
            if (s.symbolType == sym_list::sym_logic) nLogic++;
        }
        qDebug("SVSymbolParser::parse: file=%s, total=%d, modules=%d, logic=%d", qPrintable(m_fileName), m_symbols.size(), nMod, nLogic);
    }
    return m_symbols;
}
