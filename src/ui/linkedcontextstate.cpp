#include "linkedcontextstate.h"
#include "pinloomhostclient.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <cmath>

namespace {
constexpr qint64 limit = 64 * 1024 * 1024;
QByteArray digest(const QByteArray& bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
bool read(const QString& path, QByteArray* bytes, QString* error) {
    QFile file(path);
    if (!file.exists()) { bytes->clear(); return true; }
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit) {
        *error = file.isOpen() ? QStringLiteral("The board state exceeds 64 MiB.") : file.errorString(); return false;
    }
    *bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) { *error = file.errorString(); return false; }
    return true;
}
bool number(QJsonValue v, double low, double high) { return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= low && v.toDouble() <= high; }
bool viewportValid(const QJsonObject& value) {
    return number(value.value("zoom"), .05, 10) && number(value.value("centerX"), -1e7, 1e7)
        && number(value.value("centerY"), -1e7, 1e7);
}
bool identifierList(QJsonValue value, QSet<QString>* ids = nullptr) {
    if (!value.isArray() || value.toArray().size() > 10000) return false;
    QSet<QString> seen;
    for (const auto& item : value.toArray()) {
        const auto id = item.toString();
        if (!item.isString() || id.isEmpty() || id.size() > 512 || seen.contains(id)) return false;
        seen.insert(id);
    }
    if (ids) *ids = seen;
    return true;
}
bool nativeViewValid(QJsonValue value) {
    if (!value.isObject()) return false;
    const auto view = value.toObject();
    if (!number(view.value("version"), 1, 1) || !view.value("weights").isArray()
        || view.value("weights").toArray().size() != 3 || !identifierList(view.value("expanded"))) return false;
    double sum = 0;
    for (const auto& weight : view.value("weights").toArray()) {
        if (!number(weight, 0, 1e9)) return false;
        sum += weight.toDouble();
    }
    return std::isfinite(sum) && sum > 0;
}
bool expandedReferencesValid(QJsonValue value, const QSet<QString>& ids) {
    if (!value.isArray() || value.toArray().size() > 10000) return false;
    QHash<QString, QString> parents;
    for (const auto& item : value.toArray()) {
        if (!item.isObject()) return false;
        const auto reference = item.toObject();
        for (const auto* key : {"id", "anchor", "parent", "title", "source"}) {
            const auto field = reference.value(QLatin1String(key));
            const bool title = QLatin1String(key) == QLatin1String("title");
            const int maximum = title || QLatin1String(key) == QLatin1String("source") ? 8192 : 512;
            if (!field.isString() || (!title && field.toString().isEmpty()) || field.toString().size() > maximum) return false;
        }
        const auto id = reference.value("id").toString(), parent = reference.value("parent").toString();
        if (!ids.contains(id) || !ids.contains(parent) || parents.contains(id) || id == parent
            || !PinloomHostIdentity::fromUri(QUrl(reference.value("source").toString())).isValid()) return false;
        parents.insert(id, parent);
    }
    QHash<QString, int> visited;
    for (auto it = parents.cbegin(); it != parents.cend(); ++it) {
        QString current = it.key(); QStringList chain;
        while (parents.contains(current) && visited.value(current) != 2) {
            if (visited.value(current) == 1) return false;
            visited.insert(current, 1); chain.append(current); current = parents.value(current);
        }
        for (const auto& id : chain) visited.insert(id, 2);
    }
    return true;
}
}

