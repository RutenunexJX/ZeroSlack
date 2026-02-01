#include "myhighlighter.h"
#include "sv_lexer.h"
#include "sv_token.h"
#include <QFile>
#include <QMutex>
#include <QTextStream>

static QStringList s_cachedKeywords;
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

MyHighlighter::MyHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    initFormats();
}

void MyHighlighter::initFormats()
{
    keywordFormat.setForeground(Qt::darkMagenta);
    keywordFormat.setFontWeight(QFont::Bold);

    commentFormat.setForeground(Qt::darkGreen);

    numberFormat.setForeground(QColor(250, 80, 50));

    stringFormat.setForeground(QColor(0, 180, 180));

    errorFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    errorFormat.setUnderlineColor(Qt::red);
}

void MyHighlighter::highlightBlock(const QString &text)
{
    SVLexer lexer(text);
    lexer.setState(previousBlockState());

    const QStringList keywords = loadKeywordsOnce();

    Token token;
    while ((token = lexer.nextToken()).type != TokenType::EOF_SYMBOL) {
        switch (token.type) {
        case TokenType::Keyword:
            setFormat(token.offset, token.length, keywordFormat);
            break;
        case TokenType::Comment:
            setFormat(token.offset, token.length, commentFormat);
            break;
        case TokenType::Identifier: {
            QString word = text.mid(token.offset, token.length);
            if (keywords.contains(word))
                setFormat(token.offset, token.length, keywordFormat);
            break;
        }
        case TokenType::Number:
            setFormat(token.offset, token.length, numberFormat);
            break;
        case TokenType::String:
            setFormat(token.offset, token.length, stringFormat);
            break;
        case TokenType::Error:
            break;
        case TokenType::Whitespace:
        default:
            break;
        }
    }

    setCurrentBlockState(lexer.getState());
}
