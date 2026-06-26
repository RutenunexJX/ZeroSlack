#include "ghostannotationservice.h"

#include <QChar>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <limits>

std::unique_ptr<GhostAnnotationService>
    GhostAnnotationService::instance = nullptr;

namespace {
struct LineInfo {
    QString text;
    int startPosition = 0;
};

struct LiteralValue {
    bool valid = false;
    bool exact = false;
    int width = 0;
    int base = 10;
    unsigned long long value = 0;
    QString digits;
    QString original;
};

using UInt128 = unsigned __int128;

struct WideValue {
    bool valid = false;
    UInt128 value = 0;
    int width = 0;
    int sourceBase = 10;
    bool simpleLiteral = false;
    bool stringLiteral = false;
};

struct UserFunctionDefinition {
    QString argumentName;
    QString expression;
};

bool isIdentifierStart(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_') || ch == QLatin1Char('$');
}

bool isIdentifierPart(QChar ch)
{
    return isIdentifierStart(ch) || ch.isDigit();
}

bool isDecimalDigit(QChar ch)
{
    return ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
}

bool isBinaryDigit(QChar ch)
{
    return ch == QLatin1Char('0') || ch == QLatin1Char('1');
}

bool isHexDigit(QChar ch)
{
    return isDecimalDigit(ch)
        || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))
        || (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
}

int hexDigitValue(QChar ch)
{
    if (isDecimalDigit(ch))
        return ch.unicode() - QLatin1Char('0').unicode();
    if (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))
        return 10 + ch.unicode() - QLatin1Char('a').unicode();
    if (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'))
        return 10 + ch.unicode() - QLatin1Char('A').unicode();
    return 0;
}

QString decimalDigits(UInt128 value)
{
    if (value == 0)
        return QStringLiteral("0");

    QString result;
    while (value > 0) {
        const int digit = static_cast<int>(value % 10);
        result.prepend(QChar(QLatin1Char('0' + digit)));
        value /= 10;
    }
    return result;
}

QString binaryDigitsWide(UInt128 value, int width)
{
    const int bitCount = qBound(1, width, 128);
    QString result;
    result.reserve(bitCount + bitCount / 4);
    for (int i = bitCount - 1; i >= 0; --i) {
        const bool bitSet = ((value >> i) & 1) != 0;
        result.append(bitSet ? QLatin1Char('1') : QLatin1Char('0'));
        if (i > 0 && i % 4 == 0)
            result.append(QLatin1Char('_'));
    }
    return result;
}

QString hexDigitsWide(UInt128 value, int width)
{
    const int nibbleCount = qBound(1, (width + 3) / 4, 32);
    QString result;
    result.reserve(nibbleCount);
    const char* chars = "0123456789ABCDEF";
    for (int i = nibbleCount - 1; i >= 0; --i) {
        const int shift = i * 4;
        const int nibble = static_cast<int>((value >> shift) & 0xFULL);
        result.append(QLatin1Char(chars[nibble]));
    }
    return result;
}

int inferredWidthWide(UInt128 value)
{
    int width = 1;
    while (value > 1) {
        value >>= 1;
        ++width;
    }
    return width;
}

UInt128 maskForWidth(int width)
{
    if (width <= 0 || width >= 128)
        return ~static_cast<UInt128>(0);
    return (static_cast<UInt128>(1) << width) - 1;
}

QString radixDisplayText(UInt128 value, int width, int omitBase = 0)
{
    const int displayWidth = qBound(1, width, 128);
    QStringList parts;
    if (omitBase != 2)
        parts.append(QStringLiteral("(B)%1").arg(binaryDigitsWide(value, displayWidth)));
    if (omitBase != 10)
        parts.append(QStringLiteral("(D)%1").arg(decimalDigits(value)));
    if (omitBase != 16)
        parts.append(QStringLiteral("(H)%1").arg(hexDigitsWide(value, displayWidth)));
    return parts.join(QLatin1Char(' '));
}

bool isUnknownDigit(QChar ch)
{
    const QChar lower = ch.toLower();
    return lower == QLatin1Char('x')
        || lower == QLatin1Char('z')
        || lower == QLatin1Char('?');
}

QString cleanDigits(const QString& text)
{
    QString result;
    for (const QChar ch : text) {
        if (ch != QLatin1Char('_'))
            result.append(ch);
    }
    return result;
}

bool digitsValidForBase(const QString& digits, int base, bool* exact)
{
    if (digits.isEmpty())
        return false;
    bool allExact = true;
    for (const QChar ch : digits) {
        if (ch == QLatin1Char('_'))
            continue;
        if (isUnknownDigit(ch)) {
            allExact = false;
            continue;
        }
        if (base == 2 && !isBinaryDigit(ch))
            return false;
        if (base == 10 && !isDecimalDigit(ch))
            return false;
        if (base == 16 && !isHexDigit(ch))
            return false;
    }
    if (exact)
        *exact = allExact;
    return true;
}

bool valueForDigitsChecked(const QString& digits,
                           int base,
                           unsigned long long* out)
{
    unsigned long long value = 0;
    for (const QChar ch : digits) {
        if (ch == QLatin1Char('_'))
            continue;
        unsigned long long digit = 0;
        if (base == 16)
            digit = static_cast<unsigned long long>(hexDigitValue(ch));
        else
            digit = static_cast<unsigned long long>(
                ch.unicode() - QLatin1Char('0').unicode());
        const unsigned long long max =
            std::numeric_limits<unsigned long long>::max();
        if (value > (max - digit) / static_cast<unsigned long long>(base))
            return false;
        value = value * static_cast<unsigned long long>(base) + digit;
    }
    if (out)
        *out = value;
    return true;
}

QString binaryDigits(unsigned long long value, int width)
{
    const int bitCount = qMax(width, 1);
    QString result;
    result.reserve(bitCount + bitCount / 4);
    for (int i = bitCount - 1; i >= 0; --i) {
        const bool bitSet = i < 64 && (value & (1ULL << i));
        result.append(bitSet ? QLatin1Char('1') : QLatin1Char('0'));
        if (i > 0 && i % 4 == 0)
            result.append(QLatin1Char('_'));
    }
    return result;
}

QString hexDigits(unsigned long long value, int width)
{
    const int nibbleCount = qMax((width + 3) / 4, 1);
    QString result;
    result.reserve(nibbleCount);
    const char* chars = "0123456789ABCDEF";
    for (int i = nibbleCount - 1; i >= 0; --i) {
        const int shift = i * 4;
        const int nibble =
            shift < 64 ? static_cast<int>((value >> shift) & 0xFULL) : 0;
        result.append(QLatin1Char(chars[nibble]));
    }
    return result;
}

int inferredWidth(const LiteralValue& literal)
{
    if (literal.width > 0)
        return literal.width;
    if (literal.base == 2)
        return cleanDigits(literal.digits).size();
    if (literal.base == 16)
        return cleanDigits(literal.digits).size() * 4;
    int width = 1;
    unsigned long long value = literal.value;
    while (value > 1) {
        value >>= 1;
        ++width;
    }
    return width;
}

QString numericLiteralHoverText(const LiteralValue& literal)
{
    if (!literal.valid || !literal.exact)
        return QString();

    const int width = inferredWidth(literal);
    return QStringLiteral("(D)%1 (B)%2 (H)%3")
        .arg(QString::number(literal.value),
             binaryDigits(literal.value, width),
             hexDigits(literal.value, width));
}

bool parseUnsignedInt(const QString& text, int* value)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;
    int next = 0;
    for (const QChar ch : trimmed) {
        if (!isDecimalDigit(ch))
            return false;
        next = next * 10 + ch.unicode() - QLatin1Char('0').unicode();
    }
    if (value)
        *value = next;
    return true;
}

