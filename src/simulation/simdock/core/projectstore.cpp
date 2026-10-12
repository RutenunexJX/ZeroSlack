#include "workspace.h"
#include "ownedpaths.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace simdock {
namespace {
QByteArray bytesAt(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) return {};
    return file.readAll();
}
QByteArray digest(const QByteArray& bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
QString manifestPath(const QString& base, const QString& name) { return QDir(base).filePath(name + QStringLiteral("/project.json")); }
QString availableName(const QString& base, const QString& requested, const QString& current)
{
    QSet<QString> names;
    for (const auto& entry : QDir(base).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        if (entry.compare(current, Qt::CaseInsensitive)) names.insert(entry.toCaseFolded());
    auto name = requested;
    for (int suffix = 2; names.contains(name.toCaseFolded()); ++suffix)
        name = requested.left(108) + QStringLiteral(" (%1)").arg(suffix);
    return name;
}
Project decode(const QJsonObject& object)
{
    Project p;
    p.id = object.value(QStringLiteral("id")).toString();
    p.name = object.value(QStringLiteral("name")).toString();
    for (const auto& value : object.value(QStringLiteral("sources")).toArray()) p.sources << value.toString();
    p.dutFile = object.value(QStringLiteral("dutFile")).toString();
    p.dutName = object.value(QStringLiteral("dutName")).toString();
    p.tbFile = object.value(QStringLiteral("tbFile")).toString();
    p.tbName = object.value(QStringLiteral("tbName")).toString();
    p.durationNs = object.value(QStringLiteral("durationNs")).toInteger(1000);
    p.waveScope = object.value(QStringLiteral("waveScope")).toString(QStringLiteral("interface"));
    for (const auto& value : object.value(QStringLiteral("waveSignals")).toArray())
        if (value.isString()) p.waveSignals << value.toString();
    p.preservedInputs = object.value(QStringLiteral("preservedInputs")).toObject();
    const auto stimulus = object.value(QStringLiteral("stimulus")).toObject();
    const auto alternate = object.value(QStringLiteral("alternateInput")).toObject();
    if (p.preservedInputs.isEmpty() && (!stimulus.isEmpty() || !alternate.value(QStringLiteral("tbFile")).toString().isEmpty()
        || !alternate.value(QStringLiteral("stimulus")).toObject().isEmpty())) {
        for (const auto& key : {"inputMode", "stimulus", "alternateInput", "tbFile", "tbName", "durationNs"})
            if (object.contains(QString::fromLatin1(key))) p.preservedInputs.insert(QString::fromLatin1(key), object.value(QString::fromLatin1(key)));
    }
    p.inputMode = InputMode::ExistingTb;
    return p;
}
}

QString validateStorageName(const QString& name)
{
    static const QRegularExpression invalid(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$"), QRegularExpression::CaseInsensitiveOption);
    if (name.isEmpty() || name.size() > 120 || name != name.trimmed() || name.endsWith(QLatin1Char('.'))
        || name.startsWith(QLatin1Char('.')) || invalid.match(name).hasMatch() || reserved.match(name).hasMatch())
        return QStringLiteral("Use a readable name of 1–120 characters without path separators, Windows reserved names, or trailing dots/spaces.");
    return {};
}

bool saveProject(const QString& root, Project& project, QString* error)
{
    error->clear();
    *error = validateProject(root, project);
    if (error->isEmpty()) *error = validateStorageName(project.name);
    if (!error->isEmpty()) return false;
    const auto base = projectDirectory(root);
    if (!plainPathWithin(root, base) || !QDir().mkpath(base)) {
        *error = QStringLiteral("Could not create safe project storage inside the workspace."); return false;
    }
    const auto lockPath = QDir(base).filePath(QStringLiteral(".storage.lock"));
    if (!plainPathWithin(base, lockPath)) { *error = QStringLiteral("Project storage lock is not safe."); return false; }
    QLockFile lock(lockPath);
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { *error = QStringLiteral("Another instance is saving projects. Try again after it finishes."); return false; }
    QString previous;
    for (const auto& entry : QDir(base).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto path = manifestPath(base, entry.fileName());
        if (!plainPathWithin(base, path)) continue;
        if (QJsonDocument::fromJson(bytesAt(path)).object().value(QStringLiteral("id")).toString() == project.id) {
            if (!previous.isEmpty()) { *error = QStringLiteral("Duplicate project identity; files were preserved."); return false; }
            previous = entry.fileName();
        }
    }
    for (auto it = project.storageRevisions.cbegin(); it != project.storageRevisions.cend(); ++it) {
        if (!plainPathWithin(base, it.key()) || digest(bytesAt(it.key())) != it.value()) {
            *error = QStringLiteral("The project was changed outside this window. Reopen it before saving."); return false;
        }
    }
    if (!previous.isEmpty() && !project.storageRevisions.contains(manifestPath(base, previous))) {
        *error = QStringLiteral("This project already exists. Reopen it before saving."); return false;
    }
    auto updated = project;
    updated.name = availableName(base, project.name, previous);
    updated.storageName = updated.name;
    const auto directory = QDir(base).filePath(updated.storageName);
    const auto path = manifestPath(base, updated.storageName);
    if (!plainPathWithin(base, path)) { *error = QStringLiteral("The project path is not safe."); return false; }
    bool moved = false;
    if (!previous.isEmpty() && previous != updated.storageName) {
        if (!QDir(base).rename(previous, updated.storageName)) {
            *error = QStringLiteral("Could not rename the project directory. The existing project is unchanged."); return false;
        }
        moved = true;
    }
    if (!QDir().mkpath(directory)) { *error = QStringLiteral("Could not create the project directory."); return false; }
    const auto content = QJsonDocument(projectJson(updated)).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() || !file.commit()) {
        *error = file.errorString();
        file.cancelWriting();
        if (moved && !QDir(base).rename(updated.storageName, previous))
            *error += QStringLiteral(" Original data is retained at %1; its directory name could not be restored.").arg(directory);
        if (previous.isEmpty()) QDir(base).rmdir(updated.storageName); // Empty directories only.
        return false;
    }
    if (!updated.legacyFile.isEmpty()) {
        const auto old = QDir(base).filePath(updated.legacyFile);
        if (plainPathWithin(base, old) && project.storageRevisions.contains(old)
            && digest(bytesAt(old)) == project.storageRevisions.value(old) && QFile::remove(old))
            updated.legacyFile.clear();
        else *error = QStringLiteral("The readable project was saved; its legacy copy was preserved at %1.").arg(old);
    }
    updated.storageRevisions = {{path, digest(content)}};
    if (!updated.legacyFile.isEmpty()) {
        const auto old = QDir(base).filePath(updated.legacyFile);
        updated.storageRevisions.insert(old, project.storageRevisions.value(old));
    }
    project = updated;
    return true;
}

QList<Project> loadProjects(const QString& root, QStringList* errors)
{
    QList<Project> result;
    const auto base = projectDirectory(root);
    if (!plainPathWithin(root, base)) return result;
    QStringList candidates;
    for (const auto& entry : QDir(base).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (!entry.fileName().startsWith(QLatin1Char('.'))) candidates << manifestPath(base, entry.fileName());
    for (const auto& entry : QDir(base).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        candidates << entry.absoluteFilePath();
    QSet<QString> seen;
    for (const auto& path : candidates) {
        if (!plainPathWithin(base, path)) { if (errors) *errors << QStringLiteral("Preserved linked project storage: %1").arg(path); continue; }
        const auto bytes = bytesAt(path);
        QJsonParseError parse;
        const auto object = QJsonDocument::fromJson(bytes, &parse).object();
        auto p = decode(object);
        const bool legacy = QFileInfo(path).absolutePath() == QDir(base).absolutePath();
        auto issue = validateProject(root, p);
        if (parse.error != QJsonParseError::NoError || object.value(QStringLiteral("schema")).toString() != QStringLiteral("simdock.project/v1"))
            issue = QStringLiteral("Unsupported project format or invalid JSON.");
        if (legacy && QFileInfo(path).fileName() != p.id + QStringLiteral(".json"))
            issue = QStringLiteral("The legacy project ID does not match its file name.");
        if (!issue.isEmpty()) { if (errors) *errors << QFileInfo(path).fileName() + QStringLiteral(": ") + issue; continue; }
        if (seen.contains(p.id)) {
            if (errors) *errors << QStringLiteral("Preserved a duplicate legacy project copy: %1").arg(path);
            continue;
        }
        p.storageRevisions.insert(path, digest(bytes));
        if (legacy) {
            p.legacyFile = QFileInfo(path).fileName();
            if (!validateStorageName(p.name).isEmpty()) {
                auto safe = p.name;
                safe.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])")), QStringLiteral(" "));
                safe = safe.simplified();
                while (safe.endsWith(QLatin1Char('.'))) safe.chop(1);
                if (!validateStorageName(safe).isEmpty()) safe = QStringLiteral("Project ") + safe;
                p.name = safe.left(120).trimmed();
                if (!validateStorageName(p.name).isEmpty()) p.name = QStringLiteral("Imported project");
            }
            QString migrationError;
            const bool migrated = saveProject(root, p, &migrationError);
            if (!migrationError.isEmpty() && errors)
                *errors << (migrated ? migrationError : QStringLiteral("Legacy project remains readable; migration was not saved: %1").arg(migrationError));
        } else p.storageName = QFileInfo(path).dir().dirName();
        if (!p.preservedInputs.isEmpty() && errors)
            *errors << QStringLiteral("%1: saved graphical inputs are preserved. Use an existing TB; graphical editing has been removed.").arg(p.name);
        seen.insert(p.id);
        result << p;
    }
    return result;
}
}
