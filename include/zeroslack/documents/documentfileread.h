#pragma once
#include <zeroslack/documents/documentsapi.h>
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringConverter>
#include <cstdint>

struct DocumentFileFormat {
    QStringConverter::Encoding encoding = QStringConverter::Utf8;
    bool byteOrderMark = false;
#ifdef Q_OS_WIN
    QString lineEnding = QStringLiteral("\r\n");
#else
    QString lineEnding = QStringLiteral("\n");
#endif
};

// One immutable read generation, shared by text, disk baseline and watchers.
struct ZEROSLACK_DOCUMENTS_API DocumentFileReadResult {
    QString fileName;
    QByteArray rawBytes;
    QString text;
    DocumentFileFormat format;
    QByteArray rawSha256;
    QByteArray logicalSha256;
    QDateTime modifiedUtc;
    qint64 size = -1;
    bool readOnly = false;
    bool available = false;
    QString failureReason;
    bool matchesDiskState() const;
};

struct DocumentFileReadMetrics { std::uint64_t reads = 0, bytes = 0; };
ZEROSLACK_DOCUMENTS_API DocumentFileReadResult readDocumentFile(const QString& fileName);
ZEROSLACK_DOCUMENTS_API DocumentFileReadMetrics documentFileReadMetricsForTest();
ZEROSLACK_DOCUMENTS_API void resetDocumentFileReadMetricsForTest();
ZEROSLACK_DOCUMENTS_API QByteArray encodeDocumentText(const QString& text, const DocumentFileFormat& format);
