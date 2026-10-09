#include "preparation.h"
#include "analyzer.h"
#include <QDir>
#include <QFile>

namespace simdock {
std::shared_ptr<const PreparedStimulus> prepareStimulus(
    const QString& root, const Project& project, const std::shared_ptr<const PreparedStimulus>& previous,
    SourceCache* cache, const std::atomic_bool* cancelled, QString* error)
{
    error->clear();
    const auto stopped = [cancelled] { return cancelled && cancelled->load(); };
    auto next = std::make_shared<PreparedStimulus>();
    next->scan.root = root;
    if (cache && cache->root != root) { cache->files.clear(); cache->root = root; }
    for (const auto& path : project.sources) {
        if (stopped()) { *error = QStringLiteral("Preparation cancelled."); return {}; }
        QFile input(QDir(root).filePath(path));
        if (!insideWorkspace(root, input.fileName()) || !input.open(QIODevice::ReadOnly)
            || input.size() > 8 * 1024 * 1024) {
            *error = QStringLiteral("Could not read stimulus source: %1").arg(path); return {};
        }
        const auto bytes = input.readAll();
        if (input.error() != QFileDevice::NoError) { *error = input.errorString(); return {}; }
        const auto* cached = cache ? cache->files.object(path) : nullptr;
        auto source = cached && cached->content == bytes ? *cached : analyzeSource(bytes, path);
        if (cache && (!cached || cached->content != bytes))
            cache->files.insert(path, new SourceFile(source), int(qMax<qsizetype>(1, (bytes.size() + 1023) / 1024)));
        next->scan.files << source;
        if (path == project.dutFile)
            for (const auto& module : source.modules)
                if (module.name == project.dutName) next->module = module;
    }
    if (next->module.name.isEmpty()) {
        *error = QStringLiteral("The DUT is no longer available. Rescan the workspace."); return {};
    }
    bool unchanged = previous && previous->scan.root == root
        && previous->module.name == project.dutName && previous->module.file == project.dutFile
        && previous->scan.files.size() == next->scan.files.size();
    if (unchanged) for (qsizetype i = 0; i < next->scan.files.size(); ++i) {
        const auto& a = previous->scan.files.at(i);
        const auto& b = next->scan.files.at(i);
        if (a.path != b.path || a.content != b.content) { unchanged = false; break; }
    }
    if (unchanged && stimulusIncludesCurrent(previous->semantics, stopped)) return previous;
    next->semantics = resolveStimulus(next->module, next->scan, stopped);
    *error = next->semantics.error;
    if (!error->isEmpty()) return {};
    // The source loader may have observed a header that changed during analysis.
    if (!stimulusIncludesCurrent(next->semantics, stopped)) {
        *error = QStringLiteral("Sources changed during preparation. Try again."); return {};
    }
    for (const auto& source : next->scan.files) {
        if (stopped()) { *error = QStringLiteral("Preparation cancelled."); return {}; }
        QFile file(QDir(root).filePath(source.path));
        if (!file.open(QIODevice::ReadOnly) || file.size() != source.content.size()
            || file.readAll() != source.content || file.error() != QFileDevice::NoError) {
            *error = QStringLiteral("Sources changed during preparation. Try again."); return {};
        }
    }
    return next;
}
}