bool parseLiteralAt(const QString& line,
                    int start,
                    LiteralValue* literal,
                    int* end)
{
    if (start < 0 || start >= line.size() || !isDecimalDigit(line.at(start)))
        return false;

    int pos = start;
    QString firstDigits;
    while (pos < line.size()
           && (isDecimalDigit(line.at(pos)) || line.at(pos) == QLatin1Char('_'))) {
        firstDigits.append(line.at(pos));
        ++pos;
    }

    LiteralValue parsed;
    parsed.original = firstDigits;
    if (pos < line.size() && line.at(pos) == QLatin1Char('\'')) {
        int width = 0;
        if (!parseUnsignedInt(cleanDigits(firstDigits), &width))
            return false;
        if (pos + 1 >= line.size())
            return false;

        const QChar baseChar = line.at(pos + 1).toLower();
        int base = 0;
        if (baseChar == QLatin1Char('b'))
            base = 2;
        else if (baseChar == QLatin1Char('d'))
            base = 10;
        else if (baseChar == QLatin1Char('h'))
            base = 16;
        else if (isDecimalDigit(baseChar))
            base = 10;
        else
            return false;

        pos += isDecimalDigit(baseChar) ? 1 : 2;
        QString digits;
        while (pos < line.size()) {
            const QChar ch = line.at(pos);
            if (ch == QLatin1Char('_') || isUnknownDigit(ch)
                || (base == 2 && isBinaryDigit(ch))
                || (base == 10 && isDecimalDigit(ch))
                || (base == 16 && isHexDigit(ch))) {
                digits.append(ch);
                ++pos;
                continue;
            }
            break;
        }

        bool exact = false;
        if (!digitsValidForBase(digits, base, &exact))
            return false;
        parsed.valid = true;
        parsed.exact = exact;
        parsed.width = width;
        parsed.base = base;
        parsed.digits = digits;
        parsed.original = line.mid(start, pos - start);
        if (exact
            && !valueForDigitsChecked(digits, base, &parsed.value)) {
            return false;
        }
    } else {
        bool exact = false;
        if (!digitsValidForBase(firstDigits, 10, &exact))
            return false;
        parsed.valid = true;
        parsed.exact = exact;
        parsed.base = 10;
        parsed.digits = firstDigits;
        if (exact
            && !valueForDigitsChecked(firstDigits, 10, &parsed.value)) {
            return false;
        }
    }

    if (literal)
        *literal = parsed;
    if (end)
        *end = pos;
    return true;
}

bool valueForDigitsWide(const QString& digits,
                        int base,
                        UInt128* out)
{
    UInt128 value = 0;
    for (const QChar ch : digits) {
        if (ch == QLatin1Char('_'))
            continue;
        UInt128 digit = 0;
        if (base == 16)
            digit = static_cast<UInt128>(hexDigitValue(ch));
        else
            digit = static_cast<UInt128>(
                ch.unicode() - QLatin1Char('0').unicode());
        value = value * static_cast<UInt128>(base) + digit;
    }
    if (out)
        *out = value;
    return true;
}

bool parseWideLiteralAt(const QString& line,
                        int start,
                        WideValue* value,
                        int* end)
{
    if (start < 0 || start >= line.size())
        return false;

    int pos = start;
    QString firstDigits;
    while (pos < line.size()
           && (isDecimalDigit(line.at(pos))
               || line.at(pos) == QLatin1Char('_'))) {
        firstDigits.append(line.at(pos));
        ++pos;
    }

    int width = 0;
    bool hasWidth = false;
    if (pos < line.size() && line.at(pos) == QLatin1Char('\'')) {
        hasWidth = !firstDigits.isEmpty();
        if (hasWidth && !parseUnsignedInt(cleanDigits(firstDigits), &width))
            return false;
        if (pos + 1 >= line.size())
            return false;

        const QChar baseChar = line.at(pos + 1).toLower();
        int base = 0;
        int digitsStart = pos + 2;
        if (baseChar == QLatin1Char('b'))
            base = 2;
        else if (baseChar == QLatin1Char('d'))
            base = 10;
        else if (baseChar == QLatin1Char('h'))
            base = 16;
        else if (hasWidth && isDecimalDigit(baseChar)) {
            base = 10;
            digitsStart = pos + 1;
        } else {
            return false;
        }

        pos = digitsStart;
        QString digits;
        while (pos < line.size()) {
            const QChar ch = line.at(pos);
            if (ch == QLatin1Char('_')
                || (base == 2 && isBinaryDigit(ch))
                || (base == 10 && isDecimalDigit(ch))
                || (base == 16 && isHexDigit(ch))) {
                digits.append(ch);
                ++pos;
                continue;
            }
            break;
        }

        bool exact = false;
        if (!digitsValidForBase(digits, base, &exact) || !exact)
            return false;

        UInt128 parsed = 0;
        valueForDigitsWide(digits, base, &parsed);
        WideValue result;
        result.valid = true;
        result.value = parsed;
        result.width = hasWidth ? width : qMax(1, base == 16
            ? cleanDigits(digits).size() * 4
            : inferredWidthWide(parsed));
        result.sourceBase = base;
        result.simpleLiteral = true;
        if (result.width > 0)
            result.value &= maskForWidth(result.width);
        if (value)
            *value = result;
        if (end)
            *end = pos;
        return true;
    }

    if (firstDigits.isEmpty())
        return false;

    bool exact = false;
    if (!digitsValidForBase(firstDigits, 10, &exact) || !exact)
        return false;
    UInt128 parsed = 0;
    valueForDigitsWide(firstDigits, 10, &parsed);
    WideValue result;
    result.valid = true;
    result.value = parsed;
    result.width = inferredWidthWide(parsed);
    result.sourceBase = 10;
    result.simpleLiteral = true;
    if (value)
        *value = result;
    if (end)
        *end = pos;
    return true;
}

bool parseStringLiteralAt(const QString& line,
                          int start,
                          WideValue* value,
                          int* end)
{
    if (start < 0 || start >= line.size()
        || line.at(start) != QLatin1Char('"')) {
        return false;
    }

    int pos = start + 1;
    UInt128 parsed = 0;
    int bytes = 0;
    bool escaped = false;
    while (pos < line.size()) {
        const QChar ch = line.at(pos);
        if (escaped) {
            parsed = (parsed << 8)
                | static_cast<UInt128>(ch.toLatin1());
            ++bytes;
            escaped = false;
            ++pos;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escaped = true;
            ++pos;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            WideValue result;
            result.valid = bytes > 0;
            result.value = parsed;
            result.width = qBound(1, bytes * 8, 128);
            result.sourceBase = 16;
            result.simpleLiteral = true;
            result.stringLiteral = true;
            if (value)
                *value = result;
            if (end)
                *end = pos + 1;
            return result.valid;
        }
        parsed = (parsed << 8)
            | static_cast<UInt128>(ch.toLatin1());
        ++bytes;
        ++pos;
    }
    return false;
}

int lineCommentStart(const QString& line)
{
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i + 1 < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }

        if (ch == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (ch == QLatin1Char('/') && line.at(i + 1) == QLatin1Char('/'))
            return i;
    }
    return -1;
}

QList<LineInfo> documentLines(const QString& text)
{
    QList<LineInfo> lines;
    int lineStart = 0;
    for (int i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text.at(i) == QLatin1Char('\n')) {
            LineInfo line;
            line.startPosition = lineStart;
            line.text = text.mid(lineStart, i - lineStart);
            if (line.text.endsWith(QLatin1Char('\r')))
                line.text.chop(1);
            lines.append(line);
            lineStart = i + 1;
        }
    }
    return lines;
}

bool positionInsideString(const QString& line, int column)
{
    bool inString = false;
    bool escaped = false;
    const int limit = qBound(0, column, line.size());
    for (int i = 0; i < limit; ++i) {
        const QChar ch = line.at(i);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }
        if (ch == QLatin1Char('"'))
            inString = true;
    }
    return inString;
}

int lineIndexForPosition(const QList<LineInfo>& lines, int position)
{
    if (position < 0 || lines.isEmpty())
        return -1;
    for (int i = 0; i < lines.size(); ++i) {
        const int start = lines.at(i).startPosition;
        const int end = start + lines.at(i).text.size();
        if (position >= start && position <= end)
            return i;
    }
    return -1;
}

QString formalNameFromInstPin(const QString& name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 ? name.mid(dot + 1) : name;
}

QString directionForPort(SymbolTaxonomy::CollectorKind kind)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    switch (kind) {
    case CollectorKind::PortInput:
        return QStringLiteral("in");
    case CollectorKind::PortOutput:
        return QStringLiteral("out");
    case CollectorKind::PortInout:
        return QStringLiteral("io");
    default:
        return QString();
    }
}

bool isPortRecord(const SemanticSymbolRecord& record)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    return record.collectorKind == CollectorKind::PortInput
        || record.collectorKind == CollectorKind::PortOutput
        || record.collectorKind == CollectorKind::PortInout;
}

