#include "workspace.h"
#include "analyzer.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace simdock {
QString resolvedPath(const QString& path)
{
    QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    QStringList tail;
    while (!info.exists()) {
        const QString parent = info.dir().absolutePath();
        if (parent == info.absoluteFilePath()) return {};
        tail.prepend(info.fileName());
        info.setFile(parent);
    }
    QString result = info.canonicalFilePath();
    if (result.isEmpty()) return {};
    for (const auto& part : tail) result = QDir(result).filePath(part);
    return QDir::cleanPath(result);
}

bool insideWorkspace(const QString& root, const QString& path)
{
    const QString base = QFileInfo(root).canonicalFilePath();
    const QString target = resolvedPath(path);
#ifdef Q_OS_WIN
    constexpr auto cs = Qt::CaseInsensitive;
#else
    constexpr auto cs = Qt::CaseSensitive;
#endif
    return !base.isEmpty() && !target.isEmpty()
        && (target.compare(base, cs) == 0 || target.startsWith(base + QLatin1Char('/'), cs));
}

QString projectDirectory(const QString& root) { return QDir(root).filePath(QStringLiteral(".simdock/projects")); }

Scan scanWorkspace(const QString& root, const std::atomic_bool* cancelled, ScanMetrics* metrics, SourceCache* cache)
{
    QElapsedTimer total; total.start();
    if (metrics) *metrics = {};
    Scan scan;
    scan.root = QFileInfo(root).canonicalFilePath();
    if (scan.root.isEmpty()) {
        scan.messages << QStringLiteral("The workspace does not exist.");
        return scan;
    }
    if (cache && cache->root != scan.root) { cache->files.clear(); cache->root = scan.root; }
    QSet<QString> seenFiles;
    QStringList pending{scan.root};
    QSet<QString> visited;
    const QSet<QString> excluded{QStringLiteral(".git"), QStringLiteral(".simdock"), QStringLiteral("build"), QStringLiteral("artifacts"), QStringLiteral("node_modules"), QStringLiteral(".qtcreator")};
    while (!pending.isEmpty() && scan.files.size() < 10000) {
        if (cancelled && cancelled->load()) return scan;
        const QString path = pending.takeLast();
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (!insideWorkspace(scan.root, path) || visited.contains(canonical)) continue;
        visited.insert(canonical);
        for (const auto& file : QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
            if (cancelled && cancelled->load()) return scan;
            if (!insideWorkspace(scan.root, file.absoluteFilePath())) continue;
            if (file.isDir()) {
                if (!excluded.contains(file.fileName()) && !file.fileName().startsWith(QStringLiteral("build-"))
                    && !file.fileName().startsWith(QStringLiteral("cmake-build-"))) pending << file.absoluteFilePath();
                continue;
            }
            const QString suffix = file.suffix().toLower();
            if (suffix != QStringLiteral("sv") && suffix != QStringLiteral("v")
                && suffix != QStringLiteral("svh") && suffix != QStringLiteral("vh")) continue;
            const QString relative = QDir(scan.root).relativeFilePath(file.absoluteFilePath());
            if (file.size() > 8 * 1024 * 1024) {
                scan.messages << QStringLiteral("%1: skipped analysis because the file exceeds 8 MiB.").arg(relative);
                continue;
            }
            QFile input(file.absoluteFilePath());
            if (!input.open(QIODevice::ReadOnly)) {
                scan.messages << QStringLiteral("%1: %2").arg(relative, input.errorString());
                continue;
            }
            QElapsedTimer stage; stage.start();
            const auto content = input.readAll();
            if (metrics) { ++metrics->readFiles; metrics->readNs += stage.nsecsElapsed(); }
            if (input.error() != QFileDevice::NoError) {
                scan.messages << QStringLiteral("%1: %2").arg(relative, input.errorString());
                continue;
            }
            if (cancelled && cancelled->load()) return scan;
            stage.restart();
            SourceFile source;
            const auto* previous = cache ? cache->files.object(relative) : nullptr;
            // Timestamps and sizes alone miss edits made by generators/sync tools.
            // Exact bytes also keep same-length, same-mtime edits correct.
            if (previous && previous->content == content) {
                source = *previous;
                if (metrics) ++metrics->reusedFiles;
            } else {
                source = analyzeSource(content, relative);
                if (metrics) ++metrics->parsedFiles;
                if (cache) cache->files.insert(relative, new SourceFile(source), 1 + content.size() / 1024);
            }
            seenFiles.insert(relative);
            if (metrics) metrics->analysisNs += stage.nsecsElapsed();
            if (source.syntaxError) scan.messages << QStringLiteral("%1: syntax or preprocessor issues detected. Check the Questa compiler output.").arg(relative);
            scan.files << source;
        }
    }
    if (!pending.isEmpty()) scan.messages << QStringLiteral("Reached the analysis limit of 10,000 HDL files.");
    if (cache) for (const auto& path : cache->files.keys())
        if (!seenFiles.contains(path)) cache->files.remove(path);
    std::sort(scan.files.begin(), scan.files.end(), [](const SourceFile& a, const SourceFile& b) { return a.path < b.path; });
    if (metrics) metrics->totalNs = total.nsecsElapsed();
    return scan;
}

