#include "editorblockpreview.h"
#include "documentblockstore.h"
#include "pinloompreviewimage.h"
#include <QAction>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMenu>

namespace {
bool supportedOriginalUrl(const QUrl& url) {
    if (!url.isValid() || url.isEmpty() || url.isRelative()) return false;
    if (url.isLocalFile()) return QFileInfo(url.toLocalFile()).isFile();
    return (url.scheme() == "http" || url.scheme() == "https") && !url.host().isEmpty();
}
}

EditorBlockPreview::EditorBlockPreview(DocumentBlockModel* model, PinloomHostClient* client, QObject* parent)
    : QObject(parent), model(model), client(client) {}
EditorBlockPreview::~EditorBlockPreview() { cancel(); }
QString EditorBlockPreview::linkOnlyText(const EditorAssociatedBlock& block) {
    if (block.type!="reference") return {};
    const QUrl url(block.uri,QUrl::StrictMode);
    if (!url.isValid() || !(url.isLocalFile() || url.scheme()=="http" || url.scheme()=="https")) return {};
    QStringList lines;
    if (!block.caption.isEmpty()) lines<<block.caption;
    lines<<block.uri<<QStringLiteral("Link only · Use Open original to view this target.");
    return lines.join('\n');
}
void EditorBlockPreview::cancel() {
    ++generation;
    if (consumer) { if (client) client->cancelReadRequests(consumer); delete consumer; }
}
void EditorBlockPreview::inspectOriginal(DocumentBlockModel* model, PinloomHostClient* client,
    const QString& id, QObject* consumer, std::function<void(const EditorBlockOriginalTarget&)> handler) {
    const auto* block = model ? model->find(id) : nullptr;
    if (!block || block->type != "reference" || !consumer) {
        handler({EditorBlockOriginalTarget::Kind::Unavailable, {}, {}, "The reference is unavailable."}); return;
    }
    const auto uri = block->uri, documentId = model->documentId();
    const QUrl url(uri, QUrl::StrictMode);
    const auto identity = PinloomHostIdentity::fromUri(url);
    if (!identity.isValid()) {
        handler(supportedOriginalUrl(url)
            ? EditorBlockOriginalTarget{EditorBlockOriginalTarget::Kind::Url, {}, url, {}}
            : EditorBlockOriginalTarget{EditorBlockOriginalTarget::Kind::Unavailable, {}, {}, "The original source is unavailable."});
        return;
    }
    if (!client) { handler({EditorBlockOriginalTarget::Kind::Unavailable, {}, {}, "Pinloom is unavailable."}); return; }
    const QPointer<DocumentBlockModel> bound(model);
    const QPointer<PinloomHostClient> provider(client);
    const QPointer<QObject> lifetime(consumer);
    const auto current = [bound,provider,lifetime,id,uri,documentId] {
        const auto* value = bound ? bound->find(id) : nullptr;
        return lifetime && provider && value && bound->documentId() == documentId
            && value->type == "reference" && value->uri == uri;
    };
    client->resolve(identity, [current,identity,handler](const PinloomHostDocument& document, const QString& error) {
        if (!current()) return;
        if (!error.isEmpty() || !document.isValid() || document.entry.deleted) {
            handler({EditorBlockOriginalTarget::Kind::Unavailable, {}, {},
                error.isEmpty() ? QStringLiteral("The referenced object is missing or was deleted.") : error});
            return;
        }
        // The provider's clip open action inserts text; only an explicit source
        // URI can supply an original-view target for a clip.
        if (!identity.clipId.isEmpty() || !document.entry.identity.clipId.isEmpty() || document.entry.type == "clip") {
            const QUrl source(document.details.value("sourceUri").toString().trimmed(), QUrl::StrictMode);
            handler(supportedOriginalUrl(source)
                ? EditorBlockOriginalTarget{EditorBlockOriginalTarget::Kind::Url, {}, source, {}}
                : EditorBlockOriginalTarget{EditorBlockOriginalTarget::Kind::Unavailable, {}, {}, "This clip has no available original source."});
            return;
        }
        const bool verifyLocation = document.details.value("locatorType").toString() == "sumatrapdf.rect";
        handler({EditorBlockOriginalTarget::Kind::Pinloom, identity, {}, {}, verifyLocation});
    }, consumer);
}
void EditorBlockPreview::bindOriginalAction(QAction* action, QMenu* menu, DocumentBlockModel* model,
    PinloomHostClient* client, const QString& id) {
    action->setObjectName("editorBlockOpenOriginal");
    menu->setToolTipsVisible(true);
    const QPointer<QAction> target(action);
    const QPointer<DocumentBlockModel> bound(model);
    const QPointer<PinloomHostClient> provider(client);
    const auto generation = std::make_shared<quint64>(0);
    const auto refresh = [target,bound,provider,id,generation](bool resolve) {
        if (!target) return;
        const auto serial = ++*generation;
        if (provider) provider->cancelReadRequests(target);
        target->setEnabled(false); target->setToolTip("Checking original source…");
        target->setStatusTip(target->toolTip());
        if (!resolve) return;
        inspectOriginal(bound,provider,id,target,[target,generation,serial](const EditorBlockOriginalTarget& original) {
            if (!target || serial != *generation) return;
            target->setEnabled(original.available());
            target->setToolTip(original.available() ? QStringLiteral("Open the original source") : original.reason);
            target->setStatusTip(target->toolTip());
        });
    };
    connect(menu,&QMenu::aboutToShow,action,[refresh] { refresh(true); });
    connect(model,&DocumentBlockModel::changed,action,[refresh,menu] { refresh(menu->isVisible()); });
    refresh(false);
}
void EditorBlockPreview::openReference(DocumentBlockModel* model, PinloomHostClient* client,
    const QString& id, QObject* consumer, PinloomHostClient::CompletionHandler handler) {
    const auto* block = model ? model->find(id) : nullptr;
    if (!block || !consumer) return;
    const auto uri = block->uri, documentId = model->documentId();
    const QPointer<DocumentBlockModel> bound(model);
    const QPointer<PinloomHostClient> provider(client);
    const QPointer<QObject> lifetime(consumer);
    const auto current = [bound,lifetime,id,uri,documentId] {
        const auto* value = bound ? bound->find(id) : nullptr;
        return lifetime && value && bound->documentId() == documentId && value->type == "reference" && value->uri == uri;
    };
    inspectOriginal(model,client,id,consumer,[provider,current,handler](const EditorBlockOriginalTarget& original) {
        if (!current()) return;
        if (!original.available()) { handler(false,original.reason); return; }
        if (original.kind == EditorBlockOriginalTarget::Kind::Url) {
            const bool opened = QDesktopServices::openUrl(original.url);
            if (current()) handler(opened,opened ? QStringLiteral("Opened original source.") : QStringLiteral("Unable to open the original source."));
        } else if (provider) {
            provider->open(original.identity,[current,handler](bool opened,const QString& message) {
                if (current()) handler(opened,message);
            },original.verifyLocation);
        }
    });
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
    const auto linkText=linkOnlyText(block);
    if (!linkText.isEmpty()) { handler({linkText, {}, {}, false}); return; }
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
        if (!document.preview.present) {
            const bool hasText = !text.trimmed().isEmpty();
            handler({hasText ? text : QString(), hasText ? QString() : QStringLiteral("Link only · Open the original in Pinloom"), {}, false});
            return;
        }
        if (!document.preview.isReady() || !document.preview.isImage()) {
            const auto reason = !document.preview.validationError.isEmpty() ? document.preview.validationError : !document.preview.error.isEmpty() ? document.preview.error : QStringLiteral("The reference provider has no ready preview.");
            handler({text,reason,{},false}); return;
        }
        auto* loader = new PinloomPreviewImageLoader(lifetime);
        loader->load(document.preview, [current,handler,text](const auto& result) { if (current()) handler({text,result.error,result.image,false}); });
    }, consumer);
}