bool isSignalLikeRecord(const SemanticSymbolRecord& record)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    return record.collectorKind == CollectorKind::Reg
        || record.collectorKind == CollectorKind::Wire
        || record.collectorKind == CollectorKind::Logic
        || isPortRecord(record);
}

bool isParameterLikeRecord(const SemanticSymbolRecord& record)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    return record.collectorKind == CollectorKind::Parameter
        || record.collectorKind == CollectorKind::Localparam
        || record.collectorKind == CollectorKind::ModuleParameter
        || record.collectorKind == CollectorKind::InterfaceParameter
        || record.collectorKind == CollectorKind::DefParameter;
}

int findNameOnLine(const QString& line,
                   const QString& name,
                   int preferredStartColumn)
{
    if (name.isEmpty())
        return -1;

    const int commentStart = lineCommentStart(line);
    const int searchEnd = commentStart >= 0 ? commentStart : line.size();
    int pos = qBound(0, preferredStartColumn - 1, searchEnd);
    while (pos < searchEnd) {
        pos = line.indexOf(name, pos, Qt::CaseSensitive);
        if (pos < 0 || pos + name.size() > searchEnd)
            break;
        const bool leftOk = pos == 0 || !isIdentifierPart(line.at(pos - 1));
        const bool rightOk =
            pos + name.size() >= line.size()
            || !isIdentifierPart(line.at(pos + name.size()));
        if (leftOk && rightOk)
            return pos;
        ++pos;
    }

    pos = 0;
    while (pos < searchEnd) {
        pos = line.indexOf(name, pos, Qt::CaseSensitive);
        if (pos < 0 || pos + name.size() > searchEnd)
            return -1;
        const bool leftOk = pos == 0 || !isIdentifierPart(line.at(pos - 1));
        const bool rightOk =
            pos + name.size() >= line.size()
            || !isIdentifierPart(line.at(pos + name.size()));
        if (leftOk && rightOk)
            return pos;
        ++pos;
    }
    return -1;
}

QString displayTypeText(const QString& rawTypeText)
{
    QString text = rawTypeText.simplified();
    if (text.isEmpty())
        return QString();
    return text;
}

QString formalPortGhostText(const SemanticSymbolRecord& port)
{
    QStringList parts;
    const QString direction = directionForPort(port.collectorKind);
    if (!direction.isEmpty())
        parts.append(direction);
    const QString type = displayTypeText(port.type.rawTypeText);
    if (!type.isEmpty())
        parts.append(type);
    return parts.join(QLatin1Char(' '));
}

QList<SemanticSymbolRecord> matchingPortRecords(
    const QList<SemanticSymbolRecord>& records,
    const QString& formalName,
    const QString& moduleName)
{
    QList<SemanticSymbolRecord> matches;
    for (const SemanticSymbolRecord& record : records) {
        if (record.name != formalName || !isPortRecord(record))
            continue;
        if (!moduleName.isEmpty() && record.owner.name != moduleName)
            continue;
        matches.append(record);
    }
    if (!matches.isEmpty() || moduleName.isEmpty())
        return matches;

    for (const SemanticSymbolRecord& record : records) {
        if (record.name == formalName && isPortRecord(record))
            matches.append(record);
    }
    return matches;
}

bool rangeWidthFromText(const QString& text, int* width)
{
    const int left = text.indexOf(QLatin1Char('['));
    const int colon = text.indexOf(QLatin1Char(':'), left + 1);
    const int right = text.indexOf(QLatin1Char(']'), colon + 1);
    if (left < 0 || colon < 0 || right < 0)
        return false;

    int msb = 0;
    int lsb = 0;
    if (!parseUnsignedInt(text.mid(left + 1, colon - left - 1), &msb))
        return false;
    if (!parseUnsignedInt(text.mid(colon + 1, right - colon - 1), &lsb))
        return false;

    if (width)
        *width = qAbs(msb - lsb) + 1;
    return true;
}

int numericArrayExtentAfterName(const QString& line,
                                int nameStart,
                                int nameLength)
{
    int pos = nameStart + nameLength;
    while (pos < line.size() && line.at(pos).isSpace())
        ++pos;
    if (pos >= line.size() || line.at(pos) != QLatin1Char('['))
        return -1;

    const int right = line.indexOf(QLatin1Char(']'), pos + 1);
    if (right < 0)
        return -1;

    const QString inner = line.mid(pos + 1, right - pos - 1).trimmed();
    int extent = 0;
    if (parseUnsignedInt(inner, &extent))
        return extent;

    const int colon = inner.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        int msb = 0;
        int lsb = 0;
        if (parseUnsignedInt(inner.left(colon), &msb)
            && parseUnsignedInt(inner.mid(colon + 1), &lsb)) {
            return qAbs(msb - lsb) + 1;
        }
    }
    return -1;
}

struct RangeInfo {
    bool valid = false;
    QString raw;
    QString left;
    QString right;
    bool leftNumeric = false;
    bool rightNumeric = false;
    int leftValue = 0;
    int rightValue = 0;
};

bool firstRangeFromText(const QString& text, RangeInfo* out)
{
    const int leftBracket = text.indexOf(QLatin1Char('['));
    const int colon = text.indexOf(QLatin1Char(':'), leftBracket + 1);
    const int rightBracket = text.indexOf(QLatin1Char(']'), colon + 1);
    if (leftBracket < 0 || colon < 0 || rightBracket < 0)
        return false;

    RangeInfo range;
    range.valid = true;
    range.raw = text.mid(leftBracket, rightBracket - leftBracket + 1).simplified();
    range.left = text.mid(leftBracket + 1, colon - leftBracket - 1).trimmed();
    range.right = text.mid(colon + 1, rightBracket - colon - 1).trimmed();
    range.leftNumeric = parseUnsignedInt(range.left, &range.leftValue);
    range.rightNumeric = parseUnsignedInt(range.right, &range.rightValue);
    if (out)
        *out = range;
    return true;
}

bool parseIdentifierOrMacroAt(const QString& text,
                              int start,
                              QString* name,
                              int* end)
{
    if (start < 0 || start >= text.size())
        return false;

    int pos = start;
    bool macro = false;
    if (text.at(pos) == QLatin1Char('`')) {
        macro = true;
        ++pos;
    }
    if (pos >= text.size() || !isIdentifierStart(text.at(pos)))
        return false;

    int nameEnd = pos + 1;
    while (nameEnd < text.size() && isIdentifierPart(text.at(nameEnd)))
        ++nameEnd;

    if (name)
        *name = macro
            ? QStringLiteral("`%1").arg(text.mid(pos, nameEnd - pos))
            : text.mid(pos, nameEnd - pos);
    if (end)
        *end = nameEnd;
    return true;
}

bool evaluateAdditiveExpression(const QString& expression,
                                const QHash<QString, int>& values,
                                int* out)
{
    int pos = 0;
    int total = 0;
    int sign = 1;
    bool consumedTerm = false;
    while (pos < expression.size()) {
        while (pos < expression.size() && expression.at(pos).isSpace())
            ++pos;
        if (pos >= expression.size())
            break;

        if (expression.at(pos) == QLatin1Char('+')) {
            sign = 1;
            ++pos;
            continue;
        }
        if (expression.at(pos) == QLatin1Char('-')) {
            sign = -1;
            ++pos;
            continue;
        }

        int value = 0;
        if (isDecimalDigit(expression.at(pos))) {
            LiteralValue literal;
            int literalEnd = 0;
            if (!parseLiteralAt(expression, pos, &literal, &literalEnd)
                || !literal.exact) {
                return false;
            }
            if (literal.value > static_cast<unsigned long long>(
                    std::numeric_limits<int>::max())) {
                return false;
            }
            value = static_cast<int>(literal.value);
            pos = literalEnd;
        } else {
            QString name;
            int nameEnd = 0;
            if (!parseIdentifierOrMacroAt(expression, pos, &name, &nameEnd))
                return false;
            if (!values.contains(name)) {
                const QString bare = name.startsWith(QLatin1Char('`'))
                    ? name.mid(1)
                    : name;
                if (!values.contains(bare))
                    return false;
                value = values.value(bare);
            } else {
                value = values.value(name);
            }
            pos = nameEnd;
        }

        total += sign * value;
        sign = 1;
        consumedTerm = true;
    }

    if (!consumedTerm)
        return false;
    if (out)
        *out = total;
    return true;
}

