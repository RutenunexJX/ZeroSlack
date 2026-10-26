#include "uidialogs.h"
#include "tabfileio.h"
#include "../common/defaultsourceextensions.h"
#include "workspacesessionstateservice.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>
#include <QThread>

QString TabFileIo::displayName(const QString& fullPath) const
{
    return fullPath.isEmpty() ? "untitled" : QFileInfo(fullPath).fileName();
}

bool TabFileIo::isSystemVerilogFile(const QString& fileName) const
{
    if (fileName.isEmpty())
        return false;

    const QString suffix = QLatin1Char('.') + QFileInfo(fileName).suffix().toLower();
    return SourceFiles::defaultExtensions().contains(suffix);
}

QString TabFileIo::promptOpenFile(QWidget* parent) const
{
    return QFileDialog::getOpenFileName(parent, "open file");
}

QString TabFileIo::resolveSaveFileName(
    QWidget* parent,
    const QString& currentFileName,
    bool forceSaveAs) const
{
    if (!forceSaveAs && !currentFileName.isEmpty() && QFile::exists(currentFileName))
        return currentFileName;

    return QFileDialog::getSaveFileName(
        parent,
        forceSaveAs ? QStringLiteral("save file as ")
                    : QStringLiteral("Save file"));
}

bool TabFileIo::readTextFile(
    QWidget* parent,
    const QString& fileName,
    QString* text) const
{
    const auto source = readFile(parent, fileName);
    if (source.available && text) *text = source.text;
    return source.available;
}

DocumentFileReadResult TabFileIo::readFile(QWidget* parent, const QString& fileName) const
{
    auto result = readDocumentFile(fileName);
    if (!result.available) UiDialogs::warning(parent, "warning", "can not open file:" + result.failureReason);
    return result;
}

bool TabFileIo::writeTextFile(
    QWidget* parent,
    const QString& fileName,
    const QString& text,
    QString* failureReason,
    QByteArray* rawSha256,
    QByteArray* logicalTextSha256,
    const std::function<bool(QString*)>& revalidateOverwrite,
    const DocumentFileFormat& format,
    const QJsonObject* templateMetadata) const
{
    Q_UNUSED(parent);
    if (failureReason)
        failureReason->clear();
    if (rawSha256)
        rawSha256->clear();
    if (logicalTextSha256)
        logicalTextSha256->clear();

    const QByteArray logicalDigest = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
    const QByteArray logicalBytes = encodeDocumentText(text, format);
    const QByteArray rawDigest = QCryptographicHash::hash(
        logicalBytes, QCryptographicHash::Sha256);

    QString expectedDiskRevision = QStringLiteral("missing");
    if (templateMetadata && QFileInfo::exists(fileName)) {
        const auto source = readDocumentFile(fileName);
        if (!source.available) {
            if (failureReason) *failureReason = source.failureReason;
            return false;
        }
        expectedDiskRevision = QString::fromLatin1(QCryptographicHash::hash(source.rawBytes, QCryptographicHash::Sha256).toHex());
    }

    QElapsedTimer recoveryTimer;
    for (int attempt = 0; ; ++attempt) {
        if (revalidateOverwrite && !revalidateOverwrite(failureReason)) return false;
        QFileDevice::FileError error = QFileDevice::NoError;
        if (templateMetadata) {
            if (WorkspaceSessionStateService().saveDocumentWithTemplates(fileName, logicalBytes,
                    expectedDiskRevision, *templateMetadata, failureReason, revalidateOverwrite, &error)) break;
        } else {
        QSaveFile file(fileName);
        const auto reportFailure = [&](const char* stage) {
            const auto error = file.error();
            const QString detail = file.errorString();
            qWarning() << "atomic save failure" << "stage" << stage
                       << "error" << int(error) << "path" << fileName
                       << "detail" << detail;
            if (failureReason)
                *failureReason = QStringLiteral("Cannot save file: %1").arg(detail);
            return false;
        };
        if (!file.open(QIODevice::WriteOnly))
            return reportFailure("open");

        qsizetype written = 0;
        while (written < logicalBytes.size()) {
            const qint64 chunk = file.write(logicalBytes.constData() + written,
                                           logicalBytes.size() - written);
            if (chunk <= 0) break;
            written += chunk;
        }
        if (written != logicalBytes.size())
            return reportFailure("write");
        if (revalidateOverwrite && !revalidateOverwrite(failureReason)) {
            file.cancelWriting();
            return false;
        }
        if (file.commit()) break;
        error = file.error();
        reportFailure("commit");
        }
#ifdef Q_OS_WIN
        // Windows readers can allow reading/writing but deny the atomic rename.
        // Recover only that stage, with a fresh conflict check before each new
        // atomic attempt. No guard (e.g. Save As), no automatic overwrite retry.
        constexpr int maxAttempts = 4;
        constexpr int retryIntervalMs = 25;
        constexpr int recoveryBudgetMs = 150;
        if (error == QFileDevice::RenameError && revalidateOverwrite && attempt + 1 < maxAttempts) {
            if (!recoveryTimer.isValid()) recoveryTimer.start();
            if (recoveryTimer.elapsed() + retryIntervalMs > recoveryBudgetMs) return false;
            QThread::msleep(retryIntervalMs);
            QString conflictReason;
            if (!revalidateOverwrite(&conflictReason)) {
                if (failureReason && !conflictReason.isEmpty()) *failureReason = conflictReason;
                return false;
            }
            if (recoveryTimer.elapsed() >= recoveryBudgetMs) return false;
            continue;
        }
#endif
        return false;
    }
    if (failureReason)
        failureReason->clear();
    if (rawSha256)
        *rawSha256 = rawDigest;
    if (logicalTextSha256)
        *logicalTextSha256 = logicalDigest;
    return true;
}
