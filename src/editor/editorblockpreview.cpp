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
bool matchesIdentity(const PinloomHostIdentity& expected,const PinloomHostIdentity& actual) {
    const auto field=[](const QString& expected,const QString& actual){return expected.isEmpty() || expected==actual;};
    return actual.isValid() && field(expected.entryId,actual.entryId) && field(expected.resourceId,actual.resourceId)
        && field(expected.anchorId,actual.anchorId) && field(expected.clipId,actual.clipId);
}
EditorBlockOriginalTarget originalForDocument(const PinloomHostIdentity& identity,const PinloomHostDocument& document) {
    if (!identity.clipId.isEmpty() || !document.entry.identity.clipId.isEmpty() || document.entry.type=="clip") {
        const QUrl source(document.details.value("sourceUri").toString().trimmed(),QUrl::StrictMode);
        return supportedOriginalUrl(source)
            ? EditorBlockOriginalTarget{EditorBlockOriginalTarget::Kind::Url,{ },source,{ }}
            : EditorBlockOriginalTarget{EditorBlockOriginalTarget::Kind::Unavailable,{ },{ },"This clip has no available original source."};
    }
    if(!identity.anchorId.isEmpty() && document.entry.type=="anchor" && document.details.value("locatorType").toString().isEmpty() && !document.preview.cropped)
        return {EditorBlockOriginalTarget::Kind::Unavailable,{},{},"This binding has no PDF region or other original locator. Use Open links."};
    return {EditorBlockOriginalTarget::Kind::Pinloom,identity,{ },{ },document.details.value("locatorType")=="sumatrapdf.rect"};
}
EditorBlockPreview::Current blockCurrent(DocumentBlockModel* model,const QString& id,QObject* consumer) {
    const QPointer<DocumentBlockModel> bound(model); const QPointer<QObject> lifetime(consumer);
    const auto block=model?model->presentation(id):EditorAssociatedBlock{};
    const auto documentId=model?model->documentId():QString();
    return [bound,lifetime,block,documentId,id] {
        if(!bound || !lifetime || bound->documentId()!=documentId || !bound->find(id))return false;
        const auto value=bound->presentation(id);
        return !value.removedReference() && value.uri==block.uri && value.reference==block.reference && value.revision==block.revision;
    };
}
}

EditorBlockPreview::EditorBlockPreview(DocumentBlockModel* model, PinloomHostClient* client, QObject* parent)
    : QObject(parent), model(model), client(client) {}
