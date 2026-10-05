#include <zeroslack/documents/documentfileread.h>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <atomic>

namespace { std::atomic_uint64_t reads{0}, bytesRead{0}; }

bool DocumentFileReadResult::matchesDiskState() const
{
    const QFileInfo file(fileName);
    return available && file.isFile() && file.size() == size
        && file.lastModified().toUTC() == modifiedUtc;
}

DocumentFileReadResult readDocumentFile(const QString& fileName)
{
    DocumentFileReadResult result;
    result.fileName = QFileInfo(fileName).absoluteFilePath();
    const QFileInfo before(result.fileName);
    QFile file(result.fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        result.failureReason = file.errorString();
        return result;
    }
    result.rawBytes = file.readAll();
    ++reads; bytesRead += result.rawBytes.size();
    if (file.error() != QFileDevice::NoError) {
        result.failureReason = file.errorString();
        return result;
    }
    const QFileInfo after(result.fileName);
    if (before.size() != after.size() || after.size() != result.rawBytes.size()
        || before.lastModified() != after.lastModified()) {
        result.failureReason = QStringLiteral("File changed while it was being read.");
        return result;
    }
    QTextStream stream(result.rawBytes);
    result.text = stream.readAll();
    result.format.encoding = stream.encoding();
    if (stream.status() != QTextStream::Ok) {
        result.failureReason = QStringLiteral("Cannot decode the complete file.");
        return result;
    }
    const auto& raw = result.rawBytes;
    result.format.byteOrderMark = raw.startsWith(QByteArray::fromHex("efbbbf"))
        || raw.startsWith(QByteArray::fromHex("fffe")) || raw.startsWith(QByteArray::fromHex("feff"))
        || raw.startsWith(QByteArray::fromHex("0000feff"));
    if (result.text.contains(QStringLiteral("\r\n"))) result.format.lineEnding = QStringLiteral("\r\n");
    else if (result.text.contains(QLatin1Char('\n'))) result.format.lineEnding = QStringLiteral("\n");
    else if (result.text.contains(QLatin1Char('\r'))) result.format.lineEnding = QStringLiteral("\r");
    result.text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    result.text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    // Match QTextDocument::toPlainText for unopened and already opened buffers.
    result.text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    result.text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    result.text.replace(QChar::Nbsp, QLatin1Char(' '));
    result.rawSha256 = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
    result.logicalSha256 = QCryptographicHash::hash(result.text.toUtf8(), QCryptographicHash::Sha256);
    result.modifiedUtc = after.lastModified().toUTC();
    result.size = after.size(); result.readOnly = !after.isWritable(); result.available = true;
    return result;
}

QByteArray encodeDocumentText(const QString& text, const DocumentFileFormat& format)
{
    QString serialized = text;
    if (format.lineEnding != QStringLiteral("\n")) serialized.replace(QStringLiteral("\n"), format.lineEnding);
    QStringEncoder encoder(format.encoding, format.byteOrderMark ? QStringConverter::Flag::WriteBom : QStringConverter::Flag::Default);
    return encoder(serialized);
}

DocumentFileReadMetrics documentFileReadMetricsForTest() { return {reads.load(), bytesRead.load()}; }
void resetDocumentFileReadMetricsForTest() { reads = 0; bytesRead = 0; }
