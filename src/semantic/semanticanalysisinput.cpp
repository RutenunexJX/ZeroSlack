#include "semanticanalysisinput.h"
#include <zeroslack/text/sourcetext.h>
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSet>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <io.h>
#include <fcntl.h>
#endif

bool SemanticAnalysisInput::equivalentTo(const SemanticAnalysisInput& other) const
{
    return projectIdentity == other.projectIdentity
        && documentRevisions == other.documentRevisions && sources == other.sources;
}

namespace {
bool openSourceForRead(QFile& file)
{
#ifdef Q_OS_WIN
    QString path = QDir::toNativeSeparators(QFileInfo(file.fileName()).absoluteFilePath());
    if (!path.startsWith(QStringLiteral("\\\\?\\")) && !path.startsWith(QStringLiteral("\\\\.\\")))
        path = path.startsWith(QStringLiteral("\\\\"))
            ? QStringLiteral("\\\\?\\UNC\\") + path.mid(2) : QStringLiteral("\\\\?\\") + path;
    // Validation must not block atomic source replacement or workspace deletion.
    // Final fingerprints still reject an input changed during the read.
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
    if (descriptor < 0) { CloseHandle(handle); return false; }
    if (file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) return true;
    _close(descriptor);
    return false;
#else
    return file.open(QIODevice::ReadOnly);
#endif
}

SemanticSourceFingerprint fingerprintFor(const SemanticCapturedSource& source)
{
    return {source.readable, source.readable
        ? QCryptographicHash::hash(source.text.toUtf8(), QCryptographicHash::Sha256)
        : QByteArray{}};
}
}

SemanticInputFingerprint SemanticAnalysisInput::fingerprint() const
{
    SemanticInputFingerprint result;
    result.projectIdentity = projectIdentity;
    for (auto it = sources.cbegin(); it != sources.cend(); ++it)
        result.sources.insert(it.key(), fingerprintFor(it.value()));
    return result;
}

bool SemanticInputCapture::matchesFingerprint(
    const SemanticInputFingerprint& fingerprint, const ProjectSnapshot& project)
{
    if (fingerprint.projectIdentity != project.semanticIdentity())
        return false;
    for (auto it = fingerprint.sources.cbegin(); it != fingerprint.sources.cend(); ++it)
        if (fingerprintFor(read(it.key())) != it.value())
            return false;
    return true;
}

qsizetype SemanticAnalysisInput::logicalBytes() const
{
    qsizetype bytes = projectIdentity.size() * qsizetype(sizeof(QChar));
    for (auto it = sources.cbegin(); it != sources.cend(); ++it)
        bytes += (it.key().size() + it->text.size()) * qsizetype(sizeof(QChar)) + 128;
    return bytes;
}

QString SemanticInputCapture::pathKey(const QString& path)
{
    if (path.isEmpty())
        return {};
    QString key = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

SemanticCapturedSource SemanticInputCapture::read(const QString& path)
{
    SemanticCapturedSource result;
    QFile file(path);
    if (openSourceForRead(file)) {
        const auto bytes = file.readAll();
        const bool readComplete = file.error() == QFileDevice::NoError;
        file.close();
        const auto decoded = decodeSourceText(bytes);
        result.text = decoded.text;
        result.readable = readComplete && decoded.valid;
        if (result.readable)
            result.rawSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    }
    return result;
}

SemanticInputCapture::SemanticInputCapture(const SemanticAnalysisRequest& request)
{
    captured.projectIdentity = request.project.semanticIdentity();
    captured.generation = request.generation;
    for (auto it = request.documentRevisions.cbegin(); it != request.documentRevisions.cend(); ++it)
        captured.documentRevisions.insert(pathKey(it.key()), it.value());
    for (auto it = request.sourceOverrides.cbegin(); it != request.sourceOverrides.cend(); ++it)
        captured.sources.insert(pathKey(it.key()), {it.value(), true, true});
}

const SemanticCapturedSource& SemanticInputCapture::source(const QString& path)
{
    const QString key = pathKey(path);
    if (compilationObservation)
        observedPaths.insert(key);
    auto it = captured.sources.constFind(key);
    if (it == captured.sources.cend()) {
        captured.sources.insert(key, read(key));
        it = captured.sources.constFind(key);
    }
    return it.value();
}

bool SemanticInputCapture::capture(const QStringList& paths, const std::function<bool()>& cancelled)
{
    for (const QString& path : paths) {
        if (cancelled && cancelled())
            return false;
        source(path);
    }
    return true;
}

QHash<QString, QString> SemanticInputCapture::contents() const
{
    QHash<QString, QString> result;
    for (auto it = captured.sources.cbegin(); it != captured.sources.cend(); ++it)
        if (it->readable)
            result.insert(it.key(), it->text);
    return result;
}

bool SemanticInputCapture::stillMatchesDisk(const std::function<bool()>& cancelled) const
{
    for (auto it = captured.sources.cbegin(); it != captured.sources.cend(); ++it) {
        if (cancelled && cancelled())
            return false;
        if (!it->overridden && read(it.key()) != it.value())
            return false;
    }
    return true;
}

std::shared_ptr<const SemanticAnalysisInput> SemanticInputCapture::seal() const
{
    auto result = std::make_shared<SemanticAnalysisInput>(captured);
    if (compilationObservation) {
        for (auto it = result->sources.begin(); it != result->sources.end();) {
            if (!it->overridden && !observedPaths.contains(it.key()))
                it = result->sources.erase(it);
            else
                ++it;
        }
    }
    QSet<QString> directories;
    for (auto it = result->sources.cbegin(); it != result->sources.cend(); ++it) {
        if (it->overridden)
            continue;
        if (it->readable)
            result->watchFiles.append(it.key());
        QDir directory = QFileInfo(it.key()).absoluteDir();
        while (!directory.exists())
            if (!directory.cdUp())
                break;
        if (directory.exists())
            directories.insert(pathKey(directory.absolutePath()));
    }
    result->watchFiles.sort(Qt::CaseSensitive);
    result->watchDirectories = directories.values();
    result->watchDirectories.sort(Qt::CaseSensitive);
    return result;
}