bool evaluateWideExpression(const QString& expression,
                            const QHash<QString, WideValue>& values,
                            const QHash<QString, UserFunctionDefinition>& functions,
                            WideValue* out);

QString stripOuterExpressionParens(QString expression)
{
    expression = expression.trimmed();
    bool changed = true;
    while (changed && expression.startsWith(QLatin1Char('('))
           && expression.endsWith(QLatin1Char(')'))) {
        changed = false;
        int depth = 0;
        bool wraps = true;
        for (int i = 0; i < expression.size(); ++i) {
            const QChar ch = expression.at(i);
            if (ch == QLatin1Char('('))
                ++depth;
            else if (ch == QLatin1Char(')'))
                --depth;
            if (depth == 0 && i != expression.size() - 1) {
                wraps = false;
                break;
            }
        }
        if (wraps) {
            expression = expression.mid(1, expression.size() - 2).trimmed();
            changed = true;
        }
    }
    return expression;
}

int matchingParenAt(const QString& text, int open)
{
    int depth = 0;
    for (int i = open; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('(')) {
            ++depth;
            continue;
        }
        if (text.at(i) != QLatin1Char(')'))
            continue;
        --depth;
        if (depth == 0)
            return i;
    }
    return -1;
}

bool lookupWideValue(const QHash<QString, WideValue>& values,
                     const QString& name,
                     WideValue* out)
{
    if (values.contains(name)) {
        if (out)
            *out = values.value(name);
        return true;
    }
    const QString alternate = name.startsWith(QLatin1Char('`'))
        ? name.mid(1)
        : QStringLiteral("`%1").arg(name);
    if (!values.contains(alternate))
        return false;
    if (out)
        *out = values.value(alternate);
    return true;
}

bool evaluateWidePrimary(const QString& expression,
                         const QHash<QString, WideValue>& values,
                         const QHash<QString, UserFunctionDefinition>& functions,
                         WideValue* out)
{
    const QString text = stripOuterExpressionParens(expression);
    if (text.isEmpty())
        return false;

    WideValue literal;
    int literalEnd = 0;
    if (parseWideLiteralAt(text, 0, &literal, &literalEnd)
        && literalEnd == text.size()) {
        if (out)
            *out = literal;
        return true;
    }
    if (parseStringLiteralAt(text, 0, &literal, &literalEnd)
        && literalEnd == text.size()) {
        if (out)
            *out = literal;
        return true;
    }

    int nameEnd = 0;
    QString name;
    if (!parseIdentifierOrMacroAt(text, 0, &name, &nameEnd))
        return false;

    if (nameEnd == text.size()) {
        WideValue value;
        if (!lookupWideValue(values, name, &value))
            return false;
        value.simpleLiteral = false;
        if (out)
            *out = value;
        return true;
    }

    int open = nameEnd;
    while (open < text.size() && text.at(open).isSpace())
        ++open;
    if (open >= text.size() || text.at(open) != QLatin1Char('('))
        return false;
    const int close = matchingParenAt(text, open);
    if (close != text.size() - 1)
        return false;

    const QString argumentExpression =
        text.mid(open + 1, close - open - 1);
    WideValue argument;
    if (!evaluateWideExpression(argumentExpression, values, functions, &argument))
        return false;

    if (name == QStringLiteral("$clog2")) {
        UInt128 v = argument.value;
        int result = 0;
        UInt128 threshold = 1;
        while (threshold < v && result < 128) {
            threshold <<= 1;
            ++result;
        }
        WideValue value;
        value.valid = true;
        value.value = static_cast<UInt128>(result);
        value.width = inferredWidthWide(value.value);
        value.sourceBase = 10;
        if (out)
            *out = value;
        return true;
    }

    if (!functions.contains(name))
        return false;

    QHash<QString, WideValue> localValues = values;
    const UserFunctionDefinition function = functions.value(name);
    if (!function.argumentName.isEmpty())
        localValues.insert(function.argumentName, argument);
    WideValue result;
    if (!evaluateWideExpression(function.expression, localValues, functions, &result))
        return false;
    result.simpleLiteral = false;
    if (out)
        *out = result;
    return true;
}

bool evaluateWideExpression(const QString& expression,
                            const QHash<QString, WideValue>& values,
                            const QHash<QString, UserFunctionDefinition>& functions,
                            WideValue* out)
{
    const QString text = stripOuterExpressionParens(expression);
    if (text.isEmpty())
        return false;

    int depth = 0;
    int question = -1;
    int colon = -1;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('(')) {
            ++depth;
        } else if (ch == QLatin1Char(')')) {
            depth = qMax(0, depth - 1);
        } else if (depth == 0 && ch == QLatin1Char('?')) {
            question = i;
        } else if (depth == 0 && ch == QLatin1Char(':') && question >= 0) {
            colon = i;
            break;
        }
    }
    if (question > 0 && colon > question) {
        WideValue condition;
        if (!evaluateWideExpression(text.left(question),
                                    values,
                                    functions,
                                    &condition)) {
            return false;
        }
        WideValue branch;
        const QString branchExpression = condition.value != 0
            ? text.mid(question + 1, colon - question - 1)
            : text.mid(colon + 1);
        if (!evaluateWideExpression(branchExpression, values, functions, &branch))
            return false;
        branch.simpleLiteral = false;
        if (out)
            *out = branch;
        return true;
    }

    depth = 0;
    for (int i = text.size() - 1; i >= 0; --i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char(')')) {
            ++depth;
            continue;
        }
        if (ch == QLatin1Char('(')) {
            depth = qMax(0, depth - 1);
            continue;
        }
        if (depth != 0 || (ch != QLatin1Char('+') && ch != QLatin1Char('-')))
            continue;
        if (i == 0)
            continue;

        WideValue left;
        WideValue right;
        if (!evaluateWideExpression(text.left(i), values, functions, &left)
            || !evaluateWideExpression(text.mid(i + 1), values, functions, &right)) {
            return false;
        }
        WideValue result;
        result.valid = true;
        result.value = ch == QLatin1Char('+')
            ? left.value + right.value
            : left.value - right.value;
        result.width = qMax(left.width, right.width);
        result.sourceBase = 10;
        result.simpleLiteral = false;
        if (out)
            *out = result;
        return true;
    }

    int castWidthEnd = 0;
    int castWidth = 0;
    while (castWidthEnd < text.size()
           && isDecimalDigit(text.at(castWidthEnd))) {
        castWidth = castWidth * 10
            + text.at(castWidthEnd).unicode() - QLatin1Char('0').unicode();
        ++castWidthEnd;
    }
    if (castWidthEnd > 0
        && castWidthEnd + 1 < text.size()
        && text.at(castWidthEnd) == QLatin1Char('\'')
        && text.at(castWidthEnd + 1) == QLatin1Char('(')) {
        const int close = matchingParenAt(text, castWidthEnd + 1);
        if (close == text.size() - 1) {
            WideValue inner;
            if (!evaluateWideExpression(
                    text.mid(castWidthEnd + 2, close - castWidthEnd - 2),
                    values,
                    functions,
                    &inner)) {
                return false;
            }
            inner.value &= maskForWidth(castWidth);
            inner.width = castWidth;
            inner.sourceBase = 10;
            inner.simpleLiteral = false;
            if (out)
                *out = inner;
            return true;
        }
    }

    return evaluateWidePrimary(text, values, functions, out);
}

void addLiteralValue(QHash<QString, int>* values,
                     const QString& name,
                     const LiteralValue& literal)
{
    if (!values || name.isEmpty() || !literal.valid || !literal.exact)
        return;
    if (literal.value > static_cast<unsigned long long>(
            std::numeric_limits<int>::max())) {
        return;
    }
    const int value = static_cast<int>(literal.value);
    values->insert(name, value);
    if (name.startsWith(QLatin1Char('`')))
        values->insert(name.mid(1), value);
    else
        values->insert(QStringLiteral("`%1").arg(name), value);
}

