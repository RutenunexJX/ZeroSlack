#include "myhighlighter.h"
#include <QTextStream>
#include <QMutex>

// 阶段 D：keywords.txt 静态缓存，避免每个 MyHighlighter 实例都读文件
static QStringList s_cachedKeywords;
static QRegularExpression s_cachedKeywordRegex;
static QMutex s_keywordCacheMutex;
static bool s_keywordsLoaded = false;

static QStringList loadKeywordsOnce()
{
    QMutexLocker lock(&s_keywordCacheMutex);
    if (s_keywordsLoaded)
        return s_cachedKeywords;
    QFile file(":/config/config/keywords.txt");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        s_keywordsLoaded = true;
        return s_cachedKeywords;
    }
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (!line.isEmpty())
            s_cachedKeywords.append(line);
    }
    file.close();
    s_keywordsLoaded = true;
    return s_cachedKeywords;
}

// 阶段 D：多个关键字合并为一个 \b(kw1|kw2|...)\b 并启用优化
QRegularExpression MyHighlighter::getKeywordPattern()
{
    loadKeywordsOnce();
    QMutexLocker lock(&s_keywordCacheMutex);
    if (!s_cachedKeywordRegex.pattern().isEmpty())
        return s_cachedKeywordRegex;
    if (s_cachedKeywords.isEmpty()) {
        s_cachedKeywordRegex = QRegularExpression(QString()); // 空模式，不匹配
        return s_cachedKeywordRegex;
    }
    QString pattern = "\\b(";
    for (int i = 0; i < s_cachedKeywords.size(); ++i) {
        if (i > 0) pattern += "|";
        pattern += QRegularExpression::escape(s_cachedKeywords.at(i));
    }
    pattern += ")\\b";
    s_cachedKeywordRegex = QRegularExpression(pattern);
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    s_cachedKeywordRegex.optimize();
#endif
    return s_cachedKeywordRegex;
}

MyHighlighter::MyHighlighter(QTextDocument *parent): QSyntaxHighlighter(parent)
{
    // Keep exact original order and logic
    addNormalTextFormat();
    addNumberFormat();
    addStringFormat();
    addKeywordsFormat();      // 先处理关键字
    addCommentFormat();       // 后处理注释，覆盖关键字
}

void MyHighlighter::addNormalTextFormat()
{
    HighLightRule rule;
    rule.pattern = QRegularExpression("[a-z0-9A-Z]+");
    QTextCharFormat normalTextFormat;

    normalTextFormat.setFont(QFont(mFontFamily, mFontSize));
    normalTextFormat.setForeground(QColor(0,0,0));

    rule.format = normalTextFormat;
    highlightRules.append(rule);
}

void MyHighlighter::addNumberFormat()
{
    HighLightRule rule;
    rule.pattern = QRegularExpression("\\b\\d+|\\d+\\.\\d+\\b");
    QTextCharFormat numberFormat;

    numberFormat.setFont(QFont(mFontFamily, mFontSize));
    numberFormat.setForeground(QColor(250,80,50));

    rule.format = numberFormat;
    highlightRules.append(rule);
}

void MyHighlighter::addStringFormat()
{
    QTextCharFormat stringFormat;

    stringFormat.setFont(QFont(mFontFamily, mFontSize));
    stringFormat.setForeground(QColor(0,180,180));

    HighLightRule rule;
    rule.format = stringFormat;

    // ''
    rule.pattern = QRegularExpression("'[^']*'");
    highlightRules.append(rule);

    // ""
    rule.pattern = QRegularExpression("\"[^\"]*\"");
    highlightRules.append(rule);
}

void MyHighlighter::addCommentFormat()
{
    QTextCharFormat commentFormat;
    commentFormat.setFont(QFont(mFontFamily, mFontSize));
    commentFormat.setForeground(Qt::darkGreen);

    HighLightRule rule;
    rule.format = commentFormat;

    // 单行注释：// 开始到行末
    rule.pattern = QRegularExpression("//.*$");
    highlightRules.append(rule);
}

void MyHighlighter::addMultiLineCommentFormat(const QString &text)
{
    setCurrentBlockState(0);

    static const QRegularExpression commentStartRegExp("/\\*");
    static const QRegularExpression commentEndRegExp("\\*/");

    QTextCharFormat multiLineCommentFormat;
    multiLineCommentFormat.setFont(QFont(mFontFamily,mFontSize));
    multiLineCommentFormat.setForeground(Qt::darkGreen);

    int startIndex = 0;
    if (previousBlockState() != 1) {
        QRegularExpressionMatch m = commentStartRegExp.match(text);
        startIndex = m.hasMatch() ? m.capturedStart(0) : -1;
    }

    while (startIndex >= 0) {
        QRegularExpressionMatch endMatch = commentEndRegExp.match(text, startIndex);
        int endIndex = endMatch.hasMatch() ? endMatch.capturedStart(0) : -1;
        int commentLength = 0;
        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
            setFormat(startIndex, commentLength, multiLineCommentFormat);
        } else {
            commentLength = endIndex - startIndex + endMatch.capturedLength(0);
            setFormat(startIndex, commentLength, multiLineCommentFormat);
        }
        QRegularExpressionMatch nextStart = commentStartRegExp.match(text, startIndex + commentLength);
        startIndex = nextStart.hasMatch() ? nextStart.capturedStart(0) : -1;
    }
}

