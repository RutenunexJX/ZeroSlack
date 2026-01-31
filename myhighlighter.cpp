#include "myhighlighter.h"
#include <QTextStream>
#include <QMutex>
#include <QDebug>

// 置为 1 时在“应用程序输出”中打印高亮调试（仅对含 struct 的行）
#ifndef SV_HIGHLIGHTER_DEBUG
#define SV_HIGHLIGHTER_DEBUG 1
#endif

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

// Two-Pass Strategy: Pass1 = Keywords & Numbers (only outside comments/strings); Pass2 = Strings & Comments
// 注释/字符串内的关键字不参与高亮，避免 "// fifo struct" 里的 struct 被标成关键字色
static const QRegularExpression s_numberPattern(R"(\b\d+|\d+\.\d+\b)");

// 收集本行中“被注释或字符串覆盖”的区间 [start, end)，用于 Pass1 中跳过关键字/数字高亮
struct MaskRange { int start; int end; };
static QVector<MaskRange> collectMaskRanges(const QString &text, int blockState, int *outNewState)
{
    QVector<MaskRange> ranges;
    int pos = 0;
    const int len = text.length();

    if (blockState == 1) {
        int endBlock = text.indexOf(QLatin1String("*/"), pos);
        if (endBlock >= 0) {
            ranges.append({0, endBlock + 2});
            pos = endBlock + 2;
            *outNewState = 0;
        } else {
            ranges.append({0, len});
            *outNewState = 1;
            return ranges;
        }
    } else {
        *outNewState = 0;
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
            int start = idxQuote;
            int end = start + 1;
            while (end < len) {
                if (text[end] == QLatin1Char('\\') && end + 1 < len) { end += 2; continue; }
                if (text[end] == QLatin1Char('"')) { end++; break; }
                end++;
            }
            ranges.append({start, end});
            pos = end;
        } else if (minIdx == idxLineComment) {
            ranges.append({idxLineComment, len});
            pos = len;
            break;
        } else {
            int endBlock = text.indexOf(QLatin1String("*/"), idxBlockComment + 2);
            if (endBlock >= 0) {
                ranges.append({idxBlockComment, endBlock + 2});
                pos = endBlock + 2;
            } else {
                ranges.append({idxBlockComment, len});
                *outNewState = 1;
                pos = len;
                break;
            }
        }
    }
    return ranges;
}

static bool isInMaskRanges(int matchStart, int matchEnd, const QVector<MaskRange> &ranges)
{
    for (const MaskRange &r : ranges) {
        if (matchStart < r.end && matchEnd > r.start)
            return true;
    }
    return false;
}

void MyHighlighter::highlightBlock(const QString &text)
{
    const int len = text.length();
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

    int nextBlockState = 0;
    QVector<MaskRange> maskRanges = collectMaskRanges(text, previousBlockState(), &nextBlockState);
    setCurrentBlockState(nextBlockState);

#if SV_HIGHLIGHTER_DEBUG
    if (text.contains(QLatin1String("struct"))) {
        int blockNum = currentBlock().blockNumber();
        qDebug() << "[Highlighter] block" << blockNum << "text:" << text.left(80).replace(QLatin1Char('\t'), QLatin1Char(' '));
        qDebug() << "  maskRanges:" << maskRanges.size();
        for (int i = 0; i < maskRanges.size(); ++i)
            qDebug() << "    [" << i << "]" << maskRanges[i].start << "-" << maskRanges[i].end
                     << "content:" << text.mid(maskRanges[i].start, maskRanges[i].end - maskRanges[i].start).left(40);
    }
#endif

    // ----- Pass 1: 先画注释与字符串（保证注释里不会出现关键字色） -----
    int pos = 0;
    if (previousBlockState() == 1) {
        int endBlock = text.indexOf(QLatin1String("*/"), pos);
        if (endBlock >= 0) {
            setFormat(0, endBlock + 2, blockCommentFormat);
            pos = endBlock + 2;
        } else {
            setFormat(0, len, blockCommentFormat);
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
            int start = idxQuote;
            int end = start + 1;
            while (end < len) {
                if (text[end] == QLatin1Char('\\') && end + 1 < len) { end += 2; continue; }
                if (text[end] == QLatin1Char('"')) { end++; break; }
                end++;
            }
            setFormat(start, end - start, stringFormat);
            pos = end;
        } else if (minIdx == idxLineComment) {
            setFormat(idxLineComment, len - idxLineComment, lineCommentFormat);
            pos = len;
            break;
        } else {
            int endBlock = text.indexOf(QLatin1String("*/"), idxBlockComment + 2);
            if (endBlock >= 0) {
                setFormat(idxBlockComment, endBlock - idxBlockComment + 2, blockCommentFormat);
                pos = endBlock + 2;
            } else {
                setFormat(idxBlockComment, len - idxBlockComment, blockCommentFormat);
                pos = len;
                break;
            }
        }
    }

    // ----- Pass 2: 关键字与数字，仅在不被注释/字符串覆盖的区间内高亮 -----
    QRegularExpression keywordPattern = getKeywordPattern();
    if (!keywordPattern.pattern().isEmpty()) {
        QRegularExpressionMatchIterator it = keywordPattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            int start = m.capturedStart(0);
            int end = start + m.capturedLength(0);
            QString kw = m.captured(0);
            bool inMask = isInMaskRanges(start, end, maskRanges);
#if SV_HIGHLIGHTER_DEBUG
            if (text.contains(QLatin1String("struct")))
                qDebug() << "  keyword" << kw << "at" << start << "-" << end << "inMask=" << inMask << (inMask ? "SKIP" : "APPLY");
#endif
            if (!inMask)
                setFormat(start, m.capturedLength(0), keywordFormat);
        }
    }
    {
        QRegularExpressionMatchIterator it = s_numberPattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            int start = m.capturedStart(0);
            int end = start + m.capturedLength(0);
            if (!isInMaskRanges(start, end, maskRanges))
                setFormat(start, m.capturedLength(0), numberFormat);
        }
    }
}