QHash<QString, int> literalValuesForDocument(
    const QList<LineInfo>& lines,
    const QList<SemanticSymbolRecord>& records)
{
    QHash<QString, int> values;
    for (const SemanticSymbolRecord& record : records) {
        if (!isParameterLikeRecord(record)
            || record.location.startLine <= 0
            || record.location.startLine > lines.size()) {
            continue;
        }
        const LineInfo& line = lines.at(record.location.startLine - 1);
        const int nameStart =
            findNameOnLine(line.text, record.name, record.location.startColumn);
        if (nameStart < 0)
            continue;
        const int equal = line.text.indexOf(QLatin1Char('='),
                                            nameStart + record.name.size());
        if (equal < 0)
            continue;
        int literalStart = equal + 1;
        while (literalStart < line.text.size()
               && line.text.at(literalStart).isSpace()) {
            ++literalStart;
        }
        LiteralValue literal;
        int literalEnd = 0;
        if (parseLiteralAt(line.text, literalStart, &literal, &literalEnd))
            addLiteralValue(&values, record.name, literal);
    }

    for (const LineInfo& line : lines) {
        int pos = 0;
        while (pos < line.text.size() && line.text.at(pos).isSpace())
            ++pos;
        if (pos >= line.text.size() || line.text.at(pos) != QLatin1Char('`'))
            continue;
        ++pos;
        const QString keyword = QStringLiteral("define");
        if (line.text.mid(pos, keyword.size()) != keyword)
            continue;
        pos += keyword.size();
        if (pos < line.text.size() && isIdentifierPart(line.text.at(pos)))
            continue;
        while (pos < line.text.size() && line.text.at(pos).isSpace())
            ++pos;
        QString name;
        int nameEnd = 0;
        if (!parseIdentifierOrMacroAt(line.text, pos, &name, &nameEnd))
            continue;
        int literalStart = nameEnd;
        while (literalStart < line.text.size()
               && line.text.at(literalStart).isSpace()) {
            ++literalStart;
        }
        LiteralValue literal;
        int literalEnd = 0;
        if (parseLiteralAt(line.text, literalStart, &literal, &literalEnd))
            addLiteralValue(&values, name, literal);
    }
    return values;
}

QHash<QString, UserFunctionDefinition> userFunctionsForDocument(
    const QList<LineInfo>& lines)
{
    QHash<QString, UserFunctionDefinition> functions;
    bool inFunction = false;
    QString functionName;
    QString argumentName;
    QString assignmentExpression;

    for (const LineInfo& line : lines) {
        const QString trimmed = line.text.trimmed();
        if (!inFunction) {
            if (!trimmed.startsWith(QStringLiteral("function")))
                continue;
            const int paren = trimmed.indexOf(QLatin1Char('('));
            const QString header = paren >= 0 ? trimmed.left(paren) : trimmed;
            const QStringList tokens =
                header.split(QRegularExpression(QStringLiteral("\\s+")),
                             Qt::SkipEmptyParts);
            if (tokens.size() < 2)
                continue;
            functionName = tokens.last();
            inFunction = !functionName.isEmpty();
            argumentName.clear();
            assignmentExpression.clear();
            if (paren >= 0) {
                const int close = trimmed.indexOf(QLatin1Char(')'), paren + 1);
                const QString arguments = close > paren
                    ? trimmed.mid(paren + 1, close - paren - 1)
                    : QString();
                const QStringList argTokens =
                    arguments.split(QRegularExpression(QStringLiteral("[,\\s]+")),
                                    Qt::SkipEmptyParts);
                if (!argTokens.isEmpty())
                    argumentName = argTokens.last();
            }
            continue;
        }

        if (trimmed.startsWith(QStringLiteral("endfunction"))) {
            if (!functionName.isEmpty() && !assignmentExpression.isEmpty()) {
                UserFunctionDefinition definition;
                definition.argumentName = argumentName;
                definition.expression = assignmentExpression;
                functions.insert(functionName, definition);
            }
            inFunction = false;
            functionName.clear();
            argumentName.clear();
            assignmentExpression.clear();
            continue;
        }

        if (argumentName.isEmpty()) {
            const QStringList tokens =
                trimmed.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                              Qt::SkipEmptyParts);
            if (!tokens.isEmpty()
                && (tokens.first() == QStringLiteral("input")
                    || tokens.contains(QStringLiteral("input")))) {
                argumentName = tokens.last();
            }
        }

        const QString prefix = functionName + QStringLiteral(" =");
        const int assign = trimmed.indexOf(prefix);
        if (assign >= 0) {
            QString expr = trimmed.mid(assign + prefix.size()).trimmed();
            if (expr.endsWith(QLatin1Char(';')))
                expr.chop(1);
            assignmentExpression = expr.trimmed();
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("return"))) {
            QString expr = trimmed.mid(QStringLiteral("return").size()).trimmed();
            if (expr.endsWith(QLatin1Char(';')))
                expr.chop(1);
            assignmentExpression = expr.trimmed();
        }
    }
    return functions;
}

QString parameterExpressionOnLine(const QString& line,
                                  int nameStart,
                                  int nameLength)
{
    const int equal = line.indexOf(QLatin1Char('='), nameStart + nameLength);
    if (equal < 0)
        return QString();
    int end = line.indexOf(QLatin1Char(';'), equal + 1);
    const int comma = line.indexOf(QLatin1Char(','), equal + 1);
    if (end < 0 || (comma >= 0 && comma < end))
        end = comma;
    if (end < 0)
        end = line.size();
    return line.mid(equal + 1, end - equal - 1).trimmed();
}

void insertWideValueAliases(QHash<QString, WideValue>* values,
                            const QString& name,
                            const WideValue& value)
{
    if (!values || name.isEmpty() || !value.valid)
        return;
    values->insert(name, value);
    if (name.startsWith(QLatin1Char('`')))
        values->insert(name.mid(1), value);
    else
        values->insert(QStringLiteral("`%1").arg(name), value);
}

QHash<QString, WideValue> wideValuesForDocument(
    const QList<LineInfo>& lines,
    QList<SemanticSymbolRecord> records,
    const QHash<QString, UserFunctionDefinition>& functions)
{
    QHash<QString, WideValue> values;

    for (const LineInfo& line : lines) {
        int pos = 0;
        while (pos < line.text.size() && line.text.at(pos).isSpace())
            ++pos;
        if (pos >= line.text.size() || line.text.at(pos) != QLatin1Char('`'))
            continue;
        ++pos;
        const QString keyword = QStringLiteral("define");
        if (line.text.mid(pos, keyword.size()) != keyword)
            continue;
        pos += keyword.size();
        if (pos < line.text.size() && isIdentifierPart(line.text.at(pos)))
            continue;
        while (pos < line.text.size() && line.text.at(pos).isSpace())
            ++pos;
        QString name;
        int nameEnd = 0;
        if (!parseIdentifierOrMacroAt(line.text, pos, &name, &nameEnd))
            continue;
        const QString expression = line.text.mid(nameEnd).trimmed();
        WideValue value;
        if (evaluateWideExpression(expression, values, functions, &value))
            insertWideValueAliases(&values, name, value);
    }

    records.erase(
        std::remove_if(records.begin(),
                       records.end(),
                       [](const SemanticSymbolRecord& record) {
                           return !isParameterLikeRecord(record)
                               || record.location.startLine <= 0;
                       }),
        records.end());
    std::stable_sort(records.begin(),
                     records.end(),
                     [](const SemanticSymbolRecord& lhs,
                        const SemanticSymbolRecord& rhs) {
                         if (lhs.location.startLine != rhs.location.startLine)
                             return lhs.location.startLine < rhs.location.startLine;
                         return lhs.location.startColumn < rhs.location.startColumn;
                     });

    for (const SemanticSymbolRecord& record : records) {
        if (record.location.startLine > lines.size())
            continue;
        const LineInfo& line = lines.at(record.location.startLine - 1);
        const int nameStart =
            findNameOnLine(line.text, record.name, record.location.startColumn);
        if (nameStart < 0)
            continue;
        const QString expression =
            parameterExpressionOnLine(line.text, nameStart, record.name.size());
        if (expression.isEmpty())
            continue;
        WideValue value;
        if (evaluateWideExpression(expression, values, functions, &value))
            insertWideValueAliases(&values, record.name, value);
    }
    return values;
}

QString widthGhostTextForType(const QString& rawTypeText,
                              const QHash<QString, int>& values)
{
    RangeInfo range;
    if (!firstRangeFromText(rawTypeText, &range))
        return QString();

    if (range.leftNumeric && range.rightNumeric) {
        const int width = qAbs(range.leftValue - range.rightValue) + 1;
        if (range.rightValue == 0)
            return QString();
        return QStringLiteral("%1 %2 bits").arg(range.raw).arg(width);
    }

    int leftValue = 0;
    int rightValue = 0;
    if (!evaluateAdditiveExpression(range.left, values, &leftValue)
        || !evaluateAdditiveExpression(range.right, values, &rightValue)) {
        return QString();
    }

    const int width = qAbs(leftValue - rightValue) + 1;
    return QStringLiteral("%1 bits").arg(width);
}

