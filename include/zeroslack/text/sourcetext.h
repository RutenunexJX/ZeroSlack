#pragma once

#include <QByteArray>
#include <QString>
#include <QTextStream>

// Shared logical-source boundary for disk documents, semantic capture and CLI
// snippets. Byte identity is kept separately by each I/O owner.
struct DecodedSourceText {
    QString text;
    QStringConverter::Encoding encoding = QStringConverter::Utf8;
#ifdef Q_OS_WIN
    QString lineEnding = QStringLiteral("\r\n");
#else
    QString lineEnding = QStringLiteral("\n");
#endif
    bool byteOrderMark = false;
    bool valid = false;
};

inline DecodedSourceText decodeSourceText(const QByteArray& raw)
{
    DecodedSourceText result;
    QTextStream stream(raw);
    result.text = stream.readAll();
    result.encoding = stream.encoding();
    result.valid = stream.status() == QTextStream::Ok;
    result.byteOrderMark = raw.startsWith(QByteArray::fromHex("efbbbf"))
        || raw.startsWith(QByteArray::fromHex("fffe")) || raw.startsWith(QByteArray::fromHex("feff"))
        || raw.startsWith(QByteArray::fromHex("0000feff"));
    if (result.text.contains(QStringLiteral("\r\n"))) result.lineEnding = QStringLiteral("\r\n");
    else if (result.text.contains(QLatin1Char('\n'))) result.lineEnding = QStringLiteral("\n");
    else if (result.text.contains(QLatin1Char('\r'))) result.lineEnding = QStringLiteral("\r");
    result.text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    result.text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    result.text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    result.text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    result.text.replace(QChar::Nbsp, QLatin1Char(' '));
    return result;
}
