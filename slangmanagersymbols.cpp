#include "slangmanager.h"
#include "slangparseoptions.h"
#include "slangsymbolcollector.h"
#include "svmacrosemantics.h"

#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QSet>
#include <QTextStream>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace slang::ast;

namespace {
QString absoluteSourcePath(const QString& filePath)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(
        QFileInfo(filePath).absoluteFilePath()));
}

QString sourceLookupKey(const QString& filePath)
{
    QString result = absoluteSourcePath(filePath);
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

bool readNormalizedSourceFile(const QString& filePath, QString* content)
{
    if (!content)
        return false;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QFile::Text))
        return false;
    QTextStream stream(&file);
    *content = stream.readAll();
    return stream.status() == QTextStream::Ok;
}
}

QList<SemanticSymbolRecord> SlangManager::extractSymbolRecords(
    const QString& fileName,
    const QString& content,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines,
    QList<EffectiveValueFact>* effectiveValueFacts)
{
    QList<SemanticSymbolRecord> result;
    if (effectiveValueFacts)
        effectiveValueFacts->clear();
    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        const QStringList effectiveIncludeDirs =
            slang_parse_options::effectiveIncludeDirsForFile(fileName, includeDirs);
        slang::SourceManager sourceManager;
        std::shared_ptr<slang::syntax::SyntaxTree> tree;
        if (effectiveIncludeDirs.isEmpty() && defines.isEmpty()) {
            tree = slang::syntax::SyntaxTree::fromText(
                std::string_view(src),
                sourceManager,
                std::string_view(nameStr),
                std::string_view{});
        } else {
            slang::Bag syntaxOptions =
                slang_parse_options::makeSyntaxOptions(effectiveIncludeDirs, defines);
            tree = slang::syntax::SyntaxTree::fromText(
                std::string_view(src),
                sourceManager,
                std::string_view(nameStr),
                std::string_view(nameStr),
                syntaxOptions);
        }

        if (!tree)
            return result;

        slang::Bag compilationOptions =
            slang_parse_options::makeCompilationOptions();
        Compilation compilation(compilationOptions);
        compilation.addSyntaxTree(tree);

        slang_symbols::collectSymbolRecords(compilation,
                                            result,
                                            nullptr,
                                            effectiveValueFacts);
        result.append(
            SvMacroSemantics::collectMacroDefinitionRecords(fileName, content));
    } catch (const std::exception& error) {
        qWarning().noquote()
            << QStringLiteral("Slang symbol extraction failed for %1: %2")
                   .arg(fileName, QString::fromUtf8(error.what()));
        result.clear();
        if (effectiveValueFacts)
            effectiveValueFacts->clear();
    } catch (...) {
        qWarning().noquote()
            << QStringLiteral("Slang symbol extraction failed for %1 with an unknown exception.")
                   .arg(fileName);
        result.clear();
        if (effectiveValueFacts)
            effectiveValueFacts->clear();
    }
    return result;
}

QList<SemanticSymbolRecord> SlangManager::extractWorkspaceSymbolRecords(
    const QStringList& filePaths,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines,
    std::function<bool()> isCancelled,
    QList<EffectiveValueFact>* effectiveValueFacts,
    QHash<QString, QString>* analyzedFileContents)
{
    if (effectiveValueFacts)
        effectiveValueFacts->clear();
    if (analyzedFileContents)
        analyzedFileContents->clear();
    if (filePaths.isEmpty())
        return {};
    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    if (cancelled())
        return {};

    QHash<QString, QString> contents;
    QStringList orderedPaths;
    orderedPaths.reserve(filePaths.size());
    for (const QString& filePath : filePaths) {
        if (cancelled())
            return {};
        const QString absolutePath = absoluteSourcePath(filePath);
        QString content;
        if (absolutePath.isEmpty()
            || !readNormalizedSourceFile(absolutePath, &content)) {
            if (analyzedFileContents)
                analyzedFileContents->clear();
            return {};
        }
        contents.insert(absolutePath, content);
        orderedPaths.append(absolutePath);
    }

    QList<EffectiveValueFact> facts;
    QList<SemanticSymbolRecord> result =
        extractOverlayWorkspaceSymbolRecords(
            contents,
            includeDirs,
            defines,
            isCancelled,
            effectiveValueFacts ? &facts : nullptr,
            orderedPaths);
    if (cancelled()) {
        if (effectiveValueFacts)
            effectiveValueFacts->clear();
        if (analyzedFileContents)
            analyzedFileContents->clear();
        return {};
    }
    if (effectiveValueFacts)
        *effectiveValueFacts = std::move(facts);
    if (analyzedFileContents)
        *analyzedFileContents = std::move(contents);
    return result;
}