EditorBlockPreview::~EditorBlockPreview() { cancel(); }
EditorBlockOriginalTarget EditorBlockPreview::originalTargetForDocument(
    const PinloomHostIdentity& identity, const PinloomHostDocument& document) {
    if (!document.isValid() || document.entry.deleted || !matchesIdentity(identity,document.entry.identity))
        return {EditorBlockOriginalTarget::Kind::Unavailable,{},{},"The referenced object is unavailable."};
    return originalForDocument(identity,document);
}
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
    inspectTarget(model->presentation(id).uri,client,consumer,blockCurrent(model,id,consumer),std::move(handler));
}
void EditorBlockPreview::inspectTarget(const QString& uri,PinloomHostClient* client,QObject* consumer,
    Current current,std::function<void(const EditorBlockOriginalTarget&)> handler) {
    if(!consumer || !current())return;
    const QUrl url(uri, QUrl::StrictMode);
    const auto identity = PinloomHostIdentity::fromUri(url);
    if (!identity.isValid()) {
        handler(supportedOriginalUrl(url)
            ? EditorBlockOriginalTarget{EditorBlockOriginalTarget::Kind::Url, {}, url, {}}
            : EditorBlockOriginalTarget{EditorBlockOriginalTarget::Kind::Unavailable, {}, {}, "The original source is unavailable."});
        return;
    }
    if (!client) { handler({EditorBlockOriginalTarget::Kind::Unavailable, {}, {}, "Pinloom is unavailable."}); return; }
    const QPointer<PinloomHostClient> provider(client);
    const QPointer<QObject> lifetime(consumer);
    client->resolve(identity, [current,identity,handler,lifetime,provider](const PinloomHostDocument& document, const QString& error) {
        if (!lifetime || !provider || !current()) return;
        if (!error.isEmpty() || !document.isValid() || document.entry.deleted || !matchesIdentity(identity,document.entry.identity)) {
            handler({EditorBlockOriginalTarget::Kind::Unavailable, {}, {},
                error.isEmpty() ? QStringLiteral("The referenced object is missing or was deleted.") : error});
            return;
        }
        handler(originalForDocument(identity,document));
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
    connect(model,&DocumentBlockModel::presentationChanged,action,[refresh,menu] { refresh(menu->isVisible()); });
    refresh(false);
}
void EditorBlockPreview::openReference(DocumentBlockModel* model, PinloomHostClient* client,
    const QString& id, QObject* consumer, PinloomHostClient::CompletionHandler handler) {
    const auto* block = model ? model->find(id) : nullptr;
    if (!block || !consumer) return;
    openTarget(model->presentation(id).uri,client,consumer,blockCurrent(model,id,consumer),std::move(handler));
}
void EditorBlockPreview::openTarget(const QString& uri,PinloomHostClient* client,QObject* consumer,
    Current current,PinloomHostClient::CompletionHandler handler) {
    const QPointer<PinloomHostClient> provider(client);
    const QPointer<QObject> lifetime(consumer);
    inspectTarget(uri,client,consumer,current,[provider,current,handler,lifetime](const EditorBlockOriginalTarget& original) {
        openInspectedTarget(original,provider,lifetime,current,handler);
    });
}
void EditorBlockPreview::openInspectedTarget(const EditorBlockOriginalTarget& original,PinloomHostClient* provider,
    QObject* consumer,Current current,PinloomHostClient::CompletionHandler handler) {
        const QPointer<QObject> lifetime(consumer);
        if (!lifetime || !current()) return;
        if (!original.available()) { handler(false,original.reason); return; }
        if (original.kind == EditorBlockOriginalTarget::Kind::Url) {
            const bool opened = QDesktopServices::openUrl(original.url);
            if (current()) handler(opened,opened ? QStringLiteral("Opened original source.") : QStringLiteral("Unable to open the original source."));
        } else if (provider) {
            provider->open(original.identity,[current,handler,lifetime](bool opened,const QString& message) {
                if (lifetime && current()) handler(opened,message);
            },original.verifyLocation);
        }
}
void EditorBlockPreview::request(const QString& id, Handler handler) {
    cancel(); if (!model || !model->find(id)) return;
    const auto block = model->presentation(id); const auto documentId = model->documentId();
    if (block.type=="attachment") {
        QString issue; const auto bytes=DocumentBlockStore::get(model)->assetBytes(block.asset,&issue);
        handler({block.caption,issue.isEmpty() ? QString("Attachment · %1 bytes · Use Export attachment to save a copy.").arg(bytes.size()) : issue,{},false}); return;
    }
    consumer = new QObject(this); const auto serial = generation;
    const QPointer<EditorBlockPreview> self(this); const QPointer<QObject> lifetime = consumer;
    const auto current = [self,lifetime,serial,id,block,documentId] {
        if (!self || !lifetime || !self->model || self->generation != serial || self->model->documentId() != documentId) return false;
        const auto value = self->model->presentation(id);
        return !value.id.isEmpty() && !value.removedReference() && value.type == block.type && value.uri == block.uri
            && value.asset == block.asset && value.reference==block.reference && value.revision==block.revision;
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
    requestTarget(block.uri,client,consumer,current,std::move(handler));
}
void EditorBlockPreview::requestTarget(const QString& uri,PinloomHostClient* client,QObject* consumer,Current current,Handler handler) {
    if(!consumer || !current())return;
    EditorAssociatedBlock block;block.type="reference";block.uri=uri;
    const auto link=linkOnlyText(block);
    if(!link.isEmpty()){
        const QUrl url(uri);EditorBlockOriginalTarget original;
        if(supportedOriginalUrl(url))original={EditorBlockOriginalTarget::Kind::Url,{},url,{}};
        handler({link,{},{},false,original});return;
    }
    const QPointer<QObject> lifetime(consumer);
    const auto identity = PinloomHostIdentity::fromUri(QUrl(uri));
    if (!identity.isValid() || !client) { handler({{}, "Preview is not supported for this URI. Use Open original.", {}, false}); return; }
    handler({{},"Loading preview…",{},true});
    client->resolve(identity, [identity,lifetime,current,handler](const PinloomHostDocument& document, const QString& error) {
        if (!lifetime || !current()) return;
        QString issue = error;
        if (issue.isEmpty() && (!document.isValid() || document.entry.deleted)) issue = "The referenced object is missing or was deleted.";
        if(issue.isEmpty() && !matchesIdentity(identity,document.entry.identity))issue="Pinloom returned a different target. Retry to refresh it.";
        if(issue.isEmpty() && document.details.value("locatorType")=="sumatrapdf.rect" && document.preview.isReady() && !document.preview.cropped)
            issue="Pinloom did not return the selected PDF region.";
        if (!issue.isEmpty()) { handler({{},issue,{},false}); return; }
        const auto original=originalForDocument(identity,document);
        const auto title=document.entry.title;
        QString location=pinloomDocumentFileName(document);
        if(location.isEmpty())location=document.entry.location;
        const int page=document.preview.page>0 ? document.preview.page : document.details.value("page").toInt();
        if(page>0)location+=QStringLiteral("  ·  Page %1").arg(page);
        const auto text = document.content.left(32768);
        if (!document.preview.present) {
            const bool hasText = !text.trimmed().isEmpty();
            handler({hasText ? text : QString(), hasText ? QString() : original.available() ? QStringLiteral("Link only · Open the original in Pinloom") : original.reason,
                {},false,original,title,location,!hasText && original.available()});
            return;
        }
        if (!document.preview.isReady() || !document.preview.isImage()) {
            const auto reason = !document.preview.validationError.isEmpty() ? document.preview.validationError : !document.preview.error.isEmpty() ? document.preview.error : QStringLiteral("The reference provider has no ready preview.");
            handler({text,reason,{},false,original,title,location}); return;
        }
        auto* loader = new PinloomPreviewImageLoader(lifetime);
        loader->load(document.preview, [current,handler,text,lifetime,original,title,location](const auto& result) { if (lifetime && current()) handler({text,result.error,result.image,false,original,title,location}); });
    }, consumer);
}