GhostAnnotation makeAnnotation(GhostAnnotationKind kind,
                               GhostAnnotationPlacement placement,
                               const QString& text,
                               int line,
                               int anchorPosition,
                               int anchorLength)
{
    GhostAnnotation annotation;
    annotation.kind = kind;
    annotation.placement = placement;
    annotation.text = text;
    annotation.line = line;
    annotation.anchorPosition = anchorPosition;
    annotation.anchorLength = qMax(anchorLength, 0);
    return annotation;
}

void appendFormalPortAnnotations(const QList<LineInfo>& lines,
                                 const QList<SemanticSymbolRecord>& instPinRecords,
                                 const QList<SemanticSymbolRecord>& allRecords,
                                 QList<GhostAnnotation>* out)
{
    if (!out)
        return;

    for (const SemanticSymbolRecord& record : instPinRecords) {
        if (record.collectorKind != SymbolTaxonomy::CollectorKind::InstPin
            || record.location.startLine <= 0
            || record.location.startLine > lines.size()) {
            continue;
        }

        const QString formalName = formalNameFromInstPin(record.name);
        const QString moduleName =
            !record.type.resolvedTypeName.isEmpty()
                ? record.type.resolvedTypeName
                : record.type.rawTypeText;
        const QList<SemanticSymbolRecord> ports =
            matchingPortRecords(allRecords, formalName, moduleName);
        if (ports.size() != 1)
            continue;

        const QString ghost = formalPortGhostText(ports.first());
        if (ghost.isEmpty())
            continue;

        const LineInfo& line = lines.at(record.location.startLine - 1);
        const QString formalToken = QStringLiteral(".%1").arg(formalName);
        const int column = line.text.indexOf(formalToken);
        if (column < 0)
            continue;

        out->append(makeAnnotation(GhostAnnotationKind::FormalPort,
                                   GhostAnnotationPlacement::RightOfLine,
                                   ghost,
                                   record.location.startLine,
                                   line.startPosition + column,
                                   formalToken.size()));
    }
}

void appendParameterOverrideAnnotations(const QList<LineInfo>& lines,
                                        QList<GhostAnnotation>* out)
{
    if (!out)
        return;

    int parameterParenDepth = 0;
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const LineInfo& line = lines.at(lineIndex);
        const int commentStart = lineCommentStart(line.text);
        const int limit = commentStart >= 0 ? commentStart : line.text.size();
        int pos = 0;
        while (pos < limit) {
            const QChar ch = line.text.at(pos);
            if (parameterParenDepth == 0) {
                if (ch == QLatin1Char('#')) {
                    int open = pos + 1;
                    while (open < limit && line.text.at(open).isSpace())
                        ++open;
                    if (open < limit && line.text.at(open) == QLatin1Char('(')) {
                        parameterParenDepth = 1;
                        pos = open + 1;
                        continue;
                    }
                }
                ++pos;
                continue;
            }

            if (ch == QLatin1Char('(')) {
                ++parameterParenDepth;
                ++pos;
                continue;
            }
            if (ch == QLatin1Char(')')) {
                parameterParenDepth = qMax(0, parameterParenDepth - 1);
                ++pos;
                continue;
            }
            if (ch != QLatin1Char('.')) {
                ++pos;
                continue;
            }

            int nameStart = pos + 1;
            if (nameStart >= limit || !isIdentifierStart(line.text.at(nameStart))) {
                ++pos;
                continue;
            }
            int nameEnd = nameStart + 1;
            while (nameEnd < limit && isIdentifierPart(line.text.at(nameEnd)))
                ++nameEnd;
            int open = nameEnd;
            while (open < limit && line.text.at(open).isSpace())
                ++open;
            if (open >= limit || line.text.at(open) != QLatin1Char('(')) {
                pos = nameEnd;
                continue;
            }
            int valueStart = open + 1;
            while (valueStart < limit && line.text.at(valueStart).isSpace())
                ++valueStart;
            LiteralValue literal;
            int valueEnd = 0;
            if (!parseLiteralAt(line.text, valueStart, &literal, &valueEnd)
                || !literal.exact) {
                pos = nameEnd;
                continue;
            }
            int close = valueEnd;
            while (close < limit && line.text.at(close).isSpace())
                ++close;
            if (close >= limit || line.text.at(close) != QLatin1Char(')')) {
                pos = nameEnd;
                continue;
            }

            out->append(makeAnnotation(
                GhostAnnotationKind::ParameterOverride,
                GhostAnnotationPlacement::RightOfAnchor,
                QStringLiteral("= %1").arg(literal.value),
                lineIndex + 1,
                line.startPosition + valueEnd,
                0));
            pos = close + 1;
        }
    }
}

void appendDeclarationAnnotations(const QList<LineInfo>& lines,
                                  const QList<SemanticSymbolRecord>& records,
                                  const QHash<QString, int>& literalValues,
                                  const QHash<QString, WideValue>& wideValues,
                                  const QHash<QString, UserFunctionDefinition>& functions,
                                  QList<GhostAnnotation>* out)
{
    if (!out)
        return;

    for (const SemanticSymbolRecord& record : records) {
        if (record.location.startLine <= 0
            || record.location.startLine > lines.size()) {
            continue;
        }

        const LineInfo& line = lines.at(record.location.startLine - 1);
        const int nameStart =
            findNameOnLine(line.text, record.name, record.location.startColumn);
        if (nameStart < 0)
            continue;

        const int anchor = line.startPosition + nameStart;
        if (isParameterLikeRecord(record)) {
            const QString valueText =
                parameterExpressionOnLine(line.text, nameStart, record.name.size());
            if (!valueText.isEmpty()) {
                WideValue value;
                if (evaluateWideExpression(valueText, wideValues, functions, &value)
                    && value.valid
                    && (!value.simpleLiteral || value.stringLiteral)) {
                    out->append(makeAnnotation(
                        GhostAnnotationKind::ParameterValue,
                        GhostAnnotationPlacement::RightOfLine,
                        radixDisplayText(value.value,
                                         value.width > 0
                                             ? value.width
                                             : inferredWidthWide(value.value)),
                        record.location.startLine,
                        anchor + record.name.size(),
                        0));
                }
            }
        }

        if (isSignalLikeRecord(record)) {
            const QString widthText =
                widthGhostTextForType(record.type.rawTypeText, literalValues);
            if (!widthText.isEmpty()) {
                out->append(makeAnnotation(
                    GhostAnnotationKind::SignalWidth,
                    GhostAnnotationPlacement::RightOfLine,
                    widthText,
                    record.location.startLine,
                    anchor + record.name.size(),
                    0));
            }
        }

        if (isSignalLikeRecord(record) || isParameterLikeRecord(record)) {
            const int entries =
                numericArrayExtentAfterName(line.text, nameStart, record.name.size());
            if (entries > 0) {
                out->append(makeAnnotation(
                    GhostAnnotationKind::ArraySummary,
                    GhostAnnotationPlacement::RightOfLine,
                    QStringLiteral("%1 entries").arg(entries),
                    record.location.startLine,
                    anchor + record.name.size(),
                    0));
            }
        }
    }
}

