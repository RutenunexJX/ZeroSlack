#include "documentblockstore.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <memory>
#include <vector>
#include <algorithm>

namespace {
QByteArray digest(const QByteArray& bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
QString normalized(const QString& path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }
bool inside(const QString& path) { return !path.isEmpty() && !QDir::isAbsolutePath(path) && path != ".." && !path.startsWith("../"); }
bool assetName(const QString& value) {
    if (value.size() < 66 || value.contains('/') || value.contains('\\') || value[64] != '.') return false;
    for (int i = 0; i < 64; ++i) if (!QStringLiteral("0123456789abcdef").contains(value[i])) return false;
    for (int i = 65; i < value.size(); ++i) if (!value[i].isLetterOrNumber()) return false;
    return true;
}
bool readBytes(const QString& path, QByteArray* bytes, QString* error, qint64 limit = 16 * 1024 * 1024) {
    QFile file(path); if (!file.exists()) { bytes->clear(); return true; }
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit) {
        if (error) *error = QStringLiteral("Cannot read association resource %1: %2").arg(path, file.size() > limit ? "resource exceeds the size limit" : file.errorString());
        return false;
    }
    *bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) { if (error) *error = file.errorString(); return false; }
    return true;
}
bool atomicWrite(const QString& path, const QByteArray& bytes, QString* error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { if (error) *error = "Cannot create the association resource directory."; return false; }
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString(); return false;
    }
    return true;
}
bool generations(const QByteArray& bytes, QJsonArray* result, QString* error) {
    QJsonParseError parse; const auto doc = QJsonDocument::fromJson(bytes, &parse); const auto object = doc.object();
    if (parse.error != QJsonParseError::NoError || object.value("format") != "zeroslack.editor-blocks"
        || object.value("version").toInt() != 1 || !object.value("generations").isArray()) {
        if (error) *error = "Unsupported or corrupt association file. The original file was preserved."; return false;
    }
    *result = object.value("generations").toArray();
    if (result->isEmpty() || result->size() > 2) { if (error) *error = "Invalid association generations."; return false; }
    for (const auto& value : *result) {
        const auto row = value.toObject(); const auto hash = row.value("sourceSha256").toString();
        if (hash != "missing" && (hash.size() != 64 || QByteArray::fromHex(hash.toLatin1()).size() != 32)) {
            if (error) *error = "Invalid association source revision."; return false;
        }
        if (!row.value("metadata").isObject()) { if (error) *error = "Invalid association metadata."; return false; }
    }
    return true;
}
QString revisionText(const QByteArray& revision) { return revision.isEmpty() ? QStringLiteral("missing") : QString::fromLatin1(revision.toHex()); }
}

