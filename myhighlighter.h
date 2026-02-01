#ifndef MYHIGHLIGHTER_H
#define MYHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class QTextDocument;

class MyHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit MyHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    // Formats
    QTextCharFormat keywordFormat;
    QTextCharFormat commentFormat;
    QTextCharFormat numberFormat;
    QTextCharFormat stringFormat;
    QTextCharFormat errorFormat; // Optional for debugging

    // Helper to initialize formats
    void initFormats();
};

#endif // MYHIGHLIGHTER_H