void appendEnumValueAnnotations(const QList<LineInfo>& lines,
                                QList<SemanticSymbolRecord> records,
                                QList<GhostAnnotation>* out)
{
    if (!out)
        return;

    records.erase(
        std::remove_if(records.begin(), records.end(),
                       [](const SemanticSymbolRecord& record) {
                           return record.collectorKind
                               != SymbolTaxonomy::CollectorKind::EnumValue;
                       }),
        records.end());
    std::stable_sort(records.begin(), records.end(),
                     [](const SemanticSymbolRecord& lhs,
                        const SemanticSymbolRecord& rhs) {
                         if (lhs.owner.name != rhs.owner.name)
                             return lhs.owner.name < rhs.owner.name;
                         if (lhs.location.fileName != rhs.location.fileName)
                             return lhs.location.fileName < rhs.location.fileName;
                         if (lhs.location.startLine != rhs.location.startLine)
                             return lhs.location.startLine < rhs.location.startLine;
                         return lhs.location.startColumn < rhs.location.startColumn;
                     });

    QString currentGroup;
    int nextValue = 0;
    for (const SemanticSymbolRecord& record : records) {
        const QString group =
            QStringLiteral("%1:%2").arg(record.location.fileName, record.owner.name);
        if (group != currentGroup) {
            currentGroup = group;
            nextValue = 0;
        }
        if (record.location.startLine <= 0
            || record.location.startLine > lines.size()) {
            continue;
        }

        const LineInfo& line = lines.at(record.location.startLine - 1);
        const int nameStart =
            findNameOnLine(line.text, record.name, record.location.startColumn);
        if (nameStart < 0)
            continue;

        int value = nextValue;
        const int valueSegmentStart = nameStart + record.name.size();
        int valueSegmentEnd = line.text.indexOf(QLatin1Char(','),
                                                valueSegmentStart);
        const int enumClose = line.text.indexOf(QLatin1Char('}'),
                                                valueSegmentStart);
        if (valueSegmentEnd < 0
            || (enumClose >= 0 && enumClose < valueSegmentEnd)) {
            valueSegmentEnd = enumClose;
        }
        if (valueSegmentEnd < 0)
            valueSegmentEnd = line.text.size();

        const int equal = line.text.indexOf(QLatin1Char('='),
                                            valueSegmentStart);
        if (equal >= 0) {
            if (equal >= valueSegmentEnd) {
                out->append(makeAnnotation(GhostAnnotationKind::EnumValue,
                                           GhostAnnotationPlacement::RightOfAnchor,
                                           QStringLiteral("= %1").arg(value),
                                           record.location.startLine,
                                           line.startPosition + nameStart
                                               + record.name.size(),
                                           0));
                nextValue = value + 1;
                continue;
            }

            int literalStart = equal + 1;
            while (literalStart < valueSegmentEnd
                   && line.text.at(literalStart).isSpace()) {
                ++literalStart;
            }
            LiteralValue literal;
            int literalEnd = 0;
            if (parseLiteralAt(line.text, literalStart, &literal, &literalEnd)
                && literalEnd <= valueSegmentEnd
                && literal.exact) {
                value = static_cast<int>(literal.value);
            }
        }

        out->append(makeAnnotation(GhostAnnotationKind::EnumValue,
                                   GhostAnnotationPlacement::RightOfAnchor,
                                   QStringLiteral("= %1").arg(value),
                                   record.location.startLine,
                                   line.startPosition + nameStart
                                       + record.name.size(),
                                   0));
        nextValue = value + 1;
    }
}

bool parsePartSelectAt(const QString& line,
                       int openBracket,
                       int* closeBracket,
                       QString* text)
{
    int pos = openBracket + 1;
    while (pos < line.size() && line.at(pos).isSpace())
        ++pos;
    int base = 0;
    int baseEnd = 0;
    LiteralValue baseLiteral;
    if (!parseLiteralAt(line, pos, &baseLiteral, &baseEnd) || !baseLiteral.exact)
        return false;
    base = static_cast<int>(baseLiteral.value);
    pos = baseEnd;
    while (pos < line.size() && line.at(pos).isSpace())
        ++pos;
    if (pos + 1 >= line.size()
        || line.at(pos) != QLatin1Char('+')
        || line.at(pos + 1) != QLatin1Char(':')) {
        return false;
    }
    pos += 2;
    while (pos < line.size() && line.at(pos).isSpace())
        ++pos;
    LiteralValue widthLiteral;
    int widthEnd = 0;
    if (!parseLiteralAt(line, pos, &widthLiteral, &widthEnd) || !widthLiteral.exact)
        return false;
    pos = widthEnd;
    while (pos < line.size() && line.at(pos).isSpace())
        ++pos;
    if (pos >= line.size() || line.at(pos) != QLatin1Char(']'))
        return false;

    const int width = static_cast<int>(widthLiteral.value);
    if (width <= 0)
        return false;
    if (closeBracket)
        *closeBracket = pos;
    if (text)
        *text = QStringLiteral("[%1:%2] %3 bits")
            .arg(base + width - 1)
            .arg(base)
            .arg(width);
    return true;
}

void appendPartSelectAnnotations(const QList<LineInfo>& lines,
                                 QList<GhostAnnotation>* out)
{
    if (!out)
        return;

    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const LineInfo& line = lines.at(lineIndex);
        const int commentStart = lineCommentStart(line.text);
        const int limit = commentStart >= 0 ? commentStart : line.text.size();
        int pos = 0;
        while (pos < limit) {
            const int bracket = line.text.indexOf(QLatin1Char('['), pos);
            if (bracket < 0 || bracket >= limit)
                break;
            int close = 0;
            QString text;
            if (parsePartSelectAt(line.text, bracket, &close, &text)) {
                out->append(makeAnnotation(GhostAnnotationKind::PartSelect,
                                           GhostAnnotationPlacement::RightOfAnchor,
                                           text,
                                           lineIndex + 1,
                                           line.startPosition + close + 1,
                                           0));
                pos = close + 1;
            } else {
                pos = bracket + 1;
            }
        }
    }
}

void appendGenerateLoopAnnotations(const QList<LineInfo>& lines,
                                   QList<GhostAnnotation>* out)
{
    if (!out)
        return;

    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const LineInfo& line = lines.at(lineIndex);
        const int forPos = line.text.indexOf(QStringLiteral("for"));
        if (forPos < 0)
            continue;
        const int less = line.text.indexOf(QLatin1Char('<'), forPos + 3);
        if (less < 0)
            continue;
        int valueStart = less + 1;
        while (valueStart < line.text.size() && line.text.at(valueStart).isSpace())
            ++valueStart;
        LiteralValue literal;
        int valueEnd = 0;
        if (!parseLiteralAt(line.text, valueStart, &literal, &valueEnd)
            || !literal.exact) {
            continue;
        }
        out->append(makeAnnotation(GhostAnnotationKind::GenerateLoop,
                                   GhostAnnotationPlacement::RightOfLine,
                                   QStringLiteral("instances=%1").arg(literal.value),
                                   lineIndex + 1,
                                   line.startPosition + valueEnd,
                                   0));
    }
}

int literalWidthAt(const QString& text, int pos, int* end)
{
    LiteralValue literal;
    int literalEnd = 0;
    if (!parseLiteralAt(text, pos, &literal, &literalEnd) || !literal.exact)
        return -1;
    if (end)
        *end = literalEnd;
    return inferredWidth(literal);
}

int matchingClosingBrace(const QString& line, int openBrace, int limit)
{
    int depth = 0;
    for (int i = openBrace; i < limit; ++i) {
        if (line.at(i) == QLatin1Char('{')) {
            ++depth;
            continue;
        }
        if (line.at(i) != QLatin1Char('}'))
            continue;
        --depth;
        if (depth == 0)
            return i;
    }
    return -1;
}

void appendConcatenationAnnotations(const QList<LineInfo>& lines,
                                    QList<GhostAnnotation>* out)
{
    if (!out)
        return;

    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const LineInfo& line = lines.at(lineIndex);
        int pos = 0;
        while (pos < line.text.size()) {
            const int open = line.text.indexOf(QLatin1Char('{'), pos);
            if (open < 0)
                break;
            const int close = matchingClosingBrace(line.text,
                                                   open,
                                                   line.text.size());
            if (close < 0)
                break;

            int totalWidth = 0;
            bool recognized = false;
            int scan = open + 1;
            LiteralValue repeatLiteral;
            int repeatEnd = 0;
            if (parseLiteralAt(line.text, scan, &repeatLiteral, &repeatEnd)
                && repeatLiteral.exact) {
                int innerOpen = repeatEnd;
                while (innerOpen < close && line.text.at(innerOpen).isSpace())
                    ++innerOpen;
                if (innerOpen < close && line.text.at(innerOpen) == QLatin1Char('{')) {
                    int innerEnd = 0;
                    const int innerWidth =
                        literalWidthAt(line.text, innerOpen + 1, &innerEnd);
                    if (innerWidth > 0 && innerEnd < close) {
                        totalWidth = static_cast<int>(repeatLiteral.value) * innerWidth;
                        recognized = true;
                    }
                }
            }

            if (!recognized) {
                scan = open + 1;
                totalWidth = 0;
                recognized = true;
                while (scan < close) {
                    while (scan < close
                           && (line.text.at(scan).isSpace()
                               || line.text.at(scan) == QLatin1Char(','))) {
                        ++scan;
                    }
                    if (scan >= close)
                        break;
                    int literalEnd = 0;
                    const int width = literalWidthAt(line.text, scan, &literalEnd);
                    if (width <= 0) {
                        recognized = false;
                        break;
                    }
                    totalWidth += width;
                    scan = literalEnd;
                }
            }

            if (recognized && totalWidth > 0) {
                out->append(makeAnnotation(
                    GhostAnnotationKind::ConcatenationWidth,
                    GhostAnnotationPlacement::RightOfAnchor,
                    QStringLiteral("%1 bits").arg(totalWidth),
                    lineIndex + 1,
                    line.startPosition + close + 1,
                    0));
            }
            pos = close + 1;
        }
    }
}

