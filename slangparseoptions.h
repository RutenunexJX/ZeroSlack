#ifndef SLANGPARSEOPTIONS_H
#define SLANGPARSEOPTIONS_H

#include <slang/ast/Compilation.h>
#include <slang/parsing/Preprocessor.h>
#include <slang/util/Bag.h>

#include <QDir>
#include <QHash>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <string>

namespace slang_parse_options {

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
