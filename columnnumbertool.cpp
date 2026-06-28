#include "columnnumbertool.h"

#include <QtGlobal>

namespace {
QString trimmedNumberText(const QString& text)
{
    return text.trimmed();
}

int baseRadix(ColumnNumberBase base)
{
    switch (base) {
    case ColumnNumberBase::Dec:
        return 10;
    case ColumnNumberBase::Hex:
        return 16;
    case ColumnNumberBase::Bin:
        return 2;
    }
    return 10;
}

QChar svBaseChar(ColumnNumberBase base)
{
    switch (base) {
    case ColumnNumberBase::Dec:
        return QLatin1Char('d');
    case ColumnNumberBase::Hex:
        return QLatin1Char('h');
    case ColumnNumberBase::Bin:
        return QLatin1Char('b');
    }
    return QLatin1Char('d');
}

bool allDigits(const QString& text)
{
    if (text.isEmpty())
        return false;
    for (const QChar ch : text) {
        if (!ch.isDigit())
            return false;
    }
    return true;
}

bool allBinaryDigits(const QString& text)
{
    if (text.isEmpty())
        return false;
    for (const QChar ch : text) {
        if (ch != QLatin1Char('0') && ch != QLatin1Char('1'))
            return false;
    }
    return true;
}

bool allHexDigits(const QString& text)
{
    if (text.isEmpty())
        return false;
    for (const QChar ch : text) {
        if (!ch.isDigit()
            && (ch.toLower() < QLatin1Char('a')
                || ch.toLower() > QLatin1Char('f'))) {
            return false;
        }
    }
    return true;
}

bool hasLowerHex(const QString& text)
{
    for (const QChar ch : text) {
        if (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))
            return true;
    }
    return false;
}

ColumnNumberBase baseFromSvChar(QChar ch, bool* ok)
{
    const QChar lower = ch.toLower();
    if (lower == QLatin1Char('d')) {
        if (ok)
            *ok = true;
        return ColumnNumberBase::Dec;
    }
    if (lower == QLatin1Char('h')) {
        if (ok)
            *ok = true;
        return ColumnNumberBase::Hex;
    }
    if (lower == QLatin1Char('b')) {
        if (ok)
            *ok = true;
        return ColumnNumberBase::Bin;
    }
    if (ok)
        *ok = false;
    return ColumnNumberBase::Dec;
}

bool bodyMatchesBase(const QString& body, ColumnNumberBase base)
{
    switch (base) {
    case ColumnNumberBase::Dec:
        return allDigits(body);
    case ColumnNumberBase::Hex:
        return allHexDigits(body);
    case ColumnNumberBase::Bin:
        return allBinaryDigits(body);
    }
    return false;
}

void applyBodyShape(ColumnNumberConfig* config, const QString& body)
{
    if (!config)
        return;
    if (body.size() > 1 && body.startsWith(QLatin1Char('0'))) {
        config->fixedDigitWidth = true;
        config->digitWidth = body.size();
        config->pad = ColumnNumberPad::Zero;
    }
    if (config->base == ColumnNumberBase::Hex)
        config->uppercaseHex = !hasLowerHex(body);
}

bool setStartFromBody(ColumnNumberConfig* config, const QString& body)
{
    if (!config || !bodyMatchesBase(body, config->base))
        return false;
    bool ok = false;
    const qint64 value = body.toLongLong(&ok, baseRadix(config->base));
    if (!ok)
        return false;
    config->start = value;
    applyBodyShape(config, body);
    return true;
}
} // namespace

ColumnNumberConfig defaultColumnNumberConfig()
{
    return ColumnNumberConfig();
}

ColumnNumberConfig inferColumnNumberConfig(const QString& text)
{
    ColumnNumberConfig config = defaultColumnNumberConfig();
    const QString sample = trimmedNumberText(text);
    if (sample.isEmpty())
        return config;

    const int quote = sample.indexOf(QLatin1Char('\''));
    if (quote >= 0 && quote + 2 <= sample.size()) {
        const QString widthText = sample.left(quote);
        bool baseOk = false;
        const ColumnNumberBase parsedBase =
            baseFromSvChar(sample.at(quote + 1), &baseOk);
        const QString body = sample.mid(quote + 2);
        if (baseOk && bodyMatchesBase(body, parsedBase)
            && (widthText.isEmpty() || allDigits(widthText))) {
            config.base = parsedBase;
            config.style = widthText.isEmpty()
                ? ColumnNumberStyle::SvUnsized
                : ColumnNumberStyle::SvSized;
            if (!widthText.isEmpty())
                config.bitWidth = qMax(1, widthText.toInt());
            setStartFromBody(&config, body);
            return config;
        }
    }

    if (sample.size() > 2
        && sample.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        const QString body = sample.mid(2);
        if (allHexDigits(body)) {
            config.base = ColumnNumberBase::Hex;
            config.style = ColumnNumberStyle::CLike;
            setStartFromBody(&config, body);
            return config;
        }
    }

    if (sample.size() > 2
        && sample.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) {
        const QString body = sample.mid(2);
        if (allBinaryDigits(body)) {
            config.base = ColumnNumberBase::Bin;
            config.style = ColumnNumberStyle::CLike;
            setStartFromBody(&config, body);
            return config;
        }
    }

    if (allDigits(sample)) {
        config.base = ColumnNumberBase::Dec;
        config.style = ColumnNumberStyle::Plain;
        setStartFromBody(&config, sample);
        return config;
    }

    return config;
}

QString formatColumnNumber(qint64 value, const ColumnNumberConfig& config)
{
    const int radix = baseRadix(config.base);
    QString body = QString::number(value, radix);
    if (config.base == ColumnNumberBase::Hex) {
        body = config.uppercaseHex ? body.toUpper() : body.toLower();
    }

    if (config.fixedDigitWidth && config.digitWidth > 0) {
        const QChar fill = config.pad == ColumnNumberPad::Zero
            ? QLatin1Char('0')
            : config.pad == ColumnNumberPad::Space
                ? QLatin1Char(' ')
                : QLatin1Char('\0');
        if (!fill.isNull() && body.size() < config.digitWidth)
            body = QString(config.digitWidth - body.size(), fill) + body;
    }

    switch (config.style) {
    case ColumnNumberStyle::Plain:
        return body;
    case ColumnNumberStyle::CLike:
        if (config.base == ColumnNumberBase::Dec)
            return body;
        return (config.base == ColumnNumberBase::Hex
                    ? QStringLiteral("0x")
                    : QStringLiteral("0b"))
            + body;
    case ColumnNumberStyle::SvUnsized:
        return QStringLiteral("'%1%2")
            .arg(QString(svBaseChar(config.base)), body);
    case ColumnNumberStyle::SvSized:
        return QStringLiteral("%1'%2%3")
            .arg(qMax(1, config.bitWidth))
            .arg(QString(svBaseChar(config.base)), body);
    }
    return body;
}

QStringList previewColumnNumbers(const ColumnNumberConfig& config,
                                 int lineCount)
{
    QStringList rows;
    if (lineCount <= 0)
        return rows;

    rows.reserve(lineCount);
    const int repeat = qMax(1, config.repeat);
    const qint64 step = qMax<qint64>(0, config.step);
    const qint64 direction =
        config.direction == ColumnNumberDirection::Down ? -1 : 1;
    for (int row = 0; row < lineCount; ++row) {
        const qint64 value =
            config.start + direction * step * (row / repeat);
        rows.append(formatColumnNumber(value, config));
    }
    return rows;
}