Project newProject(const QString& name)
{
    Project p;
    p.inputMode = InputMode::Graphical;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = name.trimmed();
    return p;
}

QJsonObject projectJson(const Project& p)
{
    return {{QStringLiteral("schema"), QStringLiteral("simdock.project/v1")},
        {QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name},
        {QStringLiteral("sources"), QJsonArray::fromStringList(p.sources)},
        {QStringLiteral("dutFile"), p.dutFile}, {QStringLiteral("dutName"), p.dutName},
        {QStringLiteral("tbFile"), p.tbFile}, {QStringLiteral("tbName"), p.tbName},
        {QStringLiteral("durationNs"), double(p.durationNs)},
        {QStringLiteral("stimulus"), p.stimulus},
        {QStringLiteral("inputMode"), p.inputMode == InputMode::Graphical ? QStringLiteral("graphical") : QStringLiteral("existing")},
        {QStringLiteral("alternateInput"), QJsonObject{
            {QStringLiteral("tbFile"), p.alternateInput.tbFile}, {QStringLiteral("tbName"), p.alternateInput.tbName},
            {QStringLiteral("durationNs"), double(p.alternateInput.durationNs)},
            {QStringLiteral("stimulus"), p.alternateInput.stimulus}}},
        {QStringLiteral("waveScope"), p.waveScope},
        {QStringLiteral("waveSignals"), QJsonArray::fromStringList(p.waveSignals)}};
}

static QString validateProject(const QString& root, const Project& p)
{
    if (QUuid(p.id).isNull() || p.name.isEmpty() || p.name.size() > 120)
        return QStringLiteral("Invalid project name or ID.");
    if (p.durationNs < 1 || p.durationNs > 3600000000000LL
        || p.alternateInput.durationNs < 1 || p.alternateInput.durationNs > 3600000000000LL)
        return QStringLiteral("Simulation duration must be between 1 ns and 1 hour.");
    if (p.sources.size() > 10000) return QStringLiteral("The project exceeds the source file limit.");
    if (p.waveScope != QStringLiteral("interface") && p.waveScope != QStringLiteral("selected")
        && p.waveScope != QStringLiteral("all")) return QStringLiteral("Unknown waveform scope.");
    if (QJsonDocument(projectJson(p)).toJson().size() > 8 * 1024 * 1024)
        return QStringLiteral("The project exceeds the 8 MiB size limit.");
    QStringList paths = p.sources;
    paths << p.dutFile << p.tbFile << p.alternateInput.tbFile;
    for (const auto& path : paths) {
        if (path.isEmpty()) continue;
        if (QDir::isAbsolutePath(path) || !insideWorkspace(root, QDir(root).filePath(path)))
            return QStringLiteral("Project files must be inside the workspace: %1").arg(path);
    }
    return {};
}

bool saveProject(const QString& root, const Project& p, QString* error)
{
    *error = validateProject(root, p);
    const QString directory = projectDirectory(root);
    if (!error->isEmpty()) return false;
    if (!insideWorkspace(root, directory) || !QDir().mkpath(directory)) {
        *error = QStringLiteral("Could not create the project directory inside the workspace.");
        return false;
    }
    const QString filePath = QDir(directory).filePath(p.id + QStringLiteral(".json"));
    if (!insideWorkspace(root, filePath)) { *error = QStringLiteral("The project configuration path is outside the workspace."); return false; }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(projectJson(p)).toJson()) < 0 || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

QList<Project> loadProjects(const QString& root, QStringList* errors)
{
    QList<Project> result;
    if (!insideWorkspace(root, projectDirectory(root))) return result;
    for (const auto& item : QDir(projectDirectory(root)).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        if (!insideWorkspace(root, item.absoluteFilePath())) continue;
        QFile file(item.absoluteFilePath());
        if (item.size() > 8 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) continue;
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        const auto o = document.object();
        Project p;
        p.id = o.value(QStringLiteral("id")).toString();
        p.name = o.value(QStringLiteral("name")).toString();
        for (const auto& v : o.value(QStringLiteral("sources")).toArray()) p.sources << v.toString();
        p.dutFile = o.value(QStringLiteral("dutFile")).toString();
        p.dutName = o.value(QStringLiteral("dutName")).toString();
        p.tbFile = o.value(QStringLiteral("tbFile")).toString();
        p.tbName = o.value(QStringLiteral("tbName")).toString();
        p.durationNs = o.value(QStringLiteral("durationNs")).toInteger(1000);
        p.stimulus = o.value(QStringLiteral("stimulus")).toObject();
        const auto mode = o.value(QStringLiteral("inputMode")).toString();
        p.inputMode = mode == QStringLiteral("graphical") || (mode.isEmpty() && (!p.stimulus.isEmpty() || p.tbFile.isEmpty()))
            ? InputMode::Graphical : InputMode::ExistingTb;
        const auto alternate = o.value(QStringLiteral("alternateInput")).toObject();
        p.alternateInput = {alternate.value(QStringLiteral("tbFile")).toString(),
            alternate.value(QStringLiteral("tbName")).toString(),
            alternate.value(QStringLiteral("durationNs")).toInteger(1000),
            alternate.value(QStringLiteral("stimulus")).toObject()};
        p.waveScope = o.value(QStringLiteral("waveScope")).toString(QStringLiteral("interface"));
        for (const auto &signal : o.value(QStringLiteral("waveSignals")).toArray())
            if (signal.isString()) p.waveSignals << signal.toString();
        QString issue = validateProject(root, p);
        if (!mode.isEmpty() && mode != QStringLiteral("graphical") && mode != QStringLiteral("existing"))
            issue = QStringLiteral("Unknown simulation input mode.");
        if (parseError.error != QJsonParseError::NoError || o.value(QStringLiteral("schema")).toString() != QStringLiteral("simdock.project/v1"))
            issue = QStringLiteral("Unsupported project format or invalid JSON.");
        if (p.id + QStringLiteral(".json") != item.fileName()) issue = QStringLiteral("The project ID does not match the file name.");
        if (!issue.isEmpty()) {
            if (errors) *errors << item.fileName() + QStringLiteral(": ") + issue;
        } else result << p;
    }
    return result;
}