void MyHighlighter::addKeywordsFormat()
{
    HighLightRule rule;
    QTextCharFormat keywordsFormat;
    keywordsFormat.setFont(QFont(mFontFamily, mFontSize));
    keywordsFormat.setForeground(Qt::darkMagenta);
    rule.format = keywordsFormat;
    rule.pattern = getKeywordPattern(); // 阶段 D：单一大正则，静态缓存
    if (!rule.pattern.pattern().isEmpty())
        highlightRules.append(rule);
}

// Two-Pass Strategy: Pass1 = Keywords & Numbers; Pass2 = Strings & Comments (masking layer)
// Priority: left-to-right "earliest of \"", \"//\", \"/*\"" so that e.g. "//" in string is not comment.
static const QRegularExpression s_numberPattern(R"(\b\d+|\d+\.\d+\b)");

void MyHighlighter::highlightBlock(const QString &text)
{
    QTextCharFormat keywordFormat;
    keywordFormat.setFont(QFont(mFontFamily, mFontSize));
    keywordFormat.setForeground(Qt::darkMagenta);

    QTextCharFormat numberFormat;
    numberFormat.setFont(QFont(mFontFamily, mFontSize));
    numberFormat.setForeground(QColor(250, 80, 50));

    QTextCharFormat stringFormat;
    stringFormat.setFont(QFont(mFontFamily, mFontSize));
    stringFormat.setForeground(QColor(0, 180, 180));

    QTextCharFormat lineCommentFormat;
    lineCommentFormat.setFont(QFont(mFontFamily, mFontSize));
    lineCommentFormat.setForeground(Qt::darkGreen);

    QTextCharFormat blockCommentFormat;
    blockCommentFormat.setFont(QFont(mFontFamily, mFontSize));
    blockCommentFormat.setForeground(Qt::darkGreen);

    // ----- Pass 1: Base layer (Keywords & Numbers only) -----
    QRegularExpression keywordPattern = getKeywordPattern();
    if (!keywordPattern.pattern().isEmpty()) {
        QRegularExpressionMatchIterator it = keywordPattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            setFormat(m.capturedStart(0), m.capturedLength(0), keywordFormat);
        }
    }
    {
        QRegularExpressionMatchIterator it = s_numberPattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            setFormat(m.capturedStart(0), m.capturedLength(0), numberFormat);
        }
    }

    // ----- Pass 2: Masking layer (Strings & Comments, earliest-match) -----
    int pos = 0;
    const int len = text.length();

    // If we're inside a block comment from previous line, consume until */
    if (previousBlockState() == 1) {
        int endBlock = text.indexOf(QLatin1String("*/"), pos);
        if (endBlock >= 0) {
            setFormat(0, endBlock + 2, blockCommentFormat);
            pos = endBlock + 2;
            setCurrentBlockState(0);
        } else {
            setFormat(0, len, blockCommentFormat);
            setCurrentBlockState(1);
            return;
        }
    }

    while (pos < len) {
        int idxQuote = text.indexOf(QLatin1Char('"'), pos);
        int idxLineComment = text.indexOf(QLatin1String("//"), pos);
        int idxBlockComment = text.indexOf(QLatin1String("/*"), pos);

        if (idxQuote == -1) idxQuote = len + 1;
        if (idxLineComment == -1) idxLineComment = len + 1;
        if (idxBlockComment == -1) idxBlockComment = len + 1;

        int minIdx = qMin(idxQuote, qMin(idxLineComment, idxBlockComment));
        if (minIdx > len) break;

        if (minIdx == idxQuote) {
            // String: from " to next unescaped " or EOL
            int start = idxQuote;
            int end = start + 1;
            while (end < len) {
                if (text[end] == QLatin1Char('\\') && end + 1 < len) {
                    end += 2;
                    continue;
                }
                if (text[end] == QLatin1Char('"')) {
                    end++;
                    break;
                }
                end++;
            }
            setFormat(start, end - start, stringFormat);
            pos = end;
        } else if (minIdx == idxLineComment) {
            setFormat(idxLineComment, len - idxLineComment, lineCommentFormat);
            pos = len;
            break;
        } else {
            // Block comment: from /* to */ or EOL
            int endBlock = text.indexOf(QLatin1String("*/"), idxBlockComment + 2);
            if (endBlock >= 0) {
                setFormat(idxBlockComment, endBlock - idxBlockComment + 2, blockCommentFormat);
                pos = endBlock + 2;
                setCurrentBlockState(0);
            } else {
                setFormat(idxBlockComment, len - idxBlockComment, blockCommentFormat);
                setCurrentBlockState(1);
                pos = len;
                break;
            }
        }
    }
}
