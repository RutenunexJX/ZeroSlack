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

WebDocumentPage::WebDocumentPage(Kind type, QWidget* parent, const Options& options)
    : QWidget(parent), kind(type), id(token()), session(token()) {
    setObjectName(QStringLiteral("webDocumentPage"));
    setProperty("documentViewId", id);
    setProperty("editorViewId", id);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto* strip = new QWidget(this); auto* row = new QHBoxLayout(strip); row->setContentsMargins(10, 5, 10, 5);
    notice = new QLabel(QStringLiteral("Loading document editor…"), strip); notice->setWordWrap(true);
    reloadButton = new QPushButton(QStringLiteral("Reload from Disk"), strip);
    keepButton = new QPushButton(QStringLiteral("Keep Local Changes"), strip);
    row->addWidget(notice, 1); row->addWidget(reloadButton); row->addWidget(keepButton);
    reloadButton->hide(); keepButton->hide(); layout->addWidget(strip);
    const QString runtime = options.runtimeDirectory.isEmpty()
        ? QCoreApplication::applicationDirPath() + QStringLiteral("/resources/document-editors") : options.runtimeDirectory;
    const QString profileRoot = qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT");
    const QString profile = options.profileDirectory.isEmpty()
        ? (profileRoot.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) : profileRoot) + QStringLiteral("/document-webview") : options.profileDirectory;
    web = new WebEditorContainer(runtime, profile, kind == Kind::Markdown ? QStringLiteral("markdown") : QStringLiteral("blocks"), this);
    web->setAssetResolver([this](const QUrl& url) { return asset(url); });
    layout->addWidget(web, 1); setFocusProxy(web);
    connect(web, &WebEditorContainer::messageReceived, this, &WebDocumentPage::receive);
    connect(web, &WebEditorContainer::activated, this, [this] { checkExternalFile(); emit activated(); });
    connect(web, &WebEditorContainer::commandRequested, this, [this](const QString& command) {
        if (operation) return;
        if (command == QStringLiteral("save") || command == QStringLiteral("saveAs")) emit saveRequested(command == QStringLiteral("saveAs"));
        else if (command == QStringLiteral("undo") || command == QStringLiteral("redo")) executeCommand(command);
        else emit fileCommandRequested(command);
    });
    connect(web, &WebEditorContainer::externalLinkRequested, this, [](const QUrl& url) { QDesktopServices::openUrl(url); });
    connect(web, &WebEditorContainer::initializationFailed, this, [this](const QString& text) {
        failure = text; initialized = false; showNotice(text); startupTimer->stop(); emit statusChanged();
    });
    connect(reloadButton, &QPushButton::clicked, this, &WebDocumentPage::reloadFile);
    connect(keepButton, &QPushButton::clicked, this, &WebDocumentPage::keepLocal);
    connect(&watcher, &QFileSystemWatcher::fileChanged, this, [this] { checkExternalFile(); watchFile(); });
    connect(&watcher, &QFileSystemWatcher::directoryChanged, this, [this] { checkExternalFile(); watchFile(); });
    startupTimer = new QTimer(this); startupTimer->setSingleShot(true); startupTimer->setInterval(30000);
    connect(startupTimer, &QTimer::timeout, this, [this] {
        if (!initialized) { failure = QStringLiteral("The document editor did not finish loading. Restore the editor resources and reopen this tab."); showNotice(failure); }
    });
    startupTimer->start();
    recoveryTimer = new QTimer(this); recoveryTimer->setSingleShot(true); recoveryTimer->setInterval(1500);
    connect(recoveryTimer, &QTimer::timeout, this, [this] { if (dirty() && !operation) emit recoveryCheckpointRequested(); });
}
WebDocumentPage::~WebDocumentPage() { session.clear(); web->shutdown(); }
bool WebDocumentPage::supportsFile(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown") || suffix == QStringLiteral("zblock");
}
WebDocumentPage::Kind WebDocumentPage::kindForFile(const QString& path) {
    return QFileInfo(path).suffix().compare(QStringLiteral("zblock"), Qt::CaseInsensitive) == 0 ? Kind::Blocks : Kind::Markdown;
}
QString WebDocumentPage::title() const {
    const QString name = path.isEmpty() ? (kind == Kind::Markdown ? QStringLiteral("Untitled.md") : QStringLiteral("Untitled.zblock")) : QFileInfo(path).fileName();
    return name + (dirty() ? QStringLiteral(" *") : QString()) + (externalConflict ? QStringLiteral(" [disk changed]") : QString());
}
bool WebDocumentPage::validateBlockPackage(const QJsonObject& value, QString* error) {
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
bool WebDocumentPage::setInitialContent(const QString& text, QString* error) {
    if (initialized || operation) { if (error) *error = QStringLiteral("The document is already initialized."); return false; }
    if (kind == Kind::Blocks && !text.isEmpty()) {
        QJsonParseError parse; const auto value = QJsonDocument::fromJson(text.toUtf8(), &parse);
        if (parse.error != QJsonParseError::NoError || !value.isObject()) { if (error) *error = QStringLiteral("The block document is not valid JSON."); return false; }
        if (!validateBlockPackage(value.object(), error)) return false;
    }
    initialContent = text; untitledDirty = !text.isEmpty();
    if (hello) initializeEditor();
    return true;
}
bool WebDocumentPage::openFile(const QString& name, QString* error) {
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
void WebDocumentPage::initializeEditor() {
    QJsonObject message{{"type", "initialize"}, {"session", session}, {"document", id}, {"kind", kind == Kind::Markdown ? "markdown" : "blocks"},
        {"content", initialContent}, {"appearance", appearance}, {"importMarkdown", !importSource.isEmpty()}, {"dark", palette().color(QPalette::Window).lightness() < 128}};
    web->postMessage(message);
}
void WebDocumentPage::receive(const QJsonObject& message) {
    const QString type = message.value("type").toString();
    if (type == QStringLiteral("hello") && !hello) { hello = true; initializeEditor(); return; }
    if (message.value("session").toString() != session || message.value("document").toString() != id) return;
    if (type == QStringLiteral("ready")) {
        initialized = true; startupTimer->stop(); initialContent.clear();
        protectedContent = message.value("protected").toBool();
        showNotice(message.value("notice").toString()); emit statusChanged();
    } else if (type == QStringLiteral("changed")) {
        const auto next = message.value("revision").toInteger(-1);
        if (next < 0 || quint64(next) < revision) return;
        revision = quint64(next); emit statusChanged();
        if (dirty() && !recoveryTimer->isActive()) recoveryTimer->start();
    } else if (type == QStringLiteral("snapshot") || type == QStringLiteral("snapshot-error")) {
        emit snapshotArrived(message.value("request").toString(), message);
    } else if (type == QStringLiteral("error")) {
        failure = message.value("message").toString(); showNotice(failure);
        if (!initialized) startupTimer->stop();
    } else if (type == QStringLiteral("open-link")) {
        const QUrl url(message.value("url").toString());
        if (url.scheme() == "https" || url.scheme() == "http") QDesktopServices::openUrl(url);
    } else if (type == QStringLiteral("font-zoom") && initialized) {
        const int steps = message.value("steps").toInt();
        if (steps == 1 || steps == -1) emit fontZoomRequested(steps);
    } else if (type == QStringLiteral("write-clipboard") && initialized) {
        QJsonObject response{{"type", "response"}, {"session", session}, {"document", id}, {"request", message.value("request")}};
        const QString text = message.value("text").toString(), html = message.value("html").toString();
        const auto imageBytes = QByteArray::fromBase64Encoding(message.value("image").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        const QImage image = imageBytes ? QImage::fromData(imageBytes.decoded) : QImage();
        if (text.size() + html.size() > 24 * 1024 * 1024 || imageBytes.decoded.size() > 24 * 1024 * 1024
            || (!message.value("image").toString().isEmpty() && image.isNull())) {
            response.insert("error", QStringLiteral("The clipboard content is invalid or exceeds 24 MiB."));
        } else if (!text.isEmpty() || !html.isEmpty() || !image.isNull()) {
            auto* mime = new QMimeData;
            if (!text.isEmpty()) mime->setText(text);
            if (!html.isEmpty()) mime->setHtml(html);
            if (!image.isNull()) mime->setImageData(image);
            QApplication::clipboard()->setMimeData(mime);
            if (!QApplication::clipboard()->ownsClipboard()) response.insert("error", QStringLiteral("The system clipboard is unavailable. Retry copying; the selection was preserved."));
        }
        web->postMessage(response);
    } else if (type == QStringLiteral("read-import-image") && !importSource.isEmpty()) {
        const auto source = asset(QUrl(QStringLiteral("https://assets.zeroslack.invalid/") + message.value("path").toString()));
        QJsonObject response{{"type", "response"}, {"session", session}, {"document", id}, {"request", message.value("request")}};
        if (source.status != 200) response.insert("error", QStringLiteral("The source image is unavailable: %1").arg(message.value("path").toString()));
        else { response.insert("data", QString::fromLatin1(source.bytes.toBase64())); response.insert("mime", QString::fromUtf8(source.contentType)); }
        web->postMessage(response);
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
        web->postMessage(response);
    }
}
bool WebDocumentPage::snapshot(bool freeze, QJsonObject* result, QString* error, const QString& mode) {
    if (!initialized) { if (error) *error = failure.isEmpty() ? QStringLiteral("The document editor is still loading.") : failure; return false; }
    const QString request = token(), expectedSession = session;
    QEventLoop loop; QTimer timeout; timeout.setSingleShot(true); bool received = false;
    QPointer<WebDocumentPage> guard(this);
    const auto connection = connect(this, &WebDocumentPage::snapshotArrived, &loop, [&](const QString& reply, const QJsonObject& value) {
        if (reply == request) { *result = value; received = true; loop.quit(); }
    });
    connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(30000);
    const bool sent = web->postMessage({{"type", "snapshot"}, {"session", session}, {"document", id}, {"request", request}, {"freeze", freeze}, {"mode", mode}});
    if (sent && !received) loop.exec();
    disconnect(connection);
    if (!guard) return false;
    if (!received || session != expectedSession) { if (error) *error = QStringLiteral("The editor could not produce a current snapshot. Finish input and retry; your document remains open."); return false; }
    if (result->value("type") == QStringLiteral("snapshot-error")) { if (error) *error = result->value("message").toString(); return false; }
    const qint64 next = result->value("revision").toInteger(-1);
    if (next < 0 || quint64(next) < revision || !result->value("content").isString()) { if (error) *error = QStringLiteral("The document changed while its snapshot was being prepared. Retry saving."); return false; }
    revision = quint64(next); emit statusChanged(); return true;
}
void WebDocumentPage::releaseSnapshot() { web->postMessage({{"type", "release"}, {"session", session}, {"document", id}}); }
bool WebDocumentPage::sameFileGeneration(const QString& target, const QString& identity, const QByteArray& expected, bool existed, QString* error) const {
    const auto fail = [&] { if (error) *error = QStringLiteral("The file changed on disk. Review the external change or use Save As before overwriting it."); return false; };
    if (EditorFileIdentity::physicalPath(target) != identity || QFileInfo::exists(target) != existed) return fail();
    if (!existed) return true;
    QByteArray bytes; QString reason;
    if (!readBytes(target, &bytes, &reason) || digest(bytes) != expected) return fail();
    return true;
}
void WebDocumentPage::setTargetValidator(std::function<bool(const QString&, QString*)> value) { targetValidator = std::move(value); }
std::shared_ptr<void> WebDocumentPage::lockForPathMutation(quint64 expectedRevision, QString* error) {
    if (operation) { if (error) *error = QStringLiteral("Finish the current document operation first."); return {}; }
    operation = true; QJsonObject value;
    if (!snapshot(true, &value, error) || revision != expectedRevision) {
        if (error && error->isEmpty()) *error = QStringLiteral("The document changed after confirmation. Review it again.");
        operation = false; releaseSnapshot(); return {};
    }
    return std::shared_ptr<void>(new bool, [guard = QPointer<WebDocumentPage>(this)](void* value) {
        delete static_cast<bool*>(value);
        if (guard) { guard->operation = false; guard->releaseSnapshot(); }
    });
}
bool WebDocumentPage::save(bool saveAs, const QString& explicitPath) {
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
        target = QFileDialog::getSaveFileName(this, QStringLiteral("Save Document As"), path.isEmpty() ? title().remove(" *") : path,
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
    if (!TabFileIo().writeTextFile(this, targetIdentity, text, &error, &writtenDigest, nullptr, validate, format)) return failed();
    if (EditorFileIdentity::physicalPath(target) != targetIdentity) { error = QStringLiteral("The file alias changed after saving. The written file was retained; review the target before continuing."); return failed(); }
    const QString previous = path; path = EditorFileIdentity::normalized(target); physicalPath = targetIdentity; baseline = writtenDigest;
    savedRevision = revision; untitledDirty = false; externalConflict = false; watchFile();
    releaseSnapshot(); showNotice(QString()); emit identityChanged(previous, path); emit statusChanged(); emit fileSaved(path); return true;
}
bool WebDocumentPage::confirmClose() {
    if (operation) return false;
    QScopedValueRollback<bool> busy(operation, true);
    QJsonObject value; QString error;
    if (initialized && !snapshot(true, &value, &error)) { releaseSnapshot(); showNotice(error); return false; }
    if (!dirty()) { releaseSnapshot(); return true; }
    UiMessageDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Unsaved Document"));
    dialog.setText(QStringLiteral("Save changes to %1 before closing?").arg(title()));
    auto* saveButton = dialog.addButton(QStringLiteral("Save"), QMessageBox::AcceptRole);
    auto* discard = dialog.addButton(QStringLiteral("Discard"), QMessageBox::DestructiveRole);
    auto* cancel = dialog.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    dialog.setDefaultButton(saveButton); dialog.setEscapeButton(cancel); dialog.exec();
    releaseSnapshot();
    if (dialog.clickedButton() == saveButton) { operation = false; return save(); }
    return dialog.clickedButton() == discard;
}
bool WebDocumentPage::captureRecovery(QString* text, quint64* capturedRevision) {
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
bool WebDocumentPage::restoreRecovery(const QString& recovery, QString* error) {
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
void WebDocumentPage::executeCommand(const QString& command) {
    if (!initialized || operation) return;
    QJsonObject message{{"type", "command"}, {"session", session}, {"document", id}, {"command", command}};
    if (command == QStringLiteral("paste")) {
        const auto* mime = QApplication::clipboard()->mimeData();
        if (!mime) return;
        QJsonObject clipboard{{"text", mime->text()}, {"html", mime->html()}};
        if (mime->hasImage()) {
            const QImage image = qvariant_cast<QImage>(mime->imageData());
            QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
            if (!image.save(&buffer, "PNG") || bytes.size() > 24 * 1024 * 1024) { showNotice(QStringLiteral("The clipboard image cannot be inserted or exceeds 24 MiB.")); return; }
            clipboard.insert("image", QString::fromLatin1(bytes.toBase64()));
        }
        message.insert("clipboard", clipboard);
    }
    web->postMessage(message);
}
void WebDocumentPage::showNotice(const QString& text) {
    notice->setText(text); notice->parentWidget()->setVisible(!text.isEmpty() || externalConflict);
    reloadButton->setVisible(externalConflict); keepButton->setVisible(externalConflict);
}
void WebDocumentPage::watchFile() {
    if (!watcher.files().isEmpty()) watcher.removePaths(watcher.files());
    if (!watcher.directories().isEmpty()) watcher.removePaths(watcher.directories());
    if (path.isEmpty()) return;
    if (QFileInfo::exists(path)) watcher.addPath(path);
    watcher.addPath(QFileInfo(path).absolutePath());
}
void WebDocumentPage::checkExternalFile() {
    if (path.isEmpty() || operation) return;
    QString error; const bool current = sameFileGeneration(path, physicalPath, baseline, true, &error);
    if (!current) { externalConflict = true; showNotice(error); emit statusChanged(); }
}
void WebDocumentPage::reloadFile() {
    if (operation || path.isEmpty()) return;
    QScopedValueRollback<bool> busy(operation, true);
    QString error; QJsonObject snapshotValue;
    if (initialized && !snapshot(true, &snapshotValue, &error)) { releaseSnapshot(); showNotice(error); return; }
    if (dirty() && !askDiscard(this, QStringLiteral("Reload Document"), QStringLiteral("Reload the current disk file and discard local changes?"))) { releaseSnapshot(); return; }
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
    untitledDirty = externalConflict = false; hello = true;
    showNotice(QStringLiteral("Loading document editor…")); watchFile(); initializeEditor(); startupTimer->start(); emit statusChanged();
}
void WebDocumentPage::keepLocal() {
    if (operation || path.isEmpty()) return;
    QByteArray bytes; QString error;
    if (EditorFileIdentity::physicalPath(path) != physicalPath || !readBytes(path, &bytes, &error)) {
        showNotice(QStringLiteral("The file is missing, unavailable or redirected. Use Save As to preserve local changes.")); return;
    }
    const QByteArray reviewed = digest(bytes);
    UiMessageDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Keep Local Changes"));
    dialog.setText(QStringLiteral("The disk file changed outside ZeroSlack. Allow the next Save to replace that disk version with this document's content?"));
    auto* keep = dialog.addButton(QStringLiteral("Keep Local Changes"), QMessageBox::AcceptRole);
    auto* cancel = dialog.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    dialog.setDefaultButton(cancel); dialog.setEscapeButton(cancel); dialog.exec();
    if (dialog.clickedButton() != keep) return;
    if (!sameFileGeneration(path, physicalPath, reviewed, true, &error)) { showNotice(error); return; }
    baseline = reviewed; untitledDirty = true; externalConflict = false; showNotice(QString()); emit statusChanged();
}
WebEditorResource WebDocumentPage::asset(const QUrl& url) const {
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
bool WebDocumentPage::saveImages(const QString& target, const QJsonArray& images, QString* error) {
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
void WebDocumentPage::setTheme(bool dark) { if (initialized) web->postMessage({{"type", "theme"}, {"session", session}, {"document", id}, {"dark", dark}}); }
bool WebDocumentPage::importMarkdown(const QString& sourcePath, QString* error) {
    if (kind != Kind::Blocks || initialized || operation || !path.isEmpty()) return false;
    const auto source = readDocumentFile(sourcePath);
    if (!source.available) { if (error) *error = source.failureReason; return false; }
    if (source.rawBytes.size() > maxDocumentBytes) { if (error) *error = QStringLiteral("This document exceeds the 64 MiB limit."); return false; }
    importSource = EditorFileIdentity::physicalPath(sourcePath); initialContent = source.text; untitledDirty = true;
    if (hello) initializeEditor(); return true;
}
bool WebDocumentPage::exportMarkdown() {
    if (kind != Kind::Blocks || operation) return false;
    QScopedValueRollback<bool> busy(operation, true); QJsonObject value; QString error;
    const auto failed = [&] { releaseSnapshot(); showNotice(error); return false; };
    if (!snapshot(true, &value, &error, QStringLiteral("exportMarkdown"))) return failed();
    QStringList warnings; for (const auto warning : value.value("warnings").toArray()) warnings.append(warning.toString());
    UiDialogs::information(this, QStringLiteral("Export as Markdown"), warnings.join(QLatin1Char('\n')));
    QString target = QFileDialog::getSaveFileName(this, QStringLiteral("Export as Markdown"), QString(), QStringLiteral("Markdown (*.md)"));
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
    if (!TabFileIo().writeTextFile(this, identity, value.value("content").toString(), &error, nullptr, nullptr, validate, outputFormat)) return failed();
    releaseSnapshot(); showNotice(QStringLiteral("Markdown exported to %1").arg(target)); return true;
}
void WebDocumentPage::setZoom(double factor) { web->setZoomFactor(factor); }
void WebDocumentPage::setAppearance(const QString& family, int sizePt, double lineHeight) {
    appearance = {{"family", family}, {"size", qBound(8, sizePt, 48) * 4.0 / 3.0}, {"lineHeight", qBound(1.0, lineHeight, 3.0)}};
    if (initialized) web->postMessage({{"type", "appearance"}, {"session", session}, {"document", id}, {"appearance", appearance}});
}
void WebDocumentPage::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) setTheme(palette().color(QPalette::Window).lightness() < 128);
}
