#include "tabfileio.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStringList>
#include <QTextStream>

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
        QMessageBox::warning(
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
    const QString& text) const
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QFile::Text)) {
        QMessageBox::warning(
            parent,
            "Warning",
            "Cannot save file: " + file.errorString());
        return false;
    }

    QTextStream out(&file);
    out << text;
    file.close();
    return true;
}