QList<SemanticSymbolRecord>
SlangManager::extractOverlayWorkspaceSymbolRecords(
    const QHash<QString, QString>& fileContents,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines,
    std::function<bool()> isCancelled,
    QList<EffectiveValueFact>* effectiveValueFacts,
    const QStringList& orderedFilePaths)
{
    QList<SemanticSymbolRecord> result;
    if (effectiveValueFacts)
        effectiveValueFacts->clear();
    if (fileContents.isEmpty())
        return result;
    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    if (cancelled())
        return result;

    try {
        QHash<QString, QString> contentsByKey;
        QHash<QString, QString> pathsByKey;
        for (auto it = fileContents.constBegin();
             it != fileContents.constEnd();
             ++it) {
            const QString path = absoluteSourcePath(it.key());
            const QString key = sourceLookupKey(path);
            if (key.isEmpty())
                continue;
            pathsByKey.insert(key, path);
            contentsByKey.insert(key, it.value());
        }

        QStringList fileNames;
        QSet<QString> added;
        for (const QString& requestedPath : orderedFilePaths) {
            const QString key = sourceLookupKey(requestedPath);
            if (key.isEmpty() || added.contains(key)
                || !contentsByKey.contains(key)) {
                continue;
            }
            added.insert(key);
            fileNames.append(pathsByKey.value(key));
        }
        QStringList remainingKeys = contentsByKey.keys();
        remainingKeys.sort(Qt::CaseInsensitive);
        for (const QString& key : std::as_const(remainingKeys)) {
            if (added.contains(key))
                continue;
            added.insert(key);
            fileNames.append(pathsByKey.value(key));
        }
        if (fileNames.isEmpty())
            return {};

        const QStringList effectiveIncludeDirs =
            slang_parse_options::effectiveIncludeDirsForFiles(
                fileNames, includeDirs);
        const slang::Bag syntaxOptions =
            slang_parse_options::makeSyntaxOptions(effectiveIncludeDirs,
                                                   defines);
        slang::SourceManager sourceManager;
        // Preload every workspace source into one SourceManager, then parse
        // the ordered list as one compilation unit. This is the in-memory
        // equivalent of fromFiles(): macro state and source ordering are
        // identical for disk and unsaved-overlay analysis.
        sourceManager.setDisableProximatePaths(true);
        std::vector<std::string> pathStrings;
        std::vector<slang::SourceBuffer> sourceBuffers;
        pathStrings.reserve(static_cast<std::size_t>(fileNames.size()));
        sourceBuffers.reserve(static_cast<std::size_t>(fileNames.size()));
        for (const QString& fileName : std::as_const(fileNames)) {
            if (cancelled())
                return {};
            const QString key = sourceLookupKey(fileName);
            const QByteArray sourceBytes =
                contentsByKey.value(key).toUtf8();
            pathStrings.push_back(fileName.toUtf8().toStdString());
            slang::SourceBuffer sourceBuffer = sourceManager.assignText(
                std::string_view(pathStrings.back()),
                std::string_view(sourceBytes.constData(),
                                 static_cast<std::size_t>(sourceBytes.size())));
            // disableProximatePaths keeps the in-memory cache key stable on
            // Windows, but Slang then exposes only the basename as the source
            // name. Attach the absolute path to the exact buffer that will be
            // parsed so semantic records retain an unambiguous file identity.
            sourceManager.addLineDirective(
                slang::SourceLocation(sourceBuffer.id, 0),
                2,
                std::string_view(pathStrings.back()),
                0);
            sourceBuffers.push_back(sourceBuffer);
        }
        if (sourceBuffers.empty() || cancelled())
            return {};

        std::shared_ptr<slang::syntax::SyntaxTree> tree =
            slang::syntax::SyntaxTree::fromBuffers(
                sourceBuffers, sourceManager, syntaxOptions);
        if (!tree)
            return {};

        slang::Bag compilationOptions =
            slang_parse_options::makeCompilationOptions();
        Compilation compilation(compilationOptions);
        compilation.addSyntaxTree(tree);
        QList<EffectiveValueFact> collectedFacts;
        slang_symbols::collectSymbolRecords(compilation,
                                            result,
                                            isCancelled,
                                            effectiveValueFacts
                                                ? &collectedFacts
                                                : nullptr);
        if (cancelled()) {
            result.clear();
            return {};
        }

        for (const QString& fileName : std::as_const(fileNames)) {
            if (cancelled())
                return {};
            const QString key = sourceLookupKey(fileName);
            result.append(SvMacroSemantics::collectMacroDefinitionRecords(
                fileName,
                contentsByKey.value(key)));
        }
        if (effectiveValueFacts)
            *effectiveValueFacts = std::move(collectedFacts);
    } catch (const std::exception& error) {
        qWarning().noquote()
            << QStringLiteral("Slang workspace symbol extraction failed: %1")
                   .arg(QString::fromUtf8(error.what()));
        result.clear();
        if (effectiveValueFacts)
            effectiveValueFacts->clear();
    } catch (...) {
        qWarning().noquote()
            << QStringLiteral("Slang workspace symbol extraction failed with an unknown exception.");
        result.clear();
        if (effectiveValueFacts)
            effectiveValueFacts->clear();
    }
    return result;
}
