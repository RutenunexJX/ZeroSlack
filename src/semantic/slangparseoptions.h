#ifndef SLANGPARSEOPTIONS_H
#define SLANGPARSEOPTIONS_H

#include <slang/ast/Compilation.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/util/Bag.h>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <string>

namespace slang_parse_options {

inline QStringList uniqueSortedIncludeDirs(QStringList dirs)
{
    QSet<QString> seen;
    QStringList result;
    for (const QString& dir : std::as_const(dirs)) {
        const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(dir));
        if (clean.isEmpty() || seen.contains(clean))
            continue;
        seen.insert(clean);
        result.append(clean);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

inline QStringList ancestorIncludeDirsForFile(const QString& fileName)
{
    QStringList dirs;
    if (fileName.isEmpty())
        return dirs;

    QDir dir(QFileInfo(fileName).absoluteDir());
    while (!dir.path().isEmpty()) {
        dirs.append(dir.path());
        if (!dir.cdUp())
            break;
    }
    return dirs;
}

inline QStringList effectiveIncludeDirsForFile(const QString& fileName,
                                               const QStringList& includeDirs)
{
    QStringList dirs = includeDirs;
    dirs.append(ancestorIncludeDirsForFile(fileName));
    return uniqueSortedIncludeDirs(dirs);
}

inline QStringList effectiveIncludeDirsForFiles(const QStringList& fileNames,
                                                const QStringList& includeDirs)
{
    QStringList dirs = includeDirs;
    for (const QString& fileName : fileNames)
        dirs.append(ancestorIncludeDirsForFile(fileName));
    return uniqueSortedIncludeDirs(dirs);
}

inline slang::Bag makeSyntaxOptions(const QStringList& includeDirs,
                                    const QHash<QString, QString>& defines)
{
    slang::Bag bag;
    if (includeDirs.isEmpty() && defines.isEmpty())
        return bag;

    auto& options = bag.insertOrGet<slang::parsing::PreprocessorOptions>();

    for (const QString& dir : includeDirs) {
        const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(dir));
        if (!clean.isEmpty())
            options.additionalIncludePaths.emplace_back(clean.toStdString());
    }

    for (auto it = defines.constBegin(); it != defines.constEnd(); ++it) {
        if (it.key().isEmpty())
            continue;
        const QString define = it.value().isEmpty()
            ? it.key()
            : QStringLiteral("%1=%2").arg(it.key(), it.value());
        options.predefines.push_back(define.toStdString());
    }

    return bag;
}

inline slang::Bag makeCompilationOptions()
{
    slang::Bag bag;
    auto& options = bag.insertOrGet<slang::ast::CompilationOptions>();
    options.flags |= slang::ast::CompilationFlags::IgnoreUnknownModules;
    return bag;
}

} // namespace slang_parse_options

#endif // SLANGPARSEOPTIONS_H
