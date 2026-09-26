#include "uidialogs.h"
#include "tabfileio.h"

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

    static const QStringList svExtensions = {
        "sv",
        "v",
        "vh",
        "svh",
        "vp",
        "svp"};
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return svExtensions.contains(suffix);
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
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QFile::Text)) {
        UiDialogs::warning(
            parent,
            "warning",
            "can not open file:" + file.errorString());
        return false;
    }

    QTextStream in(&file);
    if (text)
        *text = in.readAll();
    file.close();
    return true;
}

bool TabFileIo::writeTextFile(
    QWidget* parent,
    const QString& fileName,
    const QString& text,
    QString* failureReason,
    QByteArray* rawSha256,
    QByteArray* logicalTextSha256,
    const std::function<bool(QString*)>& revalidateOverwrite) const
{
    Q_UNUSED(parent);
    if (failureReason)
        failureReason->clear();
    if (rawSha256)
        rawSha256->clear();
    if (logicalTextSha256)
        logicalTextSha256->clear();

    QByteArray logicalBytes = text.toUtf8();
    const QByteArray logicalDigest = QCryptographicHash::hash(
        logicalBytes, QCryptographicHash::Sha256);
#ifdef Q_OS_WIN
    // Match QIODevice::Text's native newline conversion while retaining the
    // exact bytes for the crash-recovery baseline digest.
    logicalBytes.replace("\n", "\r\n");
#endif
    const QByteArray rawDigest = QCryptographicHash::hash(
        logicalBytes, QCryptographicHash::Sha256);

    QElapsedTimer recoveryTimer;
    for (int attempt = 0; ; ++attempt) {
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
        if (file.commit()) break;
        const auto error = file.error();
        reportFailure("commit");
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
