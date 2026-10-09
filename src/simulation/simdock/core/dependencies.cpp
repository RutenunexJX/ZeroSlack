#include "dependencies.h"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

namespace simdock {
namespace {
QString key(QString path)
{
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}
class Resolver {
public:
    Resolver(const DependencyIndex& index, const QStringList& selected)
        : index(index), root(index.scan.root), selected(selected.begin(), selected.end()),
          paths(index.paths), units(index.units), packages(index.packages) {}
    QStringList ordered(const QStringList& roots, const QSet<QString>* allowed = nullptr)
    {
        struct Step { QString file; bool leave; };
        QList<Step> pending;
        for (auto it = roots.crbegin(); it != roots.crend(); ++it) pending << Step{*it, false};
        QSet<QString> active, done;
        QStringList result;
        while (!pending.isEmpty()) {
            const auto step = pending.takeLast();
            if (done.contains(step.file)) continue;
            if (step.leave) {
                active.remove(step.file);
                done.insert(step.file);
                result << step.file;
                continue;
            }
            if (active.contains(step.file)) {
                messages << QStringLiteral("Dependency cycle involving %1; check the compilation order.").arg(step.file);
                continue;
            }
            active.insert(step.file);
            pending << Step{step.file, true};
            const auto dependencies = direct(step.file);
            for (auto it = dependencies.crbegin(); it != dependencies.crend(); ++it)
                if (!allowed || allowed->contains(*it)) pending << Step{*it, false};
        }
        return result;
    }
    QStringList messages;
private:
    QString provider(const QString& file, const QString& name, const QStringList& candidates, const QString& kind)
    {
        if (candidates.contains(file)) return file;
        if (candidates.size() == 1) return candidates.first();
        QStringList preferred;
        for (const auto& candidate : candidates) if (selected.contains(candidate)) preferred << candidate;
        if (preferred.size() == 1) return preferred.first();
        messages << (candidates.isEmpty()
            ? QStringLiteral("%1: %2 '%3' was not found in the workspace. Add its source or simulation model manually.").arg(file, kind, name)
            : QStringLiteral("%1: %2 '%3' has multiple definitions (%4). Select the intended file manually.").arg(file, kind, name, candidates.join(QStringLiteral(", "))));
        return {};
    }
    QString includeFile(const QString& file, const QString& include)
    {
        const QString relative = QDir::isAbsolutePath(include) ? QDir(root).relativeFilePath(include) : include;
        const auto local = key(QDir(QFileInfo(file).path()).filePath(relative));
        if (!QDir::isAbsolutePath(include) && paths.contains(local)) return paths.value(local);
        const auto fromRoot = key(relative);
        if (paths.contains(fromRoot)) return paths.value(fromRoot);
        if (fromRoot == QStringLiteral("..") || fromRoot.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(relative)) {
            messages << QStringLiteral("%1: include '%2' is outside the analyzed workspace.").arg(file, include);
            return {};
        }
        QStringList matches;
        for (auto it = paths.constBegin(); it != paths.constEnd(); ++it)
            if (it.key().endsWith(QLatin1Char('/') + fromRoot)) matches << it.value();
        matches.sort();
        return provider(file, include, matches, QStringLiteral("include"));
    }
    QStringList direct(const QString& path)
    {
        if (cache.contains(path)) return cache.value(path);
        const auto found = index.files.constFind(path);
        if (found == index.files.cend()) return {};
        const auto* file = &index.scan.files.at(found.value());
        QStringList result;
        for (const auto& include : file->includes) result << includeFile(path, include);
        for (const auto& name : file->referencedPackages)
            result << provider(path, name, packages.value(name), QStringLiteral("package"));
        for (const auto& name : file->instantiatedUnits)
            result << provider(path, name, units.value(name), QStringLiteral("module/interface"));
        if (file->dynamicInclude)
            messages << QStringLiteral("%1: a macro include cannot be resolved automatically.").arg(path);
        result.removeAll(QString());
        result.removeAll(path);
        result.removeDuplicates();
        cache.insert(path, result);
        return result;
    }
    const DependencyIndex& index;
    QString root;
    QSet<QString> selected;
    QHash<QString, QString> paths;
    QHash<QString, QStringList> units, packages, cache;
};
}

DependencyIndex::DependencyIndex(const Scan& snapshot) : scan(snapshot)
{
    for (qsizetype i = 0; i < scan.files.size(); ++i) {
        const auto& file = scan.files.at(i);
        files.insert(file.path, i);
        paths.insert(key(file.path), file.path);
        for (const auto& name : file.declaredUnits) units[name] << file.path;
        for (const auto& name : file.declaredPackages) packages[name] << file.path;
    }
}

DependencySelection selectDependencies(const DependencyIndex& index, const QString& source, const QStringList& selected)
{
    QStringList roots = selected;
    if (!roots.contains(source)) roots << source;
    Resolver resolver(index, roots);
    const auto dependencies = resolver.ordered({source});
    for (const auto& file : dependencies) if (!roots.contains(file)) roots << file;
    const QSet<QString> allowed(roots.begin(), roots.end());
    // Order the selected compilation units without restoring manually unchecked dependencies.
    const auto ordered = resolver.ordered(roots, &allowed);
    resolver.messages.removeDuplicates();
    return {ordered, resolver.messages};
}
DependencySelection selectDependencies(const Scan& scan, const QString& source, const QStringList& selected)
{
    return selectDependencies(DependencyIndex(scan), source, selected);
}
}
