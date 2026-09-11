#include "editorlexicalboundary.h"

#include <QStringList>
#include <QtGlobal>

namespace EditorLexicalBoundary {
namespace {

bool isHorizontalSpace(QChar character)
{
    return character == QLatin1Char(' ')
        || character == QLatin1Char('\t');
}

bool isIdentifierCharacter(QChar character)
{
    return character.isLetterOrNumber()
        || character == QLatin1Char('_')
        || character == QLatin1Char('$');
}

bool isIdentifierStart(QChar character)
{
    return character.isLetter()
        || character == QLatin1Char('_')
        || character == QLatin1Char('$');
}

bool isBasedDigit(QChar character, QChar base)
{
    const QChar lower = character.toLower();
    if (character == QLatin1Char('_')
        || lower == QLatin1Char('x')
        || lower == QLatin1Char('z')
        || lower == QLatin1Char('?')) {
        return true;
    }
    if (base == QLatin1Char('b'))
        return character == QLatin1Char('0')
            || character == QLatin1Char('1');
    if (base == QLatin1Char('o'))
        return character >= QLatin1Char('0')
            && character <= QLatin1Char('7');
    if (base == QLatin1Char('d'))
        return character.isDigit();
    if (base == QLatin1Char('h'))
        return character.isDigit()
            || (lower >= QLatin1Char('a')
                && lower <= QLatin1Char('f'));
    return false;
}

int consumeBasedTail(const QString& text, int position)
{
    int cursor = position;
    if (cursor < text.size()
        && (text.at(cursor) == QLatin1Char('s')
            || text.at(cursor) == QLatin1Char('S'))) {
        ++cursor;
    }
    if (cursor >= text.size())
        return position;
    const QChar base = text.at(cursor).toLower();
    if (base != QLatin1Char('b')
        && base != QLatin1Char('o')
        && base != QLatin1Char('d')
        && base != QLatin1Char('h')) {
        return position;
    }
    ++cursor;
    const int digitsStart = cursor;
    while (cursor < text.size()
           && isBasedDigit(text.at(cursor), base)) {
        ++cursor;
    }
    return cursor > digitsStart ? cursor : position;
}

int numericLiteralEnd(const QString& text, int start)
{
    if (start < 0 || start >= text.size())
        return start;
    int cursor = start;
    if (text.at(cursor) == QLatin1Char('\'')) {
        ++cursor;
        const int basedEnd = consumeBasedTail(text, cursor);
        if (basedEnd > cursor)
            return basedEnd;
        if (cursor < text.size()) {
            const QChar unbased = text.at(cursor).toLower();
            if (unbased == QLatin1Char('0')
                || unbased == QLatin1Char('1')
                || unbased == QLatin1Char('x')
                || unbased == QLatin1Char('z')) {
                return cursor + 1;
            }
        }
        return start;
    }
    if (!text.at(cursor).isDigit())
        return start;

    while (cursor < text.size()
           && (text.at(cursor).isDigit()
               || text.at(cursor) == QLatin1Char('_'))) {
        ++cursor;
    }
    if (cursor < text.size()
        && text.at(cursor) == QLatin1Char('\'')) {
        const int basedEnd = consumeBasedTail(text, cursor + 1);
        return basedEnd > cursor + 1 ? basedEnd : cursor;
    }
    if (cursor + 1 < text.size()
        && text.at(cursor) == QLatin1Char('.')
        && text.at(cursor + 1).isDigit()) {
        cursor += 2;
        while (cursor < text.size()
               && (text.at(cursor).isDigit()
                   || text.at(cursor) == QLatin1Char('_'))) {
            ++cursor;
        }
    }
    if (cursor < text.size()
        && (text.at(cursor) == QLatin1Char('e')
            || text.at(cursor) == QLatin1Char('E'))) {
        int exponent = cursor + 1;
        if (exponent < text.size()
            && (text.at(exponent) == QLatin1Char('+')
                || text.at(exponent) == QLatin1Char('-'))) {
            ++exponent;
        }
        const int digitsStart = exponent;
        while (exponent < text.size()
               && (text.at(exponent).isDigit()
                   || text.at(exponent) == QLatin1Char('_'))) {
            ++exponent;
        }
        if (exponent > digitsStart)
            cursor = exponent;
    }
    static const QStringList timeUnits{
        QStringLiteral("ms"), QStringLiteral("us"),
        QStringLiteral("ns"), QStringLiteral("ps"),
        QStringLiteral("fs"), QStringLiteral("s")};
    for (const QString& unit : timeUnits) {
        const int unitEnd = cursor + unit.size();
        if (text.mid(cursor, unit.size()).compare(
                unit, Qt::CaseInsensitive) == 0
            && (unitEnd >= text.size()
                || !isIdentifierCharacter(text.at(unitEnd)))) {
            cursor += unit.size();
            break;
        }
    }
    return cursor;
}

Range numericLiteralAt(const QString& text, int position)
{
    if (text.isEmpty() || position < 0 || position >= text.size())
        return {};
    int lineStart = text.lastIndexOf(QLatin1Char('\n'), position);
    lineStart = lineStart < 0 ? 0 : lineStart + 1;
    for (int start = lineStart; start <= position; ++start) {
        if (start > lineStart
            && isIdentifierCharacter(text.at(start - 1))) {
            continue;
        }
        if (!text.at(start).isDigit()
            && text.at(start) != QLatin1Char('\'')) {
            continue;
        }
        const int end = numericLiteralEnd(text, start);
        if (end > start && position >= start && position < end)
            return {start, end};
    }
    return {};
}

enum class FragmentClass {
    Lower,
    Upper,
    Digit,
    Separator,
    Other,
};

FragmentClass fragmentClass(QChar character)
{
    if (character == QLatin1Char('_')
        || character == QLatin1Char('$')) {
        return FragmentClass::Separator;
    }
    if (character.isDigit())
        return FragmentClass::Digit;
    if (character.isUpper())
        return FragmentClass::Upper;
    if (character.isLower() || character.isLetter())
        return FragmentClass::Lower;
    return FragmentClass::Other;
}

bool fragmentBreak(const QString& text,
                   int fragmentStart,
                   int position)
{
    if (position <= fragmentStart || position >= text.size())
        return false;
    const FragmentClass previous = fragmentClass(text.at(position - 1));
    const FragmentClass current = fragmentClass(text.at(position));
    if (previous == FragmentClass::Separator
        || current == FragmentClass::Separator) {
        return true;
    }
    if ((previous == FragmentClass::Digit)
        != (current == FragmentClass::Digit)) {
        return true;
    }
    if (previous == FragmentClass::Lower
        && current == FragmentClass::Upper) {
        return true;
    }
    if (previous == FragmentClass::Upper
        && current == FragmentClass::Upper
        && position + 1 < text.size()
        && fragmentClass(text.at(position + 1))
               == FragmentClass::Lower
        && position - fragmentStart > 1) {
        return true;
    }
    return false;
}

Range identifierFragment(const QString& text,
                         const Range& identifier,
                         int position)
{
    if (!identifier.isValid())
        return {};
    const int index = qBound(identifier.start,
                             position,
                             identifier.end - 1);
    if (fragmentClass(text.at(index)) == FragmentClass::Separator) {
        int start = index;
        int end = index + 1;
        while (start > identifier.start
               && fragmentClass(text.at(start - 1))
                      == FragmentClass::Separator) {
            --start;
        }
        while (end < identifier.end
               && fragmentClass(text.at(end))
                      == FragmentClass::Separator) {
            ++end;
        }
        return {start, end};
    }

    int start = identifier.start;
    for (int cursor = identifier.start + 1;
         cursor <= index;
         ++cursor) {
        if (fragmentBreak(text, start, cursor))
            start = cursor;
    }
    int end = identifier.end;
    for (int cursor = index + 1;
         cursor < identifier.end;
         ++cursor) {
        if (fragmentBreak(text, start, cursor)) {
            end = cursor;
            break;
        }
    }
    return {start, end};
}

Range stringAt(const QString& text, int position)
{
    if (text.isEmpty() || position < 0 || position >= text.size())
        return {};
    int lineStart = text.lastIndexOf(QLatin1Char('\n'), position);
    lineStart = lineStart < 0 ? 0 : lineStart + 1;
    const int lineEndCandidate = text.indexOf(QLatin1Char('\n'), position);
    const int lineEnd = lineEndCandidate < 0
        ? text.size() : lineEndCandidate;

    bool escaped = false;
    int opening = -1;
    for (int cursor = lineStart; cursor < lineEnd; ++cursor) {
        const QChar character = text.at(cursor);
        if (escaped) {
            escaped = false;
            continue;
        }
        if (character == QLatin1Char('\\')) {
            escaped = opening >= 0;
            continue;
        }
        if (character != QLatin1Char('"'))
            continue;
        if (opening < 0) {
            opening = cursor;
            continue;
        }
        const Range range{opening, cursor + 1};
        if (position >= range.start && position < range.end)
            return range;
        opening = -1;
    }
    if (opening >= 0 && position >= opening)
        return {opening, lineEnd};
    return {};
}

Range operatorAt(const QString& text, int position)
{
    static const QStringList operators = {
        QStringLiteral("<<<"), QStringLiteral(">>>"),
        QStringLiteral("<->"), QStringLiteral("|->"),
        QStringLiteral("|=>"), QStringLiteral("!=="),
        QStringLiteral("==="), QStringLiteral("<<"),
        QStringLiteral(">>"), QStringLiteral("<="),
        QStringLiteral(">="), QStringLiteral("=="),
        QStringLiteral("!="), QStringLiteral("&&"),
        QStringLiteral("||"), QStringLiteral("++"),
        QStringLiteral("--"), QStringLiteral("**"),
        QStringLiteral("->"), QStringLiteral("=>"),
        QStringLiteral("+:"), QStringLiteral("-:"),
        QStringLiteral("~&"), QStringLiteral("~|"),
        QStringLiteral("~^"), QStringLiteral("^~"),
    };
    for (const QString& candidate : operators) {
        const int firstStart = qMax(0, position - candidate.size() + 1);
        for (int start = firstStart;
             start <= position && start + candidate.size() <= text.size();
             ++start) {
            if (text.mid(start, candidate.size()) == candidate
                && position >= start
                && position < start + candidate.size()) {
                return {start,
                        start + static_cast<int>(candidate.size())};
            }
        }
    }
    return {};
}

bool isNavigationSeparator(QChar character)
{
    return isHorizontalSpace(character)
        || character == QLatin1Char('_')
        || character == QLatin1Char('$');
}

} // namespace

Range horizontalWhitespaceAt(const QString& text, int position)
{
    if (text.isEmpty() || position < 0 || position >= text.size()
        || !isHorizontalSpace(text.at(position))) {
        return {};
    }
    int start = position;
    int end = position + 1;
    while (start > 0 && isHorizontalSpace(text.at(start - 1)))
        --start;
    while (end < text.size() && isHorizontalSpace(text.at(end)))
        ++end;
    return {start, end};
}

Range identifierAt(const QString& text, int position)
{
    if (text.isEmpty())
        return {};
    int index = qBound(0, position, text.size());
    if (index == text.size()
        || !isIdentifierCharacter(text.at(index))) {
        if (index <= 0
            || !isIdentifierCharacter(text.at(index - 1))) {
            return {};
        }
        --index;
    }
    int start = index;
    int end = index + 1;
    while (start > 0 && isIdentifierCharacter(text.at(start - 1)))
        --start;
    while (end < text.size() && isIdentifierCharacter(text.at(end)))
        ++end;
    if (!isIdentifierStart(text.at(start)))
        return {};
    return {start, end};
}

Range unitAt(const QString& text, int position)
{
    if (text.isEmpty())
        return {};
    const int index = qBound(0, position, text.size() - 1);
    if (const Range whitespace = horizontalWhitespaceAt(text, index);
        whitespace.isValid()) {
        return whitespace;
    }
    if (const Range string = stringAt(text, index); string.isValid())
        return string;
    if (const Range number = numericLiteralAt(text, index);
        number.isValid()) {
        return number;
    }
    if (const Range identifier = identifierAt(text, index);
        identifier.isValid()) {
        return identifierFragment(text, identifier, index);
    }
    if (const Range op = operatorAt(text, index); op.isValid())
        return op;
    return {index, index + 1};
}

int moveLeft(const QString& text, int position)
{
    int cursor = qBound(0, position, text.size());
    if (cursor <= 0)
        return 0;
    while (cursor > 0
           && isNavigationSeparator(text.at(cursor - 1))) {
        --cursor;
    }
    if (cursor <= 0)
        return 0;
    const Range range = unitAt(text, cursor - 1);
    return range.isValid() ? range.start : cursor - 1;
}

int moveRight(const QString& text, int position)
{
    int cursor = qBound(0, position, text.size());
    if (cursor >= text.size())
        return text.size();
    const Range current = unitAt(text, cursor);
    if (current.isValid())
        cursor = current.end;
    while (cursor < text.size()
           && isNavigationSeparator(text.at(cursor))) {
        ++cursor;
    }
    return cursor;
}

Range deleteBackward(const QString& text, int position)
{
    const int cursor = qBound(0, position, text.size());
    if (cursor <= 0)
        return {};
    const Range range = unitAt(text, cursor - 1);
    if (!range.isValid())
        return {};
    return {range.start, cursor};
}

Range deleteForward(const QString& text, int position)
{
    const int cursor = qBound(0, position, text.size());
    if (cursor >= text.size())
        return {};
    const Range range = unitAt(text, cursor);
    if (!range.isValid())
        return {};
    return {cursor, range.end};
}

} // namespace EditorLexicalBoundary