bool startsBefore(const GhostAnnotation& lhs,
                  const GhostAnnotation& rhs)
{
    if (lhs.line != rhs.line)
        return lhs.line < rhs.line;
    if (lhs.anchorPosition != rhs.anchorPosition)
        return lhs.anchorPosition < rhs.anchorPosition;
    return lhs.text < rhs.text;
}

QList<GhostAnnotation> mergeLineTailAnnotations(
    QList<GhostAnnotation> annotations)
{
    std::stable_sort(annotations.begin(), annotations.end(), startsBefore);

    QHash<int, bool> lineHasWidth;
    for (const GhostAnnotation& annotation : annotations) {
        if (annotation.kind == GhostAnnotationKind::SignalWidth)
            lineHasWidth.insert(annotation.line, true);
    }

    QList<GhostAnnotation> merged;
    GhostAnnotation current;
    bool hasCurrent = false;
    QStringList currentParts;
    auto flushCurrent = [&]() {
        if (!hasCurrent)
            return;
        current.text = currentParts.join(QStringLiteral(" | "));
        merged.append(current);
        current = GhostAnnotation();
        hasCurrent = false;
        currentParts.clear();
    };

    for (const GhostAnnotation& annotation : annotations) {
        if (lineHasWidth.value(annotation.line)
            && (annotation.kind == GhostAnnotationKind::ParameterValue
                || annotation.kind == GhostAnnotationKind::ParameterOverride)) {
            continue;
        }

        if (annotation.placement == GhostAnnotationPlacement::LeftOfAnchor) {
            flushCurrent();
            merged.append(annotation);
            continue;
        }

        if (!hasCurrent || current.line != annotation.line) {
            flushCurrent();
            current = annotation;
            current.placement = GhostAnnotationPlacement::RightOfLine;
            currentParts.append(annotation.text);
            hasCurrent = true;
            continue;
        }

        if (!currentParts.contains(annotation.text))
            currentParts.append(annotation.text);
        current.anchorPosition =
            qMin(current.anchorPosition, annotation.anchorPosition);
    }

    flushCurrent();
    std::stable_sort(merged.begin(), merged.end(), startsBefore);
    return merged;
}
}

GhostAnnotationService* GhostAnnotationService::getInstance()
{
    if (!instance)
        instance = std::make_unique<GhostAnnotationService>();
    return instance.get();
}

GhostAnnotationService::GhostAnnotationService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

GhostAnnotationService::~GhostAnnotationService() = default;

void GhostAnnotationService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

SemanticIndex* GhostAnnotationService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

GhostAnnotationReport GhostAnnotationService::annotationsForDocument(
    const GhostAnnotationQuery& query) const
{
    GhostAnnotationReport report;
    if (query.documentText.isEmpty())
        return report;

    const QList<LineInfo> lines = documentLines(query.documentText);
    const QList<SemanticSymbolRecord> fileRecords =
        query.fileName.isEmpty()
            ? QList<SemanticSymbolRecord>()
            : semanticIndex()->getSymbolRecords(query.fileName);
    const QList<SemanticSymbolRecord> allRecords =
        query.fileName.isEmpty()
            ? QList<SemanticSymbolRecord>()
            : semanticIndex()->getSymbolRecords();

    appendFormalPortAnnotations(lines,
                                fileRecords,
                                allRecords,
                                &report.annotations);
    appendParameterOverrideAnnotations(lines, &report.annotations);
    const QHash<QString, UserFunctionDefinition> userFunctions =
        userFunctionsForDocument(lines);
    const QHash<QString, WideValue> wideValues =
        wideValuesForDocument(lines, fileRecords, userFunctions);
    const QHash<QString, int> literalValues =
        literalValuesForDocument(lines, fileRecords);
    appendDeclarationAnnotations(lines,
                                 fileRecords,
                                 literalValues,
                                 wideValues,
                                 userFunctions,
                                 &report.annotations);
    appendEnumValueAnnotations(lines, fileRecords, &report.annotations);
    appendPartSelectAnnotations(lines, &report.annotations);
    appendGenerateLoopAnnotations(lines, &report.annotations);
    appendConcatenationAnnotations(lines, &report.annotations);

    report.annotations.erase(
        std::remove_if(report.annotations.begin(), report.annotations.end(),
                       [](const GhostAnnotation& annotation) {
                           return !annotation.isValid();
                       }),
        report.annotations.end());
    report.annotations = mergeLineTailAnnotations(report.annotations);
    std::stable_sort(report.annotations.begin(),
                     report.annotations.end(),
                     startsBefore);
    return report;
}

GhostNumericLiteralReport GhostAnnotationService::numericLiteralAt(
    const GhostNumericLiteralQuery& query) const
{
    GhostNumericLiteralReport report;
    if (query.documentText.isEmpty() || query.cursorPosition < 0)
        return report;

    const QList<LineInfo> lines = documentLines(query.documentText);
    const int lineIndex = lineIndexForPosition(lines, query.cursorPosition);
    if (lineIndex < 0)
        return report;

    const LineInfo& line = lines.at(lineIndex);
    const int column =
        qBound(0, query.cursorPosition - line.startPosition, line.text.size());
    const int commentStart = lineCommentStart(line.text);
    const int limit = commentStart >= 0 ? commentStart : line.text.size();
    if (column >= limit)
        return report;

    if (positionInsideString(line.text, column)) {
        int quoteStart = -1;
        bool escaped = false;
        for (int i = column; i >= 0; --i) {
            const QChar ch = line.text.at(i);
            if (ch == QLatin1Char('"') && !escaped) {
                quoteStart = i;
                break;
            }
            escaped = ch == QLatin1Char('\\') && !escaped;
            if (ch != QLatin1Char('\\'))
                escaped = false;
        }
        if (quoteStart < 0)
            return report;
        if (line.text.left(quoteStart).trimmed().startsWith(
                QStringLiteral("`include"))) {
            return report;
        }
        WideValue stringValue;
        int stringEnd = 0;
        if (parseStringLiteralAt(line.text, quoteStart, &stringValue, &stringEnd)
            && column >= quoteStart + 1
            && column < stringEnd - 1) {
            report.available = true;
            report.displayText =
                radixDisplayText(stringValue.value, stringValue.width);
            report.startPosition = line.startPosition + quoteStart + 1;
            report.endPosition = line.startPosition + stringEnd - 1;
        }
        return report;
    }

    int pos = 0;
    while (pos < limit) {
        if (pos > 0 && isIdentifierPart(line.text.at(pos - 1))) {
            ++pos;
            continue;
        }
        if (!isDecimalDigit(line.text.at(pos))
            && line.text.at(pos) != QLatin1Char('\'')) {
            ++pos;
            continue;
        }

        if (positionInsideString(line.text, pos)) {
            ++pos;
            continue;
        }

        WideValue literal;
        int end = 0;
        if (!parseWideLiteralAt(line.text, pos, &literal, &end)) {
            ++pos;
            continue;
        }
        if (end < limit && isIdentifierPart(line.text.at(end))) {
            pos = end + 1;
            continue;
        }
        if (column >= pos && column <= end) {
            const QString text =
                radixDisplayText(literal.value,
                                 literal.width > 0
                                     ? literal.width
                                     : inferredWidthWide(literal.value),
                                 literal.sourceBase);
            if (text.isEmpty())
                return report;
            report.available = true;
            report.displayText = text;
            report.startPosition = line.startPosition + pos;
            report.endPosition = line.startPosition + end;
            return report;
        }
        pos = end;
    }

    return report;
}
