#ifndef SLANGPARSEOPTIONS_H
#define SLANGPARSEOPTIONS_H

#include <slang/ast/Compilation.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/parsing/Parser.h>
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

inline QStringList uniqueOrderedIncludeDirs(QStringList dirs)
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
    // Include search order is observable when two directories contain the
    // same header. Keep configured paths before inferred ancestor paths.
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
    return uniqueOrderedIncludeDirs(dirs);
}

inline QStringList effectiveIncludeDirsForFiles(const QStringList& fileNames,
                                                const QStringList& includeDirs)
{
    QStringList dirs = includeDirs;
    for (const QString& fileName : fileNames)
        dirs.append(ancestorIncludeDirsForFile(fileName));
    return uniqueOrderedIncludeDirs(dirs);
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

    auto defineNames = defines.keys();
    defineNames.sort();
    for (const auto& name : defineNames) {
        if (name.isEmpty())
            continue;
        const QString define = defines.value(name).isEmpty()
            ? name
            : QStringLiteral("%1=%2").arg(name, defines.value(name));
        options.predefines.push_back(define.toStdString());
    }

    return bag;
}

inline slang::Bag makeUnitSyntaxOptions(const QString& file, const QStringList& includeDirs,
                                       const QHash<QString, QString>& defines)
{
    auto bag = makeSyntaxOptions(includeDirs, defines);
    if (QFileInfo(file).suffix().compare(QStringLiteral("v"), Qt::CaseInsensitive) == 0) {
        bag.insertOrGet<slang::parsing::PreprocessorOptions>().languageVersion = slang::LanguageVersion::v1364_2005;
        bag.insertOrGet<slang::parsing::LexerOptions>().languageVersion = slang::LanguageVersion::v1364_2005;
        bag.insertOrGet<slang::parsing::ParserOptions>().languageVersion = slang::LanguageVersion::v1364_2005;
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
