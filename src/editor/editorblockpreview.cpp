#include "editorblockpreview.h"
#include "documentblockstore.h"
#include "pinloompreviewimage.h"

EditorBlockPreview::EditorBlockPreview(DocumentBlockModel* model, PinloomHostClient* client, QObject* parent)
    : QObject(parent), model(model), client(client) {}
EditorBlockPreview::~EditorBlockPreview() { cancel(); }
void EditorBlockPreview::cancel() {
    ++generation;
    if (consumer) { if (client) client->cancelReadRequests(consumer); delete consumer; }
}
void EditorBlockPreview::request(const QString& id, Handler handler) {
    cancel(); if (!model || !model->find(id)) return;
    const auto block = *model->find(id); const auto documentId = model->documentId();
    consumer = new QObject(this); const auto serial = generation;
    const QPointer<EditorBlockPreview> self(this); const QPointer<QObject> lifetime = consumer;
    const auto current = [self,lifetime,serial,id,block,documentId] {
        if (!self || !lifetime || !self->model || self->generation != serial || self->model->documentId() != documentId) return false;
        const auto* value = self->model->find(id);
        return value && value->type == block.type && value->uri == block.uri && value->asset == block.asset;
    };
    handler({{}, "Loading preview…", {}, true});
    if (!current()) return;
    if (block.type == "image") {
        auto* store = DocumentBlockStore::get(model);
        const auto bytes = store->pendingImage(block.asset);
        auto* loader = new PinloomPreviewImageLoader(consumer);
        const auto complete = [current,handler](const auto& image) { if (current()) handler({{}, image.error, image.image, false}); };
        if (!bytes.isEmpty()) loader->loadLocalImage(bytes,complete);
        else loader->loadLocalImageFile(store->assetPath(block.asset),QByteArray::fromHex(block.asset.left(64).toLatin1()),complete);
        return;
    }
    const auto identity = PinloomHostIdentity::fromUri(QUrl(block.uri));
    if (!identity.isValid() || !client) { handler({{}, "Preview is not supported for this URI. Use Open original.", {}, false}); return; }
    client->resolve(identity, [self,lifetime,current,handler](const PinloomHostDocument& document, const QString& error) {
        if (!current()) return;
        QString issue = error;
        if (issue.isEmpty() && (!document.isValid() || document.entry.deleted)) issue = "The referenced object is missing or was deleted.";
        if (!issue.isEmpty()) { handler({{},issue,{},false}); return; }
        const auto text = document.content.left(32768);
        if (!document.preview.present) { handler({text,text.isEmpty() ? QStringLiteral("No preview is available. Use Open original.") : QString(),{},false}); return; }
        if (!document.preview.isReady() || !document.preview.isImage()) {
            const auto reason = !document.preview.validationError.isEmpty() ? document.preview.validationError : !document.preview.error.isEmpty() ? document.preview.error : QStringLiteral("The reference provider has no ready preview.");
            handler({text,reason,{},false}); return;
        }
        auto* loader = new PinloomPreviewImageLoader(lifetime);
        loader->load(document.preview, [current,handler,text](const auto& result) { if (current()) handler({text,result.error,result.image,false}); });
    }, consumer);
}
