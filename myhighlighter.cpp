#include "myhighlighter.h"
#include <QTextStream>

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
    QFile file(":/config/config/keywords.txt");
    QTextStream keywordsStream(&file);

    HighLightRule rule;
    QTextCharFormat keywordsFormat;
    keywordsFormat.setFont(QFont(mFontFamily,mFontSize));
    keywordsFormat.setForeground(Qt::darkMagenta);
    rule.format = keywordsFormat;

    if(file.open(QIODevice::ReadOnly|QIODevice::Text)){
        keywordsStream.seek(0);
        QString line;
        while(!keywordsStream.atEnd()){
            line = keywordsStream.readLine();
            if(line != ""){
                rule.pattern = QRegularExpression("\\b" + QRegularExpression::escape(line) + "\\b");
                highlightRules.append(rule);
            }
        }
        file.close();
    }
}

// Every line is a block
void MyHighlighter::highlightBlock(const QString &text)
{
    for (const HighLightRule &rule : highlightRules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            int index = m.capturedStart(0);
            int length = m.capturedLength(0);
            setFormat(index, length, rule.format);
        }
    }

    addMultiLineCommentFormat(text);
}
