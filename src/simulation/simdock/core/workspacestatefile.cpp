#include "workspacestatefile.h"
#include "ownedpaths.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>

namespace simdock {
namespace {
bool readRecord(const QString& path, QByteArray* bytes, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        *error = QStringLiteral("Cannot read workspace state: %1").arg(path);
        return false;
    }
    *bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) { *error = file.errorString(); return false; }
    return true;
}
QByteArray revision(const QByteArray& bytes)
{ return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
}
bool WorkspaceStateFile::open(const QString& root, const QString& name, const QString& schema, QString* error)
{
    error->clear();
    WorkspaceStateFile next;
    next.m_root = QFileInfo(root).canonicalFilePath();
    next.m_path = QDir(next.m_root).filePath(QStringLiteral(".simdock/") + name + QStringLiteral(".json"));
    next.m_schema = schema;
    if (next.m_root.isEmpty() || !QFileInfo(next.m_root).isDir() || !plainPathWithin(next.m_root, next.m_path)) {
        *error = QStringLiteral("Workspace state must remain inside an existing workspace."); return false;
    }
    next.m_exists = QFileInfo::exists(next.m_path);
    if (next.m_exists) {
        QByteArray bytes;
        if (!readRecord(next.m_path, &bytes, error)) return false;
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(bytes, &parse);
        next.m_value = document.object();
        if (parse.error != QJsonParseError::NoError || !document.isObject()
            || next.m_value.value("schema").toString() != schema) {
            *error = QStringLiteral("Unsupported or damaged workspace state; the file was preserved: %1").arg(next.m_path);
            return false;
        }
        next.m_revision = revision(bytes);
    }
    *this = std::move(next);
    return true;
}
bool WorkspaceStateFile::save(const QJsonObject& value, QString* error)
{
    error->clear();
    if (m_root.isEmpty() || !plainPathWithin(m_root, m_path)
        || !QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        *error = QStringLiteral("Cannot create safe workspace state storage."); return false;
    }
    const auto lockPath = m_path + QStringLiteral(".lock");
    if (!plainPathWithin(m_root, lockPath)) { *error = QStringLiteral("Unsafe workspace state lock."); return false; }
    QLockFile lock(lockPath);
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { *error = QStringLiteral("Another instance is saving this workspace state."); return false; }
    const bool present = QFileInfo::exists(m_path);
    QByteArray previous;
    if (present && !readRecord(m_path, &previous, error)) return false;
    if (present != m_exists || (present && revision(previous) != m_revision)) {
        *error = QStringLiteral("Workspace state changed outside this window. Reopen the workspace before saving."); return false;
    }
    auto record = value;
    record.insert("schema", m_schema);
    const auto bytes = QJsonDocument(record).toJson();
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString(); file.cancelWriting(); return false;
    }
    m_value = record;
    m_revision = revision(bytes);
    m_exists = true;
    return true;
}
}
