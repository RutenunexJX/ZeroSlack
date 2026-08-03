#include "editorfileidentity.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <filesystem>
#include <system_error>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
#endif

namespace {
int checks = 0;
int failures = 0;

void expect(const char* name, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
}

std::filesystem::path nativePath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(
        QDir::toNativeSeparators(path).toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

bool writeFixture(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly
                   | QIODevice::Text
                   | QIODevice::Truncate)) {
        return false;
    }
    QTextStream(&file) << "module identity_fixture;\nendmodule\n";
    return true;
}

bool createDirectoryAlias(const QString& target,
                          const QString& alias)
{
#ifdef Q_OS_WIN
    QProcess process;
    process.start(
        QStringLiteral("cmd.exe"),
        {
            QStringLiteral("/d"),
            QStringLiteral("/s"),
            QStringLiteral("/c"),
            QStringLiteral("mklink"),
            QStringLiteral("/J"),
            QDir::toNativeSeparators(alias),
            QDir::toNativeSeparators(target),
        });
    return process.waitForFinished(10000)
        && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0
        && QFileInfo(alias).isDir();
#else
    std::error_code error;
    std::filesystem::create_directory_symlink(
        nativePath(target), nativePath(alias), error);
    return !error && QFileInfo(alias).isDir();
#endif
}

std::error_code createFileAlias(const QString& target,
                                const QString& alias)
{
#ifdef Q_OS_WIN
    const QString nativeTarget =
        QDir::toNativeSeparators(target);
    const QString nativeAlias =
        QDir::toNativeSeparators(alias);
    if (CreateSymbolicLinkW(
            reinterpret_cast<LPCWSTR>(nativeAlias.utf16()),
            reinterpret_cast<LPCWSTR>(nativeTarget.utf16()),
            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        return {};
    }
    const DWORD firstError = GetLastError();
    if (firstError == ERROR_INVALID_PARAMETER
        && CreateSymbolicLinkW(
            reinterpret_cast<LPCWSTR>(nativeAlias.utf16()),
            reinterpret_cast<LPCWSTR>(nativeTarget.utf16()),
            0)) {
        return {};
    }
    return std::error_code(
        static_cast<int>(GetLastError()),
        std::system_category());
#else
    std::error_code error;
    std::filesystem::create_symlink(
        nativePath(target), nativePath(alias), error);
    return error;
#endif
}
}

int main()
{
    QTemporaryDir temporary;
    expect("temporary identity fixture is available",
           temporary.isValid());
    if (!temporary.isValid())
        return 1;

    const QString targetDirectory =
        temporary.filePath(QStringLiteral("target"));
    const QString aliasDirectory =
        temporary.filePath(QStringLiteral("alias"));
    expect("target directory is created",
           QDir().mkpath(targetDirectory));

    const QString targetFile =
        QDir(targetDirectory)
            .absoluteFilePath(QStringLiteral("unit.sv"));
    expect("target file is created", writeFixture(targetFile));

    const QString lexicalAlias =
        QDir(targetDirectory)
            .absoluteFilePath(
                QStringLiteral("nested/../unit.sv"));
    expect("lexical aliases share one identity",
           EditorFileIdentity::same(
               targetFile, lexicalAlias));

    const QString fileAlias =
        temporary.filePath(QStringLiteral("unit_alias.sv"));
    const std::error_code linkError =
        createFileAlias(targetFile, fileAlias);
    if (!linkError) {
        expect("file symlink resolves to the target identity",
               EditorFileIdentity::same(
                   targetFile, fileAlias));
    } else {
        std::printf(
            "[SKIP] file symlink creation is unavailable: %s\n",
            linkError.message().c_str());
    }

    if (createDirectoryAlias(
            targetDirectory, aliasDirectory)) {
        const QString junctionFile =
            QDir(aliasDirectory)
                .absoluteFilePath(QStringLiteral("unit.sv"));
        expect("directory symlink or junction resolves file identity",
               EditorFileIdentity::same(
                   targetFile, junctionFile));

        const QString targetFuture =
            QDir(targetDirectory)
                .absoluteFilePath(QStringLiteral("future.sv"));
        const QString aliasFuture =
            QDir(aliasDirectory)
                .absoluteFilePath(QStringLiteral("future.sv"));
        expect("missing leaf below a reparse alias has stable identity",
               EditorFileIdentity::same(
                   targetFuture, aliasFuture));
    } else {
        std::printf(
            "[SKIP] directory symlink or junction creation is unavailable\n");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
