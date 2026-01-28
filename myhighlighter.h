#ifndef MYHIGHLIGHTER_H
#define MYHIGHLIGHTER_H

#include <QObject>
#include <QSyntaxHighlighter>

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
        QRegExp pattern;
        QTextCharFormat format;
    };

    QVector<HighLightRule> highlightRules;
    void addNormalTextFormat();
    void addNumberFormat();
    void addStringFormat();
    void addCommentFormat();
    void addMultiLineCommentFormat(const QString &text);
    void addKeywordsFormat();
};

#endif // MYHIGHLIGHTER_H