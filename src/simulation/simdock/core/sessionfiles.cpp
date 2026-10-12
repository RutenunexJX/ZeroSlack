#include "sessionfiles.h"
#include "ownedpaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#endif

namespace simdock {
namespace {
const QString schema = QStringLiteral("zeroslack.simulation-session/v1");
bool running(qint64 pid)
{
    if (pid <= 0) return false;
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!process) return GetLastError() != ERROR_INVALID_PARAMETER;
    const bool alive = WaitForSingleObject(process, 0) != WAIT_OBJECT_0;
    CloseHandle(process);
    return alive;
#else
    return kill(pid_t(pid), 0) == 0 || errno != ESRCH;
#endif
}
QJsonObject owner(const QString& directory)
{
    QFile file(QDir(directory).filePath(QStringLiteral("owner.json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
bool removeSession(const QString& root, const QString& directory, QLockFile* lock, QString* error)
{
    if (!QFileInfo::exists(directory)) return true;
    const auto marker = QDir(directory).filePath(QStringLiteral("owner.json"));
    const auto lockPath = QDir(directory).filePath(QStringLiteral("session.lock"));
    if (!plainPathWithin(root, directory) || !plainPathWithin(root, marker) || !plainPathWithin(root, lockPath)) {
        *error = QStringLiteral("Preserved unsafe simulation storage: %1").arg(directory); return false;
    }
    QFile file(marker);
    QByteArray ownership;
    if (file.open(QIODevice::ReadOnly)) { ownership = file.readAll(); file.close(); }
    // Keep ownership and the lock until all potentially occupied files are gone.
    // Otherwise a partial Windows deletion would make the remainder unidentifiable.
    for (const auto& entry : QDir(directory).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        if (entry.fileName() == QStringLiteral("owner.json") || entry.fileName() == QStringLiteral("session.lock")) continue;
        const auto path = entry.absoluteFilePath();
        if (!plainPathWithin(root, path)) {
            *error = QStringLiteral("Preserved unsafe simulation storage: %1").arg(path); return false;
        }
        if (entry.isDir()) {
            if (!removeOwnedTree(root, path, error)) return false;
        } else if (!QFile::remove(path)) {
            *error = QStringLiteral("Could not release managed files: %1").arg(path); return false;
        }
    }
    if (lock) lock->unlock();
    if (removeOwnedTree(root, directory, error)) return true;
    if (!ownership.isEmpty() && QFileInfo::exists(directory) && !QFileInfo::exists(marker)
        && plainPathWithin(root, marker)) {
        QSaveFile restore(marker);
        if (!restore.open(QIODevice::WriteOnly) || restore.write(ownership) != ownership.size() || !restore.commit())
            *error += QStringLiteral(" The ownership record could not be restored; files must be preserved: %1").arg(directory);
    }
    return false;
}
}
SessionFiles::SessionFiles() = default;
SessionFiles::~SessionFiles() { QString ignored; release(&ignored); }
QString SessionFiles::cacheRoot()
{
    const auto override = qEnvironmentVariable("ZEROSLACK_SIMULATION_CACHE_ROOT");
    return override.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("simulation"))
        : QDir::cleanPath(override);
}
QStringList SessionFiles::recover(const QString& root)
{
    QStringList notices;
    for (const auto& entry : QDir(root).entryInfoList({QStringLiteral("session-*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto path = entry.absoluteFilePath();
        if (!plainPathWithin(root, path) || !plainPathWithin(root, QDir(path).filePath(QStringLiteral("owner.json")))) {
            notices << QStringLiteral("Preserved linked simulation storage: %1").arg(path); continue;
        }
        const auto record = owner(path);
        if (record.value(QStringLiteral("schema")).toString() != schema
            || record.value(QStringLiteral("directory")).toString() != entry.fileName()
            || record.value(QStringLiteral("hostPid")).toInteger() <= 0
            || record.value(QStringLiteral("childPid")).toInteger(-1) < 0) {
            notices << QStringLiteral("Preserved unrecognized simulation storage: %1").arg(path); continue;
        }
        if (running(record.value(QStringLiteral("hostPid")).toInteger()) || running(record.value(QStringLiteral("childPid")).toInteger())) continue;
        const auto lockPath = QDir(path).filePath(QStringLiteral("session.lock"));
        if (!plainPathWithin(root, lockPath)) continue;
        QLockFile lock(lockPath); lock.setStaleLockTime(0);
        if (!lock.tryLock(0)) continue;
        QString issue;
        if (!removeSession(root, path, &lock, &issue)) notices << issue;
    }
    return notices;
}
bool SessionFiles::create(QString* error, QStringList* notices)
{
    m_root = cacheRoot();
    auto ancestor = QFileInfo(m_root).dir();
    while (!ancestor.exists() && ancestor.cdUp()) {}
    if (!QDir::isAbsolutePath(m_root) || !plainPathWithin(ancestor.absolutePath(), m_root) || !QDir().mkpath(m_root)
        || !plainPathWithin(m_root, QDir(m_root).filePath(QStringLiteral("session-probe")))) {
        *error = QStringLiteral("Simulation cache must be an absolute, unlinked directory."); return false;
    }
    *notices += recover(m_root);
    QTemporaryDir directory(QDir(m_root).filePath(QStringLiteral("session-XXXXXX")));
    directory.setAutoRemove(false);
    if (!directory.isValid()) { *error = QStringLiteral("Could not create simulation storage."); return false; }
    m_path = directory.path();
    m_lock = std::make_unique<QLockFile>(QDir(m_path).filePath(QStringLiteral("session.lock")));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) { *error = QStringLiteral("Could not own simulation storage."); return false; }
    return setChild(0, error);
}
bool SessionFiles::setChild(qint64 pid, QString* error)
{
    m_child = pid;
    const QJsonObject record{{QStringLiteral("schema"), schema},
        {QStringLiteral("directory"), QFileInfo(m_path).fileName()},
        {QStringLiteral("hostPid"), QCoreApplication::applicationPid()}, {QStringLiteral("childPid"), pid}};
    const auto path = QDir(m_path).filePath(QStringLiteral("owner.json"));
    if (!plainPathWithin(m_root, path)) { *error = QStringLiteral("Simulation ownership path is not safe."); return false; }
    QSaveFile file(path);
    const auto bytes = QJsonDocument(record).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = QStringLiteral("Could not record simulation ownership: %1").arg(file.errorString()); return false;
    }
    return true;
}
bool SessionFiles::prune(const QStringList& retained, QString* error)
{
    if (m_path.isEmpty() || !m_lock || !m_lock->isLocked()) return true;
    QStringList issues;
    for (const auto& entry : QDir(m_path).entryInfoList({QStringLiteral("run-*"), QStringLiteral("library-*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (retained.contains(entry.absoluteFilePath())) continue;
        QString issue;
        if (!removeOwnedTree(m_path, entry.absoluteFilePath(), &issue)) issues << issue;
    }
    *error = issues.join(QLatin1Char('\n'));
    return issues.isEmpty();
}
bool SessionFiles::release(QString* error)
{
    if (m_path.isEmpty()) return true;
    if (running(m_child)) { *error = QStringLiteral("Simulation storage retained while Questa is still exiting."); return false; }
    if (!removeSession(m_root, m_path, m_lock.get(), error)) return false;
    m_lock.reset(); m_path.clear();
    return true;
}
void SessionFiles::preserveForRecovery()
{
    m_lock.reset();
    m_path.clear();
}
}
