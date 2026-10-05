#ifndef TABFILEIO_H
#define TABFILEIO_H

#include <QByteArray>
#include <QString>
#include <functional>
#include <zeroslack/documents/documentfileread.h>

class QWidget;

class TabFileIo
{
public:
    QString displayName(const QString& fullPath) const;
    bool isSystemVerilogFile(const QString& fileName) const;
    QString promptOpenFile(QWidget* parent) const;
    QString resolveSaveFileName(
        QWidget* parent,
        const QString& currentFileName,
        bool forceSaveAs) const;
    bool readTextFile(
        QWidget* parent,
        const QString& fileName,
        QString* text) const;
    DocumentFileReadResult readFile(QWidget* parent, const QString& fileName) const;
    bool writeTextFile(
        QWidget* parent,
        const QString& fileName,
        const QString& text,
        QString* failureReason = nullptr,
        QByteArray* rawSha256 = nullptr,
        QByteArray* logicalTextSha256 = nullptr,
        const std::function<bool(QString*)>& revalidateOverwrite = {},
        const DocumentFileFormat& format = {}) const;
};

#endif // TABFILEIO_H
