#include "webdocumentsession.h"
#include "filecreation.h"
#include "webdocumentpage.h"
#include "editorfileidentity.h"
#include "tabfileio.h"
#include "uidialogs.h"
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QImage>
#include <QLabel>
#include <QMimeDatabase>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace {
constexpr qsizetype maxDocumentBytes = 64 * 1024 * 1024;
QString token() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QByteArray digest(const QByteArray& bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
bool readBytes(const QString& path, QByteArray* bytes, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxDocumentBytes) {
        if (error) *error = file.isOpen() ? QStringLiteral("This document exceeds the 64 MiB limit.") : file.errorString();
        return false;
    }
    *bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) { if (error) *error = file.errorString(); return false; }
    return true;
}
bool askDiscard(QWidget* parent, const QString& title, const QString& message) {
    UiMessageDialog dialog(parent); dialog.setWindowTitle(title); dialog.setText(message);
    auto* discard = dialog.addButton(QStringLiteral("Discard Changes"), QMessageBox::DestructiveRole);
    auto* cancel = dialog.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    dialog.setDefaultButton(cancel); dialog.setEscapeButton(cancel); dialog.exec();
    return dialog.clickedButton() == discard;
}
}

WebDocumentSession::WebDocumentSession(Kind type, const Options& value)
    : kind(type), options(value), id(value.documentIdentity.isEmpty() ? token() : value.documentIdentity), session(token()) {
    connect(&watcher, &QFileSystemWatcher::fileChanged, this, [this] { checkExternalFile(); watchFile(); });
    connect(&watcher, &QFileSystemWatcher::directoryChanged, this, [this] { checkExternalFile(); watchFile(); });
    recoveryTimer = new QTimer(this); recoveryTimer->setSingleShot(true); recoveryTimer->setInterval(1500);
    connect(recoveryTimer, &QTimer::timeout, this, [this] { if (dirty() && !operation) emit recoveryCheckpointRequested(); });
}
bool WebDocumentSession::supportsFile(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown") || suffix == QStringLiteral("zblock");
}
WebDocumentSession::Kind WebDocumentSession::kindForFile(const QString& path) {
    return QFileInfo(path).suffix().compare(QStringLiteral("zblock"), Qt::CaseInsensitive) == 0 ? Kind::Blocks : Kind::Markdown;
}
QString WebDocumentSession::title() const {
    const QString name = path.isEmpty() ? (kind == Kind::Markdown ? QStringLiteral("Untitled.md") : QStringLiteral("Untitled.zblock")) : QFileInfo(path).fileName();
    return name + (dirty() ? QStringLiteral(" *") : QString()) + (externalConflict ? QStringLiteral(" [disk changed]") : QString());
}
bool WebDocumentSession::validateBlockPackage(const QJsonObject& value, QString* error) {
    const auto fail = [&](const QString& text) { if (error) *error = text; return false; };
    if (value.value("format") != QStringLiteral("zeroslack.blocks") || value.value("version") != 1
        || value.value("engine") != QStringLiteral("blocksuite-0.22.4"))
        return fail(QStringLiteral("This block document uses an unknown format or engine version. The original file has not been changed."));
    for (const char* key : {"workspaceId", "documentId", "root", "state"})
        if (!value.value(key).isString() || value.value(key).toString().isEmpty()) return fail(QStringLiteral("The block document is incomplete: %1.").arg(QString::fromLatin1(key)));
    for (const char* key : {"root", "state"}) {
        const auto bytes = QByteArray::fromBase64Encoding(value.value(key).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!bytes || bytes.decoded.isEmpty()) return fail(QStringLiteral("The block document has invalid native state."));
    }
    if (!value.value("attachments").isArray()) return fail(QStringLiteral("The block attachment manifest is missing."));
    QSet<QString> ids;
    for (const auto item : value.value("attachments").toArray()) {
        const auto asset = item.toObject(); const QString id = asset.value("id").toString();
        const auto bytes = QByteArray::fromBase64Encoding(asset.value("data").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (id.isEmpty() || ids.contains(id) || !bytes || !asset.value("mime").isString()
            || asset.value("size").toInteger(-1) != bytes.decoded.size()
            || asset.value("sha256").toString().toLatin1() != digest(bytes.decoded).toHex())
            return fail(QStringLiteral("A block attachment is missing, duplicated or damaged: %1.").arg(id));
        ids.insert(id);
    }
    return true;
}
bool WebDocumentSession::setInitialContent(const QString& text, QString* error) {
    if (initialized || operation) { if (error) *error = QStringLiteral("The document is already initialized."); return false; }
    if (kind == Kind::Blocks && !text.isEmpty()) {
        QJsonParseError parse; const auto value = QJsonDocument::fromJson(text.toUtf8(), &parse);
        if (parse.error != QJsonParseError::NoError || !value.isObject()) { if (error) *error = QStringLiteral("The block document is not valid JSON."); return false; }
        if (!validateBlockPackage(value.object(), error)) return false;
    }
    initialContent = text; untitledDirty = !text.isEmpty();
    for (auto* view : views()) if (view->hello) initializeEditor(view);
    return true;
}
bool WebDocumentSession::openFile(const QString& name, QString* error) {
    if (initialized || operation) return false;
    const QString identity = EditorFileIdentity::physicalPath(name);
    if (kind == Kind::Markdown) {
        if (QFileInfo(name).size() > maxDocumentBytes) { if (error) *error = QStringLiteral("This document exceeds the 64 MiB limit."); return false; }
        const auto source = readDocumentFile(name);
        if (!source.available) { if (error) *error = source.failureReason; return false; }
        if (!setInitialContent(source.text, error)) return false;
        format = source.format; baseline = source.rawSha256;
    } else {
        QByteArray bytes;
        if (!readBytes(name, &bytes, error) || !setInitialContent(QString::fromUtf8(bytes), error)) return false;
        baseline = digest(bytes); format.encoding = QStringConverter::Utf8; format.byteOrderMark = false; format.lineEnding = QStringLiteral("\n");
    }
    if (EditorFileIdentity::physicalPath(name) != identity) { if (error) *error = QStringLiteral("The file target changed while it was being opened."); return false; }
    path = EditorFileIdentity::normalized(name); physicalPath = identity;
    untitledDirty = false; watchFile(); return true;
}
void WebDocumentSession::initializeEditor(WebDocumentPage* view) {
    if (!view || !pages.contains(view) || !view->hello) return;
    view->initialized = false; view->startupTimer->start();
    view->web->postMessage({{"type", "initialize"}, {"session", session}, {"document", id},
        {"view", view->viewId}, {"kind", kind == Kind::Markdown ? "markdown" : "blocks"},
        {"content", initialContent}, {"appearance", view->appearance}, {"importMarkdown", !importSource.isEmpty()},
        {"dark", view->palette().color(QPalette::Window).lightness() < 128}});
}
void WebDocumentSession::receive(WebDocumentPage* view, const QJsonObject& message) {
    if (!view || !pages.contains(view)) return;
    const QString type = message.value("type").toString();
    if (type == QStringLiteral("hello") && !view->hello) { view->hello = true; initializeEditor(view); return; }
    if (message.value("session").toString() != session || message.value("document").toString() != id) return;
    if (type == QStringLiteral("view-state")) {
        if (!view->initialized) return;
        view->presentationState = message.value("state").toObject(); emit view->viewStateChanged();
    } else if (type == QStringLiteral("ready")) {
        initialized = view->initialized = true; view->startupTimer->stop();
        protectedContent = message.value("protected").toBool();
        showNotice(message.value("notice").toString()); emit statusChanged();
        if (!view->presentationState.isEmpty()) view->restoreViewState(view->presentationState);
    } else if (type == QStringLiteral("changed")) {
        const auto next = message.value("revision").toInteger(-1);
        if (next < 0 || quint64(next) < revision) return;
        revision = quint64(next); emit statusChanged();
        if (dirty() && !recoveryTimer->isActive()) recoveryTimer->start();
    } else if (type == QStringLiteral("snapshot") || type == QStringLiteral("snapshot-error") || type == QStringLiteral("prepared")) {
        emit snapshotArrived(message.value("request").toString(), message);
    } else if (type == QStringLiteral("error")) {
        failure = message.value("message").toString(); showNotice(failure);
        if (!view->initialized) view->startupTimer->stop();
    } else if (type == QStringLiteral("open-link")) {
        const QUrl url(message.value("url").toString());
        if (url.scheme() == "https" || url.scheme() == "http") QDesktopServices::openUrl(url);
    } else if (type == QStringLiteral("font-zoom") && initialized) {
        const int steps = message.value("steps").toInt();
        if (steps == 1 || steps == -1) emit view->fontZoomRequested(steps);
    } else if (type == QStringLiteral("write-clipboard") && initialized) {
        QJsonObject response{{"type", "response"}, {"session", session}, {"document", id}, {"request", message.value("request")}};
        const QString text = message.value("text").toString(), html = message.value("html").toString();
        const auto imageBytes = QByteArray::fromBase64Encoding(message.value("image").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        const QImage image = imageBytes ? QImage::fromData(imageBytes.decoded) : QImage();
        if (text.size() + html.size() > 24 * 1024 * 1024 || imageBytes.decoded.size() > 24 * 1024 * 1024
            || (!message.value("image").toString().isEmpty() && image.isNull())) {
            response.insert("error", QStringLiteral("The clipboard content is invalid or exceeds 24 MiB."));
        } else if (!text.isEmpty() || !html.isEmpty() || !image.isNull()) {
            auto* retry = new QTimer(view); retry->setInterval(50);
            const QPointer<WebDocumentSession> guard(this);
            auto attempt = [guard, view, retry, response, text, html, image, attempts = 0]() mutable {
                if (!guard || response.value("session").toString() != guard->session) { retry->stop(); retry->deleteLater(); return; }
                auto* mime = new QMimeData;
                if (!text.isEmpty()) mime->setText(text);
                if (!html.isEmpty()) mime->setHtml(html);
                if (!image.isNull()) mime->setImageData(image);
                QApplication::clipboard()->setMimeData(mime);
                if (!QApplication::clipboard()->ownsClipboard()) {
                    if (++attempts < 20) { retry->start(); return; }
                    response.insert("error", QStringLiteral("The system clipboard is unavailable. Retry copying; the selection was preserved."));
                }
                retry->stop(); retry->deleteLater(); view->web->postMessage(response);
            };
            connect(retry, &QTimer::timeout, view, std::move(attempt));
            retry->start();
            return;
        }
        view->web->postMessage(response);
    } else if (type == QStringLiteral("read-import-image") && !importSource.isEmpty()) {
        const auto source = asset(QUrl(QStringLiteral("https://assets.zeroslack.invalid/") + message.value("path").toString()));
        QJsonObject response{{"type", "response"}, {"session", session}, {"document", id}, {"request", message.value("request")}};
        if (source.status != 200) response.insert("error", QStringLiteral("The source image is unavailable: %1").arg(message.value("path").toString()));
        else { response.insert("data", QString::fromLatin1(source.bytes.toBase64())); response.insert("mime", QString::fromUtf8(source.contentType)); }
        view->web->postMessage(response);
    } else if (type == QStringLiteral("store-image") && kind == Kind::Markdown) {
        const auto bytes = QByteArray::fromBase64Encoding(message.value("data").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        const QString mime = message.value("mime").toString();
        static const QHash<QString, QString> extensions{{"image/png", "png"}, {"image/jpeg", "jpg"}, {"image/gif", "gif"}, {"image/webp", "webp"}, {"image/bmp", "bmp"}};
        QJsonObject response{{"type", "response"}, {"session", session}, {"document", id}, {"request", message.value("request")}};
        if (!bytes || bytes.decoded.size() > 24 * 1024 * 1024 || !extensions.contains(mime) || QImage::fromData(bytes.decoded).isNull()) {
            response.insert("error", QStringLiteral("Choose a valid PNG, JPEG, GIF, WebP or BMP image smaller than 24 MiB."));
        } else {
            const QString relative = QStringLiteral(".zeroslack-assets/") + QString::fromLatin1(digest(bytes.decoded).toHex()) + '.' + extensions.value(mime);
            stagedImages.insert(relative, {bytes.decoded, mime.toUtf8(), 200}); response.insert("path", relative);
        }
        view->web->postMessage(response);
    }
}
bool WebDocumentSession::snapshot(bool freeze, QJsonObject* result, QString* error, const QString& mode) {
    QPointer<WebDocumentPage> view = presentationView();
    if (!view || !view->ready()) { if (error) *error = failure.isEmpty() ? QStringLiteral("The document editor is still loading.") : failure; return false; }
    const QString expectedSession = session;
    QPointer<WebDocumentSession> guard(this);
    if (freeze) {
        for (auto* page : views()) if (!page->ready()) {
            if (error) *error = QStringLiteral("Wait for all document views to finish loading, then retry saving.");
            return false;
        }
        QSet<QString> pending; QString prepareError; QEventLoop loop; QTimer timeout;
        timeout.setSingleShot(true); timeout.start(30000);
        const auto connection = connect(this, &WebDocumentSession::snapshotArrived, &loop, [&](const QString& request, const QJsonObject& value) {
            if (!pending.remove(request)) return;
            if (value.value("type") == QStringLiteral("snapshot-error")) prepareError = value.value("message").toString();
            if (pending.isEmpty()) loop.quit();
        });
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);
        for (auto* page : views()) if (page->ready()) {
            const QString request = token(); pending.insert(request);
            if (!page->web->postMessage({{"type", "prepare"}, {"session", session}, {"document", id}, {"request", request}})) {
                prepareError = QStringLiteral("A document view is no longer available."); pending.remove(request);
            }
        }
        if (!pending.isEmpty()) loop.exec();
        disconnect(connection);
        if (!guard || !view) return false;
        if (!pending.isEmpty() || session != expectedSession || !prepareError.isEmpty()) {
            if (error) *error = prepareError.isEmpty() ? QStringLiteral("Finish input in all document views, then retry saving.") : prepareError;
            return false;
        }
    }
    const QString request = token(); QEventLoop loop; QTimer timeout; timeout.setSingleShot(true); bool received = false;
    const auto connection = connect(this, &WebDocumentSession::snapshotArrived, &loop, [&](const QString& reply, const QJsonObject& value) {
        if (reply == request) { *result = value; received = true; loop.quit(); }
    });
    connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit); timeout.start(30000);
    const bool sent = view->web->postMessage({{"type", "snapshot"}, {"session", session}, {"document", id}, {"request", request}, {"freeze", freeze}, {"mode", mode}});
    if (sent && !received) loop.exec();
    disconnect(connection);
    if (!guard) return false;
    if (!received || session != expectedSession) { if (error) *error = QStringLiteral("The editor could not produce a current snapshot. Finish input and retry; your document remains open."); return false; }
    if (result->value("type") == QStringLiteral("snapshot-error")) { if (error) *error = result->value("message").toString(); return false; }
    const qint64 next = result->value("revision").toInteger(-1);
    if (next < 0 || quint64(next) < revision || !result->value("content").isString()) { if (error) *error = QStringLiteral("The document changed while its snapshot was being prepared. Retry saving."); return false; }
    revision = quint64(next); emit statusChanged(); return true;
}
void WebDocumentSession::releaseSnapshot() {
    for (auto* view : views()) if (view->hello) view->web->postMessage({{"type", "release"}, {"session", session}, {"document", id}});
}
bool WebDocumentSession::sameFileGeneration(const QString& target, const QString& identity, const QByteArray& expected, bool existed, QString* error) const {
    const auto fail = [&] { if (error) *error = QStringLiteral("The file changed on disk. Review the external change or use Save As before overwriting it."); return false; };
    if (EditorFileIdentity::physicalPath(target) != identity || QFileInfo::exists(target) != existed) return fail();
    if (!existed) return true;
    QByteArray bytes; QString reason;
    if (!readBytes(target, &bytes, &reason) || digest(bytes) != expected) return fail();
    return true;
}
void WebDocumentSession::setTargetValidator(std::function<bool(const QString&, QString*)> value) { targetValidator = std::move(value); }
std::shared_ptr<void> WebDocumentSession::lockForPathMutation(quint64 expectedRevision, QString* error, bool requireCurrentFile) {
    if (operation) { if (error) *error = QStringLiteral("Finish the current document operation first."); return {}; }
    if (requireCurrentFile && !sameFileGeneration(path, physicalPath, baseline, true, error)) return {};
    operation = true; QJsonObject value;
    if (!snapshot(true, &value, error) || revision != expectedRevision) {
        if (error && error->isEmpty()) *error = QStringLiteral("The document changed after confirmation. Review it again.");
        operation = false; releaseSnapshot(); return {};
    }
    return std::shared_ptr<void>(new bool, [guard = QPointer<WebDocumentSession>(this)](void* value) {
        delete static_cast<bool*>(value);
        if (guard) { guard->operation = false; guard->releaseSnapshot(); }
    });
}
bool WebDocumentSession::retargetAfterRename(const QString& target, QString* error) {
    if (!operation || path.isEmpty()) { if (error) *error = QStringLiteral("The document rename was not prepared."); return false; }
    const QString identity = EditorFileIdentity::physicalPath(target);
    if (!sameFileGeneration(target, identity, baseline, true, error)) return false;
    if (targetValidator && !targetValidator(target, error)) return false;
    const auto previous = path; path = EditorFileIdentity::normalized(target); physicalPath = identity;
    externalConflict = false; watchFile(); showNotice(QString());
    emit identityChanged(previous, path); emit statusChanged();
    if (dirty()) recoveryTimer->start();
    return true;
}
bool WebDocumentSession::save(bool saveAs, const QString& explicitPath) {
    if (operation) return false;
    QScopedValueRollback<bool> busy(operation, true);
    QJsonObject value; QString error;
    const auto failed = [&] { releaseSnapshot(); showNotice(error); emit fileSaveFailed(path, error); return false; };
    if (!snapshot(true, &value, &error)) return failed();
    if (!saveAs && explicitPath.isEmpty() && !path.isEmpty() && !dirty()) {
        if (!sameFileGeneration(path, physicalPath, baseline, true, &error)) return failed();
        releaseSnapshot(); return true;
    }
    if (protectedContent) { error = QStringLiteral("This document contains syntax the visual editor cannot safely round-trip. Saving is disabled to preserve the original file."); return failed(); }
    QString target = explicitPath;
    if (target.isEmpty() && !saveAs) target = path;
    if (target.isEmpty()) {
        target = QFileDialog::getSaveFileName(dialogParent(), QStringLiteral("Save Document As"), path.isEmpty() ? title().remove(" *") : path,
            kind == Kind::Markdown ? QStringLiteral("Markdown (*.md *.markdown)") : QStringLiteral("ZeroSlack Block Document (*.zblock)"));
        if (target.isEmpty()) { releaseSnapshot(); return false; }
        if (QFileInfo(target).suffix().isEmpty()) target += kind == Kind::Markdown ? QStringLiteral(".md") : QStringLiteral(".zblock");
    }
    if (!supportsFile(target) || kindForFile(target) != kind) { error = QStringLiteral("Use the document's own file extension. Conversion is a separate operation."); return failed(); }
    if (targetValidator && !targetValidator(target, &error)) return failed();
    const QString targetIdentity = EditorFileIdentity::physicalPath(target);
    const bool sameName = !path.isEmpty() && EditorFileIdentity::normalized(target).compare(path, Qt::CaseInsensitive) == 0;
    const bool same = sameName || (!path.isEmpty() && EditorFileIdentity::lookupKey(target) == EditorFileIdentity::lookupKey(physicalPath));
    const bool exists = QFileInfo::exists(target);
    if (exists && !QFileInfo(target).isWritable()) { error = QStringLiteral("This file is read-only. Use Save As to preserve your changes."); return failed(); }
    QByteArray expected = baseline;
    if (!same && exists) { QByteArray bytes; if (!readBytes(target, &bytes, &error)) return failed(); expected = digest(bytes); }
    if (same && (EditorFileIdentity::physicalPath(path) != physicalPath || !exists)) { error = QStringLiteral("The original file was moved, removed or redirected. Use Save As to keep your changes."); return failed(); }
    const QString text = value.value("content").toString();
    if (text.toUtf8().size() > maxDocumentBytes) { error = QStringLiteral("This document exceeds the 64 MiB limit."); return failed(); }
    if (kind == Kind::Blocks) {
        QJsonParseError parse; const auto package = QJsonDocument::fromJson(text.toUtf8(), &parse);
        if (parse.error != QJsonParseError::NoError || !validateBlockPackage(package.object(), &error)) return failed();
    }
    const auto validate = [&](QString* why) { return sameFileGeneration(target, targetIdentity, expected, exists, why); };
    if (kind == Kind::Markdown && !saveImages(targetIdentity, value.value("images").toArray(), &error)) return failed();
    QByteArray writtenDigest;
    if (!TabFileIo().writeTextFile(dialogParent(), targetIdentity, text, &error, &writtenDigest, nullptr, validate, format)) return failed();
    if (EditorFileIdentity::physicalPath(target) != targetIdentity) { error = QStringLiteral("The file alias changed after saving. The written file was retained; review the target before continuing."); return failed(); }
    const QString previous = path; path = EditorFileIdentity::normalized(target); physicalPath = targetIdentity; baseline = writtenDigest;
    savedRevision = revision; untitledDirty = false; externalConflict = false; watchFile();
    releaseSnapshot(); showNotice(QString()); emit identityChanged(previous, path); emit statusChanged(); emit fileSaved(path); return true;
}
bool WebDocumentSession::createFile(const QString& target, QString* error) {
    if (operation || !path.isEmpty()) { if (error) *error = QStringLiteral("The document cannot be created in its current state."); return false; }
    QScopedValueRollback<bool> busy(operation, true);
    QJsonObject value; QString why;
    const auto failed = [&] { releaseSnapshot(); if (error) *error = why; return false; };
    if (!supportsFile(target) || kindForFile(target) != kind) { why = QStringLiteral("Use the document's own file extension."); return failed(); }
    if (targetValidator && !targetValidator(target, &why)) return failed();
    if (!snapshot(true, &value, &why)) return failed();
    if (protectedContent) { why = QStringLiteral("The document cannot safely round-trip through this editor."); return failed(); }
    const auto bytes = value.value("content").toString().toUtf8();
    if (kind == Kind::Blocks && !validateBlockPackage(QJsonDocument::fromJson(bytes).object(), &why)) return failed();
    const auto result = FileCreation::create(target, bytes);
    if (!result.created()) { why = result.message; return failed(); }
    path = EditorFileIdentity::normalized(target); physicalPath = EditorFileIdentity::physicalPath(target); baseline = digest(bytes);
    format.encoding = QStringConverter::Utf8; format.byteOrderMark = false; format.lineEnding = QStringLiteral("\n");
    savedRevision = revision; untitledDirty = false; externalConflict = false; watchFile(); releaseSnapshot();
    showNotice(QString()); emit identityChanged(QString(), path); emit statusChanged(); emit fileSaved(path); return true;
}
bool WebDocumentSession::confirmClose() {
    if (operation) return false;
    QScopedValueRollback<bool> busy(operation, true);
    QJsonObject value; QString error;
    if (initialized && !snapshot(true, &value, &error)) { releaseSnapshot(); showNotice(error); return false; }
    if (!dirty()) { releaseSnapshot(); return true; }
    UiMessageDialog dialog(dialogParent()); dialog.setWindowTitle(QStringLiteral("Unsaved Document"));
    dialog.setText(QStringLiteral("Save changes to %1 before closing?").arg(title()));
    auto* saveButton = dialog.addButton(QStringLiteral("Save"), QMessageBox::AcceptRole);
    auto* discard = dialog.addButton(QStringLiteral("Discard"), QMessageBox::DestructiveRole);
    auto* cancel = dialog.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    dialog.setDefaultButton(saveButton); dialog.setEscapeButton(cancel); dialog.exec();
    releaseSnapshot();
    if (dialog.clickedButton() == saveButton) { operation = false; return save(); }
    return dialog.clickedButton() == discard;
}
bool WebDocumentSession::captureRecovery(QString* text, quint64* capturedRevision) {
    if (operation || !initialized || !dirty()) return false;
    QScopedValueRollback<bool> busy(operation, true); QJsonObject value; QString error;
    if (!snapshot(false, &value, &error)) return false;
    QJsonArray images;
    if (kind == Kind::Markdown) for (const auto item : value.value("images").toArray()) {
        const QString relative = item.toString(); if (!QUrl(relative).isRelative() || relative.startsWith('/')) continue;
        const auto source = asset(QUrl(QStringLiteral("https://assets.zeroslack.invalid/") + relative));
        if (source.status == 200) images.append(QJsonObject{{"path", QUrl::fromPercentEncoding(QUrl(relative).path(QUrl::FullyEncoded).toUtf8())}, {"mime", QString::fromUtf8(source.contentType)}, {"data", QString::fromLatin1(source.bytes.toBase64())}});
    }
    const QJsonObject recovery{{"format", "zeroslack.web-recovery"}, {"version", 1}, {"kind", kind == Kind::Markdown ? "markdown" : "blocks"},
        {"content", value.value("content")}, {"images", images}, {"encoding", int(format.encoding)}, {"bom", format.byteOrderMark}, {"lineEnding", format.lineEnding}};
    *text = QString::fromUtf8(QJsonDocument(recovery).toJson(QJsonDocument::Compact)); *capturedRevision = revision; return true;
}
bool WebDocumentSession::restoreRecovery(const QString& recovery, QString* error) {
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(recovery.toUtf8(), &parse); const auto value = document.object();
    if (parse.error != QJsonParseError::NoError || value.value("format") != QStringLiteral("zeroslack.web-recovery") || value.value("version") != 1
        || value.value("kind") != (kind == Kind::Markdown ? QStringLiteral("markdown") : QStringLiteral("blocks"))) {
        if (error) *error = QStringLiteral("The web document recovery record has an unknown format."); return false;
    }
    const int encoding = value.value("encoding").toInt(-1);
    if (encoding != int(QStringConverter::Utf8) && encoding != int(QStringConverter::Utf16LE) && encoding != int(QStringConverter::Utf16BE)
        && encoding != int(QStringConverter::Utf16) && encoding != int(QStringConverter::Latin1)) { if (error) *error = QStringLiteral("Unsupported recovery encoding."); return false; }
    QHash<QString, WebEditorResource> images;
    for (const auto item : value.value("images").toArray()) {
        const auto image = item.toObject(); const auto bytes = QByteArray::fromBase64Encoding(image.value("data").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        const QString name = image.value("path").toString();
        if (name.isEmpty() || name.startsWith('/') || name.contains("..") || name.contains(':') || name.contains('\\') || !bytes || bytes.decoded.size() > 24 * 1024 * 1024) {
            if (error) *error = QStringLiteral("Invalid image in recovery record."); return false;
        }
        images.insert(name, {bytes.decoded, image.value("mime").toString().toUtf8(), 200});
    }
    if (!setInitialContent(value.value("content").toString(), error)) return false;
    stagedImages = std::move(images); format.encoding = QStringConverter::Encoding(encoding); format.byteOrderMark = value.value("bom").toBool();
    format.lineEnding = value.value("lineEnding").toString() == QStringLiteral("\r\n") ? QStringLiteral("\r\n") : QStringLiteral("\n");
    untitledDirty = true; return true;
}
void WebDocumentSession::showNotice(const QString& text) { noticeText = text; emit noticeChanged(text); }
void WebDocumentSession::watchFile() {
    if (!watcher.files().isEmpty()) watcher.removePaths(watcher.files());
    if (!watcher.directories().isEmpty()) watcher.removePaths(watcher.directories());
    if (path.isEmpty()) return;
    if (QFileInfo::exists(path)) watcher.addPath(path);
    watcher.addPath(QFileInfo(path).absolutePath());
}
void WebDocumentSession::checkExternalFile() {
    if (path.isEmpty() || operation) return;
    QString error; const bool current = sameFileGeneration(path, physicalPath, baseline, true, &error);
    if (!current) { externalConflict = true; showNotice(error); emit statusChanged(); }
}
void WebDocumentSession::reloadFile() {
    if (operation || path.isEmpty()) return;
    QScopedValueRollback<bool> busy(operation, true);
    QString error; QJsonObject snapshotValue;
    if (initialized && !snapshot(true, &snapshotValue, &error)) { releaseSnapshot(); showNotice(error); return; }
    if (dirty() && !askDiscard(dialogParent(), QStringLiteral("Reload Document"), QStringLiteral("Reload the current disk file and discard local changes?"))) { releaseSnapshot(); return; }
    const QString identity = EditorFileIdentity::physicalPath(path);
    QString content; QByteArray sourceDigest; DocumentFileFormat sourceFormat;
    if (kind == Kind::Markdown) {
        const auto source = readDocumentFile(path);
        if (!source.available || source.rawBytes.size() > maxDocumentBytes) {
            releaseSnapshot(); showNotice(source.available ? QStringLiteral("This document exceeds the 64 MiB limit.") : source.failureReason); return;
        }
        content = source.text; sourceFormat = source.format; sourceDigest = source.rawSha256;
    } else {
        QByteArray bytes;
        if (!readBytes(path, &bytes, &error)) { releaseSnapshot(); showNotice(error); return; }
        QJsonParseError parse; const auto value = QJsonDocument::fromJson(bytes, &parse);
        if (parse.error != QJsonParseError::NoError || !value.isObject() || !validateBlockPackage(value.object(), &error)) {
            releaseSnapshot(); showNotice(error.isEmpty() ? QStringLiteral("The block document is not valid JSON.") : error); return;
        }
        content = QString::fromUtf8(bytes); sourceDigest = digest(bytes); sourceFormat.lineEnding = QStringLiteral("\n");
    }
    if (!sameFileGeneration(path, identity, sourceDigest, true, &error)) { releaseSnapshot(); showNotice(error); return; }
    initialized = false; session = token(); revision = savedRevision = 0;
    initialContent = content; format = sourceFormat; baseline = sourceDigest; physicalPath = identity;
    stagedImages.clear(); importSource.clear(); failure.clear(); protectedContent = false;
    untitledDirty = externalConflict = false;
    for (auto* view : views()) view->initialized = false;
    showNotice(QStringLiteral("Loading document editor…")); watchFile();
    for (auto* view : views()) if (view->hello) initializeEditor(view);
    emit statusChanged();
}
void WebDocumentSession::keepLocal() {
    if (operation || path.isEmpty()) return;
    QByteArray bytes; QString error;
    if (EditorFileIdentity::physicalPath(path) != physicalPath || !readBytes(path, &bytes, &error)) {
        showNotice(QStringLiteral("The file is missing, unavailable or redirected. Use Save As to preserve local changes.")); return;
    }
    const QByteArray reviewed = digest(bytes);
    UiMessageDialog dialog(dialogParent()); dialog.setWindowTitle(QStringLiteral("Keep Local Changes"));
    dialog.setText(QStringLiteral("The disk file changed outside ZeroSlack. Allow the next Save to replace that disk version with this document's content?"));
    auto* keep = dialog.addButton(QStringLiteral("Keep Local Changes"), QMessageBox::AcceptRole);
    auto* cancel = dialog.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    dialog.setDefaultButton(cancel); dialog.setEscapeButton(cancel); dialog.exec();
    if (dialog.clickedButton() != keep) return;
    if (!sameFileGeneration(path, physicalPath, reviewed, true, &error)) { showNotice(error); return; }
    baseline = reviewed; untitledDirty = true; externalConflict = false; showNotice(QString()); emit statusChanged();
}
WebEditorResource WebDocumentSession::asset(const QUrl& url) const {
    const QString relative = QUrl::fromPercentEncoding(url.path(QUrl::FullyEncoded).toUtf8()).mid(1);
    if (relative.isEmpty() || relative.contains(':') || relative.contains('\\')) return {};
    if (stagedImages.contains(relative)) return stagedImages.value(relative);
    const QString source = importSource.isEmpty() ? path : importSource;
    if ((kind != Kind::Markdown && importSource.isEmpty()) || source.isEmpty()) return {};
    const QString root = QFileInfo(source).absoluteDir().canonicalPath();
    const QString resolved = QFileInfo(QDir(root).filePath(relative)).canonicalFilePath();
    if (resolved.isEmpty() || !resolved.startsWith(root + '/', Qt::CaseInsensitive)) return {};
    const auto mime = QMimeDatabase().mimeTypeForFile(resolved).name();
    if (!mime.startsWith(QStringLiteral("image/"))) return {};
    QFile file(resolved); if (!file.open(QIODevice::ReadOnly) || file.size() > 24 * 1024 * 1024) return {};
    return {file.readAll(), mime.toUtf8(), 200};
}
bool WebDocumentSession::saveImages(const QString& target, const QJsonArray& images, QString* error) {
    const QString targetRoot = QFileInfo(target).absolutePath();
    const bool moving = path.isEmpty() || !EditorFileIdentity::same(QFileInfo(path).absolutePath(), targetRoot);
    QSet<QString> copied;
    for (const auto value : images) {
        const QString reference = value.toString(); const QUrl url(reference);
        if (!url.isRelative() || reference.startsWith('/')) continue;
        const QString relative = QUrl::fromPercentEncoding(url.path(QUrl::FullyEncoded).toUtf8());
        if (!moving && !stagedImages.contains(relative)) continue;
        const QString clean = QDir::cleanPath(relative);
        if (clean == ".." || clean.startsWith("../") || clean.contains(':') || clean.contains('\\')) {
            if (error) *error = QStringLiteral("Copy this image into the document folder before Save As: %1").arg(reference); return false;
        }
        if (copied.contains(clean)) continue;
        copied.insert(clean);
        const auto source = asset(QUrl(QStringLiteral("https://assets.zeroslack.invalid/") + reference));
        if (source.status != 200) { if (error) *error = QStringLiteral("The relative image is unavailable: %1. Restore it before Save As.").arg(reference); return false; }
        const QString destination = QDir(targetRoot).filePath(clean);
        const QString resolvedRoot = EditorFileIdentity::physicalPath(targetRoot);
        const QString resolvedDestination = EditorFileIdentity::physicalPath(destination);
        if (!resolvedDestination.startsWith(resolvedRoot + '/', Qt::CaseInsensitive)
            || !QDir().mkpath(QFileInfo(resolvedDestination).absolutePath())) {
            if (error) *error = QStringLiteral("The image destination is outside the document folder or is unavailable."); return false;
        }
        if (QFileInfo::exists(destination)) {
            QByteArray existing; if (!readBytes(destination, &existing, error)) return false;
            if (digest(existing) != digest(source.bytes)) { if (error) *error = QStringLiteral("An existing image at the destination differs: %1. Choose another document folder.").arg(reference); return false; }
            continue;
        }
        QSaveFile file(resolvedDestination);
        if (!file.open(QIODevice::WriteOnly) || file.write(source.bytes) != source.bytes.size()) { if (error) *error = QStringLiteral("Cannot save image: %1").arg(file.errorString()); return false; }
        if (QFileInfo::exists(resolvedDestination) || EditorFileIdentity::physicalPath(destination) != resolvedDestination || EditorFileIdentity::physicalPath(targetRoot) != resolvedRoot) {
            file.cancelWriting(); if (error) *error = QStringLiteral("The image destination changed while saving. Retry in another folder."); return false;
        }
        if (!file.commit()) { if (error) *error = QStringLiteral("Cannot save image: %1").arg(file.errorString()); return false; }
    }
    return true;
}
bool WebDocumentSession::importMarkdown(const QString& sourcePath, QString* error) {
    if (kind != Kind::Blocks || initialized || operation || !path.isEmpty()) return false;
    const auto source = readDocumentFile(sourcePath);
    if (!source.available) { if (error) *error = source.failureReason; return false; }
    if (source.rawBytes.size() > maxDocumentBytes) { if (error) *error = QStringLiteral("This document exceeds the 64 MiB limit."); return false; }
    importSource = EditorFileIdentity::physicalPath(sourcePath); initialContent = source.text; untitledDirty = true;
    for (auto* view : views()) if (view->hello) initializeEditor(view); return true;
}
bool WebDocumentSession::exportMarkdown() {
    if (kind != Kind::Blocks || operation) return false;
    QScopedValueRollback<bool> busy(operation, true); QJsonObject value; QString error;
    const auto failed = [&] { releaseSnapshot(); showNotice(error); return false; };
    if (!snapshot(true, &value, &error, QStringLiteral("exportMarkdown"))) return failed();
    QStringList warnings; for (const auto warning : value.value("warnings").toArray()) warnings.append(warning.toString());
    UiDialogs::information(dialogParent(), QStringLiteral("Export as Markdown"), warnings.join(QLatin1Char('\n')));
    QString target = QFileDialog::getSaveFileName(dialogParent(), QStringLiteral("Export as Markdown"), QString(), QStringLiteral("Markdown (*.md)"));
    if (target.isEmpty()) { releaseSnapshot(); return false; }
    if (QFileInfo(target).suffix().isEmpty()) target += QStringLiteral(".md");
    if (kindForFile(target) != Kind::Markdown || !supportsFile(target) || (!path.isEmpty() && EditorFileIdentity::same(path, target))) {
        error = QStringLiteral("Choose a separate Markdown file for export."); return failed();
    }
    if (targetValidator && !targetValidator(target, &error)) return failed();
    const QString identity = EditorFileIdentity::physicalPath(target); const bool exists = QFileInfo::exists(target); QByteArray expected;
    if (exists) { QByteArray bytes; if (!readBytes(target, &bytes, &error)) return failed(); expected = digest(bytes); }
    QJsonArray images;
    for (const auto item : value.value("attachments").toArray()) {
        const auto attachment = item.toObject(); const QString relative = attachment.value("path").toString();
        const auto bytes = QByteArray::fromBase64Encoding(attachment.value("data").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!relative.startsWith("assets/") || !bytes || bytes.decoded.size() > 24 * 1024 * 1024 || !attachment.value("mime").toString().startsWith("image/")) {
            error = QStringLiteral("An exported attachment is invalid."); return failed();
        }
        stagedImages.insert(relative, {bytes.decoded, attachment.value("mime").toString().toUtf8(), 200}); images.append(relative);
    }
    if (!saveImages(identity, images, &error)) return failed();
    DocumentFileFormat outputFormat; outputFormat.lineEnding = QStringLiteral("\n");
    const auto validate = [&](QString* reason) { return sameFileGeneration(target, identity, expected, exists, reason); };
    if (!TabFileIo().writeTextFile(dialogParent(), identity, value.value("content").toString(), &error, nullptr, nullptr, validate, outputFormat)) return failed();
    releaseSnapshot(); showNotice(QStringLiteral("Markdown exported to %1").arg(target)); return true;
}

QList<WebDocumentPage*> WebDocumentSession::views() const {
    QList<WebDocumentPage*> result; for (const auto& page : pages) if (page) result.append(page); return result;
}
void WebDocumentSession::attach(WebDocumentPage* page) { if (page && !pages.contains(page)) pages.append(page); }
void WebDocumentSession::detach(WebDocumentPage* page) {
    pages.removeAll(page); if (presentation == page) presentation.clear();
    for (auto* current : views()) if (current->hello)
        current->web->postMessage({{"type", "detach-view"}, {"session", session}, {"document", id}, {"view", page->viewId}});
    initialized = false; for (auto* current : views()) initialized |= current->ready();
}
void WebDocumentSession::setPresentationView(WebDocumentPage* view) { if (pages.contains(view)) presentation = view; }
WebDocumentPage* WebDocumentSession::presentationView() const {
    if (presentation && presentation->ready()) return presentation;
    for (auto* page : views()) if (page->ready()) return page;
    return views().value(0);
}
QWidget* WebDocumentSession::dialogParent() const { return presentationView(); }
bool WebDocumentSession::prepareViewClose(WebDocumentPage* view) {
    if (operation) return false;
    setPresentationView(view);
    if (views().size() <= 1) return confirmClose();
    if (!view->ready()) return true;
    bool anotherReady = false;
    for (auto* current : views()) if (current != view && current->ready()) anotherReady = true;
    if (!anotherReady) { showNotice(QStringLiteral("Wait for the other document view to finish loading before closing this view.")); return false; }
    QScopedValueRollback<bool> busy(operation, true); QJsonObject value; QString error;
    const bool result = snapshot(true, &value, &error); releaseSnapshot();
    if (!result) showNotice(error);
    return result;
}