bool LinkedContextState::validate(const QJsonObject& state, QString* error) {
    const auto fail = [error] { if (error) *error = QStringLiteral("The saved board has an unsupported or invalid layout. Its file was preserved."); return false; };
    if (state.isEmpty()) return true;
    if (number(state.value("version"), 3, 3)) {
        QSet<QString> ids;
        if (!identifierList(state.value("referenceIds"), &ids)
            || !expandedReferencesValid(state.value("expandedReferences"), ids)
            || !nativeViewValid(state.value("nativeView")) || !number(state.value("codeRatio"), .3, .7)) return fail();
        return true;
    }
    if (state.value(QStringLiteral("version")).toInt() != 2 || !state.value(QStringLiteral("native")).isObject()) return fail();
    const auto native = state.value(QStringLiteral("native")).toObject();
    if (native.value("format") != "zeroslack.linked-board" || native.value("engine") != "blocksuite-0.22.4"
        || native.value("version").toInt() != 1 || native.value("workspaceId").toString().isEmpty()
        || native.value("documentId").toString().isEmpty() || !native.value("attachments").isArray()) return fail();
    for (const auto* key : {"root", "state"}) {
        const QByteArray encoded = native.value(QLatin1String(key)).toString().toLatin1();
        if (encoded.isEmpty() || encoded.size() > limit || !QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors)) return fail();
    }
    if (native.contains("documents")) {
        if (!native.value("documents").isArray() || native.value("documents").toArray().size() > 10000) return fail();
        QSet<QString> documents;
        for (const auto& item : native.value("documents").toArray()) {
            const auto current = item.toObject(); const auto id = current.value("id").toString();
            const auto encoded = current.value("state").toString().toLatin1();
            if (id.isEmpty() || id.size() > 512 || documents.contains(id) || encoded.isEmpty()
                || !QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors)) return fail();
            documents.insert(id);
        }
        if (!documents.contains(native.value("documentId").toString())) return fail();
    }
    const auto ids = state.value("referenceIds").toArray();
    if (ids.size() > 10000) return fail();
    for (const auto& id : ids) if (!id.isString() || id.toString().size() > 512) return fail();
    const auto viewport = state.value(QStringLiteral("viewport")).toObject();
    if (!viewportValid(viewport)) return fail();
    const auto views = state.value("documentViews").toObject();
    if (views.size() > 10000) return fail();
    for (const auto& view : views) if (!viewportValid(view.toObject())) return fail();
    if (state.contains(QStringLiteral("codeRatio")) && !number(state.value(QStringLiteral("codeRatio")), .3, .7)) return fail();
    if (state.contains("nativeView") && !nativeViewValid(state.value("nativeView"))) return fail();
    return true;
}

bool LinkedContextState::load(const QString& workspace, const QString& fileName) {
    value = {}; error.clear(); path.clear(); revision.clear(); writable = false;
    const QFileInfo file(fileName), root(workspace);
    if (!file.isAbsolute() || !file.exists() || !root.isDir()) {
        error = QStringLiteral("Save this code file in an open workspace to keep its board layout."); return false;
    }
    identity = QDir::fromNativeSeparators(file.canonicalFilePath());
#ifdef Q_OS_WIN
    identity = identity.toCaseFolded();
#endif
    path = QDir(root.canonicalFilePath()).filePath(QStringLiteral(".zeroslack/linked-context/")
        + QString::fromLatin1(digest(identity.toUtf8()).toHex()) + QStringLiteral(".json"));
    QByteArray bytes;
    if (!read(path, &bytes, &error)) return false;
    revision = digest(bytes);
    if (!bytes.isEmpty()) {
        QJsonParseError parse;
        const auto object = QJsonDocument::fromJson(bytes, &parse).object();
        if (parse.error != QJsonParseError::NoError || object.value(QStringLiteral("file")).toString() != identity
            || !object.value(QStringLiteral("state")).isObject()) {
            error = QStringLiteral("The saved board could not be read. Its file was preserved."); return false;
        }
        value = object.value(QStringLiteral("state")).toObject();
        if (!validate(value, &error)) { value = {}; return false; }
    }
    writable = true; return true;
}

bool LinkedContextState::save(const QJsonObject& state) {
    if (!writable) return false;
    if (!validate(state, &error)) return false;
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { error = QStringLiteral("The board folder could not be created."); return false; }
    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) { error = QStringLiteral("Another operation is saving this board. Try again."); return false; }
    QByteArray bytes;
    if (!read(path, &bytes, &error)) return false;
    if (digest(bytes) != revision) {
        writable = false; error = QStringLiteral("This board changed on disk. Reload the board before saving; the external version was preserved."); return false;
    }
    const auto data = QJsonDocument(QJsonObject{{QStringLiteral("file"), identity}, {QStringLiteral("state"), state}}).toJson(QJsonDocument::Indented);
    if (data.size() > limit || !QDir().mkpath(QFileInfo(path).absolutePath())) { error = QStringLiteral("The board folder could not be created or its state is too large."); return false; }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) { error = output.errorString(); return false; }
    value = state; revision = digest(data); error.clear(); return true;
}

bool LinkedContextState::writeCopy(const QString& destination, const QJsonObject& state, QString* failure) const {
    if (!validate(state, failure)) return false;
    if (!path.isEmpty() && QFileInfo(destination).absoluteFilePath().compare(QFileInfo(path).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        *failure = QStringLiteral("Choose another file to preserve the original board."); return false;
    }
    const auto data = QJsonDocument(QJsonObject{{QStringLiteral("file"), identity}, {QStringLiteral("state"), state}}).toJson(QJsonDocument::Indented);
    if (data.size() > limit) { *failure = QStringLiteral("The board state exceeds 64 MiB."); return false; }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) { *failure = output.errorString(); return false; }
    return true;
}
