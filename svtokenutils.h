#ifndef SVTOKENUTILS_H
#define SVTOKENUTILS_H

#include <QChar>
#include <QString>

namespace SvTokenUtils {

inline bool isAsciiAlpha(QChar ch)
{
    const ushort value = ch.unicode();
    return (value >= 'A' && value <= 'Z')
        || (value >= 'a' && value <= 'z');
}

inline bool isAsciiDigit(QChar ch)
{
    const ushort value = ch.unicode();
    return value >= '0' && value <= '9';
}

inline bool isIdentifierStart(QChar ch)
{
    return ch == QLatin1Char('_') || isAsciiAlpha(ch);
}

inline bool isIdentifierContinue(QChar ch, bool allowDollar = false)
{
    return isIdentifierStart(ch)
        || isAsciiDigit(ch)
        || (allowDollar && ch == QLatin1Char('$'));
}

inline bool isIdentifier(const QString& text, bool allowDollar = false)
{
    if (text.isEmpty() || !isIdentifierStart(text.front()))
        return false;
    for (int i = 1; i < text.size(); ++i) {
        if (!isIdentifierContinue(text.at(i), allowDollar))
            return false;
    }
    return true;
}

inline bool hasIdentifierBoundaryAt(
    const QString& text,
    int index,
    bool allowDollar = false)
{
    return index < 0
        || index >= text.size()
        || !isIdentifierContinue(text.at(index), allowDollar);
}

inline bool isWordAt(
    const QString& text,
    const QString& word,
    int position,
    bool allowDollar = false)
{
    if (position < 0 || word.isEmpty())
        return false;
    if (position + word.size() > text.size())
        return false;
    if (text.mid(position, word.size()) != word)
        return false;
    return hasIdentifierBoundaryAt(text, position - 1, allowDollar)
        && hasIdentifierBoundaryAt(text, position + word.size(), allowDollar);
}

inline int indexOfWord(
    const QString& text,
    const QString& word,
    int from = 0,
    bool allowDollar = false)
{
    int position = from < 0 ? 0 : from;
    while ((position = text.indexOf(word, position)) >= 0) {
        if (isWordAt(text, word, position, allowDollar))
            return position;
        ++position;
    }
    return -1;
}

inline bool containsWord(
    const QString& text,
    const QString& word,
    bool allowDollar = false)
{
    return indexOfWord(text, word, 0, allowDollar) >= 0;
}

inline QString collapseWhitespaceRuns(const QString& text)
{
    QString result;
    result.reserve(text.size());
    bool inWhitespace = false;
    for (const QChar ch : text) {
        if (ch.isSpace()) {
            if (!inWhitespace)
                result.append(QLatin1Char(' '));
            inWhitespace = true;
            continue;
        }
        result.append(ch);
        inWhitespace = false;
    }
    return result.trimmed();
}

}

#endif // SVTOKENUTILS_H