QString projectTbPath(const Project& p, const QString& top)
{
    return QStringLiteral("sim/%1/%2.sv").arg(p.id.left(8), top);
}

QString validateInputs(const QString& root, const Project& p, QMap<QString, QByteArray>* fingerprints,
                       const std::atomic_bool* cancelled, const QMap<QString, QByteArray>& overrides)
{
    if (fingerprints) fingerprints->clear();
    const QString issue = validateProject(root, p);
    if (!issue.isEmpty()) return issue;
    if (p.sources.isEmpty()) return QStringLiteral("Select source files first.");
    if (p.tbFile.isEmpty() || p.tbName.isEmpty()) return QStringLiteral("Create or select a TB first.");
    if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*$")).match(p.tbName).hasMatch())
        return QStringLiteral("Invalid TB top-level name.");
    QStringList pending = p.sources;
    // Match runScript: the TB is always compiled last, including when it was
    // explicitly selected as a source. Include-directory precedence must agree.
    pending.removeAll(p.tbFile);
    pending << p.tbFile;
    QStringList includeDirs{root};
    for (const auto& path : pending) {
        const auto dir = QFileInfo(QDir(root).filePath(path)).absolutePath();
        if (!includeDirs.contains(dir)) includeDirs << dir;
    }
    QSet<QString> visited;
    while (!pending.isEmpty()) {
        if (cancelled && cancelled->load()) return QStringLiteral("Preparation cancelled.");
        const QString relative = pending.takeLast();
        const QString absolute = QDir(root).filePath(relative);
        if (!insideWorkspace(root, absolute)) return QStringLiteral("File is outside the workspace: %1").arg(relative);
        const QString key = resolvedPath(absolute);
        if (visited.contains(key)) continue;
        visited.insert(key);
        QByteArray bytes;
        if (overrides.contains(relative)) bytes = overrides.value(relative);
        else {
            QFile file(absolute);
            if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("Could not read %1: %2").arg(relative, file.errorString());
            if (file.size() > 8 * 1024 * 1024) return QStringLiteral("File exceeds the 8 MiB analysis limit: %1").arg(relative);
            bytes = file.readAll();
            if (file.error() != QFileDevice::NoError) return file.errorString();
        }
        if (fingerprints) fingerprints->insert(QDir::cleanPath(absolute), QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
        const auto source = analyzeSource(bytes, relative);
        if (source.dynamicInclude) return QStringLiteral("%1 uses a macro include. Only literal include paths inside the workspace are supported.").arg(relative);
        for (const auto& inc : source.includes) {
            QString found;
            QStringList dirs{QFileInfo(absolute).absolutePath()};
            dirs += includeDirs;
            for (const auto& dir : dirs) {
                const QString candidate = QDir(dir).filePath(inc);
                if (QFileInfo(candidate).isFile()) { found = candidate; break; }
                if (fingerprints) fingerprints->insert(QDir::cleanPath(candidate), {});
            }
            if (found.isEmpty()) return QStringLiteral("%1: include file not found: %2").arg(relative, inc);
            if (!insideWorkspace(root, found)) return QStringLiteral("%1 includes a file outside the workspace: %2").arg(relative, inc);
            pending << QDir(root).relativeFilePath(found);
        }
    }
    return {};
}

bool writeNewTb(const QString& root, const QString& relative, const QString& content, QString* error)
{
    const QString path = QDir(root).filePath(relative);
    if (QDir::isAbsolutePath(relative) || !insideWorkspace(root, path)) {
        *error = QStringLiteral("The TB must be saved inside the workspace."); return false;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { *error = QStringLiteral("Could not create the TB directory."); return false; }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        *error = QStringLiteral("Could not create the TB (existing files are never overwritten): %1").arg(file.errorString()); return false;
    }
    const auto bytes = content.toUtf8();
    if (file.write(bytes) != bytes.size()) { *error = file.errorString(); return false; }
    return true;
}
}