DocumentBlockStore* DocumentBlockStore::get(DocumentBlockModel* model) {
    if (!model) return nullptr;
    for (auto* child : model->children()) if (child->objectName() == "editorAssociationStore") return static_cast<DocumentBlockStore*>(child);
    return new DocumentBlockStore(model);
}
DocumentBlockStore::DocumentBlockStore(DocumentBlockModel* value) : QObject(value), model(value) { setObjectName("editorAssociationStore"); }
QString DocumentBlockStore::rootFor(const QString& source, const QString& workspace) {
    if (source.isEmpty()) return {};
    const auto parent = QFileInfo(source).absolutePath();
    return !workspace.isEmpty() && inside(QDir(workspace).relativeFilePath(normalized(source))) ? normalized(workspace) : parent;
}
QString DocumentBlockStore::pathFor(const QString& source, const QString& workspace) {
    if (source.isEmpty()) return {};
    const auto root = rootFor(source, workspace);
    return QDir(root).filePath(".zeroslack/editor-blocks/" + QDir(root).relativeFilePath(normalized(source)) + ".json");
}
QByteArray DocumentBlockStore::fileRevision(const QString& path, QString* error) {
    QFile file(path); if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return {}; }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) { if (error) *error = "Cannot read the complete file revision."; return {}; }
    return hash.result();
}
bool DocumentBlockStore::fail(const QString& message, QString* error) { if (error) *error = message; model->setError(message); return false; }
bool DocumentBlockStore::needed() const { return !model->items().isEmpty() || model->dirty() || !observed.isEmpty() || blocked(); }
bool DocumentBlockStore::bind(const QString& file, const QString& workspace, QString* error) {
    if (bound) return !blocked();
    bound = true; source = file.isEmpty() ? QString() : normalized(file); root = rootFor(file, workspace); sidecar = pathFor(file, workspace);
    savedMetadata = model->serialize();
    return source.isEmpty() || read(sidecar, false, error);
}
bool DocumentBlockStore::read(const QString& path, bool explicitAttach, QString* error) {
    QString issue; QByteArray bytes;
    const bool hadAssociations = !observed.isEmpty();
    if (!readBytes(path, &bytes, &issue)) { blockReason = issue; return fail(issue, error); }
    // Ordinary source-only opens do not need another full-file read/hash.
    if (!explicitAttach && !hadAssociations && bytes.isEmpty() && !QFileInfo::exists(path)) return true;
    sourceRevision = fileRevision(source, &issue);
    if (!issue.isEmpty()) { blockReason = issue; return fail(issue, error); }
    if (!explicitAttach) observed = bytes.isEmpty() && !QFileInfo::exists(path) ? QByteArray() : digest(bytes);
    if (bytes.isEmpty() && !QFileInfo::exists(path)) {
        if (!explicitAttach && hadAssociations) {
            blockReason = "The association file was removed externally. In-memory content was retained; attach associations explicitly before saving.";
            return fail(blockReason, error);
        }
        return true;
    }
    QJsonArray rows;
    if (!generations(bytes, &rows, &issue)) { blockReason = issue; return fail(issue, error); }
    int matched = -1;
    for (int i = 0; i < rows.size(); ++i) if (rows[i].toObject().value("sourceSha256").toString() == revisionText(sourceRevision)) { matched = i; break; }
    auto metadata = rows[matched < 0 ? 0 : matched].toObject().value("metadata").toObject();
    if (matched < 0) {
        auto items = metadata.value("blocks").toArray();
        for (int i = 0; i < items.size(); ++i) { auto item = items[i].toObject(); item["missing"] = true; items[i] = item; }
        metadata["blocks"] = items;
    }
    QHash<QString,QByteArray> importedAssets;
    if (explicitAttach) {
        const int split = path.indexOf("/.zeroslack/editor-blocks/");
        const auto importedRoot = split < 0 ? QString() : path.left(split);
        for (const auto& value : metadata.value("blocks").toArray()) {
            const auto asset = value.toObject().value("asset").toString(); if (asset.isEmpty()) continue;
            QByteArray bytes;
            if (importedRoot.isEmpty() || !assetName(asset) || !readBytes(QDir(importedRoot).filePath(".zeroslack/editor-blocks/assets/" + asset), &bytes, &issue, 50 * 1024 * 1024)
                || digest(bytes).toHex() != asset.left(64).toLatin1()) return fail("The selected association image is missing or changed.", error);
            importedAssets.insert(asset,bytes);
        }
    }
    if (!model->restore(metadata, &issue)) { blockReason = issue; return fail(issue, error); }
    savedMetadata = metadata; canAcceptMatching = matched >= 0;
    blockReason = matched < 0 ? QStringLiteral("The source revision does not match its associations. Content was retained with missing anchors; attach explicitly before saving.")
        : matched > 0 ? QStringLiteral("An interrupted save contains a newer association generation. The matching saved generation is displayed; review it before saving.") : QString();
    if (explicitAttach) {
        for (auto it = importedAssets.cbegin(); it != importedAssets.cend(); ++it) pendingAssets.insert(it.key(),it.value());
        blockReason.clear(); model->markUnsaved();
    }
    model->setError(blockReason);
    if (!blockReason.isEmpty() && error) *error = blockReason;
    return blockReason.isEmpty();
}
bool DocumentBlockStore::reload(QString* error) {
    blockReason.clear(); canAcceptMatching = false;
    model->resetHistory("The source was reloaded; previous association undo history is no longer available.");
    if (sidecar.isEmpty()) return true;
    return read(sidecar, false, error);
}
bool DocumentBlockStore::attach(const QString& path, QString* error) { return read(normalized(path), true, error); }
bool DocumentBlockStore::acceptMatchingGeneration(QString* error) {
    if (!canAcceptMatching) return fail("No saved association generation matches this source. Use Attach associations to review and relocate it.", error);
    blockReason.clear(); model->setError({}); model->markUnsaved(); return true;
}
QString DocumentBlockStore::importImage(const QString& path, QString* error) {
    QByteArray bytes; if (!readBytes(path, &bytes, error, 50 * 1024 * 1024) || bytes.isEmpty()) return {};
    QBuffer input(&bytes); input.open(QIODevice::ReadOnly); QImageReader reader(&input);
    const auto size = reader.size();
    if (!reader.canRead() || !size.isValid() || qint64(size.width()) * size.height() > 50000000) { fail("Unsupported image or image dimensions exceed the limit.", error); return {}; }
    const QString name = QString::fromLatin1(digest(bytes).toHex()) + '.' + QString::fromLatin1(reader.format()).toLower();
    if (!assetName(name)) { fail("Unsupported image format.", error); return {}; }
    pendingAssets.insert(name, bytes); return name;
}
QString DocumentBlockStore::assetPath(const QString& asset) const { return root.isEmpty() || !assetName(asset) ? QString() : QDir(root).filePath(".zeroslack/editor-blocks/assets/" + asset); }
QString DocumentBlockStore::importBytes(const QByteArray& bytes,const QString& suffix,QString* error) {
    if (bytes.isEmpty()) { fail("The resource is empty; no block was inserted.",error); return {}; }
    QString extension=suffix.toLower();
    if (extension.isEmpty() || extension.size()>12 || std::any_of(extension.cbegin(),extension.cend(),[](QChar c) { return !c.isLetterOrNumber(); })) extension="bin";
    const auto name=QString::fromLatin1(digest(bytes).toHex())+'.'+extension;
    return stageAsset(name,bytes,error) ? name : QString();
}
QString DocumentBlockStore::importFile(const QString& path,QString* error) {
    if (!QFileInfo(path).isFile()) { fail("Choose a readable file, not a folder.",error); return {}; }
    QByteArray bytes; if(!readBytes(path,&bytes,error,50*1024*1024)) return {};
    return importBytes(bytes,QFileInfo(path).suffix(),error);
}
QByteArray DocumentBlockStore::assetBytes(const QString& asset, QString* error) const {
    QByteArray bytes;
    if (!assetName(asset)) { if (error) *error = "Invalid image resource reference."; return {}; }
    if (pendingAssets.contains(asset)) bytes = pendingAssets.value(asset);
    else if (assetPath(asset).isEmpty() || !readBytes(assetPath(asset), &bytes, error, 50 * 1024 * 1024)) return {};
    if (digest(bytes).toHex() != asset.left(64).toLatin1()) { if (error) *error = "Image resource is missing or changed."; return {}; }
    return bytes;
}
bool DocumentBlockStore::validateStorage(QString* error) const {
    if (blocked()) { if (error) *error=blockReason; return false; }
    if (sidecar.isEmpty()) return true;
    QString issue;
    const auto actual=fileRevision(sidecar,&issue);
    if (!issue.isEmpty() || actual!=observed) {
        if (error) *error="Association storage changed externally. Reload or resolve it before transferring blocks. "+issue;
        return false;
    }
    return true;
}
bool DocumentBlockStore::stageAsset(const QString& name, const QByteArray& bytes, QString* error) {
    if (!assetName(name) || bytes.size()>50*1024*1024 || digest(bytes).toHex()!=name.left(64).toLatin1())
        return fail("The block resource is invalid or exceeds 50 MiB.",error);
    const auto path=assetPath(name);
    if (!path.isEmpty() && QFileInfo::exists(path) && fileRevision(path).toHex()!=name.left(64).toLatin1())
        return fail("The destination resource changed externally. Both documents were preserved.",error);
    pendingAssets.insert(name,bytes); return true;
}
bool DocumentBlockStore::writeAssets(const QString& targetRoot, const QJsonObject& metadata, QString* error) const {
    for (const auto& value : metadata.value("blocks").toArray()) {
        const auto asset = value.toObject().value("asset").toString(); if (asset.isEmpty()) continue;
        const auto bytes = assetBytes(asset, error); if (bytes.isEmpty()) return false;
        const auto path = QDir(targetRoot).filePath(".zeroslack/editor-blocks/assets/" + asset);
        if (QFileInfo::exists(path)) {
            QString issue; if (fileRevision(path, &issue).toHex() != asset.left(64).toLatin1()) { if (error) *error = "Existing image resource changed; it was preserved."; return false; }
        } else if (!atomicWrite(path, bytes, error)) return false;
    }
    return true;
}
bool DocumentBlockStore::save(const QString& file, const QString& workspace, const QByteArray& expectedSource,
    const QByteArray& nextSource, bool copyIdentity, const SourceWriter& writer, const Guard& revalidate, QString* error) {
    QString issue; if (error) error->clear();
    if (!model->flushContents(error)) return false;
    const auto target = normalized(file), targetRoot = rootFor(target, workspace), targetSidecar = pathFor(target, workspace);
    if (blocked()) return fail(blockReason, error);
    if (!QDir().mkpath(QFileInfo(targetSidecar).absolutePath())) return fail("Cannot create the association directory; nothing was saved.", error);
    QLockFile sourceLock(target + ".zeroslack-blocks.lock"), associationLock(targetSidecar + ".lock");
    if (!sourceLock.tryLock(0) || !associationLock.tryLock(0)) return fail("Source or association storage is busy; nothing was saved.", error);
    const bool sameStore = targetSidecar.compare(sidecar, Qt::CaseInsensitive) == 0;
    QByteArray original;
    if (!readBytes(targetSidecar, &original, &issue)) return fail(issue, error);
    const bool existed = QFileInfo::exists(targetSidecar);
    const auto originalHash = existed ? digest(original) : QByteArray();
    if ((sameStore && originalHash != observed) || (!sameStore && existed)) return fail("Association revision conflict; the external file and unsaved changes were preserved.", error);
    if (fileRevision(target, &issue) != expectedSource || !issue.isEmpty()) return fail("Source revision conflict; nothing was saved. " + issue, error);
    const auto modelRevision = model->revision();
    auto metadata = model->serialize(); metadata["sourcePath"] = QDir(targetRoot).relativeFilePath(target);
    if (copyIdentity) metadata["documentId"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject previous = savedMetadata;
    if (!sameStore) { previous = metadata; previous["blocks"] = QJsonArray(); }
    previous["sourcePath"] = QDir(targetRoot).relativeFilePath(target);
    QJsonArray rows{QJsonObject{{"sourceSha256", revisionText(nextSource)}, {"metadata", metadata}}};
    if (expectedSource != nextSource) rows.append(QJsonObject{{"sourceSha256", revisionText(expectedSource)}, {"metadata", previous}});
    const QByteArray serialized = QJsonDocument(QJsonObject{{"format", "zeroslack.editor-blocks"}, {"version", 1}, {"generations", rows}}).toJson(QJsonDocument::Indented);
    if (serialized.size() > 16 * 1024 * 1024) return fail("Association data exceeds the size limit.", error);
    if (!writeAssets(targetRoot, metadata, &issue)) return fail(issue, error);
    QSaveFile prepared(targetSidecar); prepared.setDirectWriteFallback(false);
    if (!prepared.open(QIODevice::WriteOnly) || prepared.write(serialized) != serialized.size()) return fail(prepared.errorString(), error);
    bool published = false;
    const Guard publish = [&](QString* reason) {
        QString check;
        if (model->revision() != modelRevision || fileRevision(target, &check) != expectedSource || !check.isEmpty()) { if (reason) *reason = "The document changed during save; unsaved content was preserved."; return false; }
        if (revalidate && !revalidate(reason)) return false;
        const auto actual = fileRevision(targetSidecar, &check);
        if (!check.isEmpty() || actual != (published ? digest(serialized) : originalHash)) { if (reason) *reason = "Association data changed during save; the external version was preserved."; return false; }
        if (!published) { if (!prepared.commit()) { if (reason) *reason = prepared.errorString(); return false; } published = true; }
        return true;
    };
    const bool saved = writer ? writer(publish, &issue) : publish(&issue);
    if (!saved || !published || fileRevision(target) != nextSource) {
        if (published && fileRevision(targetSidecar) == digest(serialized) && fileRevision(target) == expectedSource) {
            QString rollbackIssue;
            if (!(existed ? atomicWrite(targetSidecar, original, &rollbackIssue) : QFile::remove(targetSidecar)))
                issue += " The matching old/new association generations remain available for recovery. " + rollbackIssue;
        }
        return fail(issue.isEmpty() ? "Save did not complete; the page still has unsaved changes." : issue, error);
    }
    source = target; root = targetRoot; sidecar = targetSidecar; bound = true;
    observed = digest(serialized); sourceRevision = nextSource; savedMetadata = metadata;
    for (const auto& block : model->items()) if (!block.asset.isEmpty()) pendingAssets.remove(block.asset);
    model->setDocumentId(metadata.value("documentId").toString()); model->markSaved(); model->setError({});
    return true;
}
bool DocumentBlockStore::relocate(const QString& file, const QString& workspace, QString* error) {
    if (source.isEmpty() || normalized(file) == source) return true;
    const auto oldSidecar = sidecar; const auto oldObserved = observed;
    if (!needed()) { source = normalized(file); root = rootFor(file, workspace); sidecar = pathFor(file, workspace); return true; }
    QString issue; const auto current = fileRevision(file, &issue);
    if (!issue.isEmpty() || current != sourceRevision) return fail("Cannot relocate associations: the source revision changed. " + issue, error);
    if (!save(file, workspace, current, current, false, {}, {}, error)) return false;
    if (oldSidecar != sidecar && !oldObserved.isEmpty()) {
        QLockFile lock(oldSidecar + ".lock");
        if (!lock.tryLock(0) || fileRevision(oldSidecar) != oldObserved || !QFile::remove(oldSidecar))
            return fail("The source and new associations were moved; the previous association file was preserved because cleanup could not be confirmed.", error);
    }
    return true;
}
QJsonObject DocumentBlockStore::recoveryState(QString* error, const QString& recoveryDirectory) {
    if (!model->flushContents(error)) return {};
    if (!needed()) return {};
    const auto recoveryRoot = QDir(recoveryDirectory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : recoveryDirectory)
        .filePath("editor-block-recovery/" + model->documentId());
    if (!writeAssets(recoveryRoot, model->serialize(), error)) return {};
    return {{"metadata", model->serialize()}, {"assetRoot", recoveryRoot}};
}
bool DocumentBlockStore::restoreRecovery(const QJsonObject& value, QString* error) {
    if (value.isEmpty()) return true;
    const auto metadata = value.value("metadata").toObject();
    const auto assetRoot = value.value("assetRoot").toString();
    QHash<QString,QByteArray> imported;
    for (const auto& item : metadata.value("blocks").toArray()) {
        const auto asset = item.toObject().value("asset").toString(); if (asset.isEmpty()) continue;
        QByteArray bytes;
        if (!assetName(asset) || !readBytes(QDir(assetRoot).filePath(".zeroslack/editor-blocks/assets/" + asset), &bytes, error, 50 * 1024 * 1024)
            || digest(bytes).toHex() != asset.left(64).toLatin1()) return fail("A recovered image is missing or changed; the recovery record was preserved.", error);
        imported.insert(asset, bytes);
    }
    if (!model->restore(metadata, error)) return false;
    for (auto it = imported.cbegin(); it != imported.cend(); ++it) pendingAssets.insert(it.key(),it.value());
    model->markUnsaved(); return true;
}

bool DocumentBlockStore::renameWithAssociations(const QString& workspace,
    const QMap<QString,QPair<QString,QByteArray>>& files, const Guard& renameSource, QString* error) {
    struct Move { QString before, after; QByteArray original, next; bool same, published = false; std::unique_ptr<QSaveFile> prepared; };
    std::vector<Move> moves;
    std::vector<std::unique_ptr<QLockFile>> locks;
    QString issue;
    const auto fail = [&](const QString& message) { if (error) *error = message; return false; };
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const auto before = pathFor(it.key(),workspace); if (!QFileInfo::exists(before)) continue;
        const auto after = pathFor(it.value().first,workspace);
        const bool same = before.compare(after,Qt::CaseInsensitive) == 0;
        if (!same && QFileInfo::exists(after)) return fail("The renamed source already has association data; both records were preserved.");
        if (!QDir().mkpath(QFileInfo(after).absolutePath())) return fail("Cannot prepare the renamed association directory.");
        QStringList paths{before + ".lock"}; if (!same) paths << after + ".lock"; paths.sort();
        for (const auto& path : paths) {
            auto lock = std::make_unique<QLockFile>(path);
            if (!lock->tryLock(0)) return fail("Association storage is busy; the source was not renamed.");
            locks.push_back(std::move(lock));
        }
        QByteArray original; QJsonArray rows;
        if (!readBytes(before,&original,&issue) || !generations(original,&rows,&issue)) return fail(issue);
        if (rows[0].toObject().value("sourceSha256").toString() != revisionText(it.value().second))
            return fail("Associations do not match the source generation; review them before renaming.");
        for (int index = 0; index < rows.size(); ++index) {
            auto row = rows[index].toObject(); auto metadata = row.value("metadata").toObject();
            metadata["sourcePath"] = QDir(workspace).relativeFilePath(it.value().first); row["metadata"] = metadata; rows[index] = row;
        }
        const auto next = QJsonDocument(QJsonObject{{"format","zeroslack.editor-blocks"},{"version",1},{"generations",rows}}).toJson(QJsonDocument::Indented);
        auto prepared = std::make_unique<QSaveFile>(after); prepared->setDirectWriteFallback(false);
        if (!prepared->open(QIODevice::WriteOnly) || prepared->write(next) != next.size()) return fail(prepared->errorString());
        moves.push_back({before,after,original,next,same,false,std::move(prepared)});
    }
    const auto rollback = [&] {
        for (auto& move : moves) if (move.published && fileRevision(move.after) == digest(move.next)) {
            if (!(move.same ? atomicWrite(move.before,move.original,&issue) : QFile::remove(move.after))) issue += " A prepared association copy was retained.";
        }
    };
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        if (fileRevision(it.key(),&issue) != it.value().second || !issue.isEmpty()) return fail("The source changed during rename preparation. " + issue);
    for (auto& move : moves) {
        if (fileRevision(move.before) != digest(move.original) || (!move.same && QFileInfo::exists(move.after))) {
            rollback(); return fail("Association data changed during rename preparation; external data was preserved.");
        }
        if (!move.prepared->commit()) { issue = move.prepared->errorString(); rollback(); return fail(issue); }
        move.published = true;
    }
    if (!renameSource(&issue)) { rollback(); return fail(issue); }
    for (const auto& move : moves) if (!move.same) {
        if (fileRevision(move.before) != digest(move.original) || !QFile::remove(move.before))
            return fail("The source was renamed and its new associations are present. The previous association file could not be retired and was preserved.");
    }
    return true;
}
