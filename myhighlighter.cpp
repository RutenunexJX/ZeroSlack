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
    rule.pattern = QRegExp("[a-z0-9A-Z]+");
    QTextCharFormat normalTextFormat;

    normalTextFormat.setFont(QFont(mFontFamily, mFontSize));
    normalTextFormat.setForeground(QColor(0,0,0));

    rule.format = normalTextFormat;
    highlightRules.append(rule);
}

void MyHighlighter::addNumberFormat()
{
    HighLightRule rule;
    rule.pattern = QRegExp("\\b\\d+|\\d+\\.\\d+\\b");
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
    rule.pattern = QRegExp("'[^']*'");
    highlightRules.append(rule);

    // ""
    rule.pattern = QRegExp("\"[^\"]*\"");
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
    rule.pattern = QRegExp("\\/\\/.*$");
    highlightRules.append(rule);
}

void MyHighlighter::addMultiLineCommentFormat(const QString &text)
{
    setCurrentBlockState(0);

    // /*
    QRegExp commentStartRegExp("\\/\\*");

    // */
    QRegExp commentEndRegExp("\\*\\/");

    // hl
    QTextCharFormat multiLineCommentFormat;
    multiLineCommentFormat.setFont(QFont(mFontFamily,mFontSize));
    multiLineCommentFormat.setForeground(Qt::darkGreen);

    int startIndex = 0;
    if(previousBlockState() != 1)
        startIndex = commentStartRegExp.indexIn(text);

    while(startIndex>=0){
        int endIndex = commentEndRegExp.indexIn(text,startIndex);
        int commentLength = 0;
        if(endIndex == -1){
            setCurrentBlockState(1);
            commentLength = text.length()-startIndex;

            setFormat(startIndex,
                      commentLength,
                      multiLineCommentFormat);
        }
        else{
            commentLength = endIndex - startIndex + commentEndRegExp.matchedLength();

            setFormat(startIndex,
                      commentLength,
                      multiLineCommentFormat);
        }
        startIndex = commentStartRegExp.indexIn(text,commentLength+startIndex);
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
                rule.pattern = QRegExp("\\b"+line+"\\b");
                highlightRules.append(rule);
            }
        }
        file.close();
    }
}

// Every line is a block
void MyHighlighter::highlightBlock(const QString &text)
{
    foreach(const HighLightRule &rule, highlightRules){
        QRegExp regExp(rule.pattern);
        int index = regExp.indexIn(text);
        while(index>=0){
            int length = regExp.matchedLength();
            setFormat(index,length,rule.format);
            index = regExp.indexIn(text,index+length);
        }
    }

    addMultiLineCommentFormat(text);
}
