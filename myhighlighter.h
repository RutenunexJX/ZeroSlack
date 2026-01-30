#ifndef MYHIGHLIGHTER_H
#define MYHIGHLIGHTER_H

#include <QObject>
#include <QSyntaxHighlighter>
#include <QRegularExpression>

class MyHighlighter: public QSyntaxHighlighter
{
public:
    explicit MyHighlighter(QTextDocument *parent = nullptr);
protected:
    void highlightBlock(const QString &text);

private:
    QString mFontFamily = "Consolas";
    int mFontSize = 14;

    struct HighLightRule{
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<HighLightRule> highlightRules;
    void addNormalTextFormat();
    /** 返回合并后的关键字正则（静态缓存，keywords.txt 只读一次） */
    static QRegularExpression getKeywordPattern();
    void addNumberFormat();
    void addStringFormat();
    void addCommentFormat();
    void addMultiLineCommentFormat(const QString &text);
    void addKeywordsFormat();
};

#endif // MYHIGHLIGHTER_H